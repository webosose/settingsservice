// Copyright (c) 2013-2023 LG Electronics, Inc.
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

#include "PrefsDb8SetValues.h"
#include "Logging.h"
#include "SettingsServiceApi.h"

bool PrefsDb8SetValues::sendRequest(LSHandle * lsHandle)
{
    // if category or dimension is changed, check if the key is used or not.
    //      sendCheckUsedKeyRequest > sendRequestByVtype
    if (!PrefsKeyDescMap::instance()->isNewKey(m_key)) {
        if ((m_categoryFlag && !PrefsKeyDescMap::instance()->isSameCategory(m_key, m_category)) ||
                (!m_dimension.isNull() && !PrefsKeyDescMap::instance()->isSameDimension(m_key, m_dimension)))
        {
                return sendCheckUsedKeyRequest(lsHandle, SETTINGSSERVICE_KIND_MAIN);
        }
    }

    return sendRequestByVtype(lsHandle);
}

bool PrefsDb8SetValues::sendCheckUsedKeyRequest(LSHandle* lsHandle, const char* targetKind)
{
    LSError lsError;
    LSErrorInit(&lsError);

    std::string category;
    std::string selectKey;
    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootSelectArray(pbnjson::Array());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());
    bool result;

    m_targetKind = targetKind ? targetKind : "";

    selectKey = "value." + m_key;
    PrefsKeyDescMap::instance()->getCategory(m_key, category);

    std::string opValue = category.empty() ? "=" : "%";

    replyRootSelectArray.append(selectKey);
    replyRootQuery.put("select", replyRootSelectArray);

    // add where
    //              add category
    replyRootItem1.put("prop", "category");
    replyRootItem1.put("op", opValue);
    replyRootItem1.put("val", category);

    replyRootWhereArray.append(replyRootItem1);
    replyRootQuery.put("limit", 1);
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", targetKind);

    // add reply root
    replyRoot.put("query", replyRootQuery);
    SSERVICELOG_TRACE("%s: %s", __FUNCTION__, replyRoot.stringify().c_str());
    ref();
    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/find", replyRoot.stringify().c_str(), cbCheckUsedKeyRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Send reply");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}


bool PrefsDb8SetValues::sendRequestByVtype(LSHandle* lsHandle) {
    m_targetKind = m_defKindFlag ? SETTINGSSERVICE_KIND_DFLT_DESC : SETTINGSSERVICE_KIND_MAIN_DESC;

    if (!m_op.empty()) {
        m_descFlag = false;

        if (m_op == "remove") {
            if (m_vtype == "Array" || m_vtype == "ArrayExt") {
                return sendFindRequest(lsHandle);
            } else {
                return sendDelRequest(lsHandle);
            }
        } else if (m_op == "add" || m_op == "update") {
            if (m_vtype == "Array" || m_vtype == "ArrayExt") {
                return sendFindRequest(lsHandle);
            } else {
                return sendMergeRequest(lsHandle);
            }
        } else                  // if(m_op == "set")
            return sendMergeRequest(lsHandle);
    }
    // for Desc mode with no m_op
    else {
        m_descFlag = true;
        return sendMergeRequest(lsHandle);
    }

    return false;
}

