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

#include "JSONUtils.h"
#include "Logging.h"
#include "PrefsDb8Condition.h"
#include "PrefsDb8Get.h"
#include "PrefsFileWriter.h"
#include "PrefsNotifier.h"
#include "PrefsVolatileMap.h"
#include "SettingsService.h"
#include "SettingsServiceApi.h"
#include "Utils.h"

#ifndef MIN
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#endif

PrefsDb8Get::PrefsDb8Get(const std::set < std::string >& inKeyList, const std::string& inAppId, const std::string& inCategory, pbnjson::JValue inDimension, bool inSubscribe, LSMessage * inMessage)
{
    Init(inKeyList, inAppId, inCategory, inDimension, inSubscribe, inMessage);
}

PrefsDb8Get::~PrefsDb8Get()
{
    if (m_taskInfo)
        PrefsFactory::instance()->releaseTask(&m_taskInfo);

    if (m_replyMsg)
        LSMessageUnref(m_replyMsg);
}

void PrefsDb8Get::Init(const std::set < std::string >& inKeyList, const std::string& inAppId, const std::string& inCategory, pbnjson::JValue inDimension, bool inSubscribe, LSMessage * inMessage)
{
    m_itemN = 0;
    m_app_id = inAppId;
    m_category = inCategory;
    m_subscribe = inSubscribe;
    m_replyMsg = inMessage;

    if (m_app_id.empty())
        m_app_id = GLOBAL_APP_ID;

    m_keyList = inKeyList;
    m_totalN = m_keyList.size();
    m_successKeyListObj = pbnjson::Object();

    if (!inDimension.isNull()) {
        m_dimensionObj = inDimension;
        PrefsKeyDescMap::removeNotUsedDimension(m_dimensionObj);
    }

    m_forCategoryFlag = (!m_keyList.empty());

    if (m_replyMsg)
        LSMessageRef(m_replyMsg);

    m_taskInfo = NULL;
    m_callback = NULL;
    m_thiz_class = m_user_data = NULL;

    m_isFactoryValueRequest = false;

    m_forceDbSync = false;
}

void PrefsDb8Get::sendConditionCategoryReply(LSHandle *lsHandle)
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot(pbnjson::Object());

    replyRoot.put(KEYSTR_CATEGORY, m_category);
    replyRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS);
    replyRoot.put("returnValue", true);

    pbnjson::JValue condition = PrefsDb8Condition::instance()->getEnvironmentCondition();
    replyRoot.put("settings", condition.isNull() ? pbnjson::Object() : condition);

    if ( !LSMessageReply(lsHandle, m_replyMsg, replyRoot.stringify().c_str(), &lsError) ) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Send condition category");
        LSErrorFree(&lsError);
    }
}

bool PrefsDb8Get::sendRequest(LSHandle * lsHandle)
{
    LSError lsError;
    LSErrorInit(&lsError);
    bool result;

    /* TODO: replace with below file cache */
    if (m_category == KEYSTR_CONDITION) {
        sendConditionCategoryReply(lsHandle);
        return true;
    }

    m_mergeCategoryDimKeyMap = PrefsKeyDescMap::instance()->getCategoryKeyListMap(m_category, m_dimensionObj, m_keyList);
    if (m_app_id != GLOBAL_APP_ID) {
        std::string categoryDim;
        if (PrefsKeyDescMap::instance()->getCategoryDim(m_category, categoryDim)) {
            m_mergeCategoryDimKeyMap[categoryDim] = m_keyList;
        }
    }

    /* load data form json file */
    // TODO defaults always empty array. This code do nothing!!!!!!!
    for (const auto& cat_key_iter : m_mergeCategoryDimKeyMap) {
        pbnjson::JValue defaults = pbnjson::Array();

        if (!defaults.isNull()) {
            std::string errorText;
            parsingResult(defaults, errorText, true);
        }
    }
    /* load data from db8 */
    pbnjson::JValue jsonArrOperations(pbnjson::Array());
    for (const auto& cat_key_iter : m_mergeCategoryDimKeyMap) {
        if (!m_isFactoryValueRequest) {
            jsonArrOperations.append(jsonFindBatchItem(cat_key_iter.first,
                              isKeyListSetting(), cat_key_iter.second, false, "", SETTINGSSERVICE_KIND_MAIN));
        }
        jsonArrOperations.append(jsonFindBatchItem(cat_key_iter.first,
                          isKeyListSetting(), cat_key_iter.second, false, "", SETTINGSSERVICE_KIND_DEFAULT));
    }

    std::set<std::string> checkKeys = m_keyList.empty() ? PrefsKeyDescMap::instance()->getKeysInCategory(m_category) : m_keyList;

    if ( !isForceDbSync() && !m_isFactoryValueRequest && PrefsFileWriter::instance()->isAvailablePreferences(m_category, checkKeys) ) {
        sendCacheReply(lsHandle, checkKeys);
        return true;
    }

    pbnjson::JValue jsonObjParam = pbnjson::Object();
    jsonObjParam.put("operations", jsonArrOperations);
    ref();
    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), cbSendQueryGet, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Send request");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}

