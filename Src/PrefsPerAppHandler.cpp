// Copyright (c) 2015-2024 LG Electronics, Inc.
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

#include <fstream>

#include "Logging.h"
#include "PrefsDb8Get.h"
#include "PrefsFactory.h"
#include "PrefsFactory.h"
#include "PrefsKeyDescMap.h"
#include "PrefsPerAppHandler.h"
#include "SettingsService.h"
#include "SettingsServiceApi.h"
#include "Utils.h"

using namespace std;

static const string _CURRENT_APP = "_CURRENT_APP";
static const string _DEFAULT_PERAPP_EXCLUDE_CONF = "/etc/palm/settingsservice.perapp.exclude.json";


PrefsPerAppHandler& PrefsPerAppHandler::instance()
{
    static PrefsPerAppHandler instance;
    return instance;
}

PrefsPerAppHandler::PrefsPerAppHandler()
    : m_taskInfo(NULL),
      m_lsHandle(NULL)
{
    LSErrorInit(&m_lsError);
}

PrefsPerAppHandler::~PrefsPerAppHandler()
{
}

bool PrefsPerAppHandler::next()
{
    /* Notification for App switching */
    switch (m_nextFunc) {
    case State::ForValue:
        m_nextFunc = State::ForDesc;
        return doSendValueQuery();
    case State::ForDesc:
        m_nextFunc = State::FinishTask;
        return doHandleDescQuery();
    case State::RemovePerApp: /* Remove Per App Settings */
        m_nextFunc = State::FinishTask;
        return doRemovePerApp();
    case State::FinishTask:   /* Finish Task */
        m_nextFunc = State::None;
        return doFinishTask();
    }
    return true;
}

set<string> PrefsPerAppHandler::findKeys(const string& a_category, const string& a_dbType) const
{
    set<string> ret;

    for ( const string& k : PrefsKeyDescMap::instance()->getKeysInCategory(a_category) ) {
        if ( PrefsKeyDescMap::instance()->getDbType(k) == a_dbType )
            ret.insert(k);
    }

    return ret;
}

bool PrefsPerAppHandler::doSendValueQuery()
{
    pbnjson::JValue jOperations(pbnjson::Array());
    {
        lock_guard<recursive_mutex> lock(m_container_mutex);
        if (m_categorySubscriptionMessagesMapForValue.empty())
            return next();

        PrefsKeyDescMap *keyDesc = PrefsKeyDescMap::instance();

        for (auto &it : m_categorySubscriptionMessagesMapForValue)
        { // each category registered per-app subscription
            if (it.second.empty())
                continue;

            set<string> keyList = findKeys(it.first, DBTYPE_MIXED);

            CategoryDimKeyListMap categoryDimKeysMap = keyDesc->getCategoryKeyListMap(it.first, pbnjson::JValue(), keyList);
            std::string categoryDim;
            if (PrefsKeyDescMap::instance()->getCategoryDim(it.first, categoryDim))
            {
                categoryDimKeysMap[categoryDim] = keyList;
            }

            for (auto &it : categoryDimKeysMap)
            {
                jOperations.append(PrefsDb8Get::jsonFindBatchItem(it.first, true, it.second, true, GLOBAL_APP_ID, SETTINGSSERVICE_KIND_DEFAULT));
                jOperations.append(PrefsDb8Get::jsonFindBatchItem(it.first, true, it.second, true, GLOBAL_APP_ID, SETTINGSSERVICE_KIND_MAIN));
                jOperations.append(PrefsDb8Get::jsonFindBatchItem(it.first, true, it.second, true, m_prevAppId, SETTINGSSERVICE_KIND_DEFAULT));
                jOperations.append(PrefsDb8Get::jsonFindBatchItem(it.first, true, it.second, true, m_prevAppId, SETTINGSSERVICE_KIND_MAIN));
                jOperations.append(PrefsDb8Get::jsonFindBatchItem(it.first, true, it.second, true, m_currAppId, SETTINGSSERVICE_KIND_DEFAULT));
                jOperations.append(PrefsDb8Get::jsonFindBatchItem(it.first, true, it.second, true, m_currAppId, SETTINGSSERVICE_KIND_MAIN));
            }
        }
    }
    pbnjson::JObject jBatchQuery;
    jBatchQuery.put("operations", jOperations);

    bool ret = DB8_luna_call(m_lsHandle, "luna://com.webos.service.db/batch", jBatchQuery.stringify().c_str(), cbSendValueQuery, this, NULL, &m_lsError);
    if (!ret) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",m_lsError.func), PMLOGKS("Error",m_lsError.message), "Send request");
        LSErrorFree(&m_lsError);
        return next();
    }

    return true; // next() continued in cbSendValueQuery callback
}