bool PrefsDb8SetValues::sendMergeRequest(LSHandle * lsHandle)
{
    LSError lsError;
    bool result;

    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootProps(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());

    /*
       luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/merge '{"props":{"values":{"array":["4d"]}}, "query":{"from":"com.webos.settings.desc.system:1", "where":[{"prop":"key","op":"=","val":"3d_mode"}]}}'
     */

    // add category to where
    replyRootItem1.put("prop", "key");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", m_key);
    replyRootWhereArray.append(replyRootItem1);

    pbnjson::JValue jWhereItem(pbnjson::Object());
    jWhereItem.put("prop", "app_id");
    jWhereItem.put("op", "=");
    jWhereItem.put("val", m_appId);
    replyRootWhereArray.append(jWhereItem);

    if ( m_defKindFlag ) {
        pbnjson::JValue whereItemObj(pbnjson::Object());
        /* avoid modifying country variations */
        whereItemObj.put("prop", "country");
        whereItemObj.put("op", "=");
        whereItemObj.put("val", "none");
        replyRootWhereArray.append(whereItemObj);
    }

    // add to query
    replyRootQuery.put("from", m_targetKind);
    replyRootQuery.put("where", replyRootWhereArray);

    // add to Props
    if (m_descFlag) {
        fillDescPropertiesForRequest(replyRootProps, false);
        if (m_categoryFlag) {
            replyRootProps.put("category", m_category);
        }
    }

    if (!m_vtype.empty()) {
        replyRootProps.put("vtype", m_vtype);
    }
    if (!m_values.isNull()) {
        replyRootProps.put("values", m_values);
    }
    // add to root

#ifdef USE_MEMORY_KEYDESC_KIND
    replyRootProps.put("key", m_key);
    replyRootProps.remove("_kind");
    setKeyInfoObj(replyRootProps);
#endif // USE_MEMORY_KEYDESC_KIND

    replyRoot.put("props", replyRootProps);
    replyRoot.put("query", replyRootQuery);

    SSERVICELOG_TRACE("%s: %s", __FUNCTION__, replyRoot.stringify().c_str());
    ref();
    // send request
    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/merge", replyRoot.stringify().c_str(), PrefsDb8SetValues::cbMergeRequest, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_MERGE_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");

        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}

bool PrefsDb8SetValues::sendDelRequest(LSHandle * lsHandle)
{
    LSError lsError;
    bool result;

    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());

/*
luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/del '{"query":{"from":"com.webos.settings.desc.system:1", "where":[{"prop":"key","op":"=","val":"brightness"}]}}'
*/
    replyRootItem1.put("prop", "key");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", m_key);
    replyRootWhereArray.append(replyRootItem1);

    replyRootQuery.put("from", m_targetKind);
    replyRootQuery.put("where", replyRootWhereArray);

    replyRoot.put("query", replyRootQuery);
    replyRoot.put("purge", true);

    ref();

    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/del", replyRoot.stringify().c_str(), PrefsDb8SetValues::cbDelRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_DEL_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}

bool PrefsDb8SetValues::sendFindRequest(LSHandle * lsHandle)
{
    LSError lsError;
    bool result;

    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());

/*
luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/find '{"query":{"from":"com.webos.settings.desc.system:1", "where":[{"prop":"key", "op":"=", "val":"3d_mode"}]}}'
*/

    replyRootItem1.put("prop", "key");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", m_key);
    replyRootWhereArray.append(replyRootItem1);

    replyRootQuery.put("from", m_targetKind);
    replyRootQuery.put("where", replyRootWhereArray);

    replyRoot.put("query", replyRootQuery);

    ref();
    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/find", replyRoot.stringify().c_str(), PrefsDb8SetValues::cbAddRemoveRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_FIND_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return true;
}

void PrefsDb8SetValues::fillDescPropertiesForRequest(pbnjson::JValue jRootProps, bool handleNewKey)
{
    if (m_descFlag) {
        if (!m_ui.isNull()) {
            jRootProps.put("ui", m_ui);
        }
        if (!m_dbtype.empty()) {
            jRootProps.put(KEYSTR_DBTYPE, m_dbtype);
        }
        if ((m_dbtype == DBTYPE_MIXED || m_dbtype == DBTYPE_EXCEPTION) && m_appId != GLOBAL_APP_ID) {
            jRootProps.put(KEYSTR_APPID, m_appId);
        }
        if (!m_dimension.isNull()) {
            jRootProps.put("dimension", m_dimension);
        }
        else if (handleNewKey && PrefsKeyDescMap::instance()->isNewKey(m_key)) {
            SSERVICELOG_DEBUG("Set default dimension due to new key.");
            jRootProps.put("dimension", pbnjson::Array());
        }
        else {
            // use previous one.
        }
        if (m_valueCheckFlag) {
            jRootProps.put("valueCheck", m_valueCheck);
        }
    }
}

