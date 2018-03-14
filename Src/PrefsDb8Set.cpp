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

#include "InfoLogger.h"
#include "Logging.h"
#include "PrefsDb8Set.h"
#include "PrefsFileWriter.h"
#include "PrefsNotifier.h"
#include "PrefsVolatileMap.h"
#include "Utils.h"
#include "SettingsServiceApi.h"
#include <iostream>

/*  setSystemSettings call tree
    |
    sendRequest
    |
    sendGetValuesRequest----+
    |                       |
    sendMergeRequest -- O   sendMergeRequestDefKind -- O
    |                       |
    sendPutRequest -- O     sendPutRequestDefKind -- O
*/
struct PrefsDebugUtil
{
    static void dump_category_keys(PrefsDb8Set &set);
};

bool PrefsDb8Set::sendRequest(LSHandle * lsHandle)
{
    return sendGetValuesRequest(lsHandle);
    //return sendMergeRequest(lsHandle);
}

#ifdef USE_MEMORY_KEYDESC_KIND
bool PrefsDb8Set::sendGetValuesRequest(LSHandle * lsHandle)
{
    std::set<std::string> keyList;

    for(pbnjson::JValue::KeyValue it : m_keyListObj.children()) {
        if (it.first.isString())
            keyList.insert(it.first.asString());
    }

    ref();
    m_keyDescRoot = PrefsKeyDescMap::instance()->getKeyDesc(m_category, keyList, m_app_id);
    cbGetValuesRequest(lsHandle, NULL, this); // root is released in here

    return true;
}
#else
bool PrefsDb8Set::sendGetValuesRequest(LSHandle * lsHandle)
{
    LSError lsError;

    pbnjson::JObject replyRoot;
    pbnjson::JObject replyRootQuery;
    pbnjson::JArray replyRootKeyArray;
    pbnjson::JArray replyRootWhereArray;
    pbnjson::JObject replyRootItem1;

    LSErrorInit(&lsError);

    /*
       luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/find '{"query":{"from":"com.webos.settings.desc.system:1", "where":[{"prop":"key", "op":"=", "val":["brightness", "contrast"]}]}}'
     */

    for(pbnjson::JValue::KeyValue it : m_keyListObj.children()) {
        replyRootKeyArray.append(it.first);
    }

    replyRootItem1.put("prop", "key");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", replyRootKeyArray);
    replyRootWhereArray.append(replyRootItem1);

    replyRootQuery.put("from", SETTINGSSERVICE_KIND_DFLT_DESC);
    replyRootQuery.put("where", replyRootWhereArray);

    replyRoot.put("query", replyRootQuery);

    ref();
    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/find", replyRoot.stringify().c_str(), PrefsDb8Set::cbGetValuesRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_FIND_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}
#endif // USE_MEMORY_KEYDESC_KIND

/**
 * Try to merge objects.
 * If fails, try to put objects.
 *
 * @sa PrefsDb8Set::cbMergeRequest()
 */
bool PrefsDb8Set::sendMergeRequest(LSHandle * lsHandle)
{
    const PrefsKeyDescMap* keyDescMap = PrefsKeyDescMap::instance();

    LSError lsError;
    pbnjson::JObject jsonObjParam;
    pbnjson::JArray jsonArrOperations;
    std::string targetKind = SETTINGSSERVICE_KIND_MAIN;

    m_subscribeAppId = m_app_id;

    LSErrorInit(&lsError);

    // SetAll
    if ( m_setAll ) {
        m_requestCategoryDimKeysMap = keyDescMap->getCategoryKeyListMapAll(m_category, m_successKeyList);
    }
    // Global
    else if (m_app_id == GLOBAL_APP_ID) {
        m_requestCategoryDimKeysMap = keyDescMap->getCategoryKeyListMap(m_category, m_dimensionObj, m_successKeyList);
    }
    // PerApp
    else {
        std::set<std::string> globalKeys, perAppKeys;
        keyDescMap->splitKeysIntoGlobalOrPerAppByDescription(m_successKeyList, m_category, m_app_id, globalKeys, perAppKeys);

        if (perAppKeys.size() > 0) {
            m_dimensionObj = pbnjson::JValue();
            m_successKeyList = perAppKeys;
            m_errorKeyList.insert(globalKeys.begin(), globalKeys.end());
            std::string categoryDim = m_category;
            keyDescMap->getCategoryDim(m_category, categoryDim);
            m_requestCategoryDimKeysMap[categoryDim] = perAppKeys;
        }
        else {
            m_app_id = GLOBAL_APP_ID; /* Ignore app_id in parameters */
            m_requestCategoryDimKeysMap = keyDescMap->getCategoryKeyListMap(m_category, m_dimensionObj, m_successKeyList);
        }
    }

    for (const CategoryDimKeyListMap::value_type& cat_key_iter : m_requestCategoryDimKeysMap) {
        jsonArrOperations.append(jsonMergeBatchItem(cat_key_iter.first, cat_key_iter.second, targetKind.c_str(), m_keyListObj, m_app_id));
    }

    ref();
    jsonObjParam.put("operations", jsonArrOperations);
    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), PrefsDb8Set::cbMergeRequest, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}


