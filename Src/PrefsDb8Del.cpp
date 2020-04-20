// Copyright (c) 2013-2020 LG Electronics, Inc.
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

#include "Db8FindChainCall.h"
#include "Logging.h"
#include "PrefsDb8Del.h"
#include "PrefsFileWriter.h"
#include "PrefsNotifier.h"
#include "PrefsVolatileMap.h"
#include "SettingsServiceApi.h"

/* deleteSystemSettings/resetSystemSettings call tree
    |
    |
   sendRequest(Main Kind)
    |
    +-- sendDelRecordRequest(Main Kind) -- sendPutRequest(Main Kind) -- O
    |                                             |
  <No main data & delete operation>             <delete operation>
    |                                             |
    +---------------------------------------- sendRequest(Default Kind) -- sendPutRequest(Main Kind) -- O
*/

#define WORKAROUND_RESET_SUBSCRIPTION_TRUMOTIONMODE

char *PrefsDb8Del::getTargetKindStr()
{
    switch (m_targetKind) {
    case KINDTYPE_DEFAULT:
        return (char *)SETTINGSSERVICE_KIND_DEFAULT;
    case KINDTYPE_VOLATILE:
        /* pass-through, PrefsDbDel class can read
         * both base and volatile(extended) kind at the same time */
    case KINDTYPE_BASE:
    default:
        return (char *)SETTINGSSERVICE_KIND_MAIN;
    }
}

void PrefsDb8Del::setTargetKind(tKindType type)
{
    m_targetKind = type;
}

void PrefsDb8Del::handleVolatileKey(const CategoryDimKeyListMap &dimKeyMap)
{
    PrefsVolatileMap *prefsVolatileMap = PrefsVolatileMap::instance();
    for (CategoryDimKeyListMap::const_iterator itDimKey = dimKeyMap.begin(); itDimKey != dimKeyMap.end(); itDimKey++) {
        const std::string & dim = itDimKey->first;
        const std::set<std::string> & keys = itDimKey->second;
        for (std::set<std::string>::const_iterator itKey = keys.begin(); itKey != keys.end(); itKey++) {
            const std::string & key = *itKey;
            if (PrefsKeyDescMap::instance()->isVolatileKey(key.c_str()) == false)
                continue;
            if (prefsVolatileMap->delVolatileValue(dim, m_app_id, key)) {
                m_errorKeyList.erase(key);

                std::pair<std::map<std::string, std::map<std::string, std::string> >::iterator, bool> retInsertDim;
                retInsertDim = m_removedVolatileDimKeyMap.insert(
                    std::pair<std::string, std::map<std::string, std::string> >(dim, std::map<std::string, std::string>()) );
                retInsertDim.first->second.insert(std::pair<std::string, std::string>(key, ""));
            }
        }
    }
}

void PrefsDb8Del::handleVolatileKeyResetAll(const std::string &category)
{
    PrefsVolatileMap *prefsVolatileMap = PrefsVolatileMap::instance();
    std::set<std::string> removedKeys = prefsVolatileMap->delVolatileKeysByCategory(category);

    PrefsKeyDescMap *prefKeyDescMap = PrefsKeyDescMap::instance();
    CategoryDimKeyListMap dimKeyMap = prefKeyDescMap->getCategoryKeyListMap(category, m_dimensionObj, removedKeys);
    handleVolatileKey(dimKeyMap);
}

bool PrefsDb8Del::removeSettings(const std::set<std::string>& a_keys, const std::string& a_app_id)
{
    bool removed = false;

    /* delete requested keys in the retrived data
     * a_keys is list of keys requested for deleting */
    for (std::pair<const int, SettingsRecord >& a_setting : m_currentSettings) {
        /* remove all data regardless per-app or not if called API is deleteSystemSettings */
        if ( !isRemoveDefKind() && a_setting.second.getAppId() != a_app_id )
            continue;

        std::set <std::string> removed_keys = a_keys.empty() ? a_setting.second.removeAllKeys() :
                                                               a_setting.second.removeKeys(a_keys);
        if ( !removed_keys.empty() )
            removed = true;

        /* update error list. m_errorKeyList is reported by sendResultReply as Error
         * If all requested key is handled, m_errorKeyList will be empty */
        for (const std::string& a_key : removed_keys) {
            m_errorKeyList.erase(a_key);
            // add to success key list to post subscribe.
            m_removedKeySet.insert(a_key);
        }
    }

    return removed;
}