/* Generated json object example for find query
    {
      "query": {
        "from": "com.webos.settings.default:1",
        "select": [ "value.brightness" ],
        "where": [
          { "prop": "category", "op": "=", "val": "picture$comp3.natural.3d" },
          { "prop": "country", "op": "=", "val": ["none", "default", "KOR"] }
        ]
      }
    }
 */
pbnjson::JValue PrefsDb8Get::jsonFindBatchItem(const std::string &categoryName, bool isKeyListSetting, const std::set < std::string >& a_keyList,
        bool a_isSupportAppId, const std::string& a_appId, const std::string& targetKindName)
{
    std::string selectItem;
    pbnjson::JValue jsonObjParam(pbnjson::Object());
    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());

    // Select property with requested keys
    if (isKeyListSetting == true) {
        pbnjson::JValue replyRootSelectArray(pbnjson::Array());

        for (const std::string& it : a_keyList) {
            selectItem = "value." + it;
            replyRootSelectArray.append(pbnjson::JValue(selectItem));
        }
        replyRootSelectArray.append(KEYSTR_KIND);
        replyRootSelectArray.append(KEYSTR_COUNTRY);
        replyRootSelectArray.append(KEYSTR_APPID);
        replyRootSelectArray.append(KEYSTR_CATEGORY);
        replyRootSelectArray.append(KEYSTR_CONDITION);
        if (targetKindName == SETTINGSSERVICE_KIND_MAIN)
            replyRootSelectArray.append(KEYSTR_VOLATILE);
        replyRootQuery.put("select", replyRootSelectArray);
    }

    if ( a_isSupportAppId ) {
        pbnjson::JValue item(pbnjson::Object());
        item.put("prop", "app_id");
        item.put("op", "=");
        item.put("val", a_appId);
        replyRootWhereArray.append(item);
    }

    // add category for where
    replyRootItem1.put("prop", "category");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", categoryName);

    // build where
    replyRootWhereArray.append(replyRootItem1);
    if (targetKindName == SETTINGSSERVICE_KIND_DEFAULT) {
        std::string cur_country = PrefsKeyDescMap::instance()->getCountryCode();
        pbnjson::JValue countryItem(pbnjson::Object());
        pbnjson::JValue countryArray(pbnjson::Array());

        countryArray.append("none");
        countryArray.append(cur_country);

        countryItem.put("prop", "country");
        countryItem.put("op", "=");
        countryItem.put("val", countryArray);

        replyRootWhereArray.append(countryItem);
    }
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", targetKindName);

    // add reply root
    replyRoot.put("query", replyRootQuery);

    // build return object
    jsonObjParam.put("method", "find");
    jsonObjParam.put("params", replyRoot);

    return jsonObjParam;
}

