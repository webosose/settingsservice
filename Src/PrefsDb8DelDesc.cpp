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

#include "Logging.h"
#include "PrefsDb8DelDesc.h"
#include "PrefsKeyDescMap.h"
#include "SettingsService.h"
#include "SettingsServiceApi.h"

using namespace std;

//
// PrefsDb8DelDesc Class
//
// 1. doFindKeys, cbFindKeys (for preserve keys for subscription) --- MainKind
// 2. doDelKeys, cbDelKeys                   |                    --- MainKind
// 3. -- End-up Task                         V
// 4. postSubscription via      m_SubscriptionKeyValueList
//

bool PrefsDb8DelDesc::sendRequest(LSHandle *lsHandle, string &errorText)
{
    return doFindKeys(lsHandle, errorText);
}

void PrefsDb8DelDesc::sendResultReply(LSHandle *lsHandle, bool success, const string &errorText)
{
    bool result;
    LSError lsError;

    pbnjson::JValue replyRoot(pbnjson::Object());

    LSErrorInit(&lsError);

    postSubscription();

    replyRoot.put("returnValue", success);
    if (!success) {
        if (!errorText.empty()) {
            replyRoot.put("errorText", errorText);
        }

        if (!m_category.empty()) {
            replyRoot.put("category", m_category);
        }
    }

    replyRoot.put("method", SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGDESC);

    result = LSMessageReply(lsHandle, m_message, replyRoot.stringify().c_str(), &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply for del-desc");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    PrefsFactory::instance()->releaseTask(&m_taskInfo, replyRoot);
}

//
// @brief Call sendResultReply with 'returnValue:false' and this->unref()
//
// @param lsHandle  service handle
// @param strErr    error string
//
// @retval          true
//
bool PrefsDb8DelDesc::handleErrorWithRef(LSHandle *lsHandle, const string &strErr)
{
    this->sendResultReply(lsHandle, false, strErr);
    this->unref();
    return true;
}

//
// @brief doFindKeys will find the actual keys to be reset in DB, then call
//        doDelKeys() in callback of this method
//
// @param lsHandle  service handle
// @param strErr    error string
//
// @retval          true
//
bool PrefsDb8DelDesc::doFindKeys(LSHandle *lsHandle, string &errorText)
{
    LSError lsError;
    pbnjson::JValue jsonRoot(pbnjson::Object());
    pbnjson::JValue jsonQuery(pbnjson::Object());
    pbnjson::JValue jsonWhereArr(pbnjson::Array());
    pbnjson::JValue jsonWhereItem;

    LSErrorInit(&lsError);

    // Build Query
    if (!m_category.empty()) {
        jsonWhereItem = pbnjson::Object();
        jsonWhereItem.put("prop", KEYSTR_CATEGORY);
        jsonWhereItem.put("op", "=");
        jsonWhereItem.put("val", m_category);
        jsonWhereArr.append(jsonWhereItem);
    }

    if (!m_keyList.empty()) {
        pbnjson::JValue jsonKeysArr(pbnjson::Array());

        for (const string& key : m_keyList) {
            jsonKeysArr.append(key);
        }
        jsonWhereItem = pbnjson::Object();
        jsonWhereItem.put("prop", KEYSTR_KEY);
        jsonWhereItem.put("op", "=");
        jsonWhereItem.put("val", jsonKeysArr);
        jsonWhereArr.append(jsonWhereItem);
    }

    jsonQuery.put("from", SETTINGSSERVICE_KIND_MAIN_DESC);
    jsonQuery.put("where", jsonWhereArr);

    jsonRoot.put("query", jsonQuery);
    // Send DB8 Request & Defer Success/Fail Response in Callback

    SSERVICELOG_TRACE("%s: %s", __FUNCTION__, jsonQuery.stringify().c_str());

    ref();
    bool result = DB8_luna_call(lsHandle,
        "luna://com.webos.service.db/find",
        jsonRoot.stringify().c_str(),
        PrefsDb8DelDesc::cbFindKeys,
        this, // Context
        NULL,
        &lsError);

    // Handle Error

    if (!result) {
        errorText = MSGID_LSCALL_DB_FIND_FAIL;
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_FIND_FAIL, 2,
            PMLOGKS("Function",lsError.func),
            PMLOGKS("Error",lsError.message),
            "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

//
// @brief Callabck registered in doFindKeys.
//
// this is LSFilterFunc type defined in <lunaservice.h>
//
// @param  lsHandle service handle
// @param  message  reply message
// @param  data     context
//
// @retval true if message is handled.
//
bool PrefsDb8DelDesc::cbFindKeys(LSHandle *lsHandle, LSMessage *message, void *data)
{
    PrefsDb8DelDesc *thiz = (PrefsDb8DelDesc *)data;

    const char *payload = LSMessageGetPayload(message);
    if (payload == NULL) {
        SSERVICELOG_WARNING(MSGID_GET_PAYLOAD_MISSING, 0, " ");
        return thiz->handleErrorWithRef(lsHandle, "missing payload");
    }

    pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
    if (root.isNull()) {
        SSERVICELOG_WARNING(MSGID_GET_PARSE_ERR, 0,
            "function : %s, payload : %s", __FUNCTION__, payload);
        return thiz->handleErrorWithRef(lsHandle, "couldn't parse json");
    }

    pbnjson::JValue label(root["returnValue"]);
    bool successFlag = label.isBoolean() ? label.asBool() : false;

    if (successFlag == false) {
        SSERVICELOG_WARNING(MSGID_GET_DB_RETURNS_FAIL, 0,
            "function : %s, payload : %s", __FUNCTION__, payload);
        return thiz->handleErrorWithRef(lsHandle, "DB8 returns fail");
    }

    pbnjson::JValue resultArr(root["results"]);
    if (!resultArr.isArray()) {
        return thiz->handleErrorWithRef(lsHandle, "no result for result array in DB");
    }

    string errorText;

    if (resultArr.arraySize() == 0) {
        thiz->sendResultReply(lsHandle, true, errorText); // Success if it is empty to update
        thiz->unref(); // unref from doFindKeys()
        return true;
    }

    // Make a 'm_keyValueListAfterFind' with keys from previous result
    for (pbnjson::JValue itemInResults : resultArr.items()) {

        pbnjson::JValue jKey = itemInResults[KEYSTR_KEY];
        if (!jKey.isString()) {
            continue;
        }
        string appId = GLOBAL_APP_ID;
        pbnjson::JValue jAppId = itemInResults[KEYSTR_APPID];
        if (jAppId.isString())
            appId = jAppId.asString();
        if (thiz->m_appId == GLOBAL_APP_ID) { // Global
            if (appId != GLOBAL_APP_ID)
                continue;
        }
        else { // PerApp
            if (thiz->m_appId != appId)
                continue;
        }

        string objectId;
        pbnjson::JValue jId = itemInResults["_id"];
        if (jId.isString()) {
            objectId = jId.asString();
        }

        string keyStr = jKey.asString();
        thiz->m_keyValueListAfterFind.insert(pair<string, string>(keyStr, objectId));
    }

    successFlag = thiz->doDelKeys(lsHandle, errorText);
    if (successFlag == false) {
        return thiz->handleErrorWithRef(lsHandle, errorText);
    }

    thiz->unref(); // unref from doFindKeys()

    return true;
}

//
// @brief Call DB8 request for deleting description
//
// @param lsHandle      instance of LSHandle
// @param errorText     if any error is occured, errorText will contain the description of error
//
// @retval              true if call success
//
bool PrefsDb8DelDesc::doDelKeys(LSHandle *lsHandle, string &errorText)
{
    pbnjson::JValue jsonRoot(pbnjson::Object());
    LSError lsError;

    LSErrorInit(&lsError);

    // Build Query

    // Make an array value of 'key' from the 'm_keyValueListAfterFind'

    pbnjson::JValue jIds(pbnjson::Array());
    for (const std::pair<std::string, std::string>& it : m_keyValueListAfterFind) {
        jIds.append(it.second);
    }

    jsonRoot.put("purge", true);
    jsonRoot.put("ids", jIds);

    // Send DB8 Request & Defer Response in Callback

    ref();
    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/del",
        jsonRoot.stringify().c_str(), PrefsDb8DelDesc::cbDelKeys,
        this, NULL, &lsError);

    // Handle Error

    if (!result) {
        errorText = "DB8 del operation fail";
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_DEL_FAIL, 2,
            PMLOGKS("Function",lsError.func),
            PMLOGKS("Error",lsError.message),
            "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

//
// @brief cbDelKeys is callback for doDelKeys
//
bool PrefsDb8DelDesc::cbDelKeys(LSHandle *lsHandle, LSMessage *message, void *data)
{
    PrefsDb8DelDesc *thiz = (PrefsDb8DelDesc *)data;
    string errorText;
    bool successFlag = false;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_DEL_PAYLOAD_MISSING, 0, " ");
            errorText = string("missing payload");
            break;
        }

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_DEL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = string("couldn't parse json");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean()) {
            successFlag = label.asBool();
        }

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_DEL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "DB8 returns fail";
            break;
        }
    } while(false);

    if (successFlag) {
        for (const std::pair<string, string>& it : thiz->m_keyValueListAfterFind) {

            // Update deleted description items
            PrefsKeyDescMap::instance()->resetKeyDesc(it.first, thiz->m_appId);
        }
    }
    thiz->sendResultReply(lsHandle, successFlag, errorText);

    if (thiz) {
        thiz->unref(); // ref() in doDelKeys()
    }

    return true;
}