bool PrefsDb8Set::sendMergeRequestDefKind(LSHandle * lsHandle)
{
    // TODO: refactoring with sendMergeRequest()
    const PrefsKeyDescMap* keyDescMap = PrefsKeyDescMap::instance();

    LSError lsError;
    pbnjson::JObject jsonObjParam;
    pbnjson::JArray jsonArrOperations;
    std::string targetKind = SETTINGSSERVICE_KIND_DEFAULT;

    LSErrorInit(&lsError);

    // SetAll
    if ( m_setAll ) {
        m_requestCategoryDimKeysMap = keyDescMap->getCategoryKeyListMapAll(m_category, m_successKeyList);
    }
    // Global
    else if (m_app_id == GLOBAL_APP_ID) {
        m_requestCategoryDimKeysMap = keyDescMap->getCategoryKeyListMap(m_category, m_dimensionObj, m_successKeyList);
    }
    // PerApp
    else {
        std::set<std::string> globalKeys, perAppKeys;
        keyDescMap->splitKeysIntoGlobalOrPerAppByDescription(m_successKeyList, m_category, m_app_id, globalKeys, perAppKeys);

        if (perAppKeys.size() > 0) {
            m_dimensionObj = pbnjson::JValue();
            m_successKeyList = perAppKeys;
            m_errorKeyList.insert(globalKeys.begin(), globalKeys.end());
            std::string categoryDim = m_category;
            keyDescMap->getCategoryDim(m_category, categoryDim);
            m_requestCategoryDimKeysMap[categoryDim] = perAppKeys;
        }
        else {
            m_app_id = GLOBAL_APP_ID; /* Ignore app_id in parameters */
            m_requestCategoryDimKeysMap = keyDescMap->getCategoryKeyListMap(m_category, m_dimensionObj, m_successKeyList);
        }
    }

    for (const CategoryDimKeyListMap::value_type& cat_key_iter : m_requestCategoryDimKeysMap) {
        jsonArrOperations.append(jsonMergeBatchItem(cat_key_iter.first, cat_key_iter.second, targetKind.c_str(), m_keyListObj, m_app_id));
    }

    jsonObjParam.put("operations", jsonArrOperations);
    ref();
    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), PrefsDb8Set::cbMergeRequestDefKind, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

/* Generated json object example for merge query param
   {
     "props": { "value": { "avMode": "on" } },
     "query": {
       "from": "com.webos.settings.default:1",
       "where": [
         { "prop": "category", "op": "=", "val": "picture$comp3.natural.x" },
         { "prop": "app_id", "op": "=", "val": [ "", "com.webos.BDP" ] }
       ]
     }
   }
 */
pbnjson::JValue PrefsDb8Set::jsonMergeBatchItem(const std::string &categoryName, const std::set < std::string >& keyList, const std::string& targetKindName, pbnjson::JValue allKeyListObj, const std::string& appId)
{
    pbnjson::JObject keyListObj;
    pbnjson::JObject jsonObjParam;
    pbnjson::JObject replyRoot;
    pbnjson::JObject replyRootProps;
    pbnjson::JObject replyRootQuery;
    pbnjson::JArray replyRootWhereArray;
    pbnjson::JObject replyRootItem1;

    // create new keyListObj(settings) object
    for (const std::string& it : keyList) {
        pbnjson::JValue selobj = allKeyListObj[it];
        if (selobj.isNull()) continue;
        keyListObj.put(it, selobj);
    }
    replyRootProps.put("value", keyListObj);

    // add category to where
    replyRootItem1.put("prop", "category");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", categoryName);
    replyRootWhereArray.append(replyRootItem1);

    // add app_id to where
    pbnjson::JObject replyRootItem2;
    replyRootItem2.put("prop", "app_id");
    replyRootItem2.put("op", "=");
    replyRootItem2.put("val", appId);
    replyRootWhereArray.append(replyRootItem2);

    // add country to where, for default kind
    if ( m_defKindFlag && !m_country.empty() ) {
        pbnjson::JObject replyRootItem3;
        replyRootItem3.put("prop", "country");
        replyRootItem3.put("op", "=");
        replyRootItem3.put("val", m_country);
        replyRootWhereArray.append(replyRootItem3);
    }

    // add volatile to where if targetKind is main

    if (targetKindName == SETTINGSSERVICE_KIND_MAIN) {
        pbnjson::JObject replyRootItem4;
        replyRootItem4.put("prop", "volatile");
        replyRootItem4.put("op", "=");
        replyRootItem4.put("val", false);
        replyRootWhereArray.append(replyRootItem4);
    }

    // add to query
    replyRootQuery.put("from", targetKindName);
    replyRootQuery.put("where", replyRootWhereArray);

    // add to root
    replyRoot.put("props", replyRootProps);
    replyRoot.put("query", replyRootQuery);

    // build return object
    jsonObjParam.put("method", "merge");
    jsonObjParam.put("params", replyRoot);

    return jsonObjParam;
}

void PrefsDb8Set::storeDone(const CategoryDimKeyListMap::value_type& done)
{
    CategoryDimKeyListMap::iterator stored = m_storedCategoryDimKeyMap.find(done.first);

    /* It is possible same category-dimension string is handled
     * if both non-volatile and volatile data are requested */

    if ( stored != m_storedCategoryDimKeyMap.end() ) {
        stored->second.insert(done.second.begin(), done.second.end());
    } else {
        m_storedCategoryDimKeyMap.insert(done);
    }
}

/**
 * Handle un-processed items and
 * handle items stored by storeDone() into m_storedCategoryDimKeyMap
 *
 * In analyzeMergeBatchResult(), it is removed that items in
 * m_requestCategoryDimKeysMap if it has been
 * processed successfully.
 *
 * @sa analyzeMergeBatchResult()
 * @sa storeDone()
 */