// after merging keys in kind priority, final result is added to successKeyList
void PrefsDb8Get::updateSuccessErrorKeyList()
{
    // If key list is empyt, the number of replied items is set for total number.
    //              With empty key_list, Requesting to Default appid is not called.
    // m_itemN can be incressed by requesting to Default app_id
    m_itemN = m_successKeyListObj.objectSize();

    // add the result to array
    m_successKeyList.clear();
    for (pbnjson::JValue::KeyValue it : m_successKeyListObj.children()) {
        m_successKeyList.insert(it.first.asString());
    }

    // remove error keys in m_keyList.
    m_errorKeyList.clear();
    if (!m_keyList.empty()) {
        m_errorKeyList.clear();
        std::set < std::string >::const_iterator successKey;
        for (const std::string& key : m_keyList) {
            for (successKey = m_successKeyList.begin(); successKey != m_successKeyList.end(); ++successKey) {
                if (key == *successKey)
                    break;
            }

            if (successKey == m_successKeyList.end()) {
                m_errorKeyList.insert(key);
            }
        }
    }
    // for categery request
    else {
        m_totalN = m_itemN;
    }
}

int PrefsDb8Get::updateSuccessValueObj(pbnjson::JValue valueObj, std::string & errorText)
{
    if (!valueObj.isObject()) {
        // {"returnValue":true, "results":[{}]}
        errorText = "no items for the request with default appid";
    } else {
        // add the result to array
        for (pbnjson::JValue::KeyValue it : valueObj.children()) {
            m_successKeyListObj << it;
        }
        return valueObj.objectSize();
    }

    return 0;
}

bool filterSettings(pbnjson::JValue jItemObj, const std::set<std::string>& validKeys, /*out*/ pbnjson::JValue jFilteredItemObj)
{
    bool found = false;

    pbnjson::JValue jValue(jItemObj[KEYSTR_VALUE]);
    if (jValue.isObject()) {
        pbnjson::JValue jFilteredValue(pbnjson::Object());

        for(pbnjson::JValue::KeyValue it : jValue.children()) {
            if ( validKeys.find(it.first.asString()) != validKeys.end() ) {
                found = true;
                jFilteredValue << it;
            }
        }

        // keep country property

        pbnjson::JValue jCountry = jItemObj[KEYSTR_COUNTRY];
        if (!jCountry.isNull()) {
            jFilteredItemObj.put(KEYSTR_COUNTRY, jCountry);
        }

        jFilteredItemObj.put(KEYSTR_VALUE, jFilteredValue);
    }

    return found;
}

/* Collect all available keys in records */
std::set<std::string> PrefsDb8Get::keysLayeredRecords(pbnjson::JValue resultArray)
{
    std::set<std::string> keys;

    for (pbnjson::JValue itemObj : resultArray.items()) {
        if ( !itemObj.isObject() )
            continue;

        pbnjson::JValue valueObj = itemObj[KEYSTR_VALUE];
        if (!valueObj.isObject())
            continue;

        for(pbnjson::JValue::KeyValue it : valueObj.children()) {
            keys.insert(it.first.asString());
        }
    }

    return keys;
}

/* Value Overwrite Score
 * If there are duplicated values, the highest score value would be selected
 * This score is used for sorting update sequence of all items in keyArray */

#define VOS_PRIOR_PERAPP 0x08000000 /* system kind and per app */
#define VOS_PRIOR_DEFAPP 0x04000000 /* system kind and app default, FIXME: deprecated?? */
#define VOS_PRIOR_GLOBAL 0x02000000 /* system kind and global */

#define VOS_PRIOR_VOLATI 0x00800000 /* volatile kind */
#define VOS_PRIOR_SYSTEM 0x00400000 /* system(main) kind */
#define VOS_PRIOR_DEFAUT 0x00200000 /* default kind and no country */

#define VOS_PRIOR_CONTRY 0x00080000 /* no country variation, default */
#define VOS_PRIOR_NOCNTR 0x00020000 /* country variation */

#define VOS_PRIOR_CONDIT 0x00001000 // Score for Condition

#define VOS_SCORE_MAXIMU 0x00000FFF

