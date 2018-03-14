// Copyright (c) 2013-2018 LG Electronics, Inc.
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

#include "PrefsDb8GetValues.h"
#include "PrefsKeyDescMap.h"
#include "PrefsNotifier.h"
#include "Utils.h"
#include "Logging.h"
#include "SettingsServiceApi.h"

bool PrefsDb8GetValues::sendRequest(LSHandle * lsHandle)
{
    if (isDescCall()) {
        return sendRequestKeys(lsHandle);
    } else {
        return sendRequestOneKey(lsHandle);
    }
}


#ifdef USE_MEMORY_KEYDESC_KIND
bool PrefsDb8GetValues::sendRequestOneKey(LSHandle * lsHandle)
{
    std::shared_ptr<PrefsHandler> handler = PrefsFactory::instance()->getPrefsHandler(m_key);
    if ( handler )
    {
        pbnjson::JValue replyRoot = handler->valuesForKey(m_key);
        if (!replyRoot.isNull())
        {
            sendHandledValues(lsHandle, replyRoot);
            return true;
        }
    }

    std::set<std::string> keyList;
    keyList.insert(m_key);

    setKeyDescInfoObj(PrefsKeyDescMap::instance()->getKeyDesc(m_category, keyList, m_appId));
    ref();
    cbSendQueryGetOneKey(lsHandle, m_replyMsg, this); // root is released in here

    return true;
}

bool PrefsDb8GetValues::sendRequestKeys(LSHandle * lsHandle)
{
    std::set<std::string> keyList;

    if(m_keyArrayObj.isArray()) {
        for (pbnjson::JValue obj : m_keyArrayObj.items()) {
            if (obj.isString())
                keyList.insert(obj.asString());
        }
    }

    setKeyDescInfoObj(PrefsKeyDescMap::instance()->getKeyDesc(m_category, keyList, m_appId));

    ref();
    cbSendQueryGetKeys(lsHandle, m_replyMsg, this); // root is released in here

    return true;
}
#else
bool PrefsDb8GetValues::sendRequestOneKey(LSHandle * lsHandle)
{
    LSError lsError;
    bool result;

    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootSelectArray(pbnjson::Array());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());

    /*
       luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/find '{"query":{"from":"com.webos.settings.desc.system:1", "where":[{"prop":"key", "op":"=", "val":"3d_mode"}]}}'
     */

    // add select
    replyRootSelectArray.append(KEYSTR_VTYPE);
    replyRootSelectArray.append(KEYSTR_VALUES);
    replyRootSelectArray.append(KEYSTR_CATEGORY);
    replyRootSelectArray.append(KEYSTR_DIMENSION);
    replyRootQuery.put("select", replyRootSelectArray);

    // add where
    //              add key
    if (!m_key.empty()) {
        pbnjson::JValue replyRootItem1(pbnjson::Object());
        replyRootItem1.put("prop", "key");
        replyRootItem1.put("op", "=");
        replyRootItem1.put("val", m_key);
        replyRootWhereArray.append(replyRootItem1);
    }
    //              add category only if it has value
    if (!m_category.empty()) {
        pbnjson::JValue replyRootItem2(pbnjson::Object());
        replyRootItem2.put("prop", "category");
        replyRootItem2.put("op", "=");
        replyRootItem2.put("val", m_category);
        replyRootWhereArray.append(replyRootItem2);
    }
    //              add to where
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", SETTINGSSERVICE_KIND_MAIN_DESC);

    // add reply root
    replyRoot.put("query", replyRootQuery);
    ref();
    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/find", replyRoot.stringify().c_str(), cbSendQueryGetOneKey, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_FIND_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message),
                            "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}