bool PrefsDb8SetValues::sendPutRequest(LSHandle * lsHandle)
{
    LSError lsError;
    bool result;

    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootObjectArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());

/*
luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/put '{"objects":[{"_kind":"com.webos.settings.desc.system:1", "key":"3d_mode", "vtype":"Array", "values":{"array":["3d", "2d"]}}]}'
*/

    // add to value

    // add to where
    replyRootItem1.put("_kind", m_targetKind);
    //pbnjson::JValue_object_add(replyRootItem1, "value", replyRootValue);
    replyRootItem1.put("key", m_key);
    if (!m_vtype.empty()) {
        replyRootItem1.put("vtype", m_vtype);
    }
//              pbnjson::JValue_object_add(replyRootItem1, "op", pbnjson::JValue_new_string(m_op.c_str()));

    if (!m_values.isNull()) {
        replyRootItem1.put("values", m_values);
    }
    fillDescPropertiesForRequest(replyRootItem1, true);

    // if the key is new one, set "" for a category
    replyRootItem1.put("category", m_category);

    replyRootItem1.put(KEYSTR_APPID, m_appId);

    replyRootObjectArray.append(replyRootItem1);
    // add to root
    replyRoot.put("objects", replyRootObjectArray);
    std::string reqStr = replyRoot.stringify();

#ifdef USE_MEMORY_KEYDESC_KIND
    replyRootItem1.remove("_kind");
    setKeyInfoObj(replyRootItem1);
#endif // USE_MEMORY_KEYDESC_KIND

    ref();
    // send a requet to DB8
    result = DB8_luna_call(lsHandle, "luna://com.webos.service.db/put", reqStr.c_str(), PrefsDb8SetValues::cbPutRequest, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_PUT_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }
    return true;
}

inline static bool array_item_equal(pbnjson::JValue one, pbnjson::JValue another)
{
    /* NOTICE: This function doesn't support whole json object type */
    bool result = false;

    auto one_type = one.getType();
    auto another_type = another.getType();

    if (one_type != another_type)
        return false;

    pbnjson::JValue array_ext_one;
    pbnjson::JValue array_ext_another;
    switch (one_type)
    {
        case JValueType::JV_STR:
            if (one.asString() == another.asString())
                result = true;
            break;
        case JValueType::JV_OBJECT:
            array_ext_one = one["value"];
            if (array_ext_one.isNull())
                break;
            array_ext_another = another["value"];
            if (array_ext_another.isNull())
                break;
            if (array_ext_one == array_ext_another)
                result = true;
            break;
        default:
            break;
    }

    return result;
}

bool PrefsDb8SetValues::cbCheckUsedKeyRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    DimKeyValueMap::iterator    itKeyValueMap;
    std::list<std::string>::iterator it;
    bool success = false;

    PrefsDb8SetValues *replyInfo = (PrefsDb8SetValues *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (!label.isBoolean() || !label.asBool()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            break;
        }

        pbnjson::JValue resultArray = root["results"];
        if (!resultArray.isArray()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_JSON_TYPE_ARRAY_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            break;
        }

        if (resultArray.arraySize() <= 0) {
            SSERVICELOG_WARNING(MSGID_SETVAL_JSON_TYPE_ARRAY_LEN_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            break;
        }

        for(pbnjson::JValue it : resultArray.items()) {
            label = it[KEYSTR_VALUE];
            if (label.isObject()) {
                pbnjson::JValue valueObj = label[replyInfo->m_key];
                if (!valueObj.isNull()) {
                    success = true;
                    SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());
                    break;
                }
            }
        }
    } while(false);

    // set default value for the keys that has no return with default kind
    if(!success) {
        if(replyInfo->m_targetKind == SETTINGSSERVICE_KIND_DEFAULT) {
            replyInfo->sendRequestByVtype(lsHandle);
        }
        // send request again to default kind
        else {
            replyInfo->sendCheckUsedKeyRequest(lsHandle, SETTINGSSERVICE_KIND_DEFAULT);
        }
    }
    else {
        std::string errorText = "the used key could not be changed with category or dimension";
        replyInfo->sendResultReply(lsHandle, false, errorText);
    }

    if (replyInfo)
        replyInfo->unref();

    return success;
}