pbnjson::JValue PrefsDb8Get::mergeLayeredRecords(const std::string& a_category, pbnjson::JValue resultArray,
        const std::string &a_app_id, bool a_filterMixed, pbnjson::JValue reqDim)
{
    std::map <int, pbnjson::JValue> update_seq;
    std::string rec_app_id;

    pbnjson::JArray filteredObjContainer;
    std::set<std::string> validGlobalKeys;
    std::set<std::string> validPerappKeys;
    std::set<std::string> complexTypeKeys;
    std::set<std::string> filteredPerAppKeys;//removed wrong dimension keys

    int arraylen = resultArray.arraySize();

    if (arraylen > VOS_SCORE_MAXIMU) {
        SSERVICELOG_WARNING(MSGID_GET_DATA_SIZE_ERR, 2,
                            PMLOGKS("Reason","resultArray size is > VOS_SCORE_MAXIMUM"),
                            PMLOGKFV("ArrayLen","%d",arraylen),
                            "settingsservice cannot merge categorized data");
    }

    /* we already know that all keys are global when a_app_id == GLOBAL_APP_ID
     * initialize key information if only a_app_id != GLOBAL_APP_ID */
    if ( a_app_id != GLOBAL_APP_ID ) {
        PrefsKeyDescMap* prefs = PrefsKeyDescMap::instance();
        std::set<std::string> allKeys = keysLayeredRecords(resultArray);
        prefs->splitKeysIntoGlobalOrPerAppByDescription( allKeys, a_category, a_app_id, validGlobalKeys, validPerappKeys);
        for ( const std::string& key : allKeys ) {
            if ( prefs->getDbType(key) == DBTYPE_MIXED || prefs->getDbType(key) == DBTYPE_EXCEPTION )
                complexTypeKeys.insert(key);
        }
    }
    //remove wrong dimesion keys due to call luna-send without keys.
    //getSystemSettings '{"category":"aspectRatio", "app_id":"youtube.leanback.v4", "dimension" : {"input": "dtv", "resolution":"x","twinMode": "x"}}'
    //issue number = WOSLQEVENT-74304
    filterWrongDimKey(validPerappKeys, filteredPerAppKeys, reqDim);

    // set result to each case
    for (int i = 0; i < arraylen; i++) {
        /* score MUST be unique, because it is used as key in MAP */
        int overwrite_score = i;

        pbnjson::JValue itemObj = resultArray[i];
        if (itemObj.isNull()) {
            SSERVICELOG_ERROR(MSGID_GET_BATCH_ERR, 0, "Batch result error");
            continue;
        }

        pbnjson::JValue label = itemObj[KEYSTR_KIND];
        if (!label.isString()) {
            SSERVICELOG_ERROR(MSGID_GET_NO_KINDSTR, 0, "Batch result has no kind string");
            continue;
        }

        std::string labelString = label.asString();
        if (labelString == SETTINGSSERVICE_KIND_MAIN) {
            overwrite_score |= VOS_PRIOR_SYSTEM;
        } else if (labelString == SETTINGSSERVICE_KIND_MAIN_VOLATILE) {
            overwrite_score |= VOS_PRIOR_VOLATI;
        } else {
            overwrite_score |= VOS_PRIOR_DEFAUT;
        }

        int conditionScore = PrefsDb8Condition::instance()->scoreByCondition(itemObj);
        if (conditionScore == 0)
            continue;
        overwrite_score |= MIN(conditionScore, 0xf) * VOS_PRIOR_CONDIT;

        label = itemObj[KEYSTR_APPID];
        rec_app_id = label.isString() ? label.asString() : GLOBAL_APP_ID;

        if ( a_app_id == GLOBAL_APP_ID && rec_app_id == GLOBAL_APP_ID ) {
            /* Accept all global data when no app_id is specified */
        }
        else if ( a_app_id == GLOBAL_APP_ID && rec_app_id != GLOBAL_APP_ID )
        {
            /* Ignore all per-app data when no app_id is specified */
            continue;
        }
        else if ( a_app_id != GLOBAL_APP_ID && rec_app_id == GLOBAL_APP_ID ) {
            /* Accept some part of global data
             *     if dbtype is M or E even though app_id is specified.
             * First, Save global values.
             * Later if rec_app_id == a_app_id matched, global value will be overwrited. */
            pbnjson::JObject filteredObj;
            filterSettings(itemObj, complexTypeKeys, filteredObj);
            itemObj = filteredObj;
            filteredObjContainer.append(filteredObj); /* for release */
        }
        else if ( a_app_id != GLOBAL_APP_ID && rec_app_id != GLOBAL_APP_ID )
        {
            /* Accept valid per-app data (including com.webos.system)
             *     if per-app descriptoin exist regarding M type, or
             *     if app_id is in not Exception App List regarding E type */
            pbnjson::JObject filteredObj;
            filterSettings(itemObj, filteredPerAppKeys, filteredObj);
            itemObj = filteredObj;
            filteredObjContainer.append(filteredObj); /* for release */
        }

        if ( rec_app_id == a_app_id ) {
            overwrite_score |= VOS_PRIOR_PERAPP;
        } else if ( rec_app_id == DEFAULT_APP_ID ) {
            overwrite_score |= VOS_PRIOR_DEFAPP;
        } else if ( rec_app_id == GLOBAL_APP_ID ) {
            overwrite_score |= VOS_PRIOR_GLOBAL;
        } else {
            /* Ignore unexpected app_id */
            continue;
        }

        label = itemObj[KEYSTR_COUNTRY];
        if (!label.isString()) {
            overwrite_score |= VOS_PRIOR_NOCNTR;
        } else if (label.asString().find(PrefsKeyDescMap::instance()->getCountryCode())!=std::string::npos) {
            overwrite_score |= VOS_PRIOR_CONTRY;
        } else {
            /* Ignore useless country variation. */
            continue;
        }

        update_seq.insert(std::map<int, pbnjson::JValue>::value_type(overwrite_score, itemObj));
    }

    // Default data is updated at first before app specific one.
    // If App specific data exists, the data remains finally.

    pbnjson::JObject mergedValueObj;

    for(const auto& update_iter : update_seq)
    {
        pbnjson::JValue keyList = update_iter.second["value"];

        if (keyList.isObject()) {
            for( pbnjson::JValue::KeyValue it : keyList.children()) {
                /* remove mixed type value in global settings.
                 * Mixed type value is in GLOBAL_APP_ID records, But it is treated as per-app and ExceptionApp*/
                if ( a_filterMixed && a_app_id == GLOBAL_APP_ID && PrefsKeyDescMap::instance()->getDbType(it.first.asString()) == DBTYPE_MIXED )
                    continue;

                if ( a_filterMixed && a_app_id == GLOBAL_APP_ID && PrefsKeyDescMap::instance()->getDbType(it.first.asString()) == DBTYPE_EXCEPTION )
                    continue;

                mergedValueObj << it;
            }
        }
    }

    return mergedValueObj;
}