bool PrefsDb8GetValues::sendRequestKeys(LSHandle * lsHandle)
{
    LSError lsError;
    bool result;

    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootSelectArray(pbnjson::Array());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());

    /*
       luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/find '{"query":{"from":"com.webos.settings.desc.system:1", "where":[{"prop":"key", "op":"=", "val":["brightness", "demoMode"]}]}}'
     */

    // add select
    replyRootSelectArray.append(KEYSTR_KEY);
    replyRootSelectArray.append(KEYSTR_UI);
    replyRootSelectArray.append(KEYSTR_VALUES);
    replyRootSelectArray.append(KEYSTR_VTYPE);
    replyRootSelectArray.append(KEYSTR_DBTYPE);
    replyRootSelectArray.append(KEYSTR_VOLATILE);
    replyRootSelectArray.append(KEYSTR_CATEGORY);
    replyRootSelectArray.append(KEYSTR_DIMENSION);
    replyRootQuery.put("select", replyRootSelectArray);

    // add where
    if (!m_keyArrayObj.isNull()) {
        pbnjson::JValue replyRootItem1(pbnjson::Object());
        replyRootItem1.put("prop", "key");
        replyRootItem1.put("op", "=");
        replyRootItem1.put("val", m_keyArrayObj);
        replyRootWhereArray.append(replyRootItem1);
    }
    //              add category only if it has value
    if (!m_category.empty()) {
        pbnjson::JValue replyRootItem2(pbnjson::Object());
        replyRootItem2.put("prop", "category");
        replyRootItem2.put("op", "=");
        replyRootItem2.put("val", m_category);
        replyRootWhereArray.append(replyRootItem2);
    }
    //              add to where
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", SETTINGSSERVICE_KIND_MAIN_DESC);

    // add reply root
    replyRoot.put("query", replyRootQuery);

    ref();
    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/find", replyRoot.stringify().c_str(), cbSendQueryGetKeys, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_FIND_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message),
                            "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}
#endif // USE_MEMORY_KEYDESC_KIND



void PrefsDb8GetValues::sendHandledValues(LSHandle *a_handle, pbnjson::JValue a_values) const
{
    LSError lsError;
    bool result = false;

    LSErrorInit(&lsError);

    a_values.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES);
    a_values.put("subscribed", false);

    // send reply
    if(!m_taskInfo->isBatchCall()){
        result = LSMessageReply(a_handle, m_replyMsg, a_values.stringify().c_str(), &lsError);
        if (!result) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply for get_values");
            LSErrorFree(&lsError);
        }
    }

    // m_taskInfo will be NULL
    PrefsFactory::instance()->releaseTask(&m_taskInfo, a_values);
}

