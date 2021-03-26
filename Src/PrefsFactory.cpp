// Copyright (c) 2013-2021 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

//->Start of API documentation comment block
/**
@page com_webos_settingsservice com.webos.settingsservice

@brief Service component for Setting. Provides APIs to set/get/manage settings

@{
@}
*/
//->End of API documentation comment block

#include <fstream>

#include "JSONUtils.h"
#include "LocalePrefsHandler.h"
#include "Logging.h"
#include "PrefsDb8DelDesc.h"
#include "PrefsDb8Init.h"
#include "PrefsFactory.h"
#include "PrefsFileWriter.h"
#include "PrefsKeyDescMap.h"
#include "PrefsInternalCategory.h"
#include "PrefsPerAppHandler.h"
#include "SettingsService.h"
#include "SettingsServiceApi.h"
#include "Utils.h"

MethodTaskMgr methodTaskMgr;

const std::string PrefsFactory::service_name("com.webos.settingsservice");
const std::string PrefsFactory::service_root_uri("luna://" + PrefsFactory::service_name + "/");

#if 0
static bool cbGetSystemSettingsBatch(LSHandle * lsHandle, LSMessage* message, void *user_data, MethodCall* taskInfo, pbnjson::JValue requestObj);
static bool cbGetSystemSettingValuesBatch(LSHandle * lsHandle, LSMessage* message, void *user_data, MethodCall* taskInfo, pbnjson::JValue requestObj);
static bool cbGetSystemSettingDescBatch(LSHandle * lsHandle, LSMessage* message, void *user_data, MethodCall* taskInfo, pbnjson::JValue requestObj);
#endif

PrefsFactory *PrefsFactory::instance()
{
    static PrefsFactory s_instance;
    return &s_instance;
}

PrefsFactory::PrefsFactory()
  : m_serviceReady(false)
  , m_publicAPIGuard()
{
}

PrefsFactory::~PrefsFactory()
{
    m_serviceReady = false;
}

void PrefsFactory::initKind()
{
    if (!m_serviceHandles.empty()) {
        PrefsDb8Init::instance()->initKind();
    }
    else {
        SSERVICELOG_ERROR(MSGID_NO_LS_HANLDE, 0, "Service Handle is not initialized.");
    }
};

void PrefsFactory::setSubscriptionCancel()
{
    LSError lsError;

    for (LSHandle *lsHandle: m_serviceHandles) {
        LSErrorInit(&lsError);
        if ( !LSSubscriptionSetCancelFunction(lsHandle, &cbSubscriptionCancel, this, &lsError))
        {
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
        }
    }
}

bool PrefsFactory::cbSubscriptionCancel(LSHandle * a_handle, LSMessage * a_message, void * a_data)
{
    PrefsFactory * thiz_class = reinterpret_cast<PrefsFactory *>(a_data);
    if (!a_data) return true;

    for (auto func : thiz_class->m_cbSubsCancel)
    {
        func(a_handle, a_message);
    }

    return true;
}

void PrefsFactory::setServiceHandle(LSHandle* serviceHandle)
{
    bool result;
    LSError lsError;
    LSErrorInit(&lsError);

    m_serviceHandles.push_back(serviceHandle);

    // register service
    result = LSRegisterCategory(serviceHandle, "/", SettingsServiceApi_GetMethods(), NULL, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Register service");
        LSErrorFree(&lsError);
        return;
    }
    result = LSCategorySetData(serviceHandle,  "/", this, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Set user data");
        LSErrorFree(&lsError);
        return;
    }

    result = LSRegisterCategory(serviceHandle, "/internal", PrefsInternalCategory::getMethods(), NULL, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2,
            PMLOGKS("Function",lsError.func),
            PMLOGKS("Error",lsError.message),
            "Register service with internal category");
        LSErrorFree(&lsError);
        return;
    }
    result = LSCategorySetData(serviceHandle,  "/internal", this, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Set user data with internal category");
        LSErrorFree(&lsError);
        return;
    }

    PrefsDb8Init::instance()->setServiceHandle(serviceHandle);
    PrefsKeyDescMap::instance()->setServiceHandle(serviceHandle);

    // Now we can create all the prefs handlers
    registerPrefHandler(std::shared_ptr<PrefsHandler>(new LocalePrefsHandler(serviceHandle)));

    setSubscriptionCancel();
    PrefsFactory::instance()->registerSubscriptionCancel(Utils::subscriptionRemove);
}