bool PrefsDb8SetValues::cbAddRemoveRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    int arraylen;
    std::string errorText;
    std::string vtype;
    std::string values_prop;

    PrefsDb8SetValues *replyInfo = (PrefsDb8SetValues *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (!label.isBoolean() || false == label.asBool()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            break;
        }

        pbnjson::JValue returnArray = root["results"];
        if (!returnArray.isArray()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_JSON_TYPE_ARRAY_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "no result in DB";
            break;
        }

        arraylen = returnArray.arraySize();
        if (arraylen <= 0) {
            SSERVICELOG_WARNING(MSGID_SETVAL_JSON_TYPE_ARRAY_LEN_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "no result in DB";
            break;
        }

        pbnjson::JValue resultRecord = returnArray[0];
        label = resultRecord["vtype"];
        if (!label.isString()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_NO_VTYPE, 0, "payload : %s", payload);
            errorText = "no vtype in the result";
            break;
        }
        vtype = label.asString();

        pbnjson::JValue values = resultRecord["values"];
        if (values.isNull()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_NO_VALUES, 0, "payload : %s", payload);
            errorText = "no values in the result";
            break;
        }

        if (replyInfo->m_vtype == "Array") {
            values_prop = "array";
        } else if (replyInfo->m_vtype == "ArrayExt") {
            values_prop = "arrayExt";
        } else {
            SSERVICELOG_WARNING(MSGID_SETVAL_TYPE_MISMATCH, 0, "payload : %s", payload);
            errorText = "Incorrect vtype " + vtype;
            break;
        }

        pbnjson::JValue oldItemArray = values[values_prop];
        if (!oldItemArray.isArray()) {
            errorText = "add, update or remove opertation is available in already defined array(Ext) type!!";
            break;
        }

        if (replyInfo->m_op == "add") {
            JValueType oldItemType = JValueType::JV_NULL;

            pbnjson::JValue newItemArray = replyInfo->m_values[values_prop];
            if(!newItemArray.isArray()) {
                errorText = "array(Ext) can't be found!!";
                break;
            }

            // Precondition: All items of oldItemArray always have same json_type.
            if (oldItemArray.arraySize() > 0)
                oldItemType = oldItemArray[0].getType();

            for (pbnjson::JValue it : newItemArray.items()) {
                if (it.isNull())
                    continue;
                if (oldItemType != JValueType::JV_NULL && it.getType() != oldItemType)
                    continue;

                // check item duplication.
                bool add = true;
                for (pbnjson::JValue it2 : oldItemArray.items()) {
                    if (array_item_equal(it, it2)) {
                        add = false;
                        break;
                    }
                }

                // add to array for DB
                //              newObj is removed by pbnjson::JValue_object_add. "array" is overapped in m_values.
                //              it should be in pbnjson::JValue_get
                if (add)
                    oldItemArray.append(it);
            }

            // assign to m_values
            replyInfo->m_values.put(values_prop, oldItemArray);
        } else if (replyInfo->m_op == "update") {
            pbnjson::JValue newItemArray(pbnjson::Array());

            pbnjson::JValue updateItemArray = replyInfo->m_values[values_prop];
            pbnjson::JValue updatedObj;

            for (pbnjson::JValue it : oldItemArray.items()) {
                if (it.isNull())
                    continue;

                // If same object is found, update
                for (pbnjson::JValue it2 : updateItemArray.items()) {
                    if (array_item_equal(it, it2)) {
                        updatedObj = it2;
                        break;
                    }
                }

                // add to array for DB & incress reference count
                if (!updatedObj.isNull())
                    newItemArray.append(it);
                else
                    newItemArray.append(updatedObj);
            }

            replyInfo->m_values.put(values_prop, newItemArray);
        } else {                    // remove
            pbnjson::JValue newItemArray(pbnjson::Array());

            pbnjson::JValue removeItemArray = replyInfo->m_values[values_prop];

            for (pbnjson::JValue it : oldItemArray.items()) {
                if (it.isNull())
                    continue;

                // check item duplication.
                bool add = true;
                for (pbnjson::JValue it2 : removeItemArray.items()) {
                    if (array_item_equal(it, it2)) {
                        add = false;
                        break;
                    }
                }

                // add to array for DB & incress reference count
                if (add)
                    newItemArray.append(it);
            }

            replyInfo->m_values.put(values_prop, newItemArray);
        }

        // set modified values
        success = replyInfo->sendMergeRequest(lsHandle);
        if (!success) {
            errorText = "ERROR!! sending a request to DB";
        }
    } while(false);

    // send repl
    //      add settings
    if (success) {
        // Do nothing. Waiting for result
    } else {
        replyInfo->sendResultReply(lsHandle, false, errorText);
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8SetValues::cbDelRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    int count = 0;
    std::string errorText;
    PrefsDb8SetValues *replyInfo = (PrefsDb8SetValues *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean() && label.asBool() == false) {
            SSERVICELOG_WARNING(MSGID_SETVAL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            break;
        }

        label = root["count"];
        if (!label.isNumber()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_NO_COUNT, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "No count property in return. DB Error!!";
            break;
        }

        count = label.asNumber<int>();
        if (!count) {
            SSERVICELOG_WARNING(MSGID_SETVAL_JSON_TYPE_INT_ERR, 0, "payload : %s", payload);
            errorText = std::string("No item is removed.");
        }

        success = true;
    } while(false);

#ifdef USE_MEMORY_KEYDESC_KIND
    replyInfo->removeKeyDescMap();
#endif // USE_MEMORY_KEYDESC_KIND
    replyInfo->sendResultReply(lsHandle, success, errorText);

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8SetValues::cbMergeRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    int returnCount;
    std::string errorText;

    PrefsDb8SetValues *replyInfo;
    replyInfo = (PrefsDb8SetValues *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean() && label.asBool() == false) {
            SSERVICELOG_WARNING(MSGID_SETVAL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("ERROR!! to put new item, in merge request");
            break;
        }

        label = root["count"];
        if (!label.isNumber()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_NO_COUNT, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "There is some error in DB";
            break;
        }

        returnCount = label.asNumber<int>();
        if (returnCount > 0) {
    #ifdef USE_MEMORY_KEYDESC_KIND
            replyInfo->addToKeyDescMap();
    #endif // USE_MEMORY_KEYDESC_KIND
            success = true;
        } else {
            success = replyInfo->sendPutRequest(lsHandle);
            if (!success) {
                SSERVICELOG_WARNING(MSGID_SETVAL_REQ_FAIL, 0, " ");
                errorText = "ERROR!! sending a request to DB";
            }
        }
    } while(false);

    if (success && !returnCount) {
        // Do nothing. Waiting for sending reply after PutRequest
    } else {                    // error case : return error msg
        replyInfo->sendResultReply(lsHandle, success, errorText);
    }

    if (replyInfo)
        replyInfo->unref();

    return true;
}