bool PrefsDb8Del::sendRequest(LSHandle * lsHandle)
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JObject replyRoot;
    pbnjson::JObject replyRootQuery;
    pbnjson::JArray replyRootWhereArray;
    pbnjson::JArray category_array;
    pbnjson::JObject replyRootItem1;
    pbnjson::JObject replyRootItem2;
    pbnjson::JObject jsonObjParam;
    pbnjson::JObject jsonObjFind;
    pbnjson::JArray jsonArrOperations;

    /*
        luna-send -n 1 -f -a com.palm.configurator palm://com.palm.db/batch
        '{"operations":[{"method":"find","params":
            {"query": { "where": [
                { "prop": "category", "op": "=", "val": [ "aspectRatio$dtv", "aspectRatio$x" ] },
                { "prop": "app_id", "op": "=", "val": "com.webos.app.smartshare" }
                ]
            "from": "com.webos.settings.system:1" }} }]}'
     */

    /* for finding all possible category_dimension at once, build array for where claus
     * for delete, request all keys. So select is not used */
    jsonObjParam.put("operations", jsonArrOperations);

    CategoryDimKeyListMap categories = PrefsKeyDescMap::instance()->getCategoryKeyListMap(m_category, m_dimensionObj, m_keyList);
    if (m_app_id != GLOBAL_APP_ID) {
        std::string categoryDim;
        if (PrefsKeyDescMap::instance()->getCategoryDim(m_category, categoryDim)) {
            categories[categoryDim] = m_keyList;
        }
    }

    for (const auto& cat_key : categories) {
        category_array.append(cat_key.first);
    }
    replyRootItem1.put("prop", "category");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", category_array);
    replyRootWhereArray.append(replyRootItem1);

    replyRootItem2.put("prop", "app_id");
    replyRootItem2.put("op", "=");
    replyRootItem2.put("val", m_app_id);
    replyRootWhereArray.append(replyRootItem2);
    replyRootQuery.put("where", replyRootWhereArray);

    handleVolatileKey(categories);

    /* TODO: To load only necessary settings, we need to 'select'
     * Current code notifies unchanged(not reset) settings */

    // add from
    replyRootQuery.put("from", getTargetKindStr());

    // add app_id query
    replyRoot.put("query", replyRootQuery);

    // add Operation
    jsonObjFind.put("method", "find");
    jsonObjFind.put("params", replyRoot);
    jsonArrOperations.append(jsonObjFind);

    if (m_app_id != GLOBAL_APP_ID && !m_globalKeys.empty())
    {
        pbnjson::JValue replyExceptionRoot(pbnjson::Object());
        pbnjson::JValue replyExceptionRootQuery(pbnjson::Object());
        pbnjson::JValue replyRootExceptionWhereArray(pbnjson::Array());
        pbnjson::JValue replyRootExceptionItem(pbnjson::Object());
        pbnjson::JValue jsonObjExceptionFind(pbnjson::Object());

        //add category for exception Applist
        replyRootExceptionWhereArray.append(replyRootItem1);

        replyRootExceptionItem.put("prop", "app_id");
        replyRootExceptionItem.put("op", "=");
        replyRootExceptionItem.put("val", GLOBAL_APP_ID);

        replyRootExceptionWhereArray.append(replyRootExceptionItem);

        replyExceptionRootQuery.put("where", replyRootExceptionWhereArray);
        replyExceptionRootQuery.put("from", getTargetKindStr());

        replyExceptionRoot.put("query", replyExceptionRootQuery);

        jsonObjExceptionFind.put("method", "find");
        jsonObjExceptionFind.put("params", replyExceptionRoot);

        jsonArrOperations.append(jsonObjExceptionFind);
    }

    ref();
    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), PrefsDb8Del::cbFindRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_FIND_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    categories.clear();

    return true;
}

bool PrefsDb8Del::sendDelRecordRequest(LSHandle * lsHandle)
{
    LSError lsError;
    pbnjson::JValue jsonObjParam(pbnjson::Object());
    pbnjson::JValue jsonArrOperations(pbnjson::Array());

    LSErrorInit(&lsError);

    for (const auto& settings : m_currentSettings) {
        if (settings.second.is_dirty()) {
            pbnjson::JValue item(pbnjson::Object());
            item.put("method", "del");
            item.put("params", settings.second.genDelQueryById());
            jsonArrOperations.append(item);
        }
    }

    jsonObjParam.put("operations", jsonArrOperations);
    ref();
    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), PrefsDb8Del::cbDelRecordRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