void PrefsFactory::loadCoreServices(const std::string& a_confPath)
{
    /* { "services": [ "com.webos.service.tv", "com.webos.applicationManager" ] } */
    std::string jsonStr;
    if (!Utils::readFile(a_confPath, jsonStr))
        return;

    pbnjson::JValue jObj = pbnjson::JDomParser::fromString(jsonStr);
    if (!jObj.isObject()) {
        return;
    }

    pbnjson::JValue jsonServices = jObj["services"];
    if (!jsonServices.isArray()) {
        return;
    }

    for (pbnjson::JValue item : jsonServices.items()) {
        if (item.isString())
            m_coreServices.insert(item.asString());
    }
}

bool PrefsFactory::isCoreService(LSMessage* a_message) const
{
    const char* senderString = LSMessageGetApplicationID(a_message);
    if (nullptr == senderString)
        senderString = LSMessageGetSenderServiceName(a_message);

    std::string sender;
    if (senderString)
        sender = senderString;

    return m_coreServices.find(sender) != m_coreServices.end();
}


bool PrefsFactory::isReady(void)
{
    return m_serviceReady;
}

void PrefsFactory::serviceStart(void)
{
    methodTaskMgr.createTaskThread();
}

void PrefsFactory::serviceReady(void)
{
    // send signal for ready
    if (!m_serviceReady) {
        m_blockCache.clear();
        m_serviceReady = true;
        if (system("initctl emit --no-wait settingsservice-ready; /usr/bin/check_settings_version.sh || grep dbVersion /etc/palm/settingsservice.conf > /var/settingsservice.ver")==-1) {
            SSERVICELOG_ERROR(MSGID_EMIT_UPSTART_EVENT_FAIL, 0, "fail to emit settingsservice-ready");
        }
    }
}

void PrefsFactory::serviceFailed(const char * name)
{
    // leave file to indicate fail to init
    SSERVICELOG_ERROR(MSGID_FAIL_TO_INIT, 0, "fail to init settingsservice");
    int fd = open(WEBOS_INSTALL_SETTINGSSERVICE_ERRORSENTINELFILE, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        SSERVICELOG_ERROR(MSGID_FAIL_TO_INIT, 0, "fail to leave fail file");
    }
    else {
        close(fd);
    }

    sleep(3);
    exit(1);
}

void PrefsFactory::releaseTask(MethodCallInfo** p, pbnjson::JValue replyObj)
{
    MethodCallInfo *taskInfo = *p;

    if ( !taskInfo->isTaskInQueue() ) {
        p = nullptr;
        taskInfo->unref();
        return;
    }

    methodTaskMgr.releaseTask(p, replyObj);
}

const std::vector<LSHandle*>& PrefsFactory::getServiceHandles() const {
    return m_serviceHandles;
}

std::shared_ptr<PrefsHandler> PrefsFactory::getPrefsHandler(const std::string& key) const
{
    PrefsHandlerMap::const_iterator it = m_handlersMaps.find(key);
    if (it == m_handlersMaps.end())
        return 0;

    return it->second;
}

void PrefsFactory::registerPrefHandler(std::shared_ptr<PrefsHandler> handler)
{
    if (!handler)
        return;

    std::list<std::string> keys = handler->keys();
    for (const std::string& a_key : keys)
        m_handlersMaps[a_key] = handler;
}