void PrefsDb8Set::updateModifiedKeyInfo(void)
{
    /* Find error keys and stored keys */
    for (const CategoryDimKeyListMap::value_type& failed : m_requestCategoryDimKeysMap) {
        m_mergeFailKeyList.insert(failed.second.begin(), failed.second.end());
    }

    /* m_storedCategoryDimKeyMap is not initialized
     * even volatile key is handled at the same time */
    for (const CategoryDimKeyListMap::value_type& stored : m_storedCategoryDimKeyMap) {
        m_toBeNotifiedKeyList.insert(stored.second.begin(), stored.second.end());
    }
}

/* Generated json object example for put
 {
   "_kind": "com.webos.settings.default:1",
   "value": { "avMode": "3" },
   "app_id": "",
   "category": "picture$comp3.natural.x",
   "volatile": false
 }
*/
pbnjson::JValue PrefsDb8Set::jsonPutObject(const std::string &categoryName, const std::set < std::string >& keyList, const std::string& targetKindName, pbnjson::JValue allKeyListObj, const std::string& appId)
{
    bool volatile_flag = (targetKindName == SETTINGSSERVICE_KIND_MAIN_VOLATILE);
    pbnjson::JObject jsonObj;
    pbnjson::JObject keyListObj;

    jsonObj.put("_kind", targetKindName);
    // create new keyListObj(settings) object
    for (const std::string& key : keyList) {
        pbnjson::JValue selobj = allKeyListObj[key];
        if (selobj.isNull()) continue;
        keyListObj.put(key, selobj);
    }
    jsonObj.put("app_id", appId);
    jsonObj.put("value", keyListObj);
    jsonObj.put("category", categoryName);
    jsonObj.put("volatile", volatile_flag);
    if(m_defKindFlag && !m_country.empty()) {
        jsonObj.put("country", m_country);
    }

    return jsonObj;
}

void PrefsDb8Set::fillPutRequest(const char* kind, /*out*/ pbnjson::JValue jsonObjParam)
{
    pbnjson::JArray jsonArrObjects;

    for (auto& itCatDim : m_requestCategoryDimKeysMap) {
        jsonArrObjects.append(jsonPutObject(itCatDim.first, itCatDim.second, kind, m_keyListObj, m_app_id));
    }
    jsonObjParam.put("objects", jsonArrObjects);
}

bool PrefsDb8Set::sendPutRequest(LSHandle * lsHandle, const char* kind, LSFilterFunc callback)
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JObject jsonObjParam;
    fillPutRequest(kind, jsonObjParam);

    ref();
    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/put", jsonObjParam.stringify().c_str(), callback, this, nullptr, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_PUT_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

bool PrefsDb8Set::handleVolatileKey()
{
    CategoryDimKeyListMap mergeCategoryDimKeyMap;

    updateModifiedKeyInfo();

    if (m_setAll) {
        mergeCategoryDimKeyMap = PrefsKeyDescMap::instance()->getCategoryKeyListMapAll(m_category, m_successKeyListVolatile);
        m_storedCategoryDimKeyMap = mergeCategoryDimKeyMap;
    } else {
        mergeCategoryDimKeyMap = PrefsKeyDescMap::instance()->getCategoryKeyListMap(m_category, m_dimensionObj, m_successKeyListVolatile);
    }

    for (const CategoryDimKeyListMap::value_type& cat_key_iter : mergeCategoryDimKeyMap) {
        for (const std::string& itKey : cat_key_iter.second) {
            if (PrefsKeyDescMap::instance()->isVolatileKey(itKey) == false)
                continue;
            pbnjson::JValue jObjVal = m_keyListObj[itKey];
            if (!jObjVal.isNull()) {
                PrefsVolatileMap::instance()->setVolatileValue(cat_key_iter.first, m_app_id, itKey, jObjVal.stringify());
                // whenever the value of key is not changed, subscription will be sent.
                m_toBeNotifiedKeyList.insert(itKey);
            }
        }
    }

    return true;
}