void PrefsDb8Get::filterWrongDimKey(const std::set<std::string>& perAppKeys, std::set<std::string>& filteredPerAppKeys, pbnjson::JValue reqDim)
{
    if (reqDim.isNull()) {
        filteredPerAppKeys = perAppKeys;
        return;
    }

    std::vector<std::string> dimensionVector;
    for (const std::string& key : perAppKeys ) {
        bool blnWrongKey = false;

        PrefsKeyDescMap::instance()->getDimensionsByKey(key,&dimensionVector);

        for (const std::string& dim : dimensionVector ){
            pbnjson::JValue label = reqDim[dim];

            if (label.isNull()) {
                blnWrongKey = true;
                break;
            }
        }

        if(!blnWrongKey)
            filteredPerAppKeys.insert(key);
    }
}


int PrefsDb8Get::parsingResult(pbnjson::JValue resultArray, std::string & errorText, bool a_filterMixed)
{
    int successCnt = 0;

    pbnjson::JValue mergedValue = mergeLayeredRecords(m_category, resultArray, m_app_id, a_filterMixed, m_dimensionObj);

    if (!mergedValue.isNull()) {
        successCnt = updateSuccessValueObj(mergedValue, errorText);
    }

    if (successCnt) {
        updateSuccessErrorKeyList();
    }

    return successCnt;
}