void PrefsFactory::registerSubscriptionCancel(SubsCancelFunc a_func)
{
    m_cbSubsCancel.push_back(a_func);
}

MethodTaskMgr& PrefsFactory::getTaskManager() const
{
    return methodTaskMgr;
}

bool PrefsFactory::noSubscriber(const char *a_key, LSMessage *a_exception) const
{
    unsigned int subscribers = 0;

    for (LSHandle *lsHandle: m_serviceHandles) {
        subscribers += subscribersCount(lsHandle,  a_key, a_exception);
        if (subscribers > 0) {
            break;
        }
    }

    return ( subscribers == 0 );
}

void PrefsFactory::postPrefChange(const char *subscribeKey, const std::string& a_reply, const char* a_sender ) const
{
    for (LSHandle *lsHandle: m_serviceHandles) {
        PrefsFactory::instance()->postPrefChangeEach(lsHandle, subscribeKey, a_reply, a_sender);
    }
}

// send subscription for each key.
void PrefsFactory::postPrefChange(const char *subscribeKey, pbnjson::JValue replyRoot, const char* a_sender, const char *a_senderId) const
{
    std::string replyString;
    if (replyRoot.isObject())
    {
        if (a_senderId)
            replyRoot.put("caller", a_senderId);

        replyString = replyRoot.stringify();
    }
    postPrefChange(subscribeKey, replyString, a_sender);
}

// send subscription for keys in one return message.
void PrefsFactory::postPrefChanges(const std::string &category, pbnjson::JValue dimObj, const std::string &app_id, pbnjson::JValue keyValueObj, bool result, bool storeFlag, const char *a_sender, const char *a_senderId) const
{
    for (LSHandle *lsHandle: m_serviceHandles) {
        PrefsFactory::instance()->postPrefChangeCategory(lsHandle, category, dimObj, app_id, keyValueObj, result, storeFlag, a_sender, a_senderId);
    }
}