bool PrefsDb8Del::sendPutRequest(LSHandle * lsHandle)
{
    LSError lsError;

    LSErrorInit(&lsError);

    /*
        luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/put '{"objects":[
            {"_kind":"com.webos.settings.system:1", "value":{"language":"kr","brightness":100},
            "app_id":"com.webos.BDP", "category":"HDMI", "space":true}]}'
    */

    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootObjectArray(pbnjson::Array());

    for (const auto& it_settings : m_currentSettings) {
        if (it_settings.second.is_dirty()) {
            replyRootObjectArray.append(it_settings.second.genObjForPut());
        }
    }

    // add to root
    replyRoot.put("objects", replyRootObjectArray);

    ref();

    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/put", replyRoot.stringify().c_str(), PrefsDb8Del::cbPutRequest, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_PUT_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

bool PrefsDb8Del::sendFindRequestToDefKind(LSHandle * lsHandle)
{
    LSError lsError;
    pbnjson::JValue jsonObjParam(pbnjson::Object());
    pbnjson::JValue jsonArrOperations(pbnjson::Array());

    LSErrorInit(&lsError);

    /* load data form json file */
    for (std::pair<const int, SettingsRecord>& it_settings : m_currentSettings) {
        if (it_settings.second.is_dirty() || it_settings.second.isVolatile()) {
            const pbnjson::JValue defaults(pbnjson::Array());
            // TODO why we need this check???
            if (!defaults.isNull()) {
                std::string errorText;
#ifdef WORKAROUND_RESET_SUBSCRIPTION_TRUMOTIONMODE
                it_settings.second.parsingResult(defaults, errorText, false);
#else
                it_settings.second.parsingResult(defaults, errorText, true);
#endif
            }
        }
    }

    /* load data form db8 */
    for (std::pair<const int, SettingsRecord>& it_settings : m_currentSettings) {
        if (it_settings.second.is_dirty() || it_settings.second.isVolatile()) {
            if ( it_settings.second.isRemovedMixedType() ) {
                /* load default data from global settings
                 * if removed data is mixed type and per app */
                it_settings.second.fixCategoryForMixedType();
            }
            jsonArrOperations.append(it_settings.second.genQueryForDefKind());
        }
    }

    jsonObjParam.put("operations", jsonArrOperations);
    ref();

    bool result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), PrefsDb8Del::cbFindRequestToDefKind, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

bool PrefsDb8Del::sendRequestResetAll(LSHandle * lsHandle)
{
    bool result;
    pbnjson::JValue find_query = pbnjson::Object(); /* query for main settings including volatile */

    m_reset_all = true;

    m_dimFilterObj = m_dimensionObj;
    m_dimensionObj = pbnjson::JValue();

    handleVolatileKeyResetAll(m_category);

    find_query.put("from", "com.webos.settings.system:1");
    pbnjson::JValue where_array = pbnjson::Array();

    if ( !m_category.empty() ) {
        where_array.append({{"prop", "category"},
                            {"op", "%"},
                            {"val", m_category}});
    }
    // TODO: support app_id, find_query += ",{\"prop\":\"app_id\", \"op\":\"=\", \"val\":\"\"}";
    find_query.put("where", where_array);

    Db8FindChainCall *chainCall = new Db8FindChainCall;
    chainCall->ref();
    chainCall->Connect(PrefsDb8Del::cbFindForResetAll, this, (void *)lsHandle);
    ref();
    result = Db8FindChainCall::sendRequest(chainCall, lsHandle, find_query);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_DB_LUNA_CALL_FAIL, 0, "Fail to find country settings");
    }

    chainCall->unref();

    return result;
}