/**
Make array_list from the payload.
The payload has 2 sets from two kinds of '.system:1' and '.default:1'.
*/
pbnjson::JValue PrefsDb8Get::newKeyArrayfromBatch(pbnjson::JValue root)
{
    bool found_value_property = false;

    pbnjson::JValue resp_array = root["responses"];
    if (!resp_array.isArray() || 0 == resp_array.arraySize()) {
        return pbnjson::JValue();
    }

    pbnjson::JValue new_array(pbnjson::Array());
    /* for all responses item */
    for (pbnjson::JValue resp_obj : resp_array.items()) {
        pbnjson::JValue result_array = resp_obj["results"];

        if (!result_array.isArray() || result_array.arraySize() == 0)
            continue;

        /* for all results in each respsonse */
        for (pbnjson::JValue result_obj : result_array.items()) {
            /* Simple validation */
            if (result_obj.hasKey("value")) {
                found_value_property = true;
                new_array.append(result_obj);
            }
        }
    }

    if (found_value_property==false) {
        SSERVICELOG_DEBUG("incorrect batch(find) result during getSystemSettings, %s", root.stringify().c_str());
    }

    return new_array;
}

void PrefsDb8Get::handleVolatileKey()
{
    PrefsVolatileMap *prefsVolatileMap = PrefsVolatileMap::instance();

    for (CategoryDimKeyListMap::const_iterator itDimKey = m_mergeCategoryDimKeyMap.begin(); itDimKey != m_mergeCategoryDimKeyMap.end(); itDimKey++) {
        const std::string & dim = itDimKey->first;
        std::set<std::string> keys = itDimKey->second;

        if ( isKeyListSetting() != true )
            keys = PrefsKeyDescMap::instance()->getKeysInCategory(m_category);

        for (const std::string& key : keys) {
            if (PrefsKeyDescMap::instance()->isVolatileKey(key.c_str()) == false)
                continue;
            const std::string val = prefsVolatileMap->getVolatileValue(dim, m_app_id, key);
            if (!val.empty()) {
                // This routine does same action of
                // updateSuccessValueObj() from parsingResult()
                m_successKeyListObj.put(key, pbnjson::JDomParser::fromString(val));
            }
        }
    }
}

bool PrefsDb8Get::cbSendQueryGet(LSHandle * lsHandle, LSMessage * message, void *data)
{
    pbnjson::JValue root;
    bool success = false;
    bool successFlag = false;
    std::string errorText;

    PrefsDb8Get *replyInfo = (PrefsDb8Get *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_GET_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_GET_PARSE_ERR, 0, "payload is : %s", payload);
            errorText = "couldn't parse json";
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean())
            successFlag = label.asBool();

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_GET_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue resultArray = replyInfo->newKeyArrayfromBatch(root);

        if (resultArray.isNull()) {
            errorText = "no result from DB";
            break;
        }

        replyInfo->parsingResult(resultArray, errorText, true);

        if (replyInfo->m_successKeyList.empty()) {
            errorText = "There is no matched result from DB";
        } else {
            success = true;
        }

        if (!replyInfo->m_isFactoryValueRequest) {
            replyInfo->handleVolatileKey();
        }
    } while(false);

    if (replyInfo->m_callback)
    {
        // In the case user preset callback, then just do callback WITHOUT sendResultReply.
        // Because, user callback do post processing accordingly.
        //

        if (success && !replyInfo->m_successKeyList.empty())
        {
            replyInfo->m_callback(replyInfo->m_thiz_class, replyInfo->m_user_data, replyInfo->m_category, replyInfo->m_app_id, replyInfo->m_dimensionObj, replyInfo->m_successKeyListObj);
        }

#if 0
        // TODO - Checks whether followings are needed.
        // If user callback is not called, the releaseTask will be called on destroy.
        //
        if (completed)
        {
            PrefsFactory::instance()->releaseTask(&replyInfo->m_taskInfo, pbnjson::Object());
        }
#endif
    }
    else
    {
        replyInfo->sendResultReply(lsHandle, success, errorText);
    }
    replyInfo->unref();

    return true;
}