static void subsFn(const string& subscribeKey, const function<void(LSMessage*)>& f)
{
    LSSubscriptionIter* iter = nullptr;
    LSError lsError;
    LSErrorInit(&lsError);

    for (auto handle : PrefsFactory::instance()->getServiceHandles()){
        if (LSSubscriptionAcquire(handle, subscribeKey.c_str(), &iter, &lsError)) {
            while (LSSubscriptionHasNext(iter)) {
                LSMessage *message = LSSubscriptionNext(iter);
                f(message);
            }
            LSSubscriptionRelease(iter);
        }
        else {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2,
                PMLOGKS("Function", lsError.func),
                PMLOGKS("Error", lsError.message), "Acquire to post");
            LSErrorFree(&lsError);
        }
    }
}

static void groupByFromBatch(pbnjson::JValue jBatchResult, map<string, vector<pbnjson::JValue>>& m, function<string(pbnjson::JValue)> f)
{
    pbnjson::JValue jResponses = jBatchResult["responses"];
    if (!jResponses.isArray())
        return;

    // each batchResponse.responses

    for (pbnjson::JValue iResp : jResponses.items()) {
        if (iResp.isNull())
            continue;

        pbnjson::JValue jReturnValue = iResp["returnValue"];
        if (!jReturnValue.isBoolean() || !jReturnValue.asBool())
            continue;

        pbnjson::JValue jResults = iResp["results"];
        if (!jResults.isArray())
            continue;
        // each Response.results

        for (pbnjson::JValue iResult : jResults.items()) {
            if (!iResult.isObject())
                continue;

            string groupKey = f(iResult);
            vector<pbnjson::JValue>& v = m[groupKey];
            if (find(v.begin(), v.end(), iResult) == v.end())
                v.push_back(iResult);
        }
    }
}