void PrefsFactory::postPrefChangeCategory(LSHandle *lsHandle, const std::string &category, pbnjson::JValue dimObj, const std::string &app_id, pbnjson::JValue keyValueObj, bool result, bool storeFlag, const char *a_sender, const char *a_senderId) const
{
    std::map<LSMessage*, pbnjson::JValue> subKeyMap;
    std::string categoryStr;
    LSError lsError;
    std::string sender = a_sender ? a_sender : "";

    // create map for the handle that registered the key.
    for(pbnjson::JValue::KeyValue it : keyValueObj.children()) {
        LSSubscriptionIter *iter = NULL;
        std::string key(it.first.asString());
        pbnjson::JValue dimInfo = PrefsKeyDescMap::instance()->getDimKeyValueObj(category, dimObj, key);
        categoryStr = category;
        if (!dimInfo.isNull()) {
            categoryStr += dimInfo.stringify();
        }

        std::string subscribeKey(SUBSCRIBE_STR_KEY(key, app_id, categoryStr));
        if(PrefsKeyDescMap::instance()->getDbType(key) == DBTYPE_EXCEPTION && PrefsKeyDescMap::instance()->isExceptionAppList(app_id))
            subscribeKey = SUBSCRIBE_STR_KEY(key, std::string(GLOBAL_APP_ID), categoryStr);

        // Find out which handle this subscription needs to go to
        LSErrorInit(&lsError);
        bool retVal = LSSubscriptionAcquire(lsHandle, subscribeKey.c_str(), &iter, &lsError);
        if (retVal) {
            while (LSSubscriptionHasNext(iter)) {
                LSMessage *message = LSSubscriptionNext(iter);
                auto itSubKeyMap = subKeyMap.find(message);
                if(itSubKeyMap == subKeyMap.end()) {
                    pbnjson::JObject item;
                    item.put(key, it.second);
                    /* TODO: We should use LSMessageElem class in PrefsNotifier
                     *       while refactoring */
                    LSMessageRef(message);
                    subKeyMap.insert({message, item});
                }
                else {
                    itSubKeyMap->second.put(key, it.second);
                }
            }

            LSSubscriptionRelease(iter);
            iter = NULL;
        } else {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Acquire to post");
            LSErrorFree(&lsError);
        }

        /* if any dimension info is not specified when user requests subscription,
         * user should receive all key change notification only if target
         * dimension is same with current one */
        if(categoryStr != category && PrefsKeyDescMap::instance()->isCurrentDimension(dimObj) ) {
            // Find out which handle this subscription needs to go to
            LSErrorInit(&lsError);
            if(PrefsKeyDescMap::instance()->getDbType(key) == DBTYPE_EXCEPTION && PrefsKeyDescMap::instance()->isExceptionAppList(app_id))
                subscribeKey = SUBSCRIBE_STR_KEY(key, std::string(GLOBAL_APP_ID), category);
            else
                subscribeKey = SUBSCRIBE_STR_KEY(key, app_id, category);
            bool retVal = LSSubscriptionAcquire(lsHandle, subscribeKey.c_str(), &iter, &lsError);
            if (retVal) {
                while (LSSubscriptionHasNext(iter)) {
                    LSMessage *message = LSSubscriptionNext(iter);
                    auto itSubKeyMap = subKeyMap.find(message);

                    if(itSubKeyMap == subKeyMap.end()) {
                        pbnjson::JObject item;
                        item.put(key, it.second);
                        /* TODO: We should use LSMessageElem class in PrefsNotifier
                         *       while refactoring */
                        LSMessageRef(message);
                        subKeyMap.insert({message, item});
                    }
                    else {
                        itSubKeyMap->second.put(key, it.second);
                    }
                }

                LSSubscriptionRelease(iter);
                iter = NULL;
            } else {
                SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Acquire to post for dimension");
                LSErrorFree(&lsError);
            }
        }
    }
    // create subscription string and send it.
    for(const std::pair<LSMessage*, pbnjson::JValue>& subscriptionValue : subKeyMap) {
        pbnjson::JObject replyRoot;
        std::string firstKey;

        if(result) {
            pbnjson::JValue resultSettingsValue = subscriptionValue.second;
            replyRoot.put("settings", resultSettingsValue);
            // store first key for subscription
            pbnjson::JValue::ObjectConstIterator it = resultSettingsValue.children().begin();
            pbnjson::JValue::KeyValue firstKeyValuePair = *it;
            firstKey = firstKeyValuePair.first.asString();
        }
        else {
            pbnjson::JArray errorKeyArray;
            for (pbnjson::JValue::KeyValue it : subscriptionValue.second.children()) {
                std::string key(it.first.asString());
                // store first key to use subscription.
                if(firstKey.empty()) {
                    firstKey = key;
                }
                errorKeyArray.append(key);
            }
            replyRoot.put("errorKey", errorKeyArray);
        }

        replyRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS);
        replyRoot.put("returnValue", result);
        //replyRoot.put("app_id", subAppId);
        replyRoot.put("app_id", app_id);
        if (storeFlag == false) {
            replyRoot.put(KEYSTR_STORE, false);
        }

        if (a_senderId)
            replyRoot.put("caller", a_senderId);

        replyRoot.put("category", category);
        // For one key, use the dimension based on the key.
        pbnjson::JValue dimInfo;
        if(subscriptionValue.second.isArray() && subscriptionValue.second.arraySize() == 1 && !firstKey.empty()) {
            dimInfo = PrefsKeyDescMap::instance()->getDimKeyValueObj(category, dimObj, firstKey);
        }
        // For keys, use the dimension of based on the category.
        else {
            pbnjson::JValue categoryDim = PrefsKeyDescMap::instance()->getDimKeyValueObj(category, dimObj);
            if (!categoryDim.isNull()) {
                // In case of sending multiple keys, and ONLY if it is related with dimensions,
                // the 'dimension' should be made using OR-ed all the possible dimension per each keys.
                //
                dimInfo = pbnjson::Object();
                for (pbnjson::JValue::KeyValue it : subscriptionValue.second.children()) {
                    PrefsKeyDescMap::instance()->getDimKeyValueObj(category, dimObj, it.first.asString(), dimInfo);
                }
            }
        }
        if (!dimInfo.isNull()) {
            replyRoot.put("dimension", dimInfo);
        }
        if ( !(LSMessageGetSender(subscriptionValue.first) && sender == LSMessageGetSender(subscriptionValue.first)) ) {
            if (!LSMessageReply(lsHandle, subscriptionValue.first, replyRoot.stringify().c_str(), &lsError)) {
                SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply to post");
                LSErrorPrint(&lsError, stderr);
                LSErrorFree(&lsError);
            }
        }

        LSMessageUnref(subscriptionValue.first);
    }
}