bool PrefsDb8Del::cbFindRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    bool successFlag = false;
    bool need_dbupdate = false;
    std::string errorText;

    PrefsDb8Del *replyInfo = (PrefsDb8Del *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_DEL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s : %s", __FUNCTION__, payload);
        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_DEL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label(root["returnValue"]);

        if (label.isBoolean())
            successFlag = label.asBool();

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_DEL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "DB8 returns fail";
            break;
        }

        pbnjson::JValue responseObjsArray(root["responses"]);
        if (label.isNull()) {
            errorText = "no result for results in DB";
            break;
        }

        if (!replyInfo->m_currentSettings.empty()) {
            /* if sendRequest is called twice after default kind is handled,
              * it's possible that unnecessary data is remained */
            replyInfo->m_currentSettings.clear();
        }
        if (!responseObjsArray.isArray()) {
            errorText = "Incorrect batch result";
            break;
        }

        bool resultsError = false;
        for (pbnjson::JValue it : responseObjsArray.items()) {
            pbnjson::JValue resultObjsArray = it["results"];
            if (!resultObjsArray.isArray()) {
                errorText = "'results' key is missing in db response";
                resultsError = true;
                break;
            }

            /* load retrived DB8 find result */
            int counter = 0;
            for (pbnjson::JValue jt : resultObjsArray.items()) {
                SettingsRecord del_record;
                if (del_record.loadJsonObj(jt)) {
                    replyInfo->m_currentSettings.insert(std::map<int, SettingsRecord>::value_type(counter, del_record));
                } else {
                    errorText = "Incorrect DB result";
                    /* STOP delete process in this kind
                     * Continue to process unhandled errorKeyList */
                    replyInfo->m_currentSettings.clear();
                    break;
                }
                counter++;
            }
        }
        if (resultsError)
            break;

        // Volatile
        for (std::pair<const std::string, std::map<std::string, std::string>>& itDim : replyInfo->m_removedVolatileDimKeyMap) {
            const std::string & categoryDim = itDim.first;
            const std::map<std::string, std::string>& keyMap = itDim.second;

            SettingsRecord delRecord;
            delRecord.loadVolatileKeys(categoryDim, keyMap);
            replyInfo->m_currentSettings.insert(std::map<int, SettingsRecord>::value_type(replyInfo->m_currentSettings.size(), delRecord));
        }

        if ( !replyInfo->m_perAppKeys.empty() )
            if ( replyInfo->removeSettings(replyInfo->m_perAppKeys, replyInfo->m_app_id) )
                need_dbupdate = true;
        if ( replyInfo->m_keyList.empty() || !replyInfo->m_globalKeys.empty() )
            if ( replyInfo->removeSettings(replyInfo->m_globalKeys, GLOBAL_APP_ID) )
                need_dbupdate = true;

        // target is default kind
        if (replyInfo->isForDefaultKind() == true) {
            if (need_dbupdate) {
                // in defalut kind process, Target kind is already set for default kind
                replyInfo->setTargetKind(KINDTYPE_DEFAULT);
                success = replyInfo->sendDelRecordRequest(lsHandle);
            }
            // if there is no key to be removed
            else {
                // do nothing
                errorText = "there is no key to remove in DB";
                success = false;
            }
        }
        // target is base kind
        else {
            if (need_dbupdate) {
                /* sendDelRecordRequest can handle boath base and volatile
                 * because SettingsRecord object includes kind information */
                replyInfo->setTargetKind(KINDTYPE_BASE);
                success = replyInfo->sendDelRecordRequest(lsHandle);
            }
            // if there is no key to be removed in base and volatile kind, do process with Default kind if needed.
            else if (replyInfo->isRemoveDefKind()) {
                replyInfo->setTargetKind(KINDTYPE_DEFAULT);
                success = replyInfo->sendRequest(lsHandle);
            }
            else if (replyInfo->m_removedVolatileDimKeyMap.size() > 0) {
                success = replyInfo->sendFindRequestToDefKind(lsHandle);
            }
            else if (!replyInfo->m_errorKeyList.empty()) {
                // This part is not called in normal case.
                errorText = "Some keys are not in DB";
                success = false;
            // there is no key to remove in Kinsd
            }
            else {
                errorText = "there is no key to remove in DB";
                success = false;
            }
        }
    } while(false);

    if (!success) {
        // if there is no item to delete, it's a success
        replyInfo->sendResultReply(lsHandle, true, errorText);
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8Del::cbFindForResetAll(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results )
{
    PrefsDb8Del *replyInfo = (PrefsDb8Del *) a_thiz_class;
    LSHandle *ls_handle = (LSHandle *)a_userdata;
    pbnjson::JValue result_concat(pbnjson::Array());
    bool result = false;
    std::string errorText;

    // Checks whether all returns are valid one.
    //
    for (const pbnjson::JValue citer: a_results) {
        pbnjson::JValue label(citer["returnValue"]);
        if (label.isBoolean()) {
            bool success = label.asBool();

            if (success == false) {
                pbnjson::JValue forPrint = citer; // for prevent const
                SSERVICELOG_WARNING(MSGID_DEL_DB_RETURNS_FAIL, 1,
                        PMLOGJSON("payload", forPrint.stringify().c_str()), "ResetAll query is failed");
                continue;
            }

            pbnjson::JValue array_list(citer["results"]);
            if (array_list.isArray()) {
                for (pbnjson::JValue iarr : array_list.items()) {
                    result_concat.append(iarr);
                }
            }
        }
    }

    ssize_t arraylen = result_concat.arraySize();

    if (arraylen + replyInfo->m_removedVolatileDimKeyMap.size() == 0) {
        /* There is no settings user modified, return success, no subscription */
        errorText = "";
        replyInfo->sendResultReply(ls_handle, true, errorText);
    } else {
        result = replyInfo->handleSettingsRecord(ls_handle, result_concat);

        if ( result == false ) {
            errorText = "Incorrect settings records";
            replyInfo->sendResultReply(ls_handle, false, errorText);
        }
    }

    replyInfo->unref();

    return result;
}

bool PrefsDb8Del::handleSettingsRecord(LSHandle *a_lsHandle, pbnjson::JValue a_records)
{
    std::string errorText;
    bool need_dbupdate = false;
    bool success = false;

    if ( !a_records.isArray() ) {
        return false;
    }

    ssize_t records_len = a_records.arraySize();

    /* This function is called if only records_len > 0 */

    if (!m_currentSettings.empty()) {
        /* if sendRequest is called twice after default kind is handled,
         * it's possible that unnecessary data is remained */
        m_currentSettings.clear();
    }

    /* load retrived DB8 find result */
    for (int i = 0; i < records_len ; i++) {
        SettingsRecord del_record;
        pbnjson::JValue r = a_records[i];

        if ( del_record.loadJsonObj(r) == false ) {
            errorText = "Incorrect DB result";
            m_currentSettings.clear();
            return false;
        }

        /* filter for specified dimension parameter.
         * dimMatched return true If m_dimFilterObj is invalid */
        if ( !del_record.dimMatched(m_dimFilterObj) ) {
            del_record.clear();
            continue;
        }

        m_currentSettings.insert(std::map<int, SettingsRecord>::value_type(i, del_record));
    }

    // Volatile
    for (const std::pair<std::string, std::map<std::string, std::string> > itDim : m_removedVolatileDimKeyMap) {
        const std::string& categoryDim = itDim.first;
        const std::map<std::string, std::string>& keyMap = itDim.second;

        SettingsRecord delRecord;
        delRecord.loadVolatileKeys(categoryDim, keyMap);
        m_currentSettings.insert(std::map<int, SettingsRecord>::value_type(m_currentSettings.size(), delRecord));
    }

    /* delete requested keys in the retrived data
     * m_keyList is list of keys requested for deleting */
    for (auto& it_settings : m_currentSettings)
    {
        std::set < std::string > removed_keys = m_keyList.empty() ? it_settings.second.removeAllKeys() :
                                                                it_settings.second.removeKeys(m_keyList);

        /* update error list. m_errorKeyList is reported by sendResultReply as Error
         * If all requested key is handled, m_errorKeyList will be empty */

        for (const std::string& it_keys : removed_keys)
        {
            m_errorKeyList.erase(it_keys);
            m_removedKeySet.insert(it_keys);
            need_dbupdate = true;
        }
    }

    /* Even m_errorKeyList is not empty, it means part of the requested
     * keys are not founded, it's not error */

    if ( need_dbupdate ) {
        /* sendDelRecordRequest can handle boath base and volatile
         * because SettingsRecord object includes kind information.
         * If target settings are found, call DB8 del operation
         * before put operation. Merge operation cant remove property */

        success = sendDelRecordRequest(a_lsHandle);
    }
    else if (m_removedVolatileDimKeyMap.size() > 0) {
        success = sendFindRequestToDefKind(a_lsHandle);
    }
    else {
        /* We need not to update db8. Done */

        if ( isRemoveDefKind() ) {
            /* This means that requested API is deleteSystemSettings
             * TODO: It sould be supported ? Not yet. */
            success = false;
        } else {
            /* This means that requested API is resetSystemSettigs
             * No change in db8.
             * No subscription because m_removedKeySet is empty*/
            sendResultReply(a_lsHandle, true, errorText);
            success = true;
        }
    }

    /* return false means caller must send reply */

    return success;
}

bool PrefsDb8Del::checkSettingsRecordDirty(const std::pair < int, SettingsRecord >& item)
{
    return item.second.is_dirty();
}

bool PrefsDb8Del::checkSettingsRecordDirtyOrVolatile(const std::pair < int, SettingsRecord >& item)
{
    return item.second.is_dirty() || item.second.isVolatile();
}

bool PrefsDb8Del::cbDelRecordRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    bool successFlag = false;
    std::string errorText;

    PrefsDb8Del *replyInfo = (PrefsDb8Del *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_DEL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s : %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_DEL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean())
            successFlag = label.asBool();

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_DEL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            break;
        }

        pbnjson::JValue resp_array(root["responses"]);
        if (!resp_array.isArray()) {
            SSERVICELOG_WARNING(MSGID_DEL_NO_RESPONSES, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "No responses property in return. DB Error!!";
            break;
        }

        /* check batch result count. Notice batch method is atomic */
        int count = std::count_if(replyInfo->m_currentSettings.begin(), replyInfo->m_currentSettings.end(), PrefsDb8Del::checkSettingsRecordDirty);
        if (count != resp_array.arraySize()) {
            SSERVICELOG_WARNING(MSGID_DEL_BATCH_OPER_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "Batch result count error";
            break;
        }

        /* check each batch result. delete operation always success */
        int i;
        for (i = 0; i < resp_array.arraySize(); i++) {
            pbnjson::JValue obj(resp_array[i]);

            label = obj["returnValue"];
            if (!label.isBoolean() || label.asBool() == false) {
                SSERVICELOG_WARNING(MSGID_DEL_BATCH_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
                errorText = "batch operation 'del' fail";
                break;
            }

            label = obj["count"];
            /* FIXME: if count is greater than 1 ? this menas there is duplicate data */
            if (label.isNumber() && label.asNumber<int>() < 1) {
                SSERVICELOG_TRACE("payload :%s .Duplicated data existed, delete batch result is incorrect", payload);
            }
        }

        if (i == resp_array.arraySize()) {
            success = replyInfo->sendPutRequest(lsHandle);
            if (!success) {
                errorText = "ERROR!! sending a request to DB";
            }
        }
    } while(false);

    if (success) {
        // do nothing. Reply is sent by cbPutRequest
    } else {
        replyInfo->sendResultReply(lsHandle, success, errorText);
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

void PrefsDb8Del::setGlobalAndPerAppKeys(const std::set < std::string >& globalKeys,const std::set < std::string >& PerAppKeys)
{
    m_perAppKeys.clear();
    m_globalKeys.clear();
    m_perAppKeys = PerAppKeys;
    m_globalKeys = globalKeys;
}

bool PrefsDb8Del::cbPutRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    bool sendRequestFlag = false;
    std::string errorText;

    PrefsDb8Del *replyInfo;
    replyInfo = (PrefsDb8Del *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_DEL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s : %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_DEL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean())
            success = label.asBool();

        if (!success) {
            SSERVICELOG_WARNING(MSGID_DEL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
        }

        if (success && !replyInfo->isForDefaultKind()) {
            if(replyInfo->isRemoveDefKind()) {
                /* After update main and volatile kind,
                 * Continue to update default kind if need. resetSystemSettings stops here */
                replyInfo->setTargetKind(KINDTYPE_DEFAULT);
                success = replyInfo->sendRequest(lsHandle);
            }
            else {
                // send subscription and result reply
                replyInfo->sendFindRequestToDefKind(lsHandle);
            }
            sendRequestFlag = true;
        }
    } while(false);

    if (sendRequestFlag) {
        // do nothing for waiting the process
    } else {
        replyInfo->sendResultReply(lsHandle, success, errorText);
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8Del::cbFindRequestToDefKind(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    std::string errorText;

    PrefsDb8Del *replyInfo = (PrefsDb8Del *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_DEL_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

        SSERVICELOG_TRACE("%s : %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_DEL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "couldn't parse json";
            break;
        }

        pbnjson::JValue return_value(root["returnValue"]);
        if (!return_value.isBoolean() || return_value.asBool() == false) {
            errorText = "ERROR!! in DB, returnValue is false";
            break;
        }

        pbnjson::JValue resp_array(root["responses"]);
        if (!resp_array.isArray()) {
            errorText = "No responses property in return. DB Error!!";
            break;
        }

        /* check batch result count. Notice batch method is atomic */
        int count = std::count_if(replyInfo->m_currentSettings.begin(), replyInfo->m_currentSettings.end(),
                PrefsDb8Del::checkSettingsRecordDirtyOrVolatile);
        if (count != resp_array.arraySize()) {
            errorText = "Batch result count error";
            break;
        }

        int dirty_index = 0;
        for (auto& it_settings : replyInfo->m_currentSettings )
        {
            if (!it_settings.second.is_dirty() && !it_settings.second.isVolatile()) {
                continue;
            }

            pbnjson::JValue responseObjArray(resp_array[dirty_index]);

            pbnjson::JValue label(responseObjArray["returnValue"]);
            if (!label.isBoolean() || label.asBool() == false) {
                errorText = "batch operation 'del' fail";
                SSERVICELOG_TRACE("%s, DB batch item %d return fail, %s", __FUNCTION__, dirty_index, root.stringify().c_str());
                dirty_index++;
                continue;
            }

            label = responseObjArray["results"];
            if (label.isArray()) {
                std::string error_text;
                if(label.arraySize() > 0) {
                    /* m_currentSettings will be used when subscription message posted */
#ifdef WORKAROUND_RESET_SUBSCRIPTION_TRUMOTIONMODE
                    it_settings.second.parsingResult(label, error_text, false);
#else
                    it_settings.second.parsingResult(label, error_text, true);
#endif
                }
                success = true;     // if there is no item in db, it's true.
            }

            dirty_index++;
        }
    }
    while(false);

    replyInfo->sendResultReply(lsHandle, success, errorText);
    if (replyInfo)
        replyInfo->unref();

    return true;
}

#if (SUBSCRIPTION_TYPE == SUBSCRIPTION_TYPE_FOREACHKEY)
void PrefsDb8Del::postSubscription()
{
    std::string errorText;

    if(!isRemoveDefKind()) {
        // send subscription for a 'reset' request
        pbnjson::JValue valuesObj(pbnjson::Object());

        // gethering all values in m_currentSettings
        for (const std::pair< int, SettingsRecord >& it_settings : m_currentSettings) {
            pbnjson::JValue tmpObj = it_settings.second.getValuesObj();
            for (pbnjson::JValue::KeyValue it : tmpObj) {
                valuesObj.put(it.first,it.second);
            }
        }

        // for the keys that have a value
        for (pbnjson::JValue::KeyValue it : valuesObj.children()) {
            std::string key = it.first.asString();
            auto itSet = m_removedKeySet.find(key);
            if(itSet != m_removedKeySet.end()) {
                postSubscriptionForEachKey(key, it.second, NULL);
                m_removedKeySet.erase(itSet);
            }
        }
    }

    // for the keys that have no values
    for(const std::string& itSet : m_removedKeySet) {
        errorText = "Removed Keys: " + (*itSet);
        postSubscriptionForEachKey(itSet, NULL, (char*)errorText.c_str());
    }
}

void PrefsDb8Del::postSubscriptionForEachKey(const std::string& key, pbnjson::JValue val, char* errorText)
{
    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootSettings;
    std::string subscribeKey;
    std::string categoryStr(m_category);
    bool returnValue = false;
    const char * caller = NULL;    // Do not add 'caller' for resetSystemSettings

    if(val) {
        replyRootSettings = pbnjson::Object();
        replyRootSettings.put(key, val);
        replyRoot.put("settings", replyRootSettings);
        returnValue = true;
    }

    replyRoot.put("returnValue", returnValue);
    replyRoot.put("app_id", m_app_id);

    replyRoot.put("category", m_category);
    pbnjson::JValue dimInfo = PrefsKeyDescMap::instance()->getDimKeyValueObj(m_category, m_dimensionObj, key);
    if (dimInfo.isString()) {
        categoryStr += dimInfo.asString();
        replyRoot.put("dimension", dimInfo);
    }

    if(errorText) {
        replyRoot.put("errorText", errorText);
    }

    subscribeKey = SUBSCRIBE_STR_KEY(key, m_app_id, categoryStr);
    PrefsFactory::instance()->postPrefChange(subscribeKey.c_str(), replyRoot, caller);

    /* if any dimension info is not specified when user requests subscription,
     * user should receive all key change notification regardless of dimension match */
    if ( categoryStr != m_category &&
            PrefsKeyDescMap::instance()->isCurrentDimension(m_dimensionObj) ) {
        subscribeKey = SUBSCRIBE_STR_KEY(key, m_app_id, m_category);
        PrefsFactory::instance()->postPrefChange(subscribeKey.c_str(), replyRoot, caller);
    }
}
#else

void PrefsDb8Del::reference(void)
{
    ref();
}

void PrefsDb8Del::finalize(void)
{
    sendResultReply();
    unref();
}

std::string PrefsDb8Del::getSettingValue(const char *a_key)
{
    std::map < int, SettingsRecord >::iterator it_settings;

    /* m_currentSettings can include two item if volatile settings are included,
       but actually, there is no volatile data in default kind */

    for (const std::pair< int, SettingsRecord >& it_settings : m_currentSettings) {
        pbnjson::JValue tmpObj = it_settings.second.getValuesObj();
        for(pbnjson::JValue::KeyValue it_obj : tmpObj.children()) {
            std::string key = it_obj.first.asString();
            if (key == a_key && it_obj.second.isString() ) {
                return it_obj.second.asString();
            }
        }
    }

    return std::string();
}

void PrefsDb8Del::postSubscription()
{
    std::set <std::string>::iterator itSet;
    std::string errorText;
    const char * caller = NULL;    // Do not add 'caller' for resetSystemSettings

    if ( m_removedKeySet.find("country")!=m_removedKeySet.end() )
    {
        /* Actually, When m_reset_all,
         * it's better sending necessary notification for
         * only relavant category which is specified in resetSystemSettings
         * TODO: if there is performance issue */
        PrefsNotifier::instance()->notifyAllSettings();
    }

    if ( m_reset_all )
        postSubscriptionBulk(caller);
    else
        postSubscriptionSingle(caller);
}

void PrefsDb8Del::postSubscriptionSingle(const char *a_caller)
{
    pbnjson::JValue valuesObj;

    if(!isRemoveDefKind()) {
        // send subscription for a 'reset' request
        // gethering all values in m_currentSettings
        valuesObj = pbnjson::Object();
        for (const std::pair<int,SettingsRecord>& it_settings : m_currentSettings) {
            pbnjson::JValue tmpObj(it_settings.second.getValuesObj());
            for(pbnjson::JValue::KeyValue it : tmpObj.children()) {
                valuesObj.put(it.first, it.second);
            }
        }

#ifdef WORKAROUND_RESET_SUBSCRIPTION_TRUMOTIONMODE
        removeMixedKeys(valuesObj, a_caller, false);
#endif

        // Notify to 'dimension' changes.
        PrefsNotifier::instance()->notifyByDimension(m_category, m_removedKeySet, valuesObj);

        // write localeInfo content if m_currentSettings has localeInfo obj.
        PrefsFileWriter::instance()->updateFilesIfTargetExistsInSettingsObj(m_category, valuesObj);

        // remove the keys that have a value from successKeySet
        for(pbnjson::JValue::KeyValue it : valuesObj.children()) {
            auto itSet = m_removedKeySet.find(it.first.asString());
            if(itSet != m_removedKeySet.end()) {
                m_removedKeySet.erase(itSet);
            }
        }

        PrefsFactory::instance()->postPrefChanges(m_category, m_dimensionObj, m_app_id, valuesObj, true, true, NULL, a_caller);
    }

    // for the keys that have not existed.
    if(!m_removedKeySet.empty()) {
        valuesObj = pbnjson::Object();
        // for the keys that have no values
        for(const std::string& itSet : m_removedKeySet) {
            valuesObj.put(itSet, pbnjson::JValue(""));
        }

        if(valuesObj.objectSize()) {
            PrefsFactory::instance()->postPrefChanges(m_category, m_dimensionObj, m_app_id, valuesObj, false,true, NULL, a_caller);
        }
    }
}

void PrefsDb8Del::removeMixedKeys(pbnjson::JValue obj, const char* a_caller, bool remove_keys /*= true*/)
{
/* Normally, Mixed type value is removed by mergeLayeredRecord in parsingResult.
 * but, mixed type value was keep to posting per-app value to current_app subscriber.
 * Remove after posting subscription */
    std::set<std::string> mixedKeys;
    pbnjson::JValue mixedObj(pbnjson::Object());

    for (pbnjson::JValue::KeyValue it : obj.children()) {
        if ( PrefsKeyDescMap::instance()->getDbType(it.first.asString()) == DBTYPE_MIXED ) {
            mixedKeys.insert(it.first.asString());
            mixedObj.put(it.first, it.second);
        }
    }

    if ( !mixedKeys.empty() ) {
        if ( m_app_id == GLOBAL_APP_ID && strcmp(PrefsFactory::instance()->getCurrentAppId(), GLOBAL_APP_ID) ) {
            /* NOTICE: workaround code for the bug not posting subscription regarding resetSystemSettings.
             * If getSystemSettings is called with current_app and subscribe, subscriber never receive regarding
             * resetSystemSettings without app_id (e.g. resetSystemSettings '{"category":"picture"}' */
            PrefsFactory::instance()->postPrefChanges(m_category, m_dimensionObj,
                   PrefsFactory::instance()->getCurrentAppId(), mixedObj, true, true, NULL, a_caller);
            }

        if (remove_keys) {
            for (const std::string& a_key : mixedKeys) {
                obj.remove(a_key);
            }
        }
    }
}

void PrefsDb8Del::postSubscriptionBulk(const char *a_caller)
{
    for (const std::pair<int, SettingsRecord>& r : m_currentSettings)
    {
        pbnjson::JValue sub_value(pbnjson::Object());
        pbnjson::JValue err_value(pbnjson::Object());

        std::set<std::string> rmd_keys = r.second.getRemovedKeys();

        for (const std::string& rmd_k : rmd_keys)
        {
            pbnjson::JValue def_value(r.second.getValuesObj()[rmd_k]);

            if (def_value.isNull()) {
                /* Settings in main kind was removed.
                 * But there is no default value in default kind.
                 * In that case, send error to subscriber */
                err_value.put(rmd_k, "");
            } else {
                /* After removing setings, deault value was founded.
                 * Notify default value as changed settings */
                sub_value.put(rmd_k, def_value);
            }
        }

        /* TODO: We need to merge the settings records those are
         * included in current dimension. This is required to send
         * one subscription message (not splitted)
         * for getSystemSettings without dimension */

        std::string s_category = r.second.getCategory();
        std::string s_appId = r.second.getAppId();
        pbnjson::JValue s_dimObj(r.second.getDimObj());

        if (sub_value.objectSize() != 0 ) {
#ifdef WORKAROUND_RESET_SUBSCRIPTION_TRUMOTIONMODE
            removeMixedKeys(sub_value, a_caller);
#endif

            if ( PrefsKeyDescMap::instance()->isCurrentDimension(s_dimObj) ) {
                // Notify to 'dimension' changes.
                PrefsNotifier::instance()->notifyByDimension(s_category, rmd_keys, sub_value);

                // write localeInfo content if m_currentSettings has localeInfo obj.
                PrefsFileWriter::instance()->updateFilesIfTargetExistsInSettingsObj(s_category, sub_value);
            }

            PrefsFactory::instance()->postPrefChanges(s_category, s_dimObj, s_appId, sub_value, true, true, NULL, a_caller);
        }

        if (err_value.objectSize() != 0 ) {
            PrefsFactory::instance()->postPrefChanges(s_category, s_dimObj, s_appId, err_value, false, true, NULL, a_caller);
        }
    }
}
#endif

void PrefsDb8Del::sendResultReply(LSHandle * lsHandle, bool success, std::string & errorText)
{
    m_lsHandle = lsHandle;
    m_errorText = errorText;
    m_reply_success = success;

    // Early notify to the dimension changes.
    // Notifier will track down the dimension values at this stage.
    //
    {
        PrefsNotifier::instance()->notifyEarly(m_category, m_removedKeySet);
    }

    // update dimension key values. // sendResultReply is called after updating dimension key
    if (!PrefsKeyDescMap::instance()->updateDimKeyValue(this, m_removedKeySet, m_dimensionObj)) {
        sendResultReply();
    }
}

void PrefsDb8Del::sendResultReply(void)
{
    bool result;

    // return error msg
    pbnjson::JValue replyRoot(pbnjson::Object());

    postSubscription();

    // if there is one or more keys, return success.
    if (m_reply_success) {
        //              r = (replyInfo->returnStr).c_str();
        // this will add success key list
        replyRoot.put("returnValue", true);
    } else {
        if (!m_errorKeyList.empty()) {
            pbnjson::JValue errorKeyArray(pbnjson::Array());
            for (const std::string& key : m_errorKeyList) {
                errorKeyArray.append(key);
            }
            replyRoot.put("keys", errorKeyArray);
        }

        if (!m_errorText.empty()) {
            replyRoot.put("errorText", m_errorText);
        }

        replyRoot.put("returnValue", false);

        // add source id
        if (!m_app_id.empty()) {
            replyRoot.put("app_id", m_app_id);
        }
        // add category
        if (!m_category.empty()) {
            replyRoot.put("category", m_category);
        }
        if (!m_dimensionObj.isNull()) {
            replyRoot.put("dimension", m_dimensionObj);
        }

    }

    // add method name
    replyRoot.put("method", isRemoveDefKind() ? SETTINGSSERVICE_METHOD_DELETESYSTEMSETTINGS : SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGS);

    if(!m_taskInfo->isBatchCall()){
        LSError lsError;
        LSErrorInit(&lsError);
        result = LSMessageReply(m_lsHandle, m_replyMsg, replyRoot.stringify().c_str(), &lsError);
        if (!result) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply batch for del");
            LSErrorFree(&lsError);
        }
    }

    // m_taskInfo will be NULL
    PrefsFactory::instance()->releaseTask(&m_taskInfo, replyRoot);
}
