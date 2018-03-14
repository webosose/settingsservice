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

//->Start of API documentation comment block
/**
@page com_webos_settingsservice com.webos.settingsservice

@brief Service component for Setting. Provides APIs to set/get/manage settings

@{
@}
*/
//->End of API documentation comment block

#include "Logging.h"
#include "PrefsDb8Get.h"
#include "PrefsKeyDescMap.h"
#include "SettingsRecord.h"
#include "SettingsService.h"

/* returned object must be released by caller */
pbnjson::JValue SettingsRecord::genDelQueryById(void) const
{
    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());

/*
luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/del '{"query":{"from":"com.webos.settings.desc.system:1", "where":[{"prop":"key","op":"=","val":"brightness"}]}}'
*/
    replyRootItem1.put("prop", "_id");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", m_id);
    replyRootWhereArray.put(0, replyRootItem1);

    replyRootQuery.put("from", m_kindName);
    replyRootQuery.put("where", replyRootWhereArray);

    replyRoot.put("query", replyRootQuery);
    replyRoot.put("purge", true);

    return replyRoot;
}

/* returned object must be released by caller */
pbnjson::JValue SettingsRecord::genQueryForDefKind(void) const
{
    std::string cur_country = PrefsKeyDescMap::instance()->getCountryCode();
    pbnjson::JValue jsonObjParam(pbnjson::Object());
    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootSelectArray(pbnjson::Array());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue countryArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());
    pbnjson::JValue replyRootItem2(pbnjson::Object());

    // Select property with requested keys
    for(const auto& itList : m_removedKeyList) {
        std::string selectItem("value.");
        selectItem += itList;
        replyRootSelectArray.append(selectItem);
    }
    replyRootSelectArray.append(KEYSTR_APPID);
    replyRootSelectArray.append(KEYSTR_CATEGORY);
    replyRootSelectArray.append(KEYSTR_KIND);
    replyRootSelectArray.append(KEYSTR_COUNTRY);
    replyRootSelectArray.append(KEYSTR_CONDITION);
    replyRootQuery.put("select", replyRootSelectArray);

    // add category for where
    replyRootItem1.put("prop", KEYSTR_CATEGORY);
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", m_categoryDim);

    // ad country for where
    countryArray.put(0, "none");
    countryArray.put(1, cur_country);
    replyRootItem2.put("prop", "country");
    replyRootItem2.put("op", "=");
    replyRootItem2.put("val", countryArray);


    // build where
    replyRootWhereArray.append(replyRootItem1);
    replyRootWhereArray.append(replyRootItem2);
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", SETTINGSSERVICE_KIND_DEFAULT);

    // add reply root
    replyRoot.put("query", replyRootQuery);

    // build return object
    jsonObjParam.put("method", "find");
    jsonObjParam.put("params", replyRoot);

    return jsonObjParam;
}

/* returned json object must be free by caller */
pbnjson::JValue SettingsRecord::genObjForPut(void) const
{
    pbnjson::JValue replyRootItem(pbnjson::Object());

    replyRootItem.put("category", m_categoryDim);
    replyRootItem.put("_kind", m_kindName);
    replyRootItem.put("value", m_valuesObj);
    replyRootItem.put("app_id", m_appId);
    if ( !m_country.empty() ) {
        replyRootItem.put("country", m_country);
    }

    return replyRootItem;
}

void SettingsRecord::clear(void)
{
    m_valuesObj = pbnjson::JValue();
    m_dimensionObj = pbnjson::JValue();

    m_id.clear();
    m_kindName.clear();
    m_appId.clear();
    m_country.clear();
    m_categoryDim.clear();
    m_dirty = false;
    m_isVolatile = false;
}

bool SettingsRecord::loadJsonObj(pbnjson::JValue obj)
{
    this->clear();

    if(obj.isNull()) {
        m_valuesObj = pbnjson::Object();
        return false;
    }

    m_valuesObj = obj["value"];
    if (m_valuesObj.isNull()) {
        SSERVICELOG_DEBUG("no properties for the request");
        m_valuesObj = pbnjson::Object();
        return false;
    }

    pbnjson::JValue label = obj["_id"];
    if (!label.isString()) {
        SSERVICELOG_DEBUG("Retrieved object has no _id property");
        return false;
    }
    m_id = label.asString();

    label = obj["_kind"];
    if (!label.isString()) {
        SSERVICELOG_DEBUG("Retrieved object has no _kind property");
        return false;
    }
    m_kindName = label.asString();

    label = obj["app_id"];
    m_appId = label.isString() ? label.asString() : std::string();

    label = obj["country"];
    m_country = label.isString() ? label.asString() : std::string();

    label = obj["category"];
    if (label.isString()) {
        m_categoryDim = label.asString();
        m_category = PrefsKeyDescMap::instance()->categoryDim2category(m_categoryDim);
        m_dimensionObj = PrefsKeyDescMap::instance()->categoryDim2dimObj(m_categoryDim);
    }

    return true;
}