bool PrefsDb8SetValues::cbPutRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    std::string errorText;

    PrefsDb8SetValues *replyInfo;
    replyInfo = (PrefsDb8SetValues *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_SETVAL_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean() && label.asBool() == true) {
            success = true;
        } else {
            SSERVICELOG_WARNING(MSGID_SETVAL_DB_RETURNS_FAIL, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("ERROR!! to add new record");
        }
    } while(false);

#ifdef USE_MEMORY_KEYDESC_KIND
    replyInfo->addToKeyDescMap();
#endif // USE_MEMORY_KEYDESC_KIND
    replyInfo->sendResultReply(lsHandle, success, errorText);

    if (replyInfo)
        replyInfo->unref();

    return true;
}

void PrefsDb8SetValues::sendResultReply(LSHandle * lsHandle, bool success, std::string & errorText)
{
    pbnjson::JValue replyRoot(pbnjson::Object());

    if (success) {
        // subscribe
        postSubscription();
    } else if (!errorText.empty()) {
        replyRoot.put("errorText", errorText);
    }
    replyRoot.put("returnValue", success);

    const char* method;
    if (m_descFlag) {
        method = m_defKindFlag ? SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYDESC :
                                 SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGDESC;
    }
    else {
        method = SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGVALUES;
    }

    replyRoot.put("method", method);

    if(!m_taskInfo->isBatchCall()){
        LSError lsError;
        LSErrorInit(&lsError);

        bool result = LSMessageReply(lsHandle, m_replyMsg, replyRoot.stringify().c_str(), &lsError);
        if (!result) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Send reply for set values");
            LSErrorFree(&lsError);
        }
    }

    // m_taskInfo will be NULL
    PrefsFactory::instance()->releaseTask(&m_taskInfo, replyRoot);
}