bool PrefsPerAppHandler::cbSendValueQuery(LSHandle* lsHandle, LSMessage* lsMessage, void* ctx)
{
    auto thiz = static_cast<PrefsPerAppHandler*>(ctx);

    const char* payload = LSMessageGetPayload(lsMessage);
    if (payload == NULL)
        return thiz->next();

    pbnjson::JValue jRoot = pbnjson::JDomParser::fromString(payload);
    if (jRoot.isNull())
        return thiz->next();

    // group by category

    map<string,vector<pbnjson::JValue>> mapCategoryItems;
    auto categoryLambda = [](pbnjson::JValue jItem) -> std::string {
        pbnjson::JValue jCategory = jItem[KEYSTR_CATEGORY];
        if (!jCategory.isString()) {
            return string();
        }
        string categoryDim = jCategory.asString();
        return PrefsKeyDescMap::instance()->categoryDim2category(categoryDim);
    };
    auto funcCategory = static_cast<std::function<string(pbnjson::JValue)>>(categoryLambda);
    groupByFromBatch(jRoot, mapCategoryItems, funcCategory);

    // each category and category's results

    for (const pair<const string,vector<pbnjson::JValue>>& it : mapCategoryItems) {
        const string& category = it.first;

        pbnjson::JArray resultArray;

        for (pbnjson::JValue jResultInCategory : it.second) {
            resultArray.append(jResultInCategory);
        }

        pbnjson::JValue jMergedPre (PrefsDb8Get::mergeLayeredRecords(category, resultArray, thiz->m_prevAppId, true));
        pbnjson::JValue jMergedCur (PrefsDb8Get::mergeLayeredRecords(category, resultArray, thiz->m_currAppId, true));

        // collect changed pairs of key:value

        set<string> unchangesKeys;
        bool changed = false;
        for(pbnjson::JValue::KeyValue m_it : jMergedCur.children()) {
            pbnjson::JValue valPrev = jMergedPre[m_it.first.asString()];
            if (valPrev == m_it.second)
                unchangesKeys.insert(m_it.first.asString());
            else
                changed = true;
        }
        if (changed) {
            for (auto& key : unchangesKeys) {
                jMergedCur.remove(key);
            }
        }

        {
            map<LSMessageElem, pbnjson::JValue> subscriptions;
            for (pbnjson::JValue::KeyValue it : jMergedCur.children()) {
                // make subscribeKey from changed keys

                string subscribeKey = SUBSCRIBE_STR_KEY(it.first.asString(), _CURRENT_APP, category);

                // collect subscriptions

                subsFn(subscribeKey, [&](LSMessage* lsMsg) {
                    if (thiz->isMessageInExclude(lsMsg)) {
                        return;
                    }
                    if (subscriptions.count(lsMsg) == 0) {
                        subscriptions[LSMessageElem(lsMsg)] = pbnjson::Object();
                    }
                    subscriptions[lsMsg] << it;
                });
            }

            for (auto& it : subscriptions) {
                LSMessage* lsMsg = it.first.get();
                pbnjson::JObject jRoot;
                jRoot.put(KEYSTR_APPID, thiz->m_currAppId);
                jRoot.put(KEYSTR_CATEGORY, category);
                jRoot.put(KEYSTR_DIMENSION, pbnjson::Object());
                jRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS);
                jRoot.put("returnValue", true);
                jRoot.put("settings", it.second);
                jRoot.put("subscribed", true);

                if (!LSMessageReply(LSMessageGetConnection(lsMsg), lsMsg, jRoot.stringify().c_str(), &thiz->m_lsError)) {
                    SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function", thiz->m_lsError.func), PMLOGKS("Error", thiz->m_lsError.message), "Reply to post");
                    LSErrorFree(&thiz->m_lsError);
                }
            }
        }
    }

    return thiz->next();
}

bool PrefsPerAppHandler::cbRemovePerApp(LSHandle* lsHandle, LSMessage* lsMessage, void* ctx)
{
    auto thiz = static_cast<PrefsPerAppHandler*>(ctx);

    const char* payload = LSMessageGetPayload(lsMessage);
    if (payload == NULL)
        return thiz->next();

    pbnjson::JValue jRoot = pbnjson::JDomParser::fromString(payload);
    if (jRoot.isNull())
        return thiz->next();

    pbnjson::JValue returnValue = jRoot["returnValue"];
    if ( !returnValue.isBoolean() ) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_DEL_FAIL, 0, "Fail to remove perapp settings (Unknown db8 resp.)");
        return thiz->next();
    }

    if ( !returnValue.asBool() ) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_DEL_FAIL, 0, "Fail to remove perapp settings");
        return thiz->next();
    }

    return thiz->next();
}

pbnjson::JValue findItemInArrayByKeyValue(pbnjson::JValue jArrItems, const string& key, const string& value)
{
    if (jArrItems.isArray()) {
        for (pbnjson::JValue jItem : jArrItems.items()) {
            if (jItem.isObject()) {
                pbnjson::JValue jProp = jItem[key];
                if (jProp.isString() && value == jProp.asString())
                    return jItem;
            }
        }
    }
    return pbnjson::JValue();
}

pbnjson::JValue pickObject(pbnjson::JValue jObj, const set<string>& keys)
{
	pbnjson::JObject jNew;
    for (const std::string& key : keys) {
        pbnjson::JValue jProp = jObj[key];
        if (!jProp.isNull())
            jNew.put(key, jProp);
    }
    return jNew;
}