std::set < std::string > SettingsRecord::removeAllKeys(void)
{
    std::set < std::string > removed_keys;

    if (m_isVolatile) {
        return m_removedKeyList;
    }
    if(m_valuesObj.isObject()) {
        for(pbnjson::JValue::KeyValue it : m_valuesObj.children()) {
            std::string key = it.first.asString();
            removed_keys.insert(key);
            m_removedKeyList.insert(key);     // store keys for subscription
        }

        m_dirty = true;
    }

    m_valuesObj = pbnjson::Object();

    return removed_keys;
}

std::set < std::string > SettingsRecord::removeKeys(const std::set < std::string >& keys)
{
    pbnjson::JValue label;
    std::set < std::string > removed_keys;
    std::set < std::string >::const_iterator it;

    if (m_isVolatile) {
        return keys;
    }

    if (keys.empty())
        return removed_keys; /* empty list */

    for (const std::string& it : keys) {
        label = m_valuesObj[it];
        if (!label.isNull()) {
            m_dirty = true;
            m_valuesObj.remove(it);
            m_removedKeyList.insert(it);     // store keys for subscription
            removed_keys.insert(it);
        }
    }

    return removed_keys;
}

bool SettingsRecord::isRemovedMixedType(void) const
{
    /* Mixed Type data should be Per App in Main(system) kind */
    if ( m_appId == GLOBAL_APP_ID )
        return false;

    std::string mixedTypeCategory;
    if ( !PrefsKeyDescMap::instance()->getCategoryDim(m_category, mixedTypeCategory) )
        return false;

    if ( mixedTypeCategory != m_categoryDim )
        return false;

    bool allMixed = true;
    bool foundMixed = false;

    for ( const std::string& k : m_removedKeyList ) {
        if ( PrefsKeyDescMap::instance()->getDbType(k.c_str()) == DBTYPE_MIXED )
            foundMixed = true;
        else
            allMixed = false;
    }

    if ( foundMixed && !allMixed ) {
        /* various type is found and one of them is mixed.
         * mixed type default data would be not loaded */
        SSERVICELOG_WARNING(MSGID_AMBIGUOUS_RECORD, 3,
                PMLOGKS("category", m_category.c_str()),
                PMLOGKS("dimension", m_dimensionObj.asString().c_str()),
                PMLOGKS("value", m_valuesObj.asString().c_str()),
                "Some per-app default is not loaded");
    }

    return allMixed;
}

void SettingsRecord::fixCategoryForMixedType(void)
{
    /* Notice : Assume that all key in this record has same categorydim */

    if (!m_removedKeyList.empty())
        PrefsKeyDescMap::instance()->getCategoryDim(*(m_removedKeyList.begin()), m_categoryDim, DimKeyValueMap());
}

int SettingsRecord::updateSuccessValueObj(pbnjson::JValue valueObj, std::string & errorText)
{
    if (valueObj.isObject()) {
        // add the result to array
        for (pbnjson::JValue::KeyValue a_item : valueObj.children()) {
            m_valuesObj.put(a_item.first, a_item.second);
        }
        return valueObj.objectSize();
    }
    // {"returnValue":true, "results":[{}]}
    errorText = "no items for the request with default appid";

    return 0;
}

bool SettingsRecord::dimMatched(pbnjson::JValue dimObj) const
{
    if ( !dimObj.isArray() || dimObj.arraySize() == 0 )
        return true;

    if ( dimObj.arraySize() != m_dimensionObj.arraySize() )
        return false;

    for(pbnjson::JValue::KeyValue it : dimObj.children()) {
        std::string key = it.first.asString();
        pbnjson::JValue value = it.second;
        pbnjson::JValue mine = m_dimensionObj[key];

        if (mine.isNull() )
            return false;
        if (!value.isString() || !mine.isString() )
            return false;
        if (value.asString() == "*")
            continue;
        if (value.asString() != mine.asString() )
            return false;
    }

    return true;
}

/**
@sa PrefsDb8Del::cbFindRequestToDefKind calls this
*/
int SettingsRecord::parsingResult(pbnjson::JValue resultArray, std::string & errorText, bool a_filterMixed)
{
    pbnjson::JValue mergedValue(PrefsDb8Get::mergeLayeredRecords(m_category, resultArray, m_appId, a_filterMixed));

    return (mergedValue.isError() ? 0 : updateSuccessValueObj(mergedValue, errorText));
}

bool SettingsRecord::loadVolatileKeys(const std::string &categoryDim, const std::map<std::string, std::string> &volatileKeyMap)
{
    m_isVolatile = true;
    m_dirty = false;

    m_valuesObj = pbnjson::Object();

    m_categoryDim = categoryDim;
    m_category = PrefsKeyDescMap::instance()->categoryDim2category(m_categoryDim);
    m_dimensionObj = PrefsKeyDescMap::instance()->categoryDim2dimObj(m_categoryDim);

    m_removedKeyList.clear();
    for (const std::pair<std::string, std::string>& it : volatileKeyMap) {
        m_removedKeyList.insert(it.first);
    }

    return true;
}