void PrefsFactory::postPrefChangeEach(LSHandle *lsHandle, const char *subscribeKey, const std::string& a_reply, const char* a_sender) const
{
    bool retVal;
    LSSubscriptionIter *iter = NULL;
    LSError lsError;
    std::string sender = a_sender ? a_sender : "";

    LSErrorInit(&lsError);
    // Find out which handle this subscription needs to go to
    retVal = LSSubscriptionAcquire(lsHandle, subscribeKey, &iter, &lsError);
    if (retVal) {
        while (LSSubscriptionHasNext(iter)) {

            LSMessage *message = LSSubscriptionNext(iter);

            if (LSMessageGetSender(message) && sender == LSMessageGetSender(message))
                continue;

            if (!LSMessageReply(lsHandle, message, a_reply.c_str(), &lsError)) {
                SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply to post each");
                LSErrorPrint(&lsError, stderr);
                LSErrorFree(&lsError);
            }
        }

        LSSubscriptionRelease(iter);
    } else {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Acquire to post each");
        LSErrorFree(&lsError);
    }
}

/* NOTICE : LSSubscriptionGetHandleSubscribersCount
 * could be used for counting. But, the function alwayse return 1
 * while last subscription cancel callback is executing */
unsigned int PrefsFactory::subscribersCount(LSHandle *a_handle, const char *a_key, LSMessage *a_exception) const
{
    unsigned int subscribers = 0;
    LSSubscriptionIter *iter = NULL;
    LSError lsError;
    LSErrorInit(&lsError);

    // Find out which handle this subscription needs to go to
    if ( LSSubscriptionAcquire(a_handle, a_key, &iter, &lsError) == false ) {
        LSErrorFree(&lsError);
        return 0;
    }

    while (LSSubscriptionHasNext(iter)) {
        LSMessage *message = LSSubscriptionNext(iter);
        if ( message != a_exception )
            subscribers++;
    }

    LSSubscriptionRelease(iter);

    return subscribers;
}

bool PrefsFactory::hasAccess(LSHandle *lsHandle, LSMessage *lsMessage)
{
    return m_publicAPIGuard.allowMessage(lsMessage);
}

bool PrefsFactory::isAvailableCache(const std::string& a_method, LSHandle* a_handle, LSMessage* a_mesg) const
{
    if ( a_method != SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS )
        return false;

    const char *payload = LSMessageGetPayload(a_mesg);
    if ( !payload )
        return false;

    pbnjson::JValue jsonParam = pbnjson::JDomParser::fromString(payload);
    if (jsonParam.isNull())
        return false;

    std::string category;
    std::set<std::string> keys;

    pbnjson::JValue jsonCategory = jsonParam[KEYSTR_CATEGORY];
    if (jsonCategory.isString()) {
        category = jsonCategory.asString();
    }

    pbnjson::JValue jsonKey = jsonParam[KEYSTR_KEY];
    if (jsonKey.isString()) {
        keys.insert(jsonKey.asString());
    }

    pbnjson::JValue jsonKeys = jsonParam[KEYSTR_KEYS];
    if (jsonKeys.isArray()) {
        for (pbnjson::JValue key : jsonKeys.items()) {
            if (key.isString())
                keys.insert(key.asString());
        }
    }

    if (keys.empty()) {
        keys = PrefsKeyDescMap::instance()->getKeysInCategory(category);
    }

    for (const std::string& a_key : keys) {
        if (m_blockCache.find( { category, a_key } ) != m_blockCache.end())
            return false;
    }

    return PrefsFileWriter::instance()->isAvailablePreferences(category, keys);
}