void PrefsDb8SetValues::postSubscription()
{
    std::string subscribeKey;
    pbnjson::JValue subscribeRoot(pbnjson::Object());
    bool changeFlag = false;
    const char *sender = NULL;
    const char *caller = NULL;

    if (m_replyMsg) {
        caller = LSMessageGetApplicationID(m_replyMsg) ?
             LSMessageGetApplicationID(m_replyMsg) :
             LSMessageGetSenderServiceName(m_replyMsg);
    }

    if (m_notifySelf == false)
        sender = LSMessageGetSender(m_replyMsg);

    /* Regardless target kind (m_defKindFlag),
     * Subscription message always should be notified */

    pbnjson::JValue cachedDesc(PrefsKeyDescMap::instance()->genDescFromCache(m_key.c_str(), m_appId));
    if (cachedDesc.isNull() ) {
        SSERVICELOG_WARNING(MSGID_SETVAL_SUBS_ERROR, 1, PMLOGKS("Key", m_key.c_str()), "No cache");
        return;
    }

    subscribeRoot.put("returnValue", true);

    pbnjson::JValue prop = cachedDesc["key"];
    if (!prop.isNull())
        subscribeRoot.put("key", prop);
    prop = cachedDesc["category"];
    if (!prop.isNull())
        subscribeRoot.put("category", prop);
    prop = cachedDesc["dimension"];
    if (!prop.isNull())
        subscribeRoot.put("dimension", prop);
    prop = cachedDesc["vtype"];
    if (!prop.isNull() )
        subscribeRoot.put("vtype", prop);
    prop = cachedDesc["values"];
    if (!prop.isNull())
        subscribeRoot.put("values", prop);
    prop = cachedDesc["app_id"];
    if (!prop.isNull())
        subscribeRoot.put("app_id", prop);

    subscribeRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES);

    // subscription for getSystemSettingValues
    if(changeFlag) {
        subscribeKey = SUBSCRIBE_STR_KEYVALUE(m_key, m_appId);
        PrefsFactory::instance()->postPrefChange(subscribeKey.c_str(), subscribeRoot, sender, caller);
    }

    // subscription for getSystemSettingDesc
    prop = cachedDesc[KEYSTR_DBTYPE];
    if (!prop.isNull())
        subscribeRoot.put(KEYSTR_DBTYPE, prop);
    prop = cachedDesc["volatile"];
    if (!prop.isNull())
        subscribeRoot.put("volatile", prop);
    prop = cachedDesc["ui"];
    if (!prop.isNull())
        subscribeRoot.put("ui", prop);
    prop = cachedDesc["valueCheck"];
    if (!prop.isNull())
        subscribeRoot.put("valueCheck", prop);

    subscribeRoot.remove("method");
    subscribeRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC);
    subscribeKey = SUBSCRIBE_STR_KEYDESC(m_key, m_appId);
    PrefsFactory::instance()->postPrefChange(subscribeKey.c_str(), subscribeRoot, sender, caller);
}