bool PrefsDb8GetValues::cbSendQueryGetOneKey(LSHandle * lsHandle, LSMessage * message, void *data)
{
    LSError lsError;
    bool result;
    bool success = false;
    bool successFlag = false;
    bool subscribeResult = false;
    int arraylen;
    std::string errorText;
    std::string vtype;
    std::string fileData;
    pbnjson::JValue fileDataObj;
    pbnjson::JValue values;

    PrefsDb8GetValues *replyInfo = (PrefsDb8GetValues *) data;

    LSErrorInit(&lsError);

    do {
        pbnjson::JValue root;
    #ifdef USE_MEMORY_KEYDESC_KIND
        root = replyInfo->getKeyDescInfoObj();
    #else
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_GETVAL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        root = pbnjson::JDomParser::fromString(payload);
    #endif // USE_MEMORY_KEYDESC_KIND

        if (root.isNull()){
            SSERVICELOG_WARNING(MSGID_GETVAL_PARSE_ERR, 0 , "function : %s", __FUNCTION__);
            errorText = std::string("Key description is not ready");
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean())
            successFlag = label.asBool();

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_GETVAL_DB_RETURNS_FAIL, 0 , "function : %s", __FUNCTION__);
            errorText = "returnValue from DB is false";
            break;
        }

        pbnjson::JValue keyArray(root["results"]);
        if (!keyArray.isArray()) {
            errorText = "no result in DB";
            break;
        }

        arraylen = keyArray.arraySize();
        if (arraylen <= 0) {
            errorText = "no result in DB";
            break;
        }

        pbnjson::JValue resultRecord(keyArray[0]);
        label = resultRecord["vtype"];
        if (!label.isString()) {
            SSERVICELOG_WARNING(MSGID_GETVAL_NO_VTYPE, 0, " ");
            errorText = "no vtype in the result";
            break;
        }
        vtype = label.asString();

        values = resultRecord["values"];
        if (values.isNull()) {
            SSERVICELOG_WARNING(MSGID_GETVAL_NO_VALUES, 0, " ");
            errorText = "no values in the result";
            break;
        }

        if (vtype == "File") {
            // Read the locale file
            std::string filePath;

            pbnjson::JValue label(values["file"]);
            if (!label.isString()) {
                errorText = "ERROR!! file path is wrong";
            } else {
                filePath = label.asString();
                if (!Utils::readFile(filePath, fileData)) {
                    errorText = std::string("ERROR!! file open: ") + filePath;
                } else {
                    fileDataObj = pbnjson::JDomParser::fromString(fileData);
                    if (fileDataObj.isNull()) {
                        SSERVICELOG_WARNING(MSGID_GETVAL_FILE_VTYPE_ERR , 2, PMLOGKS("File",filePath.c_str()),
                                            PMLOGKS("Reason","external json format error"), "");
                        errorText = std::string("Failed to parsing files: ") + filePath;
                    } else {
                        fileDataObj.put("returnValue", true);
                        success = true;
                    }
                }
            }
        } else {
            success = true;
        }
    } while(false);

    pbnjson::JValue replyRoot;
    if (success) {
        // for file type.
        if (vtype == "File") {
            replyRoot = fileDataObj;
        }
        // for other vtypes
        else {
            //      add settings
            replyRoot = pbnjson::Object();
            if (!replyInfo->m_key.empty() && replyInfo->m_subscribe == true) {
                PrefsNotifier::instance()->addDescSubscriptionPerDimension(lsHandle, replyInfo->m_category, replyInfo->m_key, replyInfo->m_appId, message);
                PrefsNotifier::instance()->addDescSubscription(lsHandle, replyInfo->m_category, replyInfo->m_key, replyInfo->m_appId, message);
                replyInfo->regSubscription(lsHandle, replyInfo->m_key, replyInfo->m_appId);
                subscribeResult = true;
            }

            replyRoot.put("vtype", vtype);
            replyRoot.put("values", values);

            // add return value
            replyRoot.put("returnValue", true);
        }
    }
    // for error
    else {
        replyRoot = pbnjson::Object();
        replyRoot.put("returnValue", false);
        replyRoot.put("error", errorText);
    }

    replyRoot.put("subscribed", subscribeResult);
    replyRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES);

    // send reply
    if(!replyInfo->m_taskInfo->isBatchCall()){
        result = LSMessageReply(lsHandle, replyInfo->m_replyMsg, replyRoot.stringify().c_str(), &lsError);
        if (!result) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply for get-values");
            LSErrorFree(&lsError);
        }
    }

    // m_taskInfo will be NULL
    PrefsFactory::instance()->releaseTask(&replyInfo->m_taskInfo, replyRoot);

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8GetValues::cbSendQueryGetKeys(LSHandle * lsHandle, LSMessage * message, void *data)
{
    LSError lsError;
    bool result;
    bool success = false;
    bool successFlag = false;
    bool subscribeResult = false;
    int arraylen = 0;
    std::string errorText;
    pbnjson::JValue resultArray;

    PrefsDb8GetValues *replyInfo = (PrefsDb8GetValues *) data;


    LSErrorInit(&lsError);

    do {
        pbnjson::JValue root;
    #ifdef USE_MEMORY_KEYDESC_KIND
        root = replyInfo->getKeyDescInfoObj();
        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());
    #else
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_GETVAL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        root = pbnjson::JDomParser::fromString(payload);
    #endif // USE_MEMORY_KEYDESC_KIND

        if (root.isNull()){
            SSERVICELOG_WARNING(MSGID_GETVAL_PARSE_ERR, 0 , "function : %s", __FUNCTION__);
            errorText = std::string("Key description is not ready");
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean())
            successFlag = label.asBool();

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_GETVAL_DB_RETURNS_FAIL, 0 , "function : %s", __FUNCTION__);
            errorText = "returnValue from DB is false";
            break;
        }

        resultArray = root["results"];
        if (!resultArray.isArray()) {
            errorText = "no result in DB";
            SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());
            break;
        }

        arraylen = resultArray.arraySize();
        if (arraylen <= 0) {
            errorText = "no result in DB";
            break;
        }

        success = true;

    } while (false);
    // send reply
    pbnjson::JValue replyRoot(pbnjson::Object());

    //      add settings
    if (success) {
        // set subscription
        std::set < std::string > reqKeyList;
        std::set < std::string > resultKeyList;

        // subscribe
        resultKeyList = replyInfo->getKeyList(resultArray);
        if (replyInfo->m_subscribe == true) {
            for (const std::string& key : resultKeyList) {
                PrefsNotifier::instance()->addDescSubscriptionPerDimension(lsHandle, replyInfo->m_category, key, replyInfo->m_appId, message);
                PrefsNotifier::instance()->addDescSubscription(lsHandle, replyInfo->m_category, key, replyInfo->m_appId, message);
                replyInfo->regSubscription(lsHandle, key, replyInfo->m_appId);
                subscribeResult = true;
            }
        }
        // add return value
        if (replyInfo->m_keyArrayObj.isArray() && replyInfo->m_keyArrayObj.arraySize() != arraylen) {
            reqKeyList = replyInfo->getKeyList(replyInfo->m_keyArrayObj);

            pbnjson::JValue errorKeyArrayObj(pbnjson::Array());
            replyRoot.put("returnValue", false);
            for (const std::string& key1 : reqKeyList) {
                bool keyNotFound = true;
                for (const std::string& key2 : resultKeyList) {
                    if (key1 == key2) {
                        keyNotFound = true;
                        break;
                    }
                }
                if (keyNotFound) {
                    errorKeyArrayObj.append(key1);
                }
            }

            replyRoot.put("errorKey", errorKeyArrayObj);
        } else {
            replyRoot.put("returnValue", true);
        }

        replyRoot.put("results", resultArray);
    } else {
        replyRoot.put("returnValue", false);
        replyRoot.put("errorText", errorText);
    }

    if (replyInfo->m_appId != GLOBAL_APP_ID) {
        replyRoot.put(KEYSTR_APPID, replyInfo->m_appId);
    }

    replyRoot.put("subscribed", subscribeResult);
    replyRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC);

    // send reply
    if(!replyInfo->m_taskInfo->isBatchCall()){
        result = LSMessageReply(lsHandle, replyInfo->m_replyMsg, replyRoot.stringify().c_str(), &lsError);
        if (!result) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply for get-settings");
            LSErrorFree(&lsError);
        }
    }

    // m_taskInfo will be NULL
    PrefsFactory::instance()->releaseTask(&replyInfo->m_taskInfo, replyRoot);

    if (replyInfo)
        replyInfo->unref();

    return true;
}

void PrefsDb8GetValues::regSubscription(LSHandle * lsHandle, const std::string &key, const std::string& a_appId)
{
    std::string subscribeKey = isDescCall() ? SUBSCRIBE_STR_KEYDESC(key, a_appId) : SUBSCRIBE_STR_KEYVALUE(key, a_appId);

    Utils::subscriptionAdd(lsHandle, subscribeKey.c_str(), m_replyMsg);

    if (isDescCall()) {
        PrefsNotifier::instance()->addDescSubscriptionPerApp(m_category, key, m_appId, m_replyMsg);
    }
    else {
        // TODO:
        // PerApp(app_id) request for /getSystemSettingValues
        // consider this situation
    }
}

std::set<std::string> PrefsDb8GetValues::getKeyList(pbnjson::JValue keyArrayObj)
{
    std::set<std::string> keyList;

    for (pbnjson::JValue item : keyArrayObj.items()) {
        if (item.isNull()) {
            continue;
        }

        if (item.isString()) {
            keyList.insert(item.asString());
        } else {
            pbnjson::JValue keyStr = item["key"];
            if (keyStr.isString()) {
                keyList.insert(keyStr.asString());
            }
        }
    }

    return keyList;
}