void PrefsFactory::blockCacheValue(LSMessage* a_message)
{
    const char *payload = LSMessageGetPayload(a_message);
    if ( !payload )
        return;

    pbnjson::JValue jsonParam = pbnjson::JDomParser::fromString(payload);
    if (jsonParam.isNull())
        return;

    std::string category;
    std::set<std::string> keys;

    pbnjson::JValue jsonCategory = jsonParam[KEYSTR_CATEGORY];
    if (jsonCategory.isString()) {
        category = jsonCategory.asString();
    }

    pbnjson::JValue jsonSettings = jsonParam[KEYSTR_SETTINGS];
    if (jsonSettings.isObject()) {
        for(pbnjson::JValue::KeyValue it : jsonSettings.children()) {
            keys.insert(it.first.asString());
        }
    }

    for ( const std::string& a_key : keys ) {
        m_blockCache.insert( { category, a_key } );
    }
}

void PrefsFactory::setCurrentAppId(const std::string& inCurAppId)
{
    PrefsPerAppHandler::instance().handleAppChange(inCurAppId, m_currentAppId);
    m_currentAppId = inCurAppId;
}

PrefsRefCounted::PrefsRefCounted()
    : m_refCount(0)
{
}

void PrefsRefCounted::ref()
{
    g_atomic_int_inc(&m_refCount);
}

bool PrefsRefCounted::unref()
{
    if (g_atomic_int_dec_and_test(&m_refCount))
    {
        delete this;
        return true;
    }

    return false;
}

PrefsFinalize::~PrefsFinalize()
{
}

void PrefsFinalize::reference()
{
}

void PrefsFinalize::finalize()
{
}

/*
 * Test commands
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"settingsservice"}'
 * Deny  luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"settingsservice"}' -P
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"keys": ["localeInfo"]}'
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"keys": ["localeInfo"]}' -P
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"keys": ["systemPin"]}'
 * Deny  luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"keys": ["systemPin"]}' -P
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"option"}'
 * Deny  luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"option"}' -P
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"option", "keys":["country"]}'
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"option", "keys":["country"]}' -P
 * Allow luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"option", "keys":["country", "countryGroup"]}'
 * Deny  luna-send -n 1 luna://com.webos.settingsservice/getSystemSettings '{"category":"option", "keys":["country", "countryGroup"]}' -P
 */