bool PrefsPerAppHandler::doHandleDescQuery()
{
    map<LSMessageElem, pbnjson::JValue> subscriptions;
    {
        lock_guard<recursive_mutex> lock(m_container_mutex);

        if (m_keySubscriptionMessagesMapForDesc.empty())
            return next();

        PrefsKeyDescMap *keyDesc = PrefsKeyDescMap::instance();

        for (auto &it : m_keySubscriptionMessagesMapForDesc)
        { // each key registered per-app subscription
            if (it.second.empty())
                continue;

            string category;
            keyDesc->getCategory(it.first, category);

            pbnjson::JValue jRootPrev(keyDesc->getKeyDesc(category, {it.first}, m_prevAppId));
            pbnjson::JValue jRootCurr(keyDesc->getKeyDesc(category, {it.first}, m_currAppId));

            pbnjson::JValue jResultsPrev = jRootPrev["results"];
            pbnjson::JValue jResultsCurr = jRootCurr["results"];

            pbnjson::JValue jItemPrev = findItemInArrayByKeyValue(jResultsPrev, KEYSTR_KEY, it.first);
            pbnjson::JValue jItemCurr = findItemInArrayByKeyValue(jResultsCurr, KEYSTR_KEY, it.first);

            pbnjson::JValue jItemPrevPicked = pickObject(jItemPrev, {"ui", "values"});
            pbnjson::JValue jItemCurrPicked = pickObject(jItemCurr, {"ui", "values"});

            if (jItemPrevPicked == jItemCurrPicked)
                continue;

            string subscribeKey = SUBSCRIBE_STR_KEYDESC(it.first, _CURRENT_APP);

            subsFn(subscribeKey, [&](LSMessage *lsMessage)
                   {
            if (isMessageInExclude(lsMessage)) {
                return;
            }
            if (subscriptions.count(lsMessage) == 0) {
                subscriptions[LSMessageElem(lsMessage)] = pbnjson::Array();
            }
            subscriptions[lsMessage].append(jItemCurr); });
        }
    }

    for (auto& it : subscriptions) {
        LSMessage* lsMessage = it.first.get();
        pbnjson::JObject jRoot;
        jRoot.put(KEYSTR_APPID,  m_currAppId);
        jRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC);
        jRoot.put("returnValue", true);
        jRoot.put("results", it.second);
        jRoot.put("subscribed", true);

        if (!LSMessageReply(LSMessageGetConnection(lsMessage), lsMessage, jRoot.stringify().c_str(), &m_lsError)) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function", m_lsError.func), PMLOGKS("Error", m_lsError.message), "Reply to post");
            LSErrorFree(&m_lsError);
        }
    }

    return next();
}

bool PrefsPerAppHandler::doFinishTask()
{
    if (m_taskInfo)
        PrefsFactory::instance()->releaseTask(&m_taskInfo);
    return true;
}

bool PrefsPerAppHandler::doRemovePerApp()
{
    pbnjson::JObject query;
    pbnjson::JArray where;
    pbnjson::JObject whereObj;
    pbnjson::JObject queryObj;

    queryObj.put("from", "com.webos.settings.system:1");
    whereObj.put("prop", "app_id");
    whereObj.put("op", "=");
    whereObj.put("val", m_removedAppId);
    where.append(whereObj);
    queryObj.put("where", where);
    query.put("query", queryObj);

    bool ret = DB8_luna_call(m_lsHandle, "luna://com.palm.db/del", query.stringify().c_str(), cbRemovePerApp, this, NULL, &m_lsError);
    if (!ret) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",m_lsError.func), PMLOGKS("Error",m_lsError.message), "Send request");
        LSErrorFree(&m_lsError);
        return next();
    }

    return true; // next() continued in cbRemovePerApp callback

}


void PrefsPerAppHandler::handleAppChange(const string& currentAppId, const string& prevAppId)
{
    m_currAppId = currentAppId;
    m_prevAppId = prevAppId;

    SSERVICELOG_INFO("PrefsPerAppHandler", 2, PMLOGKS("m_currAppId", m_currAppId.c_str()), PMLOGKS("m_prevAppId", m_prevAppId.c_str()), "handleAppChange");

    m_nextFunc = (!Settings::settings()->supportAppSwitchNotify || m_currAppId == m_prevAppId) ? State::FinishTask : State::ForValue;

    (void)next();
}

void PrefsPerAppHandler::removePerAppSettings(const std::string& app_id)
{
    m_removedAppId = app_id;
    m_nextFunc = State::RemovePerApp;

    (void)next();
}