bool PrefsDb8Set::cbGetValuesRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    LSError lsError;
    bool success = false;
    std::string errorText("???");
    PrefsDb8Set *replyInfo = (PrefsDb8Set *) data;

    pbnjson::JValue root;
    do {
        LSErrorInit(&lsError);

    #ifdef USE_MEMORY_KEYDESC_KIND
        root = replyInfo->m_keyDescRoot;
    #else
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SET_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        root = pbnjson::JDomParser::fromString(payload);
    #endif // USE_MEMORY_KEYDESC_KIND

        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SET_PARSE_ERR, 0, "function : %s", __FUNCTION__);
            errorText = "Key description is not ready";
            break;
        }

        if(!replyInfo->parsingKeyDescInfo(root, errorText)) {
            SSERVICELOG_WARNING(MSGID_SET_DESC_ERR, 0, "KeyDescInfo parse fail. Reason : %s", errorText.c_str());
            break;
        }
        // This log means that change request is received. The request could be failed.
        InfoLogger::instance()->logSetChange(replyInfo->m_category, replyInfo->m_successKeyListObj,
                LSMessageGetApplicationID(replyInfo->m_replyMsg) ?
                    LSMessageGetApplicationID(replyInfo->m_replyMsg) : LSMessageGetSenderServiceName(replyInfo->m_replyMsg));

        // for default kind, setting factory values
        if (replyInfo->m_defKindFlag) {
            // only if all items are success.
            if (replyInfo->m_successKeyList.size() == replyInfo->m_totalN) {
                // store and send reply
                if (replyInfo->m_successKeyList.size()) {
                    /* FIXME: If user-modified settings is in settings.system kind,
                     * this factory setting would send incorrect subscription message */
                    success = replyInfo->sendMergeRequestDefKind(lsHandle);
                } else {
                    SSERVICELOG_DEBUG("There are some keys that are not for Default Value.");
                    errorText = "ERROR!! There are some keys that are not for Default Value.";
                }

                if (!success) {
                    SSERVICELOG_WARNING(MSGID_SET_MERGE_FAIL, 1, PMLOGKS("target","default"), "Error sending a request to DB");
                    errorText = "ERROR!! sending a request to DB";
                }
            } else {
                errorText = "Not all items are successful.";
            }
        }
        // for base and volatile kind
        else if (replyInfo->m_errorKeyList.size() == 0) {
            if (replyInfo->m_successKeyListVolatile.size() > 0) {
                if (replyInfo->m_storeFlag) {
                    success = replyInfo->handleVolatileKey();
                    if (!success) {
                        SSERVICELOG_WARNING(MSGID_SET_MERGE_FAIL, 1, PMLOGKS("target", "volatile"), "ERROR sending a request to DB");
                        errorText = "ERROR!! sending a request to DB";
                    } else if (replyInfo->m_successKeyList.size() == 0) {
                        replyInfo->sendResultReply(lsHandle, errorText);
                    }
                }
                else {
                    replyInfo->m_toBeNotifiedKeyList.insert(
                        replyInfo->m_successKeyListVolatile.begin(),
                        replyInfo->m_successKeyListVolatile.end());
                    success = true;
                }
            }

            // store and send reply
            //              send notify & reply without saving
            //      Both store and notify flags shouldn't be false
            if (replyInfo->m_successKeyList.size() > 0) {
                if (replyInfo->m_storeFlag) {
                    success = replyInfo->sendMergeRequest(lsHandle);
                    if (!success) {
                        SSERVICELOG_WARNING(MSGID_SET_MERGE_FAIL , 1, PMLOGKS("target","volatile"), "ERROR sending a request to DB");
                        errorText = "ERROR!! sending a request to DB";
                    }
                }
                else {
                    replyInfo->m_toBeNotifiedKeyList.insert(
                        replyInfo->m_successKeyList.begin(),
                        replyInfo->m_successKeyList.end());
                    success = true;
                }
            }
        }
    } while (false);

    // if one of items have invalid value
    if (success) {
        // when store flag is false.send a reply for true.
        if(!replyInfo->m_storeFlag) {
            if(replyInfo->sendResultReply(lsHandle, errorText)) {
                // do nothing.
            }
        }
        else {
            // do nothing for waiting for the process
        }
    } else {
        SSERVICELOG_WARNING(MSGID_SET_MERGE_FAIL, 0, "Fail to handle request for changing settings.%s. Payload : %s", errorText.c_str(), root.stringify().c_str());

        // send error reply
        pbnjson::JObject replyRoot;
        replyRoot.put("returnValue", false);
        replyRoot.put("errorText", errorText);

        if (!replyInfo->m_errorKeyList.empty()) {
            pbnjson::JArray errorKeyArray;

            for (const std::string& key : replyInfo->m_errorKeyList) {
                errorKeyArray.append(key);
            }
            replyRoot.put("errorKey", errorKeyArray);
        }

        replyRoot.put("method", replyInfo->m_defKindFlag ? SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYVALUE :
                                                           SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS);

        if(!replyInfo->m_taskInfo->isBatchCall()){
            bool result = LSMessageReply(lsHandle, replyInfo->m_replyMsg, replyRoot.stringify().c_str(), &lsError);
            if (!result) {
                SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply for get values");
                LSErrorFree(&lsError);
            }
        }

        // m_taskInfo will be NULL
        PrefsFactory::instance()->releaseTask(&replyInfo->m_taskInfo, replyRoot);
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

/* Func analyzeMergeBatchResult compares batch request and corresponding responses
 * If the number of batch operations and responses are exactly same,
 * Func analyzeMergeBatchResult return true.
 * The false return values means that there is format error in DB8 query result.
 * When there is some error - somthing is handled, something is not - ,
 * The keys in m_mergeCategoryDimKey are moved into m_storedCategoryDimKeyMap
 * if merge operation is success.
 * But, failed keys are remained. The remains are handled in the next put function.
 * Event though all operations has 'returnValue=false', this func returns true.
 */
bool PrefsDb8Set::analyzeMergeBatchResult(pbnjson::JValue root)
{
    int batch_idx = 0;
    CategoryDimKeyListMap::iterator cat_key_iter;

    pbnjson::JValue label(root["returnValue"]);
    if (!label.isBoolean() || !label.asBool()) {
        SSERVICELOG_WARNING(MSGID_SET_DB_RETURNS_FAIL, 0 ,"in %s, payload : %s", __FUNCTION__, root.stringify().c_str());
        return false;
    }

    pbnjson::JValue arr_merge_rsp = root["responses"];

    /* Analyze Batch Results with Requested Data */
    if (!arr_merge_rsp.isArray()) {
        SSERVICELOG_WARNING(MSGID_SET_BATCH_ERR, 0, "Incorrect batch result");
        return false;
    }

    if (static_cast<size_t>(arr_merge_rsp.arraySize()) != m_requestCategoryDimKeysMap.size()) {
        SSERVICELOG_WARNING(MSGID_SET_DATA_SIZE_ERR, 0, "Unexpected responses size in batch(merge) result");
        return false;
    }

    /* Remove stored settings in m_requestCategoryDimKeysMap
     * Rmains are handle by put operation in the next call back function */
    for ( cat_key_iter = m_requestCategoryDimKeysMap.begin();
            cat_key_iter != m_requestCategoryDimKeysMap.end();
            /* nothing because erasing in loop */ ) {
        pbnjson::JValue resp_obj = arr_merge_rsp[batch_idx++];

        pbnjson::JValue resp = resp_obj["returnValue"];
        if ( resp.isBoolean() && resp.asBool() ) {
            resp = resp_obj["count"];
            /* TODO: How to do if count is greater than ONE */
            if (resp.isNumber() && resp.asNumber<int>() > 0) {
                storeDone(*cat_key_iter);
                cat_key_iter = m_requestCategoryDimKeysMap.erase(cat_key_iter);
                if ( batch_idx >= arr_merge_rsp.arraySize() )
                    break;
                continue;
            }
        }
        cat_key_iter++;
    }

    return true;
}

bool PrefsDb8Set::cbMergeRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool end_call_chain = true;
    std::string errorText;
    PrefsDb8Set *replyInfo = (PrefsDb8Set *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SET_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SET_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "couldn't parse json";
            break;
        }

        bool success = replyInfo->analyzeMergeBatchResult(root);
        if (!success) {
            SSERVICELOG_WARNING(MSGID_SET_BATCH_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("Unexpected batch result in MERGE responses");
            break;
        }

        if ( !replyInfo->m_requestCategoryDimKeysMap.empty() ) {
            /* If there is volatile data, those are handled by seperated DB8 query */
            if (!(success = replyInfo->sendPutRequest(lsHandle, SETTINGSSERVICE_KIND_MAIN, PrefsDb8Set::cbPutRequest))) {
                errorText = "Fail to sending put rquest";
            }
            else {
                end_call_chain = false;
            }
        }
    } while(false);

    if (end_call_chain) {
        if(replyInfo->sendResultReply(lsHandle, errorText)) {
            // do nothing.
        }
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8Set::cbPutRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool end_call_chain  = true;
    std::string errorText;
    PrefsDb8Set *replyInfo = (PrefsDb8Set *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SET_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SET_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "couldn't parse json";
            break;
        }

        /* PUT operation is atomic.
         * all requested objects are stored or nothing */
        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean() && label.asBool()) {
            for (const CategoryDimKeyListMap::value_type& merged : replyInfo->m_requestCategoryDimKeysMap) {
                replyInfo->storeDone(merged);
            }
            replyInfo->m_requestCategoryDimKeysMap.clear();

        } else {
            /* m_requestCategoryDimKeysMap includes something due to the put error,
             * those are reported as 'errorKeys' by sendResultReply func. */
        }
    } while(false);

    if (end_call_chain) {
        if(replyInfo->sendResultReply(lsHandle, errorText)) {
        }
        else {
            // do nothing. waiting for description cache update
            // replyInfo is released after cache update.
        }
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8Set::cbPutRequestVolatile(LSHandle * lsHandle, LSMessage * message, void *data)
{
    std::string errorText;
    PrefsDb8Set *replyInfo = (PrefsDb8Set *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SET_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

       SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

       pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
       if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SET_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "couldn't parse json";
            break;
        }

        /* PUT operation is atomic.
         * all requested objects are stored or nothing */
        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean() && label.asBool()) {
            for (const CategoryDimKeyListMap::value_type& merged : replyInfo->m_requestCategoryDimKeysMap) {
                replyInfo->storeDone(merged);
            }
            replyInfo->m_requestCategoryDimKeysMap.clear();
        } else {
            /* If m_requestCategoryDimKeysMap error keys,
             * Those are reported as 'errorKeys' by sendResultReply func. */
        }
    } while(false);

    if(replyInfo->sendResultReply(lsHandle, errorText)) {

    }
    else {
        // do nothing. waiting for description cache update
        // replyInfo is released after cache update.
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8Set::cbMergeRequestDefKind(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool end_call_chain = true;
    std::string errorText;
    PrefsDb8Set *replyInfo = (PrefsDb8Set *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SET_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

       SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

       pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
       if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SET_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "couldn't parse json";
            break;
        }

        bool success = replyInfo->analyzeMergeBatchResult(root);
        if (!success) {
            SSERVICELOG_WARNING(MSGID_SET_BATCH_ERR , 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "Unexpected batch result in MERGE responses";
            break;
        }

        if ( !replyInfo->m_requestCategoryDimKeysMap.empty() ) {
            // if there are some keys that are not merged, try again with 'put' request.
            if (!(success = replyInfo->sendPutRequest(lsHandle, SETTINGSSERVICE_KIND_DEFAULT, PrefsDb8Set::cbPutRequestDefKind))) {
                SSERVICELOG_WARNING(MSGID_SET_MERGE_ERR , 1, PMLOGKS("target","default"), "Fail to sending put rquest");
                errorText = "Fail to sending put rquest";
            }
            else {
                end_call_chain = false; // wait for the result of other db request
            }
        }
    } while(false);

    if (end_call_chain) {
        if(replyInfo->sendResultReply(lsHandle, errorText)) {
        }
        else {
            // do nothing. waiting for description cache update
            // replyInfo is released after cache update.
        }
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8Set::cbPutRequestDefKind(LSHandle * lsHandle, LSMessage * message, void *data)
{
    std::string errorText;
    PrefsDb8Set *replyInfo = (PrefsDb8Set *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SET_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SET_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "couldn't parse json";
            break;
        }

        /* PUT operation is atomic.
         * all requested objects are stored or nothing */
        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean() && label.asBool()) {
            for (const CategoryDimKeyListMap::value_type& merged : replyInfo->m_requestCategoryDimKeysMap) {
                replyInfo->storeDone(merged);
            }
            replyInfo->m_requestCategoryDimKeysMap.clear();
        } else {
            /* If m_requestCategoryDimKeysMap includes error keys,
             * Those are reported as 'errorKeys' by sendResultReply func. */
        }
    } while(false);

    if(replyInfo->sendResultReply(lsHandle, errorText)) {
        // do nothing.
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}


bool PrefsDb8Set::sendResultReply(LSHandle * lsHandle, std::string & errorText)
{
    m_lsHandle = lsHandle;
    m_errorText = errorText;

    updateModifiedKeyInfo();

    if(m_successKeyList.size() + m_successKeyListVolatile.size() !=
            m_mergeFailKeyList.size() + m_toBeNotifiedKeyList.size()) {
        SSERVICELOG_WARNING(MSGID_SET_DATA_SIZE_ERR, 4,
                PMLOGKFV("successKeyList size", "%zd", m_successKeyList.size()),
                PMLOGKFV("successKeyListVolatile size", "%zd", m_successKeyListVolatile.size()),
                PMLOGKFV("mergeFailKeyList size", "%zd", m_mergeFailKeyList.size()),
                PMLOGKFV("toBeNotifiedKeyList size", "%zd", m_toBeNotifiedKeyList.size()), "");
    }

    // Early notify to the dimension changes.
    // Notifier will track down the dimension values at this stage.
    //
    {
        PrefsNotifier::instance()->notifyEarly(m_category, m_toBeNotifiedKeyList);
    }

    // update dimension key values. // sendResultReply is called after updating dimension key
    if(PrefsKeyDescMap::instance()->updateDimKeyValue(this, m_toBeNotifiedKeyList, m_dimensionObj)) {
        return false;
    }
    sendResultReply();
    return true;
}

void PrefsDb8Set::reference(void)
{
    ref();
}

void PrefsDb8Set::finalize(void)
{
    sendResultReply();
    unref();
}

std::string PrefsDb8Set::getSettingValue(const char *reqkey)
{
    if (reqkey) {
        for(pbnjson::JValue::KeyValue it : m_keyListObj.children()) {
            if (it.first.asString() == reqkey) {
                return it.second.asString(); /// TODO check if stringify() is better
            }
        }
    }
    return "";
}

void PrefsDb8Set::sendResultReply()
{
    pbnjson::JObject replyRoot;
    pbnjson::JArray completedKeyArray;
    pbnjson::JArray errorKeyArray;
    const char *sender = nullptr;

    if (m_notifySelf == false)
    {
        sender = LSMessageGetSender(m_replyMsg);
    }

    LSError lsError;
    LSErrorInit(&lsError);

    /* m_successKeyListVolatileObj doesn't include localeInfo */
    PrefsFileWriter::instance()->updateFilesIfTargetExistsInSettingsObj(m_category, m_successKeyListObj);

    // subscribe (send notify)
    if (!m_defKindFlag && m_notifyFlag) {
        postSubscription(sender);
    }

    /* setSystemSettings set 'returnValue' as true
     * if all non-volatile and volatile key is sotred successfully.
     */

    bool returnValue = (m_successKeyList.size() + m_successKeyListVolatile.size() == m_toBeNotifiedKeyList.size());

    replyRoot.put("method", m_defKindFlag ? SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYVALUE :
                                            SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS);

    if (m_storeFlag == false) {
        replyRoot.put(KEYSTR_STORE, false);
    }

    if (m_app_id != GLOBAL_APP_ID && m_errorKeyList.empty() == false) {
        returnValue = false;
    }

    replyRoot.put("returnValue", returnValue);
    if (!returnValue) {
        for (const std::string& iter : m_toBeNotifiedKeyList)
            completedKeyArray.append(iter);
        // m_errorKeyList, m_successKeyList, and m_successKeyListVolatile are set in parsing key description info
        for (const std::string& iter : m_errorKeyList)
            errorKeyArray.append(iter);

        for (const std::string& iter : m_successKeyList) {
            if (std::find(m_toBeNotifiedKeyList.begin(), m_toBeNotifiedKeyList.end(), iter) == m_toBeNotifiedKeyList.end())
                errorKeyArray.append(iter);
        }
        for (const std::string& iter : m_successKeyListVolatile) {
            if (std::find(m_toBeNotifiedKeyList.begin(), m_toBeNotifiedKeyList.end(), iter) == m_toBeNotifiedKeyList.end())
                errorKeyArray.append(iter);
        }

        if(completedKeyArray.arraySize()) {
            replyRoot.put("completed", completedKeyArray);
        }

        if(errorKeyArray.arraySize()) {
            replyRoot.put("errorKey", errorKeyArray);
        }

        if(!m_errorText.empty()) {
            replyRoot.put("errorText", m_errorText);
        }
    }

    if (m_taskInfo != nullptr) {
        if (!m_taskInfo->isBatchCall()) {
            bool result = LSMessageReply(m_lsHandle, m_replyMsg, replyRoot.stringify().c_str(), &lsError);
            if (!result) {
                SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply for set");
                LSErrorFree(&lsError);
            }
        }

        // m_taskInfo will be NULL
        PrefsFactory::instance()->releaseTask(&m_taskInfo, replyRoot);
    }
}

void PrefsDb8Set::postSubscription(const char *a_sender)
{
    const char * caller = LSMessageGetApplicationID(m_replyMsg) ?
             LSMessageGetApplicationID(m_replyMsg) :
             LSMessageGetSenderServiceName(m_replyMsg);

    if ( m_toBeNotifiedKeyList.find("country") != m_toBeNotifiedKeyList.end() ) {
        /* notify all settings to all subscribers, even though settings is not changed */
        PrefsNotifier::instance()->notifyAllSettings();
    }

    if ( m_setAll ) {
        postSubscriptionBulk(a_sender, caller);
    } else {
        postSubscriptionSingle(a_sender, caller);
    }
}

void PrefsDb8Set::postSubscriptionSingle(const char *a_senderToken, const char * a_senderId)
{
    pbnjson::JObject subscribeObj;

    for(pbnjson::JValue::KeyValue it : m_keyListObj.children()) {
        if (m_toBeNotifiedKeyList.find(it.first.asString()) != m_toBeNotifiedKeyList.end() )
            subscribeObj << it;
    }

    PrefsFactory::instance()->postPrefChanges(m_category, m_dimensionObj, m_subscribeAppId, subscribeObj, true, m_storeFlag, a_senderToken, a_senderId);

    // Notify to the dimension changes.
    //
    {
        PrefsNotifier::instance()->notifyByDimension(m_category, m_toBeNotifiedKeyList, subscribeObj);
    }
}

void PrefsDb8Set::postSubscriptionBulk(const char *a_senderToken, const char * a_senderId)
{
    /* This function would splitted multiple subscription message if
     * requested keys have different dimension */

    for (const CategoryDimKeyListMap::value_type& updated : m_storedCategoryDimKeyMap)
    {
        pbnjson::JObject subscribeObj;
        std::string categoryDimStr = updated.first;

        for(pbnjson::JValue::KeyValue it : m_keyListObj.children()) {
            if ( updated.second.find(it.first.asString()) != updated.second.end() )
                subscribeObj << it;
        }

        pbnjson::JValue updated_dimObj(PrefsKeyDescMap::instance()->categoryDim2dimObj(categoryDimStr));

        PrefsFactory::instance()->postPrefChanges(m_category, updated_dimObj, m_subscribeAppId, subscribeObj, true, m_storeFlag, a_senderToken, a_senderId);

        // Notify to the dimension changes.
        //
        if ( PrefsKeyDescMap::instance()->isCurrentDimension(updated_dimObj) ){
            PrefsNotifier::instance()->notifyByDimension(m_category, m_toBeNotifiedKeyList, subscribeObj);
        }
    }
}

bool checkDbType(bool isGlobal, const std::string& dbtype)
{
    if (dbtype == DBTYPE_MIXED || dbtype == DBTYPE_EXCEPTION || dbtype == DBTYPE_PERSOURCE) {
        return true;
    }

    if (isGlobal && dbtype == DBTYPE_GLOBAL) {
        return true;
    }

    return false;
}

/**
 * @sa m_successKeyList
 * @sa m_successKeyListVolatile
 */
bool PrefsDb8Set::parsingKeyDescInfo(pbnjson::JValue root, std::string &errorText)
{
    SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

    std::set < std::string > descKeyList;

    pbnjson::JValue label(root["returnValue"]);
    if (label.isBoolean() && !label.asBool()) {
        errorText = "returnValue is false from DB";
        return false;
    }

    pbnjson::JValue  keyArray(root["results"]);
    if (!keyArray.isArray()) {
        errorText = "Error!! to get Key description from DB";
        return false;
    }

    ssize_t arraylen = keyArray.arraySize();
    if (arraylen < 0) {
        errorText = "Error!! to get Key description from DB";
        return false;
    }

    for (pbnjson::JValue resultRecord : keyArray.items()) {
        pbnjson::JValue label(resultRecord["key"]);
        if (!label.isString()) {
            errorText = "no key in the result from Description Kind";
            return false;
        }
        std::string keyReq = label.asString();

        label = resultRecord["valueCheck"];
        bool value_check = label.isBoolean() && label.asBool();

        bool strict_value_check = false;
        /* TODO: strictValueCheck should be added in description.json
         * This workaround code is added for production branch */
        if ( PrefsKeyDescMap::instance()->needStrictValueCheck(keyReq) )
            strict_value_check = true;

        // store keys to the list.
        descKeyList.insert(keyReq);

        label = resultRecord[KEYSTR_DBTYPE];
        std::string dbtype = label.isString() ? label.asString() : DBTYPE_GLOBAL;

        label = resultRecord["vtype"];
        if (!label.isString()) {
            errorText = "no vtype in the result from Description Kind";
            m_errorKeyList.insert(keyReq);
            continue;
        }
        std::string vtype = label.asString();

        label = resultRecord["volatile"];
        bool volatileFlag = label.isBoolean() && label.asBool();
         //              volatileStr = json_object_get_string(label);

        pbnjson::JValue valuesObj(resultRecord["values"]);
        if (valuesObj.isNull()) {
            errorText = "no valuesObj in the result from Description Kind";
            m_errorKeyList.insert(keyReq);
            continue;
        }
        // take a value in the request
        pbnjson::JValue valReq(m_keyListObj[keyReq]);
        if (valReq.isNull()) {
            errorText = keyReq + " is not in Description Kind!";
            m_errorKeyList.insert(keyReq);
            continue;
        }
        // check dbtype Global or App
        if (!checkDbType(isForGlobalSetting(), dbtype)) {
            errorText = "Some keys are not matched with DBTYPE, Global or App Setting";
            m_errorKeyList.insert(keyReq);
            continue;
        }
        // Validation check is passed if caller want to set forcely without validation.
        // value_check : the flag in key description
        // m_valueCheck : the flag of valueCheck parameter in API payload.
        //                this flag is true if the parameter valueCheck is omitted.
        if( value_check && m_valueCheck ) {
            if (vtype == "Array") {
                pbnjson::JValue validValuesArray(valuesObj["array"]);

                if (!validValuesArray.isArray()) {
                    errorText = "values type mismatched";
                    m_errorKeyList.insert(keyReq);
                    continue;
                }

                bool valueNotFound = true;
                for (pbnjson::JValue it : validValuesArray.items()) {
                    if (it.isNull())
                        continue;
                    if (valReq == it) {
                        valueNotFound = false;
                        break;
                    }
                }

                if (valueNotFound) {
                    errorText = "There is No matched item: " + keyReq;
                    m_errorKeyList.insert(keyReq);
                    continue;
                }
            } else if (vtype == "ArrayExt") {
                pbnjson::JValue validValuesArray(valuesObj["arrayExt"]);
                if(!validValuesArray.isArray())
                    continue;

                bool valueNotFound = true;
                for (pbnjson::JValue it : validValuesArray.items()) {
                    if (it.isNull())
                        continue;

                    label = it["value"];
                    if (label.isNull()) {
                        errorText = "Incorrect arrayExt object: ";
                        m_errorKeyList.insert(keyReq);
                        continue;
                    }
                    if ( strict_value_check )
                    {
                        pbnjson::JValue valueActive(it["active"]);
                        pbnjson::JValue valueVisible(it["visible"]);
                        if (valueActive.isBoolean() && valueVisible.isBoolean())
                            if (valueActive.asBool() == false || valueVisible.asBool() == false)
                                continue;
                    }
                    if (valReq == label) {
                        valueNotFound = false;
                        break;
                    }
                }

                if (valueNotFound) {
                    errorText = "There is No matched extended item: " + keyReq;
                    m_errorKeyList.insert(keyReq);
                    continue;
                }

            } else if (vtype == "Range") {

                pbnjson::JValue validValuesObj = valuesObj["range"];
                int value = valReq.isNumber() ? valReq.asNumber<int>() : 0;

                label = validValuesObj["max"];
                if (!label.isNumber()) {
                    errorText = "no 'max' property in the result from Description Kind";
                    m_errorKeyList.insert(keyReq);
                    continue;
                }
                int max = label.asNumber<int>();

                label = validValuesObj["min"];
                if (!label.isNumber()) {
                    errorText = "no 'min' property in the result from Description Kind";
                    m_errorKeyList.insert(keyReq);
                    continue;
                }
                int min = label.asNumber<int>();

                label = validValuesObj["interval"];
                if (!label.isNumber()) {
                    errorText = "no 'interval' property in the result from Description Kind";
                    m_errorKeyList.insert(keyReq);
                    continue;
                }
                int interval = label.asNumber<int>();

                if (interval <= 0) {
                    errorText = "'interval' property contains invalid value";
                    m_errorKeyList.insert(keyReq);
                    continue;
                }

                if ((value > max || value < min) || ((value - min) % interval)) {
                    errorText = "The value is Out of range or interval: " + keyReq;
                    m_errorKeyList.insert(keyReq);
                    continue;
                }
            } else if (vtype == "Date") {
                // support 'set' operation
            } else {
                errorText = "The item has no Vailed range.";
                m_errorKeyList.insert(keyReq);
                continue;
            }
        }

        if (!m_defKindFlag && volatileFlag) {
            m_successKeyListVolatile.insert(keyReq);
            m_successKeyListVolatileObj.put(keyReq, valReq);
        } else {
            m_successKeyList.insert(keyReq);
            m_successKeyListObj.put(keyReq, valReq);
        }
    }

    // If there are(is) some keys has no description, store them to BaseKind.
    if ((size_t)arraylen < m_totalN) {
        for(pbnjson::JValue::KeyValue it : m_keyListObj.children()) {
            std::string key(it.first.asString());
            bool keyNotFound = true;
            for (const std::string& vaildkey : descKeyList) {
                if (vaildkey == key) {
                    keyNotFound = false;
                    break;
                }
            }
            if (keyNotFound) {
                if (PrefsKeyDescMap::instance()->getKeysInCategory(m_category).count(key) == 0) {
                    // insert no description
                    m_successKeyListObj << it;
                    m_successKeyList.insert(key);
                } else {
                    // maybe key is filtered by app_id in sendGetValuesRequest with PrefsKeyDescMap.getKeyDesc
                    m_errorKeyList.insert(key);
                }
            }
        }
    }
    return true;
}

void PrefsDebugUtil::dump_category_keys(PrefsDb8Set &set)
{
    for ( CategoryDimKeyListMap::const_iterator cat = set.m_requestCategoryDimKeysMap.begin();
            cat != set.m_requestCategoryDimKeysMap.end(); ++cat )
    {
        std::cout << "\n";
        std::cout << "MERGE Category:" + cat->first + ", Keys:";
        for ( std::set<std::string>::const_iterator key = cat->second.begin();
                key != cat->second.end(); ++key )
        {
            std::cout << *key + ", ";
        }
        std::cout << "\n";
    }

    for ( CategoryDimKeyListMap::const_iterator cat = set.m_storedCategoryDimKeyMap.begin();
            cat != set.m_storedCategoryDimKeyMap.end(); ++cat )
    {
        std::cout << "\n";
        std::cout << "STORE Category:" + cat->first + ", Keys:";
        for ( std::set<std::string>::const_iterator key = cat->second.begin();
                key != cat->second.end(); ++key )
        {
            std::cout << *key + ", ";
        }
        std::cout << "\n";
    }

}