PrefsFactory::PublicAPIGuard::PublicAPIGuard()
{
    /* init method list to check access policy */
    m_readMethods.insert(SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS);
    m_readMethods.insert(SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES);
    m_writeMethods.insert(SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS);

    /* load access configuration */
    std::ifstream ifs;
    ifs.open(SETTINGSSERVICE_API_ALLOW_PATH, std::ios_base::in);
    if (!ifs.fail()) {
        std::string content;
        try{
            content.assign((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        } catch (const std::length_error& e) {
            SSERVICELOG_DEBUG("Exception while reading file: %s, exception: %s", SETTINGSSERVICE_API_ALLOW_PATH, e.what());
        }
        pbnjson::JValue root = pbnjson::JDomParser::fromString(content);
        if (root.isArray()) {
            for (pbnjson::JValue idx : root.items()) {
                pbnjson::JValue category = idx[KEYSTR_CATEGORY];
                pbnjson::JValue key = idx[KEYSTR_KEY];
                pbnjson::JValue permArr = idx[STR_PERMISSIONS];
                if (category.isString() && key.isString()) {
                    int perm_mask = ACPERM_N;

                    if (permArr.isArray()) {
                        for (pbnjson::JValue p : permArr.items()) {
                            if (p.isString())
                                perm_mask |= permissionMask(p.asString());
                        }
                    } else {
                        /* backward compatibility with previous allow-JSON format */
                        perm_mask = ACPERM_R;
                    }

                    m_accessControlTable.insert({ {category.asString(), key.asString()}, perm_mask});
                }
            }
        }
    }
}

int PrefsFactory::PublicAPIGuard::permissionMask(const std::string &perm)
{
    if ( perm == "read" )
        return ACPERM_R;
    if ( perm == "write" )
        return ACPERM_W;

    return ACPERM_N;
}

bool PrefsFactory::PublicAPIGuard::allowMessage(LSMessage *a_message)
{
    std::string method_name = LSMessageGetMethod(a_message);

    if ( m_readMethods.find(method_name) != m_readMethods.end() &&
            allowReadMessage(a_message) )
    {
        return true;
    }
    if ( m_writeMethods.find(method_name) != m_writeMethods.end() &&
            allowWriteMessage(a_message) )
    {
        return true;
    }

    return false;
}

bool PrefsFactory::PublicAPIGuard::allowWriteMessage(LSMessage *message)
{
    const char *payload = LSMessageGetPayload(message);
    pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
    if (root.isNull()) {
        return false;
    }

    pbnjson::JValue category = root[KEYSTR_CATEGORY];
    pbnjson::JValue settings = root["settings"];

    if (!settings.isObject())
    {
        return false;
    }

    std::string categoryStr = "";
    if (category.isString()) {
        categoryStr = category.asString();
    }
    for (pbnjson::JValue::KeyValue it : settings.children()) {
        std::map<std::pair<std::string,std::string>, int>::iterator rule;
        std::string keyStr = it.first.asString();

        rule = m_accessControlTable.find(std::make_pair(categoryStr, keyStr));

        if ( rule == m_accessControlTable.end() ) {
            return false;
        }

        if ( !(rule->second & ACPERM_W) ) {
            return false;
        }
    }

    return true;
}

bool PrefsFactory::PublicAPIGuard::allowReadMessage(LSMessage *message)
{
    bool retVal = true;

    const char *payload = LSMessageGetPayload(message);
    pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
    if (root.isNull()) {
        return false;
    }

    pbnjson::JValue category = root[KEYSTR_CATEGORY];
    pbnjson::JValue keys = root["keys"];
    pbnjson::JValue key = root["key"];

    if (keys.isNull() && !key.isNull()) {
        keys = pbnjson::Array();
        keys.append(key);
    }

    // both category and keys
    if (keys.isArray()) {
        std::string categoryStr;
        if (category.isString()) {
            categoryStr = category.asString();
        }
        for (pbnjson::JValue idx : keys.items()) {
            std::map<std::pair<std::string,std::string>, int>::iterator rule;

            if (!idx.isString()) {
                retVal = false;
                break;
            }

            std::string keyStr = idx.asString();

            rule = m_accessControlTable.find(std::make_pair(categoryStr, keyStr));

            if ( rule == m_accessControlTable.end() ) {
                retVal = false;
                break;
            }

            if ( !(rule->second & ACPERM_R) ) {
                retVal = false;
                break;
            }
        }
    }

    // category
    if (retVal && keys.isNull() && !category.isNull()) {
        retVal = false;

        std::map<std::pair<std::string,std::string>, int>::iterator rule;
        std::string categoryStr = category.asString();
        std::string keyStr = "";

        rule = m_accessControlTable.find(std::make_pair(categoryStr, keyStr));

        if ( rule != m_accessControlTable.end() && !(rule->second & ACPERM_R) ) {
            retVal = true;
        }
    }

    return retVal;
}