void PrefsPerAppHandler::addSubscription(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage)
{
    if (appId == GLOBAL_APP_ID)
        return;

    // accept only items that is dbtype:M
    if ( PrefsKeyDescMap::instance()->getDbType(key) != DBTYPE_MIXED )
        return;

    // register additional subscription for items dbtype:M

    string subscribeKey = SUBSCRIBE_STR_KEY(key, _CURRENT_APP, category);

    LSHandle* lsHandle = LSMessageGetConnection(lsMessage);
    Utils::subscriptionAdd(lsHandle, subscribeKey.c_str(), lsMessage);

    lock_guard<recursive_mutex> lock(m_container_mutex);
    LSMessageElem messageElem(lsMessage);
    m_categorySubscriptionMessagesMapForValue[category].insert(messageElem);
}

void PrefsPerAppHandler::addDescSubscription(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage)
{
    if (appId == GLOBAL_APP_ID)
        return;

    // accept only items that is dbtype:M
    if ( PrefsKeyDescMap::instance()->getDbType(key) != DBTYPE_MIXED )
        return;

    // register additional subscription for items dbtype:M

    string subscribeKey = SUBSCRIBE_STR_KEYDESC(key, _CURRENT_APP);

    LSHandle* lsHandle = LSMessageGetConnection(lsMessage);
    Utils::subscriptionAdd(lsHandle, subscribeKey.c_str(), lsMessage);

    lock_guard<recursive_mutex> lock(m_container_mutex);
    LSMessageElem messageElem(lsMessage);
    m_keySubscriptionMessagesMapForDesc[key].insert(messageElem);
}

void PrefsPerAppHandler::delSubscription(LSMessage* lsMessage)
{
    SSERVICELOG_INFO("PrefsPerAppHandler", 2,
        PMLOGKS("service", LSMessageGetSenderServiceName(lsMessage)),
        PMLOGKS("appId",   LSMessageGetApplicationID(lsMessage)),
        "delSubscription");

    lock_guard<recursive_mutex> lock(m_container_mutex);

    for (auto& it : m_categorySubscriptionMessagesMapForValue) {
        it.second.erase(LSMessageElem(lsMessage));
    }
    for (auto& it : m_keySubscriptionMessagesMapForDesc) {
        it.second.erase(LSMessageElem(lsMessage));
    }
}

std::set<std::string>& PrefsPerAppHandler::getExcludeAppIdList()
{
    if (m_excludeAppIdListLoaded == false) {
        loadExcludeAppIdList();
    }
    return m_excludeAppIdList;
}

void PrefsPerAppHandler::loadExcludeAppIdList()
{
    std::string inpath = _DEFAULT_PERAPP_EXCLUDE_CONF;

    m_excludeAppIdListLoaded = true;

    std::string content;
    if (!Utils::readFile(inpath, content)) {
        SSERVICELOG_WARNING("PrefsPerAppHandler", 2, PMLOGKS("msg", "cannot open file"), PMLOGKS("path", inpath.c_str()), "loadExcludeAppIdList");
        return;
    }

    pbnjson::JValue jRoot = pbnjson::JDomParser::fromString(content);
    if (jRoot.isNull()) {
        SSERVICELOG_WARNING("PrefsPerAppHandler", 2, PMLOGKS("msg", "cannot parse json"), PMLOGKS("path", inpath.c_str()), "loadExcludeAppIdList");
        return;
    }

    if (!jRoot.isArray()) {
        SSERVICELOG_WARNING("PrefsPerAppHandler", 1, PMLOGKS("msg", "root item must be array"), "loadExcludeAppIdList");
        return;
    }

    for (pbnjson::JValue idx : jRoot.items()) {
        if (idx.isString()) {
            string strItem = idx.asString();
            m_excludeAppIdList.insert(strItem);
        }
    }

    SSERVICELOG_INFO("PrefsPerAppHandler", 1, PMLOGKFV("loaded", "%d", m_excludeAppIdList.size()), "loadExcludeAppIdList");
}

bool PrefsPerAppHandler::isMessageInExclude(LSMessage* lsMessage)
{
    const char* messageAppId = LSMessageGetApplicationID(lsMessage);
    if (messageAppId != NULL && getExcludeAppIdList().count(messageAppId) > 0) {
        return true;
    }
    const char* messageServiceName = LSMessageGetSenderServiceName(lsMessage);
    if (messageServiceName != NULL && getExcludeAppIdList().count(messageServiceName) > 0) {
        return true;
    }
    return false;
}