void PrefsDb8Get::sendCacheReply(LSHandle* a_handle, const std::set<std::string>& a_keys)
{
    for ( const std::string& k : a_keys ) {
        pbnjson::JValue jsonCache = PrefsFileWriter::instance()->getPreferences(m_category, k);
        if ( !jsonCache.isNull() ) {
            m_successKeyListObj.put(k, jsonCache);
        }
    }

    updateSuccessErrorKeyList();

    bool success = !m_successKeyList.empty();

    if ( m_callback )
    {
        // In the case user preset callback, then just do callback WITHOUT sendResultReply.
        // Because, user callback do post processing accordingly.
        //
        if ( success ) {
            m_callback(m_thiz_class, m_user_data, m_category, m_app_id, m_dimensionObj, m_successKeyListObj);
        } else {
            SSERVICELOG_WARNING(MSGID_GET_CACHE_FAIL, 2,
                    PMLOGKS("Category", m_category.c_str()),
                    PMLOGKFV("Keys", "%d", a_keys.size()),
                    "Cache loading fail");
        }
    }
    else
    {
        sendResultReply(a_handle, success);
    }
}


void PrefsDb8Get::sendResultReply(LSHandle * lsHandle, bool success, const std::string &errorText)
{
    bool resultReply;
    bool resultValue;
    bool subscribeResult = false;

    // send reply
    pbnjson::JObject replyRoot;
    //      add settings
    if (success) {
        // set subscription
        //              m_totalN is zero with no key list for the result
        if (m_itemN && m_subscribe) {
            for (pbnjson::JValue::KeyValue it : m_successKeyListObj.children()) {
                regSubscription(lsHandle, it.first.asString());
            }
            subscribeResult = true;
        }
        // add result key list
        if (!m_successKeyList.empty()) {
            replyRoot.put("settings", m_successKeyListObj);
        }
        // add return value
        //              check all item is success.
        resultValue = (m_itemN && m_itemN == m_totalN);
    } else {
        resultValue = false;
        if (!errorText.empty()) {
            replyRoot.put("errorText", errorText);
        }
    }

    // add source id
    if (isForGlobalSettings() == false) {
        if ( m_app_id == DEFAULT_APP_ID ) m_app_id = ""; /* DEFAULT_APP_ID is used internally */
        replyRoot.put("app_id", m_app_id);
    }

    // add category
    if (!m_category.empty()) {
        replyRoot.put("category", m_category);
    }

    // add dimension
    {
        pbnjson::JValue categoryDim(PrefsKeyDescMap::instance()->getDimKeyValueObj(m_category, m_dimensionObj));
        if (!categoryDim.isNull()) {
            // In case of sending multiple keys, and ONLY if it is related with dimensions,
            // the 'dimension' should be made using OR-ed all the possible dimension per each keys.
            //
            pbnjson::JObject diminfo;
            for (pbnjson::JValue::KeyValue it : m_successKeyListObj.children()) {
                PrefsKeyDescMap::instance()->getDimKeyValueObj(m_category, m_dimensionObj, it.first.asString(), diminfo);
            }

            if (!diminfo.isNull()) {
                replyRoot.put("dimension", diminfo);
            }
        }
    }

    // add error Keys
    if (!m_errorKeyList.empty()) {
        pbnjson::JValue replyRootErrorKeyArray(pbnjson::Array());
        for (const std::string& it : m_errorKeyList) {
            replyRootErrorKeyArray.append(it);
        }

        replyRoot.put("errorKey", replyRootErrorKeyArray);
        resultValue = false;
    }

    if ( !m_isFactoryValueRequest )
        replyRoot.put("subscribed", subscribeResult);
    replyRoot.put("method", m_taskInfo->getMethodName());

    replyRoot.put("returnValue", resultValue);
    if(!m_taskInfo->isBatchCall()){
        LSError lsError;
        LSErrorInit(&lsError);
        resultReply = LSMessageReply(lsHandle, m_replyMsg, replyRoot.stringify().c_str(), &lsError);
        if (!resultReply) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Send reply");
            LSErrorFree(&lsError);
        }
    }

    // m_taskInfo will be NULL
    PrefsFactory::instance()->releaseTask(&m_taskInfo, replyRoot);
}