//
// @brief getKeyValueListForSubscription just returns m_keyValueListAfterFind that
//        contains the keys updated by the /resetSystemDesc method.
//
// @retval the map of key and its value in json_object form.
//
map<string, string>* PrefsDb8DelDesc::getKeyValueListForSubscription()
{
    return &m_keyValueListAfterFind;
}

//
// @brief postSubscription send the subscription message thru
//        PrefsFactory::postPrefChange() with updated by the /resetSystemDesc method.
//
void PrefsDb8DelDesc::postSubscription()
{
    const char *caller = NULL;
    if (m_message) {
        caller = LSMessageGetApplicationID(m_message);
        if(NULL == caller)
             caller = LSMessageGetSenderServiceName(m_message);
    }

    // the value of each key should be a value from default_table or NULL.
    map<string, string> *list = getKeyValueListForSubscription();

    for (const pair<string, string>& it : *list) {

        // {
        //   "returnValue": true,
        //   "method": "getSystemSettingDesc",
        //   "results": [
        //     ___desc_obj___
        //   ],
        //   "errorText": ""
        // }

        pbnjson::JValue jRoot(pbnjson::Object());

        jRoot.put("returnValue", true);
        jRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC);

        pbnjson::JValue jResultsArr(pbnjson::Array());

        pbnjson::JValue currentValue = PrefsKeyDescMap::instance()->genDescFromCache(it.first, m_appId);
        if (currentValue.isValid()) {
            currentValue.put(KEYSTR_APPID, m_appId);
        }

        jResultsArr.append(currentValue);

        jRoot.put("results", jResultsArr);

        string subscribeKey = SUBSCRIBE_STR_KEYDESC(it.first, m_appId);

        // Really send subscription messages.
        PrefsFactory::instance()->postPrefChange(subscribeKey.c_str(), jRoot, caller);
    }
}