void PrefsDb8Get::regSubscription(LSHandle * lsHandle, const std::string &a_key)
{
    std::string subscribeKey;
    std::string categoryStr(m_category);
    pbnjson::JValue effectiveDimensionObj;

    /* dimension parameter is useless when a_key is E type and m_app_id is not in exceptin app list.
         * Even though dimension info is specified,
         * Do not add dimension info into subscribe key if the key is not related with dimension */
    std::set<std::string> validGlobalKeys;
    std::set<std::string> validPerappKeys;
    PrefsKeyDescMap::instance()->splitKeysIntoGlobalOrPerAppByDescription( { a_key }, m_category, m_app_id, validGlobalKeys, validPerappKeys);
    if ( validPerappKeys.empty() ) {
        effectiveDimensionObj = m_dimensionObj;
    }

    /* if any dimension info is not specified, user should receive all key change notification
     * regardless of dimension match
     * So, if effectiveDimensionObj is NULL, subscribeKey is created with key and category */
    pbnjson::JValue dimInfo = PrefsKeyDescMap::instance()->getDimKeyValueObj(m_category, effectiveDimensionObj, a_key);
    if (!dimInfo.isNull() && !effectiveDimensionObj.isNull())
        categoryStr += dimInfo.stringify();

    if (dimInfo.isObject() && effectiveDimensionObj.isNull())
    {
        // If 'dimension' is not specified and ONLY if it's related with dimension,
        // then add subscription for dimension.
        //
        std::vector<std::string> dimensionList;
        for(pbnjson::JValue::KeyValue it : dimInfo.children())
        {
            if (it.first.isNull() || it.second.isNull())
            {
                continue;
            }
            if ("x" != it.second.asString())
            {
                dimensionList.push_back(it.first.asString());
            }
        }

        if (dimensionList.size())
        {
            PrefsNotifier::instance()->addSubscriptionPerDimension(lsHandle, m_category, a_key,  m_app_id, m_replyMsg);
        }
    }

    /* This registers subscription element for posting all subscription message
     * regardless modification of settings.
     * SettingsService would send notification message for whole elements if country is changed.
     * country key is not registered.
     * country subscribe message should be notified at first before executing PrefsNotifier. */
    if ( a_key != KEYSTR_COUNTRY && PrefsKeyDescMap::instance()->hasCountryVar(a_key) ) {
        PrefsNotifier::instance()->addSubscription(lsHandle, m_category, effectiveDimensionObj, a_key, m_replyMsg);
    }

    if (m_app_id != GLOBAL_APP_ID) {
        PrefsNotifier::instance()->addSubscriptionPerApp(m_category, a_key, m_app_id, m_replyMsg);
    }
    /* TODO: remove this scheme and use PrefsNotifier instance */
    bool isGlobalID = PrefsKeyDescMap::instance()->getDbType(a_key) == DBTYPE_EXCEPTION && PrefsKeyDescMap::instance()->isExceptionAppList(m_app_id);
    subscribeKey = SUBSCRIBE_STR_KEY(a_key, (isGlobalID ? std::string(GLOBAL_APP_ID) : m_app_id), categoryStr);

    //Add app_id for exception subscribe
    if ( !m_app_id.empty() )
        PrefsKeyDescMap::instance()->setAppIdforSubscribe(m_replyMsg,m_app_id);

    Utils::subscriptionAdd(lsHandle, subscribeKey.c_str(), m_replyMsg);
}


void PrefsDb8Get::Connect(Callback a_func, void *thiz_class, void *userdata)
{
    m_callback = a_func;
    m_thiz_class = thiz_class;
    m_user_data = userdata;
}

void PrefsDb8Get::Disconnect()
{
    m_callback = NULL;
}
