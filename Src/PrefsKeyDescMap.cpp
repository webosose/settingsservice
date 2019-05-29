// Copyright (c) 2013-2019 LG Electronics, Inc.
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

/* Key-Description MAP build sequence

   initKeyDescMap(description_json)
    |
   sendCountryCodeRequest()
    |
   sendLoadKeyDescDefaultKindRequest
    |
   sendLoadKeyDescMainKindRequest
    |
   setKeyDescData
    |
 ( build key-category-dimension MAP )

*/

#include <cstdio>
#include <fstream>
#include <sstream>
#include <functional>
#include <iterator>

#include "Db8FindChainCall.h"
#include "Logging.h"
#include "PrefsDb8Condition.h"
#include "PrefsDb8Get.h"
#include "PrefsFactory.h"
#include "PrefsFileWriter.h"
#include "PrefsKeyDescMap.h"
#include "PrefsVolatileMap.h"
#include "Utils.h"

using namespace std;

#define DIMENSIONKEYTYPE_INDEPENDENT    1
#define DIMENSIONKEYTYPE_DEPENDENTD1    2
#define DIMENSIONKEYTYPE_DEPENDENTD2    3

#define NONE_COUNTRY_CODE ""
#define DEFAULT_COUNTRY_CODE "default"

#define RANFIRSTUSE_PATH "/var/luna/preferences/ran-firstuse"
#define SETTINGSSERVICE_EXCEPTION_APPLIST_PATH "/etc/palm/exceptionAppList.json"

#define GATHER_DEFAULT ((void*)0)
#define GATHER_COUNTRY ((void*)1)
#define GATHER_DEFAULT_JSON ((void*)2)
#define GATHER_COUNTRY_JSON ((void*)3)

static PrefsKeyDescMap *s_instance = 0;

PrefsKeyDescMap *PrefsKeyDescMap::instance()
{
    if (!s_instance) {
        s_instance = new PrefsKeyDescMap;
    }

    return s_instance;
}

PrefsKeyDescMap::PrefsKeyDescMap() :
    m_initFlag(false),
    m_doFirstFlag(true),
    m_serviceHandle(NULL),
    m_cntr_desc_populated(false),
    m_cntr_sett_populated(false),
    m_initByDimChange(false),
    m_finalize(NULL),
    m_conservativeButler(new ConservativeButler())
{
}

void PrefsKeyDescMap::initialize()
{
    using namespace std::placeholders;

    PrefsFactory::SubsCancelFunc func = std::bind(&PrefsKeyDescMap::cbSubscriptionCancel, _1, _2, this);
    PrefsFactory::instance()->registerSubscriptionCancel(func);
}


PrefsKeyDescMap::~PrefsKeyDescMap()
{
    //resetDescKindObj();
    //s_instance = 0;

    delete m_conservativeButler;
}

void PrefsKeyDescMap::getDimKeyList(int a_dimKeyType, std::set<std::string>& a_keyList) const
{
    a_keyList.clear();
    switch(a_dimKeyType) {
    case DIMENSIONKEYTYPE_INDEPENDENT :
        for(DimKeyMap::const_iterator citer = m_indepDimKeyMap.begin();
            citer != m_indepDimKeyMap.end();
            ++citer) {
            a_keyList.insert(citer->first);
        }
        break;
    case DIMENSIONKEYTYPE_DEPENDENTD1 :
        for(DimKeyMap::const_iterator citer = m_depDimKeyMapD1.begin();
            citer != m_depDimKeyMapD1.end();
            ++citer) {
            a_keyList.insert(citer->first);
        }
        break;
    case DIMENSIONKEYTYPE_DEPENDENTD2 :
        break;
    default :
        break;
    }
}

void tmpFileLog_PrintList(char* msg, const std::set<std::string> &keyList) {
    SSERVICELOG_TRACE("%s : [%s] [%d]", __FUNCTION__, msg, keyList.size());
    for(const std::string& it __attribute__((unused)): keyList) {
        SSERVICELOG_TRACE("[%s] %s", msg, it.c_str());
    }
}

void PrefsKeyDescMap::tmpFileLog(const char* fileName) {
    FILE *fp = NULL;
    KeyMap::iterator it;
    CategoryMap::iterator itCategory;
    std::set<std::string>::iterator itList;
    int i = 0;

    fp = fopen(fileName, "w");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "\n====default===============\n");
    for (it=m_fileDescCache.begin(); it!=m_fileDescCache.end(); ++it) {
        fprintf(fp, "[%s]  %s \n", it->first.c_str(), it->second.stringify().c_str());
    }

    for (auto& it : m_defaultDescCache) {
        fprintf(fp, "[%s:%s] %s \n", it.first.m_key.c_str(), it.first.m_appId.c_str(), it.second.stringify().c_str());
    }

    fprintf(fp, "\n====main==================\n");
    for (auto& it : m_systemDescCache) {
        fprintf(fp, "[%s:%s] %s \n", it.first.m_key.c_str(), it.first.m_appId.c_str(), it.second.stringify().c_str());
    }

    fprintf(fp, "\n====override==============\n");
    for (it=m_overrideDescCahce.begin(); it!=m_overrideDescCahce.end(); ++it) {
        fprintf(fp, "[%s] %s \n", it->first.c_str(), it->second.stringify().c_str());
    }

    fprintf(fp, "\n===category===============\n");
    for (itCategory = m_categoryMap.begin(); itCategory !=m_categoryMap.end(); ++itCategory) {
        fprintf(fp, "[%s] ", itCategory->first.c_str());

        fputc('\n', fp);
        for(itList = itCategory->second.begin(); itList != itCategory->second.end(); itList++) {
            i++;
            fprintf(fp, "(%d) %s ", i, itList->c_str());
        }
    }

    std::set<std::string> keyList;
    for (itCategory = m_categoryMap.begin(); itCategory !=m_categoryMap.end(); ++itCategory) {
        keyList = itCategory->second;
        if(!keyList.empty()) {
            for(itList = keyList.begin(); itList != keyList.end(); itList++) {
                if ( m_fileDescCache.find(*itList) != m_fileDescCache.end() ) continue;
                if ( m_defaultDescCache.hasKey(*itList)) continue;
                if ( m_systemDescCache.hasKey(*itList)) continue;
                if ( m_overrideDescCahce.find(*itList) != m_overrideDescCahce.end() ) continue;
                fprintf(fp, "(%s) has no description cache", (*itList).c_str());
            }
        }
    }

    {
        CategoryDimKeyListMap categoryDimKeyListMap;
        CategoryDimKeyListMap::iterator itCategoryDimKeyListMap;

        keyList.clear();
        keyList.insert("pictureMode");
        keyList.insert("backlight");
        keyList.insert("contrast");
        keyList.insert("sharpness");
        keyList.insert("color");
        keyList.insert("tint");
        keyList.insert("energySaving");

        keyList.insert("zipcode");
        keyList.insert("country");

        keyList.insert("aaaa");
        keyList.insert("bbbb");
        keyList.insert("cccc");

        pbnjson::JObject dimObj;

        dimObj.put("input", "aaaa");
        dimObj.put("_3dStatus", "bbbb");

        categoryDimKeyListMap = getCategoryKeyListMap("picture", dimObj, keyList);

        for(itCategoryDimKeyListMap = categoryDimKeyListMap.begin(); itCategoryDimKeyListMap != categoryDimKeyListMap.end(); itCategoryDimKeyListMap++) {
            fprintf(fp, "[%s]\n", itCategoryDimKeyListMap->first.c_str());
            for(itList = itCategoryDimKeyListMap->second.begin(); itList != itCategoryDimKeyListMap->second.end(); itList++) {
                fprintf(fp, "%s\n", itList->c_str());
            }
        }
    }

    fprintf(fp, "===============================\n");
    {
        pbnjson::JValue dimension;
        pbnjson::JObject dimObj;

        dimObj.put("input", "aaaa");
        dimObj.put("_3dStatus", "bbbb");

        dimension = getDimKeyValueObj("picture", dimObj);
        fprintf(fp, "%s\n", dimension.stringify().c_str());

        dimension = getDimKeyValueObj("sound", dimObj);
        fprintf(fp, "%s\n", dimension.stringify().c_str());

        dimension = getDimKeyValueObj("3d", dimObj);
        fprintf(fp, "%s\n", dimension.stringify().c_str());

        dimension = getDimKeyValueObj("picture", pbnjson::JValue());
        fprintf(fp, "%s\n", dimension.stringify().c_str());

        dimension = getDimKeyValueObj("sound", pbnjson::JValue());
        fprintf(fp, "%s\n", dimension.stringify().c_str());

        dimension = getDimKeyValueObj("3d", pbnjson::JValue());
        fprintf(fp, "%s\n", dimension.stringify().c_str());
    }

    fflush(fp);
    fclose(fp);
}

void PrefsKeyDescMap::tmpFileLogDimInfo(const char* fileName) {
    FILE *fp = NULL;
    DimKeyValueMap::iterator itDim;
    DimKeyMap::iterator it;
    CategoryMap::iterator itCategory;
    std::list<std::string>::iterator itSet;
    int i = 0;

    fp = fopen(fileName, "w");
    if(fp) {
        for (itDim=m_dimKeyValueMap.begin(); itDim!=m_dimKeyValueMap.end(); ++itDim) {
            fprintf(fp, "[%d] %s:%s\n", ++i, itDim->first.c_str(), itDim->second.c_str() );
        }
        fprintf(fp, "===============\n");
        i = 0;
        for (it=m_indepDimKeyMap.begin(); it!=m_indepDimKeyMap.end(); ++it) {
            fprintf(fp, "[%d] %s\n", ++i, it->first.c_str() );
            if(it->second.size()) {
                for(itSet = it->second.begin(); itSet != it->second.end(); itSet++) {
                    fprintf(fp, "%s\n", itSet->c_str() );
                }
            }
        }
        fprintf(fp, "===============\n");
        for (it=m_depDimKeyMapD1.begin(); it!=m_depDimKeyMapD1.end(); ++it) {
            fprintf(fp, "[%d] %s\n", ++i, it->first.c_str() );
            if(it->second.size()) {
                for(itSet = it->second.begin(); itSet != it->second.end(); itSet++) {
                    fprintf(fp, "%s\n", itSet->c_str() );
                }
            }
        }
        fprintf(fp, "===============\n");
        for (it=m_depDimKeyMapD2.begin(); it!=m_depDimKeyMapD2.end(); ++it) {
            fprintf(fp, "[%d] %s\n", ++i, it->first.c_str() );
            if(it->second.size()) {
                for(itSet = it->second.begin(); itSet != it->second.end(); itSet++) {
                    fprintf(fp, "%s\n", itSet->c_str() );
                }
            }
        }

        fflush(fp);
        fclose(fp);
    }
}

bool PrefsKeyDescMap::isNewKey(const std::string& key) const
{
    DefaultBson::Query query;
    query.addWhere("key", key);
    if (query.countEx(&m_fileDescDefaultBson) == 0)
        return false;

    if ( m_defaultDescCache.hasKey(key) == false )
        return false;

    if ( m_systemDescCache.hasKey(key) == false )
        return false;

    /* We assume that all keys in m_overrideDescCache must be in m_defaultDescCache */

    return true;
}

bool PrefsKeyDescMap::isSameCategory(const std::string& a_key, const std::string& a_category) const
{
    CategoryMap::const_iterator citer;
    bool result = false;

    // check category
    citer = m_categoryMap.find(a_category);
    if(citer != m_categoryMap.end()) {
        if (std::find(citer->second.begin(), citer->second.end(), a_key)!= citer->second.end()) {
            // it has same category
            result = true;
        }
    }

    return result;
}

bool PrefsKeyDescMap::isSameDimension(const std::string& a_key, pbnjson::JValue a_dimObj) const
{
    pbnjson::JValue desc_obj = genDescFromCache(a_key);

    if (desc_obj.isNull()) {
        return false;
    }

    // check dimension
    pbnjson::JValue keyArrayNew = a_dimObj.isArray() ? a_dimObj : pbnjson::Array();
    int keyArrayNewLen = keyArrayNew.arraySize();

    pbnjson::JValue label = desc_obj[KEYSTR_DIMENSION];
    pbnjson::JValue keyArrayOld = label.isArray() ? label : pbnjson::Array();
    int keyArrayOldLen = keyArrayOld.arraySize();

    // return false, if they have different length
    if(keyArrayNewLen != keyArrayOldLen) {
        return false;
    }
    // return true, if they are all empty dimension.
    else if(0 == keyArrayNewLen && 0 == keyArrayOldLen) {
        // same with no item;
        return true;
    }

    int sameCnt = 0;
    for (pbnjson::JValue dimKeyOldObj : keyArrayOld.items()) {
        if (dimKeyOldObj.isString()) {
            std::string dimKeyStr = dimKeyOldObj.asString();
            for (pbnjson::JValue dimKeyNewObj : keyArrayNew.items()) {
                if (dimKeyNewObj.isString()) {
                    if(dimKeyStr == dimKeyNewObj.asString()) {
                        sameCnt ++;
                    }
                }
                else {
                    return false;
                }
            }
        }
        else {
            return false;
        }
    }

    return sameCnt == keyArrayNewLen;
}

static pbnjson::JValue merge_desc_prop(const char *prop_name, pbnjson::JValue base, pbnjson::JValue override, bool merge_array = true);
static pbnjson::JValue merge_desc_arrayExt(pbnjson::JValue barr, pbnjson::JValue oarr)
{
    pbnjson::JArray target;

    /* All items in oarr(override array list) are copied into barr (base array list).
     * If there is a item wich has same value string, the item would be overwrited.
     * If there is no item having same value, new item would be created.
     * NOTICE: this function keep the sequece of base array list */

    for (pbnjson::JValue bitem : barr.items()) {
        bool item_matched = false;

        if ( !bitem.isObject() ) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ERR, 1,
                    PMLOGJSON("reason", bitem.isNull() ? "{}" : bitem.stringify().c_str()), "no ArrayExt");
            target.append(bitem.duplicate());
            continue; /* just copy if unexpected item */
        }

        pbnjson::JValue bvalue = bitem["value"];
        if (!bvalue.isString()) {
            if (bvalue.isNull()) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ERR, 1,
                        PMLOGJSON("reason", bitem.isNull() ? "{}" : bitem.stringify().c_str()), "no value");
            }
            target.append(bitem.duplicate());
            continue; /* just copy if unexpected item */
        }

        for (pbnjson::JValue oitem : oarr.items()) {
            if (!oitem.isObject()) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ERR, 1,
                        PMLOGJSON("reason", oitem.isNull() ? "{}" : oitem.stringify().c_str()), "no ArrayExt in override");
                continue; /* override fail */
            }

            pbnjson::JValue ovalue = oitem["value"];
            if (!ovalue.isString()) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ERR, 1,
                        PMLOGJSON("reason", ovalue.isNull() ? "{}" : ovalue.stringify().c_str()), "incorrect value in override");
                continue; /* override fail */
            }

            if ( ovalue.asString() == bvalue.asString() ) {
                target.append(merge_desc_prop("noname", bitem, oitem));
                item_matched = true;
                break;
            }
        }

        /* discard unmatched override item. if it is inserted, the sequece is break */
        if ( item_matched == false )
            target.append(bitem.duplicate());

        item_matched = false;
    }

    return target;
}

static pbnjson::JValue merge_desc_prop(const char *prop_name, pbnjson::JValue base, pbnjson::JValue override, bool merge_array)
{
    pbnjson::JValue target;

    if ( override.isNull() || base.isNull()) {
        SSERVICELOG_WARNING(MSGID_KEYDESC_CACHE_ERR, 3, PMLOGKS("prop", prop_name),
                PMLOGKS("base", base.isNull() ? "" : base.stringify().c_str()),
                PMLOGKS("over", override.isNull() ? "" : override.stringify().c_str()),
                " ");
        return pbnjson::JValue();
    }

    /* We can reference base and override (cached json object), but we never modify them.
     * When modification is needed, create new object, copy them, update new one. */

    if ( base == override ) {
        return override.duplicate();
    }

    if ( base.getType() != override.getType() ) {
        return override.duplicate();
    }

    if (base.isObject()) {
        /* create new object. recursive call might modify cached json object */
        target = pbnjson::Object();
        for(pbnjson::JValue::KeyValue it : base.children()) {
            std::string keyStr(it.first.asString());
            pbnjson::JValue source = override[it.first.asString()];
            if (!source.isNull())
                target.put(keyStr, merge_desc_prop(keyStr.c_str(), it.second, source, merge_array));
            else
                target.put(keyStr, it.second.duplicate());
        }
    } else if (base.isArray() && std::string(prop_name) == KEYSTR_ARRAYEXT && merge_array) {
        /* In the case of arrayExt,
         * we can identify relative item as comparing 'value' string.
         * In the case of other array type object,
         * just overwrite at last 'else' clause */
        target = merge_desc_arrayExt(base, override);
    } else {
        target = override.duplicate();
    }

    return target;
}

std::string PrefsKeyDescMap::getOverrideKey(const std::string &key) const
{
    std::set<std::string> target_keys;
    CategoryDimKeyListMap categories;
    std::string o_key;

    /* Search in only default kind.
     * Because override data never affect on main data. */
    pbnjson::JValue default_desc = queryDescriptionByKey(key, NONE_COUNTRY_CODE, m_fileDescDefaultBson);

    if (default_desc.isNull()) {
        SSERVICELOG_WARNING(MSGID_KEYDESC_CATEGORY_ERR, 2, PMLOGKS("key", key.c_str()), PMLOGKS("reason", "unknown key"), " ");
        return "unknown"; /* This causes no data from db8 query */
    }

    pbnjson::JValue c_obj = default_desc["category"];
    if (!c_obj.isString()) {
        SSERVICELOG_WARNING(MSGID_KEYDESC_CATEGORY_ERR, 2, PMLOGKS("key", key.c_str()), PMLOGKS("reason", "no category"), " ");
        return "unknown"; /* This causes no data from db8 query */
    }

    std::string category_str = c_obj.asString();

    target_keys.insert(key);

    categories = PrefsKeyDescMap::instance()->getCategoryKeyListMap(category_str, pbnjson::JValue(), target_keys);

    if ( categories.size() != 1 ) {
        SSERVICELOG_WARNING(MSGID_KEYDESC_CATEGORY_ERR, 1, PMLOGKS("key", key.c_str()), " ");
        o_key = key + "@" + category_str;
    } else {
        o_key = key + "@" + categories.begin()->first;
    }

    return o_key;
}

static bool exist_arrayExt(pbnjson::JValue array_obj, const std::string& val)
{
    if (!array_obj.isArray())
        return false;

    for (pbnjson::JValue aobj : array_obj.items()) {
        if (!aobj.isObject())
            continue;
        pbnjson::JValue value_obj = aobj[KEYSTR_VALUE];
        if (!value_obj.isString())
            continue;
        if (value_obj.asString() == val)
            return true;
    }

    return false;
}

static void add_desc_arrayExt(pbnjson::JValue target, pbnjson::JValue source)
{
    if ( !target.isArray() || !source.isArray())
        return;

    for (pbnjson::JValue sobj : source.items()) {
        if (!sobj.isObject())
            continue;
        pbnjson::JValue value_obj = sobj[KEYSTR_VALUE];
        if ( !value_obj.isString())
            continue;
        if (!exist_arrayExt(target, value_obj.asString())) {
            target.append(sobj);
        }
    }
}

static pbnjson::JValue add_desc_prop(pbnjson::JValue target, pbnjson::JValue source)
{
    if ( !target.isObject() || !source.isObject())
        return target;

    for (pbnjson::JValue::KeyValue it : source.children()) {
        std::string propname(it.first.asString());
        pbnjson::JValue propobj(it.second);
        pbnjson::JValue t = target[propname];
        if (t.isNull()) {
            target.put(propname, propobj.duplicate());
        } else {
            if (propobj.isObject()) {
                /* object t will be released while json_object_obect_add is executing */
                target.put(propname, add_desc_prop(t, propobj).duplicate());
            } else if (propobj.isArray()) {
                if (propname == KEYSTR_ARRAYEXT) {
                    add_desc_arrayExt(t, propobj);
                } else {
                    SSERVICELOG_DEBUG("array copy function is not supported");
                }
            }
        }
    }

    return target;
}

/* Returned object should be released by caller */
pbnjson::JValue PrefsKeyDescMap::genDescFromCache(const std::string &key, const std::string &appId) const
{
    pbnjson::JValue desc_def;
    pbnjson::JValue desc_main;
    pbnjson::JValue desc_override;
    pbnjson::JValue desc_base;
    pbnjson::JValue desc_target;
    pbnjson::JValue overwrite_obj;
    KeyMap::const_iterator citer;
    bool perAppDefaultFound = false;

    pbnjson::JValue desc_file = queryDescriptionByKey(key, NONE_COUNTRY_CODE, m_fileDescDefaultBson, appId);

    if (desc_file.isObject())
    {
        auto aobj = desc_file[KEYSTR_APPID];
        if (aobj.isString()) {
            std::string astr = aobj.asString();
            if ( astr != GLOBAL_APP_ID && astr != DEFAULT_APP_ID ) {
                perAppDefaultFound = true;
            }
        }
    }

    {
        auto it = m_defaultDescCache.find({key, appId});        // per-app
        if (it == m_defaultDescCache.end()) {
            it = m_defaultDescCache.find({key, GLOBAL_APP_ID}); // global
        } else {
            perAppDefaultFound = true;
        }
        if (it != m_defaultDescCache.end()) {
            desc_def = it->second;
        }
    }

    {
        auto it = m_systemDescCache.find({key, appId});        // per-app
        if (it == m_systemDescCache.end()) {
            /* Do not apply global description data in system layer if
             * app specific default description exist.
             * Global description data is used if only there is NO
             * per-app description data */
            if ( perAppDefaultFound == false )
                it = m_systemDescCache.find({key, GLOBAL_APP_ID}); // global
        }
        if (it != m_systemDescCache.end()) {
            desc_main = it->second;
        }
    }

    desc_override = queryDescriptionByKey(getOverrideKey(key), NONE_COUNTRY_CODE, m_overrideDescDefaultBson);

    if (desc_main.isNull() && desc_def.isNull() && desc_file.isNull())
        return pbnjson::JValue();

    if (desc_file.isNull())
    {
        desc_base = (desc_def.isNull()) ? desc_main : desc_def;
    }
    else
    {
        desc_base = desc_file;
    }

    desc_target = pbnjson::Object();

    /* Create new json object and merge other layer's data (other kind's data). */

    SSERVICELOG_DEBUG("Cache objects for %s: %s %s %s %s",
            key.c_str(),
            (desc_main.isNull()) ? "" : "Main",
            (desc_override.isNull()) ? "" : "Override",
            (desc_def.isNull()) ? "" : "Default",
            (desc_file.isNull()) ? "" : "File"
            );

    std::lock_guard<std::mutex> lock(m_lock_desc_json);

    SSERVICELOG_DEBUG("desc_base:%s", desc_base.stringify().c_str());

    for (pbnjson::JValue::KeyValue it : desc_base.children()) {
        std::string keyStr(it.first.asString());
        pbnjson::JValue valObj(it.second);
        pbnjson::JValue mergedObj;
        pbnjson::JValue newObj;

        if (keyStr == "key") {
            /* key property cannot be changed */
            desc_target.put(keyStr, valObj.duplicate());
            continue;
        }

        mergedObj = valObj.duplicate();

        /* TODO: Do we need to start from file? */
        overwrite_obj = desc_file.isNull() ? pbnjson::JValue() : desc_file[keyStr];
        if (!overwrite_obj.isNull()) {
            newObj = merge_desc_prop(keyStr.c_str(), mergedObj, overwrite_obj);
            mergedObj = newObj;
        }

        overwrite_obj = desc_def.isNull() ? pbnjson::JValue() : desc_def[keyStr];
        if (!overwrite_obj.isNull()) {
            newObj = merge_desc_prop(keyStr.c_str(), mergedObj, overwrite_obj);
            mergedObj = newObj;
        }

        overwrite_obj = desc_override.isNull() ? pbnjson::JValue() : desc_override[keyStr];
        if (!overwrite_obj.isNull()) {
            newObj = merge_desc_prop(keyStr.c_str(), mergedObj, overwrite_obj);
            mergedObj = newObj;
        }

        overwrite_obj = desc_main.isNull() ? pbnjson::JValue() : desc_main[keyStr];
        if (!overwrite_obj.isNull()) {
            newObj = merge_desc_prop(keyStr.c_str(), mergedObj, overwrite_obj);
            mergedObj = newObj;
        }

        desc_target.put(keyStr, mergedObj);
    }

    /* add properties in desc_main, if the properties are not in desc_def */
    if ( !desc_main.isNull() && (desc_base == desc_def || desc_base == desc_file) ) {
        desc_target = add_desc_prop(desc_target, desc_main);
    }

    SSERVICELOG_DEBUG("Merged description : %s", desc_target.stringify().c_str());

    return desc_target;
}

bool PrefsKeyDescMap::isInDimKeyList(const std::set<std::string>& keyList) const
{
    std::set<std::string>  dimKeyListDep;
    std::set<std::string>  dimKeyListIndepD1;
    bool result = false;

    getDimKeyList(DIMENSIONKEYTYPE_INDEPENDENT, dimKeyListDep);
    getDimKeyList(DIMENSIONKEYTYPE_DEPENDENTD1, dimKeyListIndepD1);

    // check the given keylist if it includes the dimension key.
    for(const std::string& itKeyList : keyList) {
        if ((std::find(dimKeyListDep.begin(), dimKeyListDep.end(), itKeyList) != dimKeyListDep.end())
            || (std::find(dimKeyListIndepD1.begin(), dimKeyListIndepD1.end(), itKeyList) != dimKeyListIndepD1.end())) {
                result = true;
                break;
        }
    }

    return result;
}

bool PrefsKeyDescMap::hasCountryVar(const std::string &a_key) const
{
    return m_countryVarKeys.find(a_key) != m_countryVarKeys.end();
}

bool PrefsKeyDescMap::needStrictValueCheck(const std::string &a_key) const
{
    return m_strictValueCheckKeys.find(a_key) != m_strictValueCheckKeys.end();
}

// only if the keys used for dimension properties are changed
//           update dimension key values.
bool PrefsKeyDescMap::updateDimKeyValue(PrefsFinalize* a_finalize, const std::set<std::string>& keyList, pbnjson::JValue curDimObj)
{
    bool result = false;

    if(!isInit()) {
        return false;
    }

    if (std::find(keyList.begin(), keyList.end(), "country") != keyList.end() ||
             std::find(keyList.begin(), keyList.end(), "localeCountryGroup") != keyList.end()) {
        // If another handle is aleady set, let the object send result reply.
        //      loading cache is on going, so don't need to load again.
        if(m_finalize) {
            result = false;
        }
        else {
            a_finalize->reference();
            m_finalize = a_finalize;
            result = true;
        }
        initKeyDescMapByCountry();
    }
    else {
        if(isInDimKeyList(keyList) && isCurrentDimension(curDimObj)) {
            if(m_finalize) {
                result = false;
            }
            else {
                a_finalize->reference();
                m_finalize = a_finalize;
                result = true;
            }
            resetInitFlag();
            m_initByDimChange = true;
            setDimensionValues();
            result = true;
        }
    }

    return result;
}

bool PrefsKeyDescMap::addKeyDescForce(const std::string &key, pbnjson::JValue inItem, bool a_def)
{
    m_initFlag = true;
    bool result = addKeyDesc(key, inItem, a_def);
    m_initFlag = false;

    return result;
}

// update or insert key desc
bool PrefsKeyDescMap::addKeyDesc(const std::string &key, pbnjson::JValue inItem, bool a_def, const std::string& appId)
{
    pbnjson::JValue oldItem;
    pbnjson::JValue newItem;
    pbnjson::JValue categoryObj;
    std::string categoryOld, categoryNew;
    bool result = false;

    if(!m_initFlag) {
        SSERVICELOG_WARNING(MSGID_KEY_DESC_NOT_READY, 0 ,"in %s", __FUNCTION__);
        return false;
    }

    DescriptionCacheMap* target_cache =  a_def ? &m_defaultDescCache : &m_systemDescCache;

    DescriptionCacheMap::iterator it = target_cache->find({key, appId});

    std::lock_guard<std::mutex> lock(m_lock_desc_json);

    // for the exist key.
    if(it != target_cache->end()) {
        oldItem = it->second;
        newItem = inItem;
        // insert to category map
        categoryObj = newItem["category"];
        //      if category is given, to check whether it is same with old one.
        if (categoryObj.isString()) {
            categoryNew = categoryObj.asString();

            categoryObj = oldItem["category"];
            categoryOld = categoryObj.isString() ? categoryObj.asString() : "";

            // update category Map
            if(categoryNew != categoryOld) {
                removeKeyInCategoryMap(categoryOld, key);
                insertKeyToCategoryMap(categoryNew, key);

                oldItem.put("category", categoryNew);
            }
        }
        else {
            // do nothing. don't changed category
        }

        /* DB8 merge operation doesn't delete the properties already exist.
         * But, Array property is exception. It is overwrited */
        for(pbnjson::JValue::KeyValue it : newItem.children()) {
            std::string keyStr(it.first.asString());
            pbnjson::JValue val(it.second);
            pbnjson::JValue old_prop = oldItem[keyStr];

            /* If old_prop is NULL, merge_desc_prop returns json_object_get(val) */
            oldItem.put(keyStr, old_prop.isNull() ? val : merge_desc_prop(keyStr.c_str(), old_prop, val, false));
        }

        add_desc_prop(oldItem, newItem);

        result = true;
    }
    // for the new key
    else {
        std::pair< DescriptionCacheMap::iterator, bool> res;

        // insert to categoryMap
        categoryObj = inItem["category"];
        categoryNew = categoryObj.isString() ? categoryObj.asString() : "";

        insertKeyToCategoryMap(categoryNew, key);

        // insert to keyMap
        newItem = inItem;

        res = target_cache->insert ( DescriptionCacheMap::value_type({key, appId}, newItem) );
        if (res.second == false)
        {
            target_cache->erase( {key, appId} );
            it = target_cache->begin();
            target_cache->insert (it, DescriptionCacheMap::value_type({key, appId}, newItem) );
        }

        result = true;
    }

    //tmpFileLog("/tmp/ss/inAddKeyDesc");

    return result;
}

bool PrefsKeyDescMap::delKeyDesc(const std::string &key, const std::string& appId) {
    pbnjson::JValue categoryObj;
    std::string category;

    if(!m_initFlag) {
        SSERVICELOG_WARNING(MSGID_KEY_DESC_NOT_READY, 0 ,"in %s", __FUNCTION__);
        return false;
    }

    auto it = m_systemDescCache.find({key, appId});
    if(it != m_systemDescCache.end()) {

        std::lock_guard<std::mutex> lock(m_lock_desc_json);

        // remove from category map
        categoryObj = it->second["category"];
        if (categoryObj.isString())
            category = categoryObj.asString();

        removeKeyInCategoryMap(category, key);

        m_systemDescCache.erase(it);

        return true;
    }

    return false;
}

//
// @brief A key will be reset in cache
//
// @param key A key will be reset in cache
//
bool PrefsKeyDescMap::resetKeyDesc(const std::string& key, const std::string& appId)
//
// 1. Find in Main Cache
// 2. Find in Default Cache
// 3. Both not, delete the category
//
{
    bool flagFind = false;

    if (!m_initFlag) {
        SSERVICELOG_WARNING(MSGID_KEY_DESC_NOT_READY, 0, "in %s", __FUNCTION__);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_lock_desc_json);
        DescriptionCacheMap::const_iterator it = m_systemDescCache.find({key, appId});
        if (it != m_systemDescCache.end()) {
            m_systemDescCache.erase(it);
            flagFind = true;
        }
    }
    if (flagFind)
        return true;

    flagFind = m_defaultDescCache.hasKey(key);

    if (flagFind == false) {
        std::string category;
        if (getCategory(key, category)) {
            removeKeyInCategoryMap(category, key);
        }
    }

    return false;
}

/**
@brief Insert or update desc obj into map.

First, make value into country-named-map and insert it into the map with key.
Second, consider the score of environment condition and update country-named-map if score is greater than before.

@param key key string
@param country country string
@param keyMap The map of key-name/country-named-map
@param newItemObj json_object from the source to insert into keyMap
*/
void PrefsKeyDescMap::insertOrUpdateDescKindObj(const std::string &key, const std::string& app_id, const std::string &country, DescInfoMap &keyMap, pbnjson::JValue newItemObj) const
{
    DescriptionCacheId cacheId {key, app_id};

    DescInfoMap::iterator itkeyMap = keyMap.find(cacheId);
    if (itkeyMap == keyMap.end()) {
        if (PrefsDb8Condition::instance()->scoreByCondition(newItemObj) == 0) {
            return;
        }

        CountryJsonMap countryMap;
        countryMap.insert(CountryJsonMap::value_type(country, newItemObj));
        keyMap.insert(DescInfoMap::value_type(cacheId, countryMap));
        return;
    }

    CountryJsonMap *pCountryMap = &(itkeyMap->second);
    CountryJsonMap::iterator foundCountryMap = pCountryMap->find(country);
    if (foundCountryMap == pCountryMap->end()) {
        pCountryMap->insert(CountryJsonMap::value_type(country, newItemObj));
        return;
    }

    pbnjson::JValue foundObj = foundCountryMap->second;

    int conditionScoreNewItemObj = PrefsDb8Condition::instance()->scoreByCondition(newItemObj);
    int conditionScoreFoundObj = PrefsDb8Condition::instance()->scoreByCondition(foundObj);
    if (conditionScoreNewItemObj > conditionScoreFoundObj) {
        // Reference count of json object is increased in buildDescriptionCache(),
        // so old pointer is not required to free.
        foundCountryMap->second = newItemObj;
    }
}

/**
 * Filter by 'PerApp Rule' with Application ID.
 *
 * @param  jDescItems     arraytype json_object instance as source
 * @param  appId          appId from request
 * @param  jFilteredItems [out] arraytype json_object instance as target.
 *                        included json_objects's reference count is increased.
 * @return                jFilteredItems also
 *
 */
pbnjson::JValue filterByPerAppRule(pbnjson::JValue jDescItems, const std::string& appId, /*out*/ pbnjson::JValue jFilteredItems)
{
    bool isRequiredToFindMixedDefault = true;
    std::list<pbnjson::JValue> dbtypeMixedGlobalItems;

    for (pbnjson::JValue jDescItem : jDescItems.items()) {
        if (jDescItem.isNull())
            continue;

        pbnjson::JValue jDescItemDbtype = jDescItem[KEYSTR_DBTYPE];
        if (!jDescItemDbtype.isString()) {
            // description.dbtype is not exist.
            // can it just be added?
            jFilteredItems.append(jDescItem);
            continue;
        }

        std::string itemDbtypeVal(jDescItemDbtype.asString());

        std::string itemAppIdVal(GLOBAL_APP_ID);
        pbnjson::JValue jDescItemAppId = jDescItem[KEYSTR_APPID];
        if (jDescItemAppId.isString()) {
            itemAppIdVal = jDescItemAppId.asString();
        }

        // Request for global description
        if (appId == GLOBAL_APP_ID) {
            if (DBTYPE_GLOBAL == itemDbtypeVal) {
                jFilteredItems.append(jDescItem);
                continue;
            }

            if (DBTYPE_MIXED == itemDbtypeVal || DBTYPE_EXCEPTION == itemDbtypeVal) {
                if (GLOBAL_APP_ID == itemAppIdVal) {
                    jFilteredItems.append(jDescItem);
                    continue;
                }
            }

            if (DBTYPE_PERSOURCE == itemDbtypeVal) {
                if (GLOBAL_APP_ID == itemAppIdVal) {
                    jFilteredItems.append(jDescItem);
                    continue;
                }
            }
            // Must be not reached!
        }

        // Request for per-app description
        if (appId.compare(GLOBAL_APP_ID) != 0) {
            if (DBTYPE_PERSOURCE == itemDbtypeVal) {
                if (GLOBAL_APP_ID == itemAppIdVal || appId == itemAppIdVal) {
                    jFilteredItems.append(jDescItem);
                    continue;
                }
            }

            //ToDo : Consider case that exceptional app has own description
            if ((DBTYPE_MIXED == itemDbtypeVal || DBTYPE_EXCEPTION == itemDbtypeVal) && GLOBAL_APP_ID == itemAppIdVal) {
                dbtypeMixedGlobalItems.push_back(jDescItem);
                continue;
            }

            if ((DBTYPE_MIXED == itemDbtypeVal || DBTYPE_EXCEPTION == itemDbtypeVal) && appId == itemAppIdVal) {
                isRequiredToFindMixedDefault = false;
                jFilteredItems.append(jDescItem);
                continue;
            }
            // Must be not reached!
        }
    }

    // If request is for PerApp and any exact match is not found, use global description.
    // dbtypeMixedGlobalItems contains global description of dbtype:m
    if (appId.compare(GLOBAL_APP_ID) != 0) {
        if (isRequiredToFindMixedDefault) {
            for (pbnjson::JValue jDescItem : dbtypeMixedGlobalItems) {
                jFilteredItems.append(jDescItem);
            }
        }
    }

    return jFilteredItems;
}

void PrefsKeyDescMap::parsingDescKindObj(pbnjson::JValue descKindObj, DescInfoMap& tmpDescInfoMap, const std::string& appId) const
{
    std::string key, country, app_id;

    pbnjson::JValue results = descKindObj["results"];
    if (!results.isArray() || results.arraySize() == 0) {
        return;
    }

    pbnjson::JArray filteredItems;

    pbnjson::JValue keyArray;
    if (appId == UNSPECIFIED_APP_ID) {
        keyArray = results;
    }
    else {
        filterByPerAppRule(results, appId, filteredItems); // results(descKindObj) is put on caller
        keyArray = filteredItems;
    }

    if (!keyArray.isArray()) {
        return;
    }

    for (pbnjson::JValue item : keyArray.items()) {

        if (item.isNull()) {
            continue;
        }

        // insert to key map
        pbnjson::JValue keyStrObj = item["key"];
        if (!keyStrObj.isString()) {
            continue;
        }

        key = keyStrObj.asString();

        pbnjson::JValue appIdObj = item[KEYSTR_APPID];
        if ( appIdObj.isString() ) {
            app_id = appIdObj.asString();
        }
        // else default is empty string

        // Do not increase the json_object count here.
        // Instead it will be increased ONLY IF it is included in the container.
        //
        pbnjson::JValue newItemObj = item;
        newItemObj.remove("_id");
        newItemObj.remove("_sync");
        newItemObj.remove("_rev");
        newItemObj.remove("_kind");

        // insert to category map
        pbnjson::JValue countryObj = newItemObj["country"];
        if (!countryObj.isString()) {
            country = "";
        }
        else {
            country = countryObj.asString();
            if (isCountryIncluded(country, getCountryCode()))
            {
                country = getCountryCode();
            }
        }

        insertOrUpdateDescKindObj(key, app_id, country, tmpDescInfoMap, newItemObj);
    }
}

void PrefsKeyDescMap::removeDB8Props(pbnjson::JValue root)
{
    if (!root.isObject())
        return;

    root.remove(KEYSTR_KIND);
    root.remove(KEYSTR_ID);
    root.remove(KEYSTR_REV);
    root.remove(KEYSTR_SYNC);
    for (pbnjson::JValue::KeyValue it : root.children()) {
        pbnjson::JValue val = it.second;
        if (val.isArray()) {
            for (pbnjson::JValue item_val : val.items()) {
                if (item_val.isNull())
                    continue;

                if (item_val.isObject())
                    PrefsKeyDescMap::removeDB8Props(item_val);
            }
        } else if (val.isObject()) {
            PrefsKeyDescMap::removeDB8Props(val);
        }
    }
}

void PrefsKeyDescMap::removeIdInArrayEx(pbnjson::JValue root)
{
//        "values":{"arrayExt":[{"_id":"27c5e","active":true,"value":"on","visible":true},{"_id":"27c5f","active":true,"value":"off","visible":true}]},"volatile":false,"vtype":"ArrayExt"}]

    pbnjson::JValue label = root[KEYSTR_VALUES];
    if (label.isNull()) {
        return;
    }

    pbnjson::JValue array = label[KEYSTR_ARRAYEXT];
    if (!array.isArray()) {
        return;
    }

    for (pbnjson::JValue item : array.items()) {
        if ( !item.isObject()) {
            g_warning("Incorrect ArradyExt description data");
            break;
        }
        item.remove(KEYSTR_ID);
    }
}

void PrefsKeyDescMap::buildDescriptionCache(DescriptionCacheMap &dst, const list<pbnjson::JValue> &src, const string &country) /*const*/
{
    DescInfoMap desc_info_map;
    for (auto &item : src) {
        parsingDescKindObj(item, desc_info_map);
    }

    for (auto &item : desc_info_map) {
        const DescriptionCacheId& cacheId = item.first;
        const CountryJsonMap &countryMap = item.second;

        // get default data
        auto desc = countryMap.find(country);
        if (desc == countryMap.end()) {
            SSERVICELOG_DEBUG("No default key desc for '%s', '%s'", cacheId.m_key.c_str(), cacheId.m_appId.c_str());
            continue;
        }
        pbnjson::JValue desc_obj = desc->second;

        // category
        pbnjson::JValue jCategory = desc_obj[KEYSTR_CATEGORY];
        if (!jCategory.isString()) {
            continue;
        }
        string category(jCategory.asString());

        // appId
        string appId = GLOBAL_APP_ID;
        pbnjson::JValue jAppId = desc_obj[KEYSTR_APPID];
        if (jAppId.isString()) {
            appId = jAppId.asString();
        }

        if ( m_cntr_desc_populated == false ) {
            /* We don't need to merge country variations
             * if they are populated already at the time of changing country.
             * And additionally, com.webos.settins.system kind has no country variations. */
            desc = countryMap.find(getCountryCode());
            if(desc != countryMap.end()) {
                pbnjson::JValue desc_cntr(desc->second);
                for (pbnjson::JValue::KeyValue it : desc_cntr.children()) {
                    desc_obj.put(it.first, it.second);
                }
            }
        }

        // insert to category map
        // don't care the key already exist. category never change.
        insertKeyToCategoryMap(category, cacheId.m_key);

        removeIdInArrayEx(desc_obj);

        // remove duplicated legacy data. use new one.
        auto keymap_idx = dst.find(cacheId);
        if (keymap_idx != dst.end()) {
            dst.erase(keymap_idx);
        }

        // insert to key map
        // Increase ref_count ONLY IF inserted into KEYMAP
        dst[cacheId] = desc_obj;
    }
}

void PrefsKeyDescMap::dumpDescriptionCache(KeyMap &key_map)
{
    KeyMap::iterator desc;

    SSERVICELOG_DEBUG("dump map - size - %zu ", key_map.size());
    for ( desc = key_map.begin(); desc != key_map.end(); desc++ ) {
        SSERVICELOG_DEBUG("key:%s, desc:%s", desc->first.c_str(), desc->second.stringify().c_str());
    }
    SSERVICELOG_DEBUG("dump map end --------------");
}

/**
 * Return a description item for the key
 */
pbnjson::JValue PrefsKeyDescMap::queryDescriptionByKey(
    const std::string &key,
    const std::string &country /* not used */,
    const DefaultBson &m_fileDescDefaultBson,
    const std::string &appId) const
{
    pbnjson::JValue retObj;

    DefaultBson::Query query;
    query.addWhere("key", key);

    pbnjson::JArray jResults;
    query.executeEx(&m_fileDescDefaultBson, jResults);

    DescInfoMap desc_info_map;

    pbnjson::JObject newObj;
    newObj.put("results", jResults);
    parsingDescKindObj(newObj, desc_info_map, appId);

    for(const DescInfoMap::value_type& descriptions : desc_info_map) {
        const DescriptionCacheId& cacheId = descriptions.first;
        const CountryJsonMap &countryMap = descriptions.second;

        CountryJsonMap::const_iterator desc = countryMap.find(getCountryCode());
        if (desc == countryMap.end()) { // there no country-specific key descRec.
            /* get default data */
            desc = countryMap.find(NONE_COUNTRY_CODE);
        }
        if (desc == countryMap.end()) {
            SSERVICELOG_DEBUG("No default key desc for '%s', '%s'", cacheId.m_key.c_str(), cacheId.m_appId.c_str());
            continue;       // there no default key descRec. it's an error
        }

        retObj = desc->second;
        break;
    }

    return retObj;
}

bool buildDescriptionCacheBson(DefaultBson &dst, const char *filepath)
{
    DefaultBson::IndexerMap indexerMap;
    indexerMap.insert(DefaultBson::IndexerMap::value_type("key", new DefaultBson::Indexer("key")));

    return dst.loadWithIndexerMap(filepath, indexerMap);
}

void buildCategoryKeysMapBson(CategoryMap &m_categoryMap, const char *filepath)
{
    DefaultBson bsonCategoryKeys;
    DefaultBson::IndexerMap indexerMap;
    bsonCategoryKeys.loadWithIndexerMap(filepath, indexerMap);

    pbnjson::JValue jResults(pbnjson::Array());
    DefaultBson::Query query;
    query.executeEx(&bsonCategoryKeys, jResults);
    if (jResults.isArray()) {
        for (pbnjson::JValue jItem : jResults.items()) {
            pbnjson::JValue jCategory = jItem["category"];
            if (!jCategory.isString())
                continue;
            std::string category = jCategory.asString();

            std::set<std::string> keyList;
            pbnjson::JValue jKeys = jItem["keys"];
            if (!jKeys.isArray())
                continue;
            for (pbnjson::JValue jKeysItem : jKeys.items()) {
                if (jKeysItem.isString()) {
                    std::string keyName = jKeysItem.asString();
                    keyList.insert(keyName);
                }
            }

            m_categoryMap.insert(CategoryMap::value_type(category, keyList));
        }
    }
}

void PrefsKeyDescMap::setKeyDescData()
{
    bool isLoadDescDefault;

    {
        std::lock_guard<std::mutex> lock(m_lock_desc_json);

        m_fileDescCache.clear();
        m_defaultDescCache.clear();
        m_overrideDescCahce.clear();
        m_systemDescCache.clear();

        m_categoryMap.clear();

        isLoadDescDefault = buildDescriptionCacheBson(m_fileDescDefaultBson,     "/etc/palm/description.bson");
        m_fileDescDefaultBson.loadAppendDirectory(DEFAULT_LOADING_DIRECTORY, ".description.bson");
        m_fileDescDefaultBson.loadAppendDirectoryJson(DEFAULT_LOADING_DIRECTORY, ".description.json");
        buildDescriptionCacheBson(m_overrideDescDefaultBson, "/etc/palm/override.bson");
        buildCategoryKeysMapBson(m_categoryMap,              "/etc/palm/description.categorykeysmap.bson");
        buildDescriptionCache(m_defaultDescCache, m_descKindDefObj,  NONE_COUNTRY_CODE);
        buildDescriptionCache(m_systemDescCache,  m_descKindMainObj, NONE_COUNTRY_CODE);
    }

    if ( m_descKindMainObj.empty() && m_descKindDefObj.empty() && !isLoadDescDefault ) {
        // TODO: if no result from db. this function is not called.
        SSERVICELOG_WARNING(MSGID_KEY_DESC_EMPTY, 0, "ERROR!! no result from DB for key decription.");
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    } else {
        SSERVICELOG_DEBUG("File     cache size = %d", m_fileDescCache.size());
        SSERVICELOG_DEBUG("Default  cache size = %d", m_defaultDescCache.size());
        SSERVICELOG_DEBUG("Override cache size = %d", m_overrideDescCahce.size());
        SSERVICELOG_DEBUG("System   cache size = %d", m_systemDescCache.size());

        setDimensionFormat();
        setDimensionKeyFromDefault();      //parsing dimension keys from desc info.
        setDimensionKeyValueList();
        setDimensionValues();   // loading dimension values.
    }

    resetDescKindObj();

    return;
}

void PrefsKeyDescMap::updateKeyDescData()
{
    setDimensionFormat();
    setDimensionKeyFromDefault();      //parsing dimension keys from desc info.
    setDimensionKeyValueList();
    setDimensionValues();   // loading dimension values.

    return;
}

// for the x.x.x type dimension
pbnjson::JValue PrefsKeyDescMap::getEmptyDimObj(const std::string &category) const
{
    pbnjson::JObject replyRoot;

    DimFormatMap::const_iterator itDimFormat = m_dimFormatMap.find(category);
    if(itDimFormat != m_dimFormatMap.end()) {
        for(const std::string& citerList : itDimFormat->second)
        {
            replyRoot.put(citerList, "x");
        }
    }

    return replyRoot;
}

/**
 * Get dimension list from the cache or m_fileDescDefaultBson.
 * If 'dimension' property is not in description item or 'dimension' property has an empty array,
 * 0 length list is saved into cache and returned.
 *
 * @param a_key key name for description
 * @param out   a std::vector<std::string> to be items written.
 * @return      true if success
 */
bool PrefsKeyDescMap::getDimensionsByKey(const std::string &key, std::vector<std::string> *out) const
{
    KeyDimensionMap::const_iterator itKeyDim = m_keyDimensionMap.find(key);

    std::lock_guard<std::mutex> lock(m_lock_keyDimensionMap);

    // Cannot find in Cache
    if (itKeyDim == m_keyDimensionMap.end()) {
        pbnjson::JValue itemObj = queryDescriptionByKey(key, NONE_COUNTRY_CODE, m_fileDescDefaultBson);

        if (itemObj.isNull()) {
            return false;
        }

        std::vector<std::string> dimensionVector;
        pbnjson::JValue jDimension(itemObj["dimension"]);
        if (jDimension.isArray()) {
            for (pbnjson::JValue jDimensionItem : jDimension.items()) {
                if (jDimensionItem.isString()) {
                    std::string dimensionItem = jDimensionItem.asString();
                    dimensionVector.push_back(dimensionItem);
                }
            }
        }

        // Insert into Cache
        itKeyDim = m_keyDimensionMap.insert(m_keyDimensionMap.begin(), KeyDimensionMap::value_type(key, dimensionVector));
    }

    // Cannot make a new cache
    if (itKeyDim == m_keyDimensionMap.end()) {
        return false;
    }

    *out = itKeyDim->second;
    return true;
}

/**
 * Find all dimensions which has dependency
 *
 * @param  a_dims     Output variable. add find all dimensions which has dependency.
 * @return            return false if any error occur
 */
std::set<std::string> PrefsKeyDescMap::findDependentDimensions(const std::string& a_key) const
{
    std::set<std::string> dims;

    std::vector<std::string> dimsOfKey;
    unsigned int dimSize = 0;

    getDimensionsByKey(a_key, &dimsOfKey);

    for ( auto key : dimsOfKey )
        dims.insert(key);

    do {
        /* while additional dependent dimension is not updated any more */

        dimSize = dims.size();

        for ( const std::string& d : dims ) {
            dimsOfKey.clear();
            getDimensionsByKey(d, &dimsOfKey);

            for ( auto key : dimsOfKey )
                dims.insert(key);
        }

    } while ( dimSize != dims.size() );

    return dims;
}


/**
 * build dimension and its value in json_object.
 *
 * @param  a_category category
 * @param  a_dimObj   suppress dimension
 * @param  a_key      key name. if a_key is empty string, dimension will be filled based on category.
 * @param  a_input    NULL for new. base for output.
 * @return            json_object contains dimension or NULL if fail. If a_input is NULL, this must be free with json_object_put().
 */
pbnjson::JValue PrefsKeyDescMap::getDimKeyValueObj(const std::string &a_category, pbnjson::JValue a_dimObj, const std::string &a_key, pbnjson::JValue a_input)
{
    pbnjson::JValue replyRoot = a_input.isNull() ? pbnjson::Object() : a_input;

    // create dimensionValue Map given by user.
    DimKeyValueMap reqDimKeyValueMap;
    if (a_dimObj.isObject()) {
        if (a_dimObj.objectSize() > 0) {
            for (pbnjson::JValue::KeyValue it : a_dimObj.children()) {
                reqDimKeyValueMap.insert(DimKeyValueMap::value_type(it.first.asString(), it.second.asString()));
            }
        }
        else {
            SSERVICELOG_DEBUG("Empty Dimension");
            return replyRoot;
        }
    }

    std::lock_guard<std::mutex> lock(m_lock_desc_json);

    bool result = false;

    // create dimension object with category and dimension
    if (a_key.empty()) {
        DimFormatMap::iterator itDimFormat = m_dimFormatMap.find(a_category);
        if (itDimFormat != m_dimFormatMap.end()) {
            const std::list<std::string> &dimFormatList = itDimFormat->second;

            for (const std::string& itList : dimFormatList) {
                // if dimension is given, only use it.
                if(!reqDimKeyValueMap.empty()) {
                    DimKeyValueMap::const_iterator itKeyValueMap = reqDimKeyValueMap.find(itList);
                    if (itKeyValueMap != reqDimKeyValueMap.end()) {
                        replyRoot.put(itList, itKeyValueMap->second);
                        result = true;
                    }
                }
                else {
                    DimKeyValueMap::const_iterator itKeyValueMap = m_dimKeyValueMap.find(itList);
                    if (itKeyValueMap != m_dimKeyValueMap.end()) {
                        replyRoot.put(itList, itKeyValueMap->second);
                        result = true;
                    }
                }
            }
        }
    }
    // create dimension obj with key
    /* TODO: avoid changing dimension information */
    else {
        std::vector<std::string> dimensions;
        if (getDimensionsByKey(a_key, &dimensions)) {
            // for the key that has no dimension ( category$x.x.x. )
            if (dimensions.size() == 0) {
                result = true;
            }

            // for the key that has one or more dimension
            for (const std::string &dimKey : dimensions) {
                // if dimension is given, only use it.
                if (!reqDimKeyValueMap.empty()) {
                    DimKeyValueMap::const_iterator itKeyValueMap = reqDimKeyValueMap.find(dimKey);
                    if(itKeyValueMap != reqDimKeyValueMap.end()) {
                        replyRoot.put(dimKey, itKeyValueMap->second);
                        result = true;
                    }
                }
                else {
                    DimKeyValueMap::const_iterator itKeyValueMap = m_dimKeyValueMap.find(dimKey);
                    if(itKeyValueMap != m_dimKeyValueMap.end()) {
                        replyRoot.put(dimKey, itKeyValueMap->second);
                        result = true;
                    }
                }
            }
        }
    }

    if (result) {
        SSERVICELOG_DEBUG("result string:%s",replyRoot.stringify().c_str());
        return replyRoot;
    }

    return pbnjson::JValue();
}

pbnjson::JValue PrefsKeyDescMap::getKeyDesc(const std::string &category, const std::set<std::string>& keyList, const std::string& appId) const
{
    if(!m_initFlag) {
        SSERVICELOG_WARNING(MSGID_KEY_DESC_NOT_READY, 0 ,"in %s", __FUNCTION__);
        return pbnjson::JValue();
    }

    pbnjson::JObject replyRoot;
    pbnjson::JArray replyRootResultArray;
    int cnt = 0;

    CategoryMap::const_iterator itCategory = m_categoryMap.find(category);
    if(itCategory != m_categoryMap.end()) {
        for(const std::string& itCategoryKeyList : itCategory->second) {
            if ( keyList.empty() ||
                    (std::find(keyList.begin(), keyList.end(), itCategoryKeyList) != keyList.end()) )
            {
                pbnjson::JValue desc_obj = genDescFromCache(itCategoryKeyList, appId);
                if (!desc_obj.isNull()) {
                    replyRootResultArray.append(desc_obj);
                    cnt++;
                }
            }

            /* FIXME: If we cannot find all keys */
        }
    }

    replyRoot.put("returnValue", true);
    replyRoot.put("results", replyRootResultArray);
    replyRoot.put("count", cnt);

    return replyRoot;
}

pbnjson::JValue PrefsKeyDescMap::getKeyDescByCategory(const std::string &category) const
{
    if(!m_initFlag) {
        SSERVICELOG_WARNING(MSGID_KEY_DESC_NOT_READY, 0 ,"in %s", __FUNCTION__);
        return pbnjson::JValue();
    }
    pbnjson::JObject replyRoot;
    pbnjson::JArray replyRootResultArray;
    int cnt = 0;

    CategoryMap::const_iterator itCategory = m_categoryMap.find(category);
    if(itCategory != m_categoryMap.end()) {
        for(const std::string& it : itCategory->second) {
            pbnjson::JValue desc_obj = genDescFromCache(it);
            if (!desc_obj.isNull()) {
                replyRootResultArray.append(desc_obj);
                cnt++;
            }
        }
    }

    replyRoot.put("returnValue", true);
    replyRoot.put("results", replyRootResultArray);
    replyRoot.put("count", cnt);

    return replyRoot;
}

std::set<std::string> PrefsKeyDescMap::getKeysInCategory(const std::string &category) const
{
    CategoryMap::const_iterator itCategory = m_categoryMap.find(category);
    return itCategory != m_categoryMap.end() ? itCategory->second : std::set<std::string>();
}

bool PrefsKeyDescMap::existPerAppDescription(const string& a_category, const string& a_appId, const string a_key) const
{
    pbnjson::JValue jDescriptionsResult = getKeyDesc(a_category, { a_key }, a_appId);
    if (jDescriptionsResult.isNull())
        return false;

    pbnjson::JValue jResult = jDescriptionsResult["results"];
    if (jResult.isNull()) {
        return false;
    }

    for (pbnjson::JValue jDescription : jResult.items()) {
        pbnjson::JValue jAppId = jDescription[KEYSTR_APPID];
        if (!jAppId.isString())
            continue;

        std::string appIdVal = jAppId.asString();

        if ( a_appId == appIdVal) {
            return true;
        }
    }

    return false;
}

/**
 * Split keys into dbtype:global and dbtype:perapp.
 * keys into dbtype:perapp checked both dbtype:DBTYPE_MIXED and ApplicationId
 *
 * @param allKeys    keys from request
 * @param category   category from request
 * @param appId      appId from request
 * @param globalKeys will contains global keys
 * @param perAppKeys will contains perapp keys
 */
void PrefsKeyDescMap::splitKeysIntoGlobalOrPerAppByDescription( const std::set<std::string>& allKeys,
        const std::string& category, const std::string& appId, std::set<std::string>& globalKeys, std::set<std::string>& perAppKeys) const
{
    std::set<std::string> targetKeys = allKeys;

    if ( targetKeys.empty() ) {
        targetKeys = getKeysInCategory(category);
    }

    if ( appId == GLOBAL_APP_ID ) {
        globalKeys = targetKeys;
        return;
    }

    for ( const string& k : targetKeys ) {
        if ( getDbType(k) == DBTYPE_PERSOURCE ) {
            perAppKeys.insert(k);
            continue;
        }

        if ( getDbType(k) == DBTYPE_EXCEPTION && !isExceptionAppList(appId) ) {
            perAppKeys.insert(k);
            continue;
        }

        /* existPerAppDescription load description data, and it costs heavy resource.
         * run this check routine at last */
        if ( getDbType(k) == DBTYPE_MIXED && existPerAppDescription(category, appId, k) ) {
            perAppKeys.insert(k);
            continue;
        }

        /* Add into perapp group if description is not undefined and app_id is specified */
        std::string tryGetCategory;
        if ( !getCategory(k, tryGetCategory) ) {
            perAppKeys.insert(k);
            continue;
        }

        // Not Matched: fall into Global
        globalKeys.insert(k);
    }
}

string PrefsKeyDescMap::getDbType(const std::string& key) const
{
    if ( m_perAppKeys.count(key) == 1 ) {
        return DBTYPE_PERSOURCE;
    }

    if ( m_mixedPerAppKeys.count(key) == 1 ) {
        return DBTYPE_MIXED;
    }

    if ( m_exceptionAppKeys.count(key) == 1 ) {
        return DBTYPE_EXCEPTION;
    }

    /* Mixed and PerSource type should be predefined */

    return DBTYPE_GLOBAL;
}

bool PrefsKeyDescMap::isVolatileKey(const std::string key) const
{
    if(!m_initFlag) {
        /* m_initFlag is always false in this function,
         * because this function is called inside description map update.
         * Now, we assume that accessing the keymap data in this function
         * guarantee no error
         * TODO: external luna call should be inserted */
    }

    /* Precondition: volatile flag is not changable */

    if (m_volatileKeys.count(key) == 1) {
        return true;
    }

    pbnjson::JValue desc_obj;
    auto citer = m_defaultDescCache.find({key, GLOBAL_APP_ID});
    if (citer != m_defaultDescCache.end()) // if found
        desc_obj = citer->second;

    bool return_value = false;
    /* default is non-volatile */
    if (!desc_obj.isNull()){
        pbnjson::JValue volatile_object = desc_obj["volatile"];
        if (volatile_object.isBoolean()) {
            return_value = volatile_object.asBool();
        }
    }

    return return_value;
}

pbnjson::JValue PrefsKeyDescMap::getKeyDescByKeys(std::set<std::string>& keyList) const
{
    if(!m_initFlag) {
        SSERVICELOG_WARNING(MSGID_KEY_DESC_NOT_READY, 0 ,"in %s", __FUNCTION__);
        return pbnjson::JValue();
    }
    pbnjson::JObject replyRoot;
    pbnjson::JArray replyRootResultArray;
    int cnt = 0;

    for(const std::string& itList : keyList) {
        pbnjson::JValue desc_obj = genDescFromCache(itList);
        if(!desc_obj.isNull()) {
            replyRootResultArray.append(desc_obj);
            cnt++;
        }
    }

    replyRoot.put("returnValue", true);
    replyRoot.put("results", replyRootResultArray);
    replyRoot.put("count", cnt);

    return replyRoot;
}

bool PrefsKeyDescMap::getCategory(const std::string &key, std::string &category) const
{
    for (CategoryMap::const_iterator it = m_categoryMap.begin(); it != m_categoryMap.end(); ++it) {
        const std::string &categoryName = it->first;
        const std::set<std::string> &keys = it->second;
        if (keys.find(key) != keys.end()) {
            category = categoryName;
            return true;
        }
    }

    return false;
}

bool PrefsKeyDescMap::setDescKindObj(const DescKindType a_type, const std::list<pbnjson::JValue>* inKeyDescInfo)
{
    std::list<pbnjson::JValue> *target = nullptr;

    if ( inKeyDescInfo == nullptr )
        return false;

    switch (a_type) {
        case DescKindType_eDefault:
            target = &m_descKindDefObj;
            break;
        case DescKindType_eSysMain:
            target = &m_descKindMainObj;
            break;
        case DescKindType_eFile:
        case DescKindType_eOverride:
        default:
            return false;
    }

    /* m_descKindxxxObj is released after parsing desc info in setKkeyDescData */
    target->clear();
    *target = *inKeyDescInfo;

    return true;
}

void PrefsKeyDescMap::resetDescKindObj() {
    m_descKindFileObj.clear();

    m_descKindDefObj.clear();

    m_descKindMainObj.clear();

    m_descKindOverObj.clear();
}

/* Two function - populateCountryDesc and populateCountrySettings.
 * The functions loads both country-specific data and country-default data.
 * All country variation setting has country-default data (country property is 'default').
 * And then mergeCountryDesc and mergeCountrySettings overwrite all data founded.
 * This means that same settings (or same category) might be written twice.
 * First - default data, and Second - country specific data. */

bool PrefsKeyDescMap::populateCountrySettings(void)
{
    if (Utils::doesExistOnFilesystem(RANFIRSTUSE_PATH) == false) {
        return findModifiedCategory();
    }

    return createNullCategory();
}

bool PrefsKeyDescMap::gatherCountrySettingsJSON(const std::set<std::string>& a_categories, Callback a_func, void *a_type)
{
    if ( a_type != GATHER_DEFAULT_JSON && a_type != GATHER_COUNTRY_JSON )
        return false;

    pbnjson::JObject find_result;
    find_result.put("returnValue", true);
    find_result.put("results", pbnjson::Array());

    std::list<pbnjson::JValue> result_list;
    result_list.push_back(find_result);
    a_func(this, a_type, result_list);

    return true;
}

bool PrefsKeyDescMap::gatherCountrySettings(const std::set<std::string> &a_categories, Callback a_func, void *a_type)
{
    bool result;
    std::string country_string = (a_type == GATHER_DEFAULT) ? "default" : getCountryCode();
    pbnjson::JObject country_query;

    m_cntr_sett_populated = false;

    /* empty a_categories doesn't make any error */

    country_query.put("from", SETTINGSSERVICE_KIND_DEFAULT);
    pbnjson::JObject where_item1;
    where_item1.put("prop", "category");
    where_item1.put("op", "=");
    pbnjson::JArray value_array;

    for (const std::string& cat : a_categories)
    {
        value_array.append(cat);
    }
    where_item1.put("val", value_array);

    pbnjson::JObject where_item2;
    where_item2.put("prop", "app_id");
    where_item2.put("op", "=");
    where_item2.put("val", "");

    pbnjson::JObject where_item3;
    where_item3.put("prop", "country");
    where_item3.put("op", "%");
    where_item3.put("val", country_string);

    pbnjson::JArray where_array;
    where_array.append(where_item1);
    where_array.append(where_item2);
    where_array.append(where_item3);
    country_query.put("where", where_array);

    Db8FindChainCall *chainCall = new Db8FindChainCall;
    chainCall->ref();
    chainCall->Connect(a_func, this, a_type);
    result = Db8FindChainCall::sendRequest(chainCall, m_serviceHandle, country_query);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_DB_LUNA_CALL_FAIL, 0, "Fail to find country settings");
    }

    chainCall->unref();

    return result;
}

void PrefsKeyDescMap::readCountryDesc(std::list<pbnjson::JValue>& a_result) const
{
    DefaultBson::Query query;
    query.addWhere("country", DEFAULT_COUNTRY_CODE);
    query.addWhere("country", getCountryCode());

    pbnjson::JValue all_desc(pbnjson::Array());
    query.executeEx(&m_fileDescDefaultBson, all_desc);

    pbnjson::JObject rootObj;
    rootObj.put("returnValue", true);
    rootObj.put("results", all_desc);

    a_result.push_back(rootObj);

    return;
}

bool PrefsKeyDescMap::populateCountryDesc(void)
{
    bool result;

    m_cntr_desc_populated = false;

    std::list<pbnjson::JValue> resultList;

    readCountryDesc(resultList);
    result = cbFindCountryDesc((void *)this, NULL, resultList);
    resultList.clear();

    return result;
}

pbnjson::JValue PrefsKeyDescMap::getLocaleBatchItem(const std::string& kindName, const std::string& catName, const std::string& countryCode)
{
    pbnjson::JObject jRoot;
    pbnjson::JObject jQuery;
    pbnjson::JArray jWhereArr;

    jQuery.put("from", kindName);
    jRoot.put("query", jQuery);


    if ( kindName != SETTINGSSERVICE_KIND_MAIN ) {
        pbnjson::JObject tempJson;
        tempJson.put("prop", "country");
        pbnjson::JArray tempArr;
        if ( countryCode != "" ) {
            tempJson.put("op", "%");
            tempArr.append(getCountryCode());
            tempArr.append("default");
        } else {
            tempJson.put("op", "=");
            tempArr.append("none");
        }
        tempJson.put("val", tempArr);
        jWhereArr.append(tempJson);
    }
    pbnjson::JValue tempJson = pbnjson::Object();
    // = "{\"prop\":\"app_id\", \"op\":\"=\", \"val\":\"\"}";
    tempJson.put("prop", "app_id");
    tempJson.put("op", "=");
    tempJson.put("val", "");
    jWhereArr.append(tempJson);
    tempJson = pbnjson::Object();
    // "{\"prop\":\"category\", \"op\":\"=\", \"val\":\"" + catName + "\"}";
    tempJson.put("prop", "category");
    tempJson.put("op", "=");
    tempJson.put("val", catName);
    jWhereArr.append(tempJson);

    jQuery.put("where", jWhereArr);

    return jRoot;
}

/**
Find some key/value from db to write to file out.

ex) localeInfo, systemPin
*/
void PrefsKeyDescMap::sendUpdatePreferenceFiles(void)
{
    CatKeyContainer requestList;
    std::set<std::string> empty;

    PrefsFileWriter::instance()->invalidatePreferences();

    for(auto category : PrefsFileWriter::instance()->getCategories())
    {
        requestList.insert(CatKeyContainer::value_type( { category.c_str(), GLOBAL_APP_ID } , empty));
    }

    TaskRequestInfo* requestInfo = new TaskRequestInfo;
    requestInfo->requestList = requestList;
    requestInfo->requestDimObj = pbnjson::JValue();
    requestInfo->requestCount = requestList.size() + REQUEST_GETSYSTEMSETTINGS_REF_CNT;
    /*requestInfo->subscribeKeys = empty */
    requestInfo->cbFunc = reinterpret_cast<void *>(&PrefsKeyDescMap::cbUpdatePreferenceFiles);
    requestInfo->thiz_class = static_cast<void *>(const_cast<PrefsKeyDescMap*>(this));
    SSERVICELOG_TRACE("%s", __FUNCTION__);
    PrefsFactory::instance()->getTaskManager().pushUserMethod(METHODID_REQUEST_GETSYSTEMSETTIGNS_SINGLE, PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE), NULL, reinterpret_cast<void *>(requestInfo), TASK_PUSH_FRONT);
}

bool PrefsKeyDescMap::cbUpdatePreferenceFiles(void *a_thiz_class, void *a_userdata, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_result)
{
    /* a_appId is ignored, it is always GLOBAL_APP_ID */
    pbnjson::JValue nonVolatileKeyValueObj;

    PrefsKeyDescMap* thiz_class = static_cast<PrefsKeyDescMap *>(a_thiz_class);
    TaskRequestInfo* requestInfo = static_cast<TaskRequestInfo *>(a_userdata);
    bool completed = false;
    if (!thiz_class || !requestInfo)
    {
        completed = true;
        return completed;
    }

    nonVolatileKeyValueObj = PrefsKeyDescMap::instance()->FilterForVolatile(a_result);

   if (nonVolatileKeyValueObj.isObject()) {
       PrefsFileWriter::instance()->updateFilesIfTargetExistsInSettingsObj(a_category, nonVolatileKeyValueObj);
    }

    if (g_atomic_int_dec_and_test(&requestInfo->requestCount) == TRUE)
    {
        // FIXME - If this callback is not called as 'requestCount' specified, this codes will not be reached.
        //
        delete requestInfo;
        completed = true;
    }

    return completed;
}

pbnjson::JValue PrefsKeyDescMap::FilterForVolatile(pbnjson::JValue a_result)
{
    if (!a_result.isObject())
         return pbnjson::JValue();

    pbnjson::JObject replyRoot;
    for (pbnjson::JValue::KeyValue kv : a_result.children()) {
        if(!PrefsKeyDescMap::instance()->isVolatileKey(kv.first.asString())){
            replyRoot << kv;
        }
    }

    return replyRoot;
}

/* Notice that mergeCountrySettings might modify descArray. */
bool PrefsKeyDescMap::mergeCountrySettings(pbnjson::JValue descArray)
{
    if (!descArray.isArray())
        return false;

    LSError lsError;
    bool result = false;
    pbnjson::JValue batchArrayObj(pbnjson::Array());

    LSErrorInit(&lsError);

    /* Even array is empty, this function call batch method */

    for (pbnjson::JValue descObj : descArray.items()) {
        if (!descObj.isObject()) {
            SSERVICELOG_WARNING(MSGID_INCORRECT_OBJECT, 0 ,"Incorrect country settings obj in country specific");
            continue;
        }

        pbnjson::JValue categoryString = descObj["category"];
        if (!categoryString.isString()) {
            SSERVICELOG_WARNING(MSGID_INCORRECT_OBJECT, 0 ,"Incorrect settings category in country specific");
            continue;
        }

        pbnjson::JValue app_id = descObj["app_id"];
        if (!app_id.isString()) {
            SSERVICELOG_WARNING(MSGID_INCORRECT_OBJECT, 0 ,"Incorrect settings app_id in country specific");
            continue;
        }

        pbnjson::JValue valueObj = descObj["value"];
        if (!valueObj.isObject()) {
            SSERVICELOG_WARNING(MSGID_INCORRECT_OBJECT, 0 ,"Incorrect settings in country specific");
            continue;
        }

        descObj.remove("country");

        PrefsKeyDescMap::removeDB8Props(descObj);

        m_conservativeButler->recoverKeepedProperty(descObj);

        /* build merge params */
        pbnjson::JArray whereArray;
        pbnjson::JObject mergeQuery;
        pbnjson::JValue whereItem = pbnjson::Object();
        whereItem.put("prop", "volatile");
        whereItem.put("op", "=");
        whereItem.put("val", false);
        whereArray.append(whereItem);
        whereItem = pbnjson::Object();
        whereItem.put("prop", "app_id");
        whereItem.put("op", "=");
        whereItem.put("val", app_id);
        whereArray.append(whereItem);
        whereItem = pbnjson::Object();
        whereItem.put("prop", "category");
        whereItem.put("op", "=");
        whereItem.put("val", categoryString);
        whereArray.append(whereItem);
        mergeQuery.put("where", whereArray);

        // any volatile item will not be found.
        // this received json_obj is from com.webos.settings.system
        // but com.webos.settings.system.volatile contains no items.
        //
        // pick up volatile items,
        // remove volatile key from valueObj,
        // insert volatile key/value into PrefsVolatileMap
        for (pbnjson::JValue::KeyValue it : valueObj.children()) {
            std::string value_key(it.first.asString());
            pbnjson::JValue value_val(it.second);
            if (isVolatileKey(value_key)) {
                valueObj.remove(value_key);
                PrefsVolatileMap::instance()->setVolatileValue(
                    categoryString.asString(),
                    app_id.asString(),
                    value_key,
                    value_val.stringify());
            }
        }

        mergeQuery.put("from", SETTINGSSERVICE_KIND_MAIN);

        /* build merge params */
        pbnjson::JObject batchOpParams;
        batchOpParams.put("query", mergeQuery);
        batchOpParams.put("props", descObj);

        /* build batch params */
        pbnjson::JObject batchOp;
        batchOp.put("method", "merge");
        batchOp.put("params", batchOpParams);

        /* add batch operation */
        batchArrayObj.append(batchOp);
    }

    pbnjson::JObject jsonObjParam;
    jsonObjParam.put("operations", batchArrayObj);

    result = DB8_luna_call(m_serviceHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), PrefsKeyDescMap::cbMergeCountrySettings, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    return result;
}

/* Notice that mergeCountryDesc might modify descArray. */
bool PrefsKeyDescMap::mergeCountryDesc(pbnjson::JValue descArray)
{
    if (!descArray.isArray())
        return false;

    bool result = false;
    LSError lsError;
    pbnjson::JValue batchArray(pbnjson::Array());

    LSErrorInit(&lsError);

    m_batchParams.clear();

    /* Even array is empty, this function call batch method */

    for (pbnjson::JValue descObj : descArray.items()) {
        if (!descObj.isObject()) {
            SSERVICELOG_DEBUG("Incorrect country desc obj");
            continue;
        }

        pbnjson::JValue keyString = descObj["key"];
        if (!keyString.isString()) {
            SSERVICELOG_DEBUG("Incorrect description key");
            continue;
        }

        descObj.remove("country");

        PrefsKeyDescMap::removeDB8Props(descObj);

        /* collect batch parameter. parameters will be used in callback function and released */
        m_batchParams.push_back(descObj);

        /* build merge params */
        pbnjson::JArray whereArray;
        pbnjson::JObject whereItem;
        pbnjson::JObject mergeQuery;
        whereItem.put("prop", "key");
        whereItem.put("op", "=");
        whereItem.put("val", keyString);
        whereArray.append(whereItem);
        mergeQuery.put("where", whereArray);
        mergeQuery.put("from", SETTINGSSERVICE_KIND_MAIN_DESC);

        /* build merge params */
        pbnjson::JObject batchOpParams;
        batchOpParams.put("query", mergeQuery);
        batchOpParams.put("props", descObj);

        /* build batch params */
        pbnjson::JObject batchOp;
        batchOp.put("method", "merge");
        batchOp.put("params", batchOpParams);

        /* add batch operation */
        batchArray.append(batchOp);
    }

    pbnjson::JObject jsonObjParam;
    jsonObjParam.put("operations", batchArray);

    result = DB8_luna_call(m_serviceHandle, "luna://com.webos.service.db/batch", jsonObjParam.stringify().c_str(), PrefsKeyDescMap::cbMergeCountryDesc, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_BATCH_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    return result;
}

bool PrefsKeyDescMap::cbMergeCountrySettings(LSHandle * lsHandle, LSMessage * message, void *data)
{
    std::string errorText;
    PrefsKeyDescMap *replyInfo = NULL;
    bool successFlag = false;

    replyInfo = (PrefsKeyDescMap *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s - payload is %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PARSE_ERR, 0, "payload is : %s", payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean())
            successFlag = label.asBool();

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
            break;
        }

        replyInfo->sett_populated();

        /* Even there is no data merged or error is occured, we don't need to put data.
         * Because that means there is no data changed by setSystemSettings.
         * This means that there is no data to be merged */

        replyInfo->updateKeyDescData();
    } while(false);

    return true;
}

bool PrefsKeyDescMap::cbMergeCountryDesc(LSHandle * lsHandle, LSMessage * message, void *data)
{
    std::string errorText;
    PrefsKeyDescMap *replyInfo = NULL;
    bool successFlag = false;
    int batch_idx = 0;

    replyInfo = (PrefsKeyDescMap *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PARSE_ERR, 0, "payload is : %s", payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean())
            successFlag = label.asBool();

        if (!successFlag) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue responses_array = root["responses"];
        if (!responses_array.isArray()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "No responses array : %s", payload);
            break;
        }

        if (replyInfo->m_batchParams.size() != static_cast<size_t>(responses_array.arraySize())) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0,
                    "Incorrect array length from batch in population %zu, %d",
                    replyInfo->m_batchParams.size(), responses_array.arraySize());
            break;
        }

        for (pbnjson::JValue descObj : replyInfo->m_batchParams)
        {
            /* update description cache if merged */
            pbnjson::JValue merge_result = responses_array[batch_idx++];
            if (merge_result.isObject()) {
                pbnjson::JValue merge_count = merge_result["count"];
                if (merge_count.isNumber() && merge_count.asNumber<int>() > 0 ) {
                    pbnjson::JValue keyObj = descObj["key"];
                    if (keyObj.isString())
                        replyInfo->addKeyDescForce(keyObj.asString(), descObj, false);
                }
            }
        }
    } while (false);

    /* Even there is no data merged or error is occured, we don't need to put data.
     * Because that means there is no data changed by setSystemSettings.
     * This means that there is no data to be merged */

    replyInfo->populateCountrySettings();

    replyInfo->m_batchParams.clear();

    return true;
}

/**
 * Create "" category into SETTINGSSERVICE_KIND_MAIN kind.
 * So this prevent changing localeInfo by allowing successful findModifiedCategory.
 *
 * TODO: Refactoring with PrefsDb8Init::cbFindNullCategory,
 *       PrefsDb8Init::cbCreateNullCategory
 */
bool PrefsKeyDescMap::createNullCategory(void)
{
    bool result = false;
    LSError lsError;

    LSErrorInit(&lsError);

    pbnjson::JObject root;
    pbnjson::JObject props;
    pbnjson::JObject query;
    props.put("_kind", SETTINGSSERVICE_KIND_MAIN);
    props.put("value", pbnjson::Object());
    props.put("app_id", "");
    props.put("category", "");
    props.put("volatile", false);
    query.put("from", SETTINGSSERVICE_KIND_MAIN);
    pbnjson::JArray where_array;
    pbnjson::JObject where_item;
    where_item.put("prop", "category");
    where_item.put("op", "=");
    where_item.put("val", "");
    where_array.append(where_item);
    query.put("where", where_array);
    root.put("props", props);
    root.put("query", query);

    result = DB8_luna_call(m_serviceHandle, "luna://com.webos.service.db/mergePut",
        root.stringify().c_str(), cbCreateNullCategory, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_MERGE_FAIL, 2,
            PMLOGKS("Function", lsError.func),
            PMLOGKS("Error", lsError.message),
            "Send condition category");
        LSErrorFree(&lsError);
    }

    return result;
}

bool PrefsKeyDescMap::cbCreateNullCategory(LSHandle *lsHandle, LSMessage *message, void *data)
{
    PrefsKeyDescMap *thiz = (PrefsKeyDescMap *)data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_ERROR("MSGID_PAYLOAD_ERR", 1, PMLOGKS("at", __FUNCTION__), "");
            break;
        }

        // {"returnValue":true,"count":1}
        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_CB_PAYLOAD_PARSE_ERR, 2,
                PMLOGKS("at", __FUNCTION__),
                PMLOGKS("payload", payload), "");
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (!label.isBoolean() || !label.asBool()) {
            SSERVICELOG_WARNING(MSGID_SET_DB_RETURNS_FAIL, 2,
                PMLOGKS("at", __FUNCTION__),
                PMLOGKS("payload", payload), "");
            break;
        }
    } while(false);

    return thiz->findModifiedCategory();
}

bool PrefsKeyDescMap::findModifiedCategory(void)
{
    m_conservativeButler->clear();

    m_cntr_sett_populated = false;

    /* find modified category and keys
     * Only country default settings will be over-written */

    pbnjson::JObject country_query;
    pbnjson::JValue replyRootSelectArray(pbnjson::Array());
    country_query.put("from", SETTINGSSERVICE_KIND_MAIN);
    replyRootSelectArray.append(KEYSTR_APPID);
    replyRootSelectArray.append(KEYSTR_CATEGORY);
    replyRootSelectArray.append(KEYSTR_VALUE);
    replyRootSelectArray.append(KEYSTR_VOLATILE);
    country_query.put("select", replyRootSelectArray);

    Db8FindChainCall *chainCall = new Db8FindChainCall;
    chainCall->ref();
    chainCall->Connect(PrefsKeyDescMap::cbFindModifiedCategory, this, NULL);
    bool result = Db8FindChainCall::sendRequest(chainCall, m_serviceHandle, country_query);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_DB_LUNA_CALL_FAIL, 1, PMLOGKS("Payload", country_query.stringify().c_str()), "Fail to find country settings on findModifiedCategory");
    }

    chainCall->unref();

    return result;
}

void PrefsKeyDescMap::initCategoryKeysMapInSystemKind()
{
    m_categoryKeysMapInSystemKind.clear();

    // ''.localeInfo must be passed to merge step
    m_categoryKeysMapInSystemKind[""] = set<string> {"localeInfo"};
}

bool PrefsKeyDescMap::cbFindModifiedCategory(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results)
{
    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) a_thiz_class;
    std::set<std::string> modified_categories;
    replyInfo->initCategoryKeysMapInSystemKind();

    // Checks whether all returns are valid one.
    //
    for (pbnjson::JValue citer : a_results)
    {
        pbnjson::JValue label = citer["returnValue"];
        if (!label.isBoolean())
            continue;

        if (label.asBool() == false) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 1,
                    PMLOGJSON("payload", citer.stringify().c_str()), "Country query is failed");
            continue;
        }

        pbnjson::JValue category_results = citer["results"];
        if (!category_results.isArray())
            continue;

        for (pbnjson::JValue result_obj : category_results.items()) {
            if (!result_obj.isObject())
                continue;

            pbnjson::JValue category_obj = result_obj["category"];
            if (!category_obj.isString())
                continue;
            std::string categoryStr = category_obj.asString();
            modified_categories.insert(categoryStr);

            pbnjson::JValue value_obj = result_obj[KEYSTR_VALUE];
            set<string> keysInValue;
            json_object_object_keys(value_obj, keysInValue);
            if (replyInfo->m_categoryKeysMapInSystemKind.find(categoryStr) == replyInfo->m_categoryKeysMapInSystemKind.end()) {
                if (keysInValue.size() > 0) {
                    replyInfo->m_categoryKeysMapInSystemKind[categoryStr] = keysInValue;
                }
            }
            else {
                replyInfo->m_categoryKeysMapInSystemKind[categoryStr].insert(keysInValue.begin(), keysInValue.end());
            }
        }
    }

    replyInfo->gatherCountrySettingsJSON(modified_categories, PrefsKeyDescMap::cbGatherCountrySettings, GATHER_DEFAULT_JSON);
    replyInfo->gatherCountrySettingsJSON(modified_categories, PrefsKeyDescMap::cbGatherCountrySettings, GATHER_COUNTRY_JSON);

    /* query from db8 */
    replyInfo->gatherCountrySettings(modified_categories, PrefsKeyDescMap::cbGatherCountrySettings, GATHER_DEFAULT);
    replyInfo->gatherCountrySettings(modified_categories, PrefsKeyDescMap::cbGatherCountrySettings, GATHER_COUNTRY);

    replyInfo->m_conservativeButler->keep();
    return true;
}

bool PrefsKeyDescMap::cbGatherCountrySettings(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results)
{
    /* TODO: The data which has default country value should be removed
     *       This repeated code could be removed after imporving country population */

    static bool default_loaded = false;
    static bool country_loaded = false;
    static bool default_json_loaded = false;
    static bool country_json_loaded = false;

    static std::list<pbnjson::JValue> default_results;
    static std::list<pbnjson::JValue> country_results;
    static std::list<pbnjson::JValue> default_json_results;
    static std::list<pbnjson::JValue> country_json_results;

    if ( a_userdata == GATHER_DEFAULT ) {
        default_results = a_results;
        default_loaded = true;
    } else if ( a_userdata == GATHER_COUNTRY ) {
        country_results = a_results;
        country_loaded = true;
    } else if ( a_userdata == GATHER_DEFAULT_JSON ) {
        default_json_results = a_results;
        default_json_loaded = true;
    } else if ( a_userdata == GATHER_COUNTRY_JSON ) {
        country_json_results = a_results;
        country_json_loaded = true;
    } else {
        return false;
    }

    if ( default_loaded && country_loaded && default_json_loaded && country_json_loaded ) {
        std::list<pbnjson::JValue> gathered_results;

        /* default at first */
        gathered_results.insert(gathered_results.end(), default_json_results.begin(), default_json_results.end());
        gathered_results.insert(gathered_results.end(), country_json_results.begin(), country_json_results.end());
        gathered_results.insert(gathered_results.end(), default_results.begin(), default_results.end());
        gathered_results.insert(gathered_results.end(), country_results.begin(), country_results.end());

        PrefsKeyDescMap::cbFindCountrySettings(a_thiz_class, a_userdata, gathered_results);

        default_results.clear();
        country_results.clear();
        default_json_results.clear();
        country_json_results.clear();

        default_loaded = false;
        country_loaded = false;
        default_json_loaded = false;
        country_json_loaded = false;
    }

    return true;
}

bool PrefsKeyDescMap::cbFindCountrySettings(void *a_thiz_class, void *a_userdata, std::list<pbnjson::JValue>& a_results )
{
    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) a_thiz_class;
    pbnjson::JValue result_concat(pbnjson::Array());
    bool success = false;
    bool result = false;

    // Checks whether all returns are valid one.
    //
    for (pbnjson::JValue citer : a_results)
    {
        pbnjson::JValue label = citer["returnValue"];
        if (label.isBoolean()) {
            success = label.asBool();

            SSERVICELOG_TRACE("%s: %s", __FUNCTION__, citer.stringify().c_str());

            if (success == false) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 1,
                        PMLOGJSON("payload", citer.stringify().c_str()), "Country query is failed");
                continue;
            }

            const pbnjson::JValue country_array = citer["results"];
            if (country_array.isArray()) {
                std::map<unsigned long, pbnjson::JValue> orderedItems;
                int iarr = 0;
                for (pbnjson::JValue itemObj : country_array.items()) {
                    if (itemObj.isNull()) {
                        continue;
                    }

                    // pick values it is really existed in system kind
                    pbnjson::JValue categoryObj = itemObj[KEYSTR_CATEGORY];
                    std::string categoryStr = categoryObj.isString() ? categoryObj.asString() : "";
                    if (categoryStr != "") {
                        pbnjson::JValue valueObj = itemObj[KEYSTR_VALUE];

                        // category that has key existed in system kind
                        if (replyInfo->m_categoryKeysMapInSystemKind.find(categoryStr) != replyInfo->m_categoryKeysMapInSystemKind.end()) {
                            json_object_object_pick(valueObj, replyInfo->m_categoryKeysMapInSystemKind[categoryStr]);
                        }
                        // category that has no key existed in system kind. maybe 'value: {}'
                        else {
                            json_object_object_pick(valueObj, set<string>{});
                        }
                    }
                    int conditionScore = PrefsDb8Condition::instance()->scoreByCondition(itemObj);
                    if (conditionScore != 0) {
                        // itemIndex will be unique even for same score, hence don't care about duplicates.
                        unsigned long itemIndex = (conditionScore << 20) + iarr;
                        orderedItems[itemIndex] = itemObj;
                    }
                    iarr++;
                }

                for (const auto item : orderedItems)
                    result_concat.append(item.second);
            }
        }
    }
    /* If we have no country specific settings,
     * proceed to next step for updating key description map.
     * Otherwise, merge country specific one before updating the map. */
    if (result_concat.arraySize() == 0) {
        // get desc kind info -> get country code -> parsing
        // m_descKindObj is set in the callback founction
        replyInfo->sett_populated();
        replyInfo->updateKeyDescData();
        result = true;
    } else {
        result = replyInfo->mergeCountrySettings(result_concat);
    }

    return result;
}

bool PrefsKeyDescMap::cbFindCountryDesc(void *a_thiz_class, void *a_userdata, std::list<pbnjson::JValue>& a_results )
{
    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) a_thiz_class;
    pbnjson::JValue result_concat(pbnjson::Array());
    bool success = false;

    // Checks whether all returns are valid one.
    //
    for (pbnjson::JValue citer : a_results)
    {
        pbnjson::JValue label = citer["returnValue"];
        if (label.isBoolean()) {
            success = label.asBool();

            SSERVICELOG_TRACE("%s: %s", __FUNCTION__, citer.stringify().c_str());

            if (success == false) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 1,
                        PMLOGJSON("payload", citer.stringify().c_str()), "Country query is failed");
                continue;
            }

            pbnjson::JValue country_array = citer["results"];
            if (country_array.isArray()) {
                std::map<unsigned long, pbnjson::JValue> orderedItems;
                int iarr = 0;
                for (pbnjson::JValue itemObj : country_array.items()) {
                    int conditionScore = PrefsDb8Condition::instance()->scoreByCondition(itemObj);
                    if (conditionScore != 0) {
                      // itemIndex will be unique even for same score, hence don't care about duplicates.
                        unsigned long itemIndex = (conditionScore << 20) + iarr;
                        orderedItems[itemIndex] = itemObj;
                    }
                    iarr++;
                }

                for ( const auto item : orderedItems )
                    result_concat.append(item.second);
            }
        }
    }
    /* If we have no country specific description,
     * proceed to next step for finding country specific settings.
     * Otherwise, merge founded country specific one before finding settings */
    if (result_concat.arraySize() == 0) {
        replyInfo->populateCountrySettings();
    } else {
        replyInfo->mergeCountryDesc(result_concat);
    }

    return true;
}

bool PrefsKeyDescMap::initKeyDescMap(void)
{
    bool result;

    if(!m_doFirstFlag && !isInit()) {
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
        return false;
    }

    m_doFirstFlag = false;
    resetInitFlag();

    /* For building description map,
     * We need to get current country code, do it at first.
     * And then, build description map. */
    result = sendCountryCodeRequest();

    return result;
}

void PrefsKeyDescMap::initKeyDescMapByCountry(void)
{
    if(!m_doFirstFlag && !isInit()) {
        return;
    }

    m_doFirstFlag = false;
    resetInitFlag();

    /* If modified data is exist in the main kind,
     * even country is changed and there is country specific data,
     * the country specific data is not provided
     *
     * After overwriting data in main kind as country default,
     * Loading description data is proceed.
     * Both description and data are included in targets of this procedure */

    {
        std::string code = m_finalize->getSettingValue("country");
        if (!code.empty())
        {
            setCountryCode(code);
        }

        code = m_finalize->getSettingValue("localeCountryGroup");
        if (!code.empty())
        {
            setCountryGroupCode(code);
        }
    }

    populateCountryDesc();
}

void PrefsKeyDescMap::loadExceptionAppList()
{
    std::ifstream ifs(SETTINGSSERVICE_EXCEPTION_APPLIST_PATH);
    if (ifs.is_open())
    {
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        pbnjson::JValue exceptionfile = pbnjson::JDomParser::fromString(content);
        if (exceptionfile.isObject())
        {
            pbnjson::JValue exceptionapplist = exceptionfile["appId"];
            for (pbnjson::JValue exceptionStr : exceptionapplist.items())
            {
                if (exceptionStr.isString())
                    m_exceptionApplist.push_back(exceptionStr.asString());
            }
        }
        else
        {
            SSERVICELOG_WARNING(MSGID_EXCEPTIONAPPLIST_FILE_FAILTO_LOAD, 0, "Fail to parse exceptionApplist file as JSON");
            return;
        }
    }
    else
    {
        SSERVICELOG_WARNING(MSGID_EXCEPTIONAPPLIST_FILE_NOTFOUND, 0, "Not found exceptionApplist file");
        return;
    }
}

bool PrefsKeyDescMap::isExceptionAppList(const std::string& appId) const
{
    return std::find(m_exceptionApplist.begin(), m_exceptionApplist.end(), appId) != m_exceptionApplist.end();
}

std::string PrefsKeyDescMap::getAppIdforSubscribe(LSMessage* message)
{
    std::string foundAppId;
    std::map <LSMessage*, std::string>::iterator  itSubKeyMap;

    m_lock_subsAppId.lock();

    itSubKeyMap = m_subsAppList.find(message);
    if ( itSubKeyMap != m_subsAppList.end() )
        foundAppId = itSubKeyMap->second;

    m_lock_subsAppId.unlock();

    return foundAppId;
}

bool PrefsKeyDescMap::cbSubscriptionCancel(LSHandle * a_handle, LSMessage * a_message, void *a_data)
{
    PrefsKeyDescMap* thiz_class = static_cast<PrefsKeyDescMap *>(a_data);
    if (!thiz_class)
        return false;

    thiz_class->removeSubscription(a_message);

    return true;
}

void PrefsKeyDescMap::removeSubscription(LSMessage * a_message)
{
    std::lock_guard<std::mutex> lock(m_lock_subsAppId);

    std::map <LSMessage*, std::string>::iterator itSubKeyMap = m_subsAppList.find(a_message);
    if(itSubKeyMap != m_subsAppList.end())
        m_subsAppList.erase(itSubKeyMap);
}

void PrefsKeyDescMap::setAppIdforSubscribe(LSMessage* message, std::string app_id)
{
    std::lock_guard<std::mutex> lock(m_lock_subsAppId);

    m_subsAppList.insert(std::map<LSMessage*, std::string>::value_type(message, app_id));
}


void PrefsKeyDescMap::loadDescFilesForEachApp(pbnjson::JValue jArrayObj)
{
    std::list<std::string> entries;

    Utils::readDirEntry(DEFAULT_LOADING_DIRECTORY, ".description.json", entries);

    for (const std::string& entry : entries) {
        std::string path(DEFAULT_LOADING_DIRECTORY);
        path.append("/");
        path.append(entry);

        std::string descriptionData;
        if (!Utils::readFile(path, descriptionData)) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_LOAD_FOR_APP, 1,
                PMLOGKS("Cannot Load - Read File Error", path.c_str()),
                MSGID_KEYDESC_LOAD_FOR_APP);
            continue;
        }

        pbnjson::JValue arr = pbnjson::JDomParser::fromString(descriptionData);
        if (arr.isArray()) {
            for (pbnjson::JValue arrItemObj : arr.items()) {
                jArrayObj.append(arrItemObj.duplicate());
            }

            SSERVICELOG_INFO(MSGID_KEYDESC_LOAD_FOR_APP, 1,
                PMLOGKS("Load", path.c_str()),
                MSGID_KEYDESC_LOAD_FOR_APP);
        }
        else {
            SSERVICELOG_WARNING(MSGID_KEYDESC_LOAD_FOR_APP, 1,
                PMLOGKS("Cannot Load - JSON Error", path.c_str()),
                MSGID_KEYDESC_LOAD_FOR_APP);
        }
    }
}

bool PrefsKeyDescMap::sendLoadKeyDescDefaultKindRequest()
{
    LSError lsError;
    LSErrorInit(&lsError);
    bool result = true; /* flag for indicating default description file sutatus */

    /*
       The data of com.webos.settings.desc.default.country is also retrieved,
       because the desc.default.country kind is extended from desc.default kind
     */
    pbnjson::JArray val_array;
    val_array.append(getCountryCode());
    val_array.append("none");
    val_array.append("default");
    pbnjson::JObject where_item;
    where_item.put("prop", "country");
    where_item.put("op", "%");
    where_item.put("val", val_array);
    pbnjson::JArray where_array;
    where_array.append(where_item);
    pbnjson::JObject query;
    query.put("from", SETTINGSSERVICE_KIND_DFLT_DESC);
    query.put("where", where_array);

    Db8FindChainCall *chainCall = new Db8FindChainCall;
    chainCall->ref();
    chainCall->Connect(PrefsKeyDescMap::cbLoadKeyDescDefaultKindRequest, this, (void *)result);
    result = Db8FindChainCall::sendRequest(chainCall, m_serviceHandle, query);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_DB_LUNA_CALL_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }
    chainCall->unref();

    return result;
}

bool PrefsKeyDescMap::sendLoadKeyDescMainKindRequest()
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JObject query;
    query.put("from", SETTINGSSERVICE_KIND_MAIN_DESC);

    Db8FindChainCall *chainCall = new Db8FindChainCall;
    chainCall->ref();
    chainCall->Connect(PrefsKeyDescMap::cbLoadKeyDescMainKindRequest, this, NULL);
    bool result = Db8FindChainCall::sendRequest(chainCall, m_serviceHandle, query);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_DB_LUNA_CALL_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }
    chainCall->unref();

    return result;
}

bool PrefsKeyDescMap::cbLoadKeyDescDefaultKindRequest(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results )
{
    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) a_thiz_class;
    std::list<pbnjson::JValue> resultList;
    bool success_file = (bool)a_userdata;
    bool success = false;

    // Checks whether all returns are valid one.
    //
    for (pbnjson::JValue citer : a_results)
    {
        pbnjson::JValue label = citer["returnValue"];
        if (label.isBoolean()) {
            success = label.asBool();
            if (success == false) {
                continue;
            }

            label = citer["results"];
            if (label.isArray()) {
                if (label.arraySize() > 0) {
                    resultList.push_back(citer);
                }
            }
        }
    }

    if (!resultList.empty())
    {
        // it should be first parameter.
        if(replyInfo->setDescKindObj(DescKindType_eDefault, &resultList)) {
            /* After KeyDescriptionMAP has been built, modified description data is applied to the MAP */
            success = true;
        }
    }

    if (success_file || success)
    {
        // If either loading file or success is successful, then go to next step.
        //
        replyInfo->sendLoadKeyDescMainKindRequest();
    }

    if (!success_file && !success) {
        // TODO: we need to refine an exception scenario for this case.
        //      From now on, If this caching process called by changing country key, SettingsService doesn't work correctly.
        SSERVICELOG_DEBUG("Fail to load description data in internal memory");

        // If this request is started by a client, not in booting time, send return and finish caching process.
        // send reply for setSettings and remove it.
        if(replyInfo->m_finalize) {
            replyInfo->m_finalize->finalize();
            replyInfo->m_finalize = NULL;
            SSERVICELOG_DEBUG("Send result reply of setSystemSettings");
        }

        replyInfo->setInitFlag();

        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }

    return success;
}

bool PrefsKeyDescMap::cbLoadKeyDescMainKindRequest(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results )
{
    bool success = false;
    std::list<pbnjson::JValue> resultList;
    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) a_thiz_class;

    for (pbnjson::JValue citer : a_results)
    {
        pbnjson::JValue label = citer["returnValue"];
        if (!label.isBoolean() || !label.asBool()) {
            continue;
        }

        label = citer["results"];
        if (label.isArray() && label.arraySize() > 0) {
            resultList.push_back(citer);
        }
    }

    if (!resultList.empty())
    {
        if(replyInfo->setDescKindObj(DescKindType_eSysMain, &resultList)) {
            success = true;
        }
    }

    if (success == false) {
        // TODO: we need to refine an exception scenario for this case.
        //      No data from com.webos.settings.desc.system is in normal case, not critical.
        SSERVICELOG_WARNING(MSGID_LOAD_DESC_DATA_FAIL, 0 ,"Fail to load description data in internal memory");
    }

    // update dimension info
    replyInfo->setKeyDescData();

    return success;
}

bool PrefsKeyDescMap::removeKeyInCategoryMap(const std::string &category, const std::string &key)
{
    std::set<std::string> keyList;

    CategoryMap::iterator itCategory = m_categoryMap.find(category);
    if(itCategory != m_categoryMap.end()) {
        std::set<std::string>::iterator it;
        keyList = itCategory->second;

        for(it = keyList.begin(); it != keyList.end(); it++) {
            if((*it) == key) {
                break;
            }
        }

        // there is a matched key.
        if(it != keyList.end()) {
            // remove key and set new key list
            keyList.erase(it);
            if(keyList.size()) {
                itCategory->second = keyList;
            }
            // if the list is empty, remove a categoryMap item.
            else {
                m_categoryMap.erase(itCategory);
            }

            return true;
        }
    }

    return false;
}

bool PrefsKeyDescMap::insertKeyToCategoryMap(const std::string &category, const std::string &key)
{
    CategoryMap::iterator it = m_categoryMap.find(category);

    // new item for categoryMap
    if (it == m_categoryMap.end()) {
        std::set<std::string> keys;
        keys.insert(key);
        m_categoryMap.insert(CategoryMap::value_type(category, keys));
    }
    // exist category
    else {
        it->second.insert(key);
    }

    return true;
}

std::vector<std::string> PrefsKeyDescMap::getDimensionInfo(void) const
{
    std::vector<std::string> dimension_keys;

    for (const std::pair<std::string, std::list<std::string>>& cit : m_dimFormatMap) {
        for (const std::string& key_cit : cit.second) {
            dimension_keys.push_back(key_cit);
        }
    }

    return dimension_keys;
}

bool PrefsKeyDescMap::setDimensionFormat() {
    std::list<std::string> keyList;

    m_dimFormatMap.clear();

    bool retVal = false;
    std::string jsonStr;
    if (!Utils::readFile(DEFAULT_DIMENSION_FORMAT_FILEPATH, jsonStr)) {
        SSERVICELOG_WARNING(MSGID_DIMENSION_FORMAT_LOAD, 1,
                PMLOGKS("Cannot Load - Read File Error ", DEFAULT_DIMENSION_FORMAT_FILEPATH),
                MSGID_DIMENSION_FORMAT_LOAD);

        return retVal;
    }

    pbnjson::JValue jObj = pbnjson::JDomParser::fromString(jsonStr);
    if (jObj.isArray()) {
        int arrSize = jObj.arraySize();
        if(arrSize == 0) {
            SSERVICELOG_WARNING(MSGID_DIMENSION_FORMAT_LOAD, 0, "There is no dimension format");
        }

        std::string key;
        int i = 0;
        for (; i < arrSize; i++) {
            pbnjson::JValue formatArrItemObj = jObj[i];
            // get dimension name
            pbnjson::JValue label = formatArrItemObj["key"];
            if (label.isString()) {
                key = label.asString();
                SSERVICELOG_DEBUG("Dimension Key: %s", key.c_str());
            }
            else {
                SSERVICELOG_WARNING(MSGID_DIMENSION_FORMAT_LOAD, 0, "Error!! Get Dimension format key.");
                break;
            }

            // get dimension format
            keyList.clear();
            label = formatArrItemObj["map"];
            if (label.isArray()) {
                int mapArrSize = label.arraySize();
                if(mapArrSize == 0) {
                    break;
                }
                int j = 0;
                for (; j < mapArrSize; j++) {
                    pbnjson::JValue itemObj = label[j];
                    if (itemObj.isString()) {
                        keyList.push_back(itemObj.asString());
                        SSERVICELOG_DEBUG("Dimension Item: %s", itemObj.stringify().c_str());
                    }
                    else {
                        SSERVICELOG_WARNING(MSGID_DIMENSION_FORMAT_LOAD, 0, "Error getting dimension format item.");
                        break;
                    }
                }
                if (j != mapArrSize)
                    break;
            }
            else {
                SSERVICELOG_WARNING(MSGID_DIMENSION_FORMAT_LOAD, 0, "Error getting dimension format item.");
                break;
            }
            m_dimFormatMap.insert( DimFormatMap::value_type(key, keyList) );
        }
        retVal = (i == arrSize);
    }
    else {
        SSERVICELOG_WARNING(MSGID_DIMENSION_FORMAT_LOAD, 0, "Error parsing dimension format item.");
    }

    if(!retVal) {
        m_dimFormatMap.clear();
    }

    return retVal;
}

static void loadKeysFromDefault(const DefaultBson& a_bson, const string& a_type, set<string>& a_result)
{
    DefaultBson::Query query;

    query.addWhere("type", a_type);

    pbnjson::JArray jResults;

    query.executeEx(&a_bson, jResults);

    if (!jResults.isArray())
        return;

    for (pbnjson::JValue jItem : jResults.items())
    {
        pbnjson::JValue jKeys = jItem["keys"];

        if (jKeys.isArray()) {
            for (pbnjson::JValue jKeysItem : jKeys.items())
            {
                if (jKeysItem.isString())
                    a_result.insert(jKeysItem.asString());
            }
        }
    }
}

bool PrefsKeyDescMap::setDimensionKeyFromDefault()
{
    DefaultBson defaultDimKey;

    DefaultBson::IndexerMap indexerMap;

    indexerMap.insert(DefaultBson::IndexerMap::value_type("type", new DefaultBson::Indexer("type")));

    defaultDimKey.loadWithIndexerMap("/etc/palm/description.map.bson", indexerMap);

    std::list<std::string> dimKeysEmpty;
    pbnjson::JValue arrResults = pbnjson::Array();

    // dimension key value
    DefaultBson::Query queryKV;
    queryKV.addWhere("type", "dimkeyvalue");
    queryKV.executeEx(&defaultDimKey, arrResults);
    if (arrResults.isArray()) {
        for (pbnjson::JValue jItem : arrResults.items()) {
            pbnjson::JValue arrKeys = jItem["keys"];
            if (arrKeys.isArray()) {
                for (pbnjson::JValue jKeysItem : arrKeys.items()) {
                    if (jKeysItem.isString())
                        m_dimKeyValueMap.insert(DimKeyValueMap::value_type(jKeysItem.asString(), ""));
                }
            }
        }
    }

    // dependent dimension keys
    DefaultBson::Query queryD0;
    queryD0.addWhere("type", "d0");
    arrResults = pbnjson::Array();
    queryD0.executeEx(&defaultDimKey, arrResults);
    if (arrResults.isArray()) {
        for (pbnjson::JValue jItem : arrResults.items()) {
            pbnjson::JValue arrKeys = jItem["keys"];
            if (arrKeys.isArray()) {
                 for (pbnjson::JValue jKeysItem : arrKeys.items()) {
                     if (jKeysItem.isString())
                         m_indepDimKeyMap.insert(DimKeyMap::value_type(jKeysItem.asString(), dimKeysEmpty));
                 }
            }
       }
   }

    // d1 dimension keys
    DefaultBson::Query queryD1;
    queryD1.addWhere("type", "d1");
    arrResults = pbnjson::Array();
    queryD1.executeEx(&defaultDimKey, arrResults);
    if (arrResults.isArray()) {
        for (pbnjson::JValue jItem : arrResults.items()) {
            pbnjson::JValue jMap = jItem["map"];
            if (jMap.isObject()) {
                for (pbnjson::JValue::KeyValue it : jMap.children()) {
                    std::list<std::string> dimKeys;
                    pbnjson::JValue jDimKeys = it.second;
                    if (jDimKeys.isArray()) {
                        for (pbnjson::JValue jDimKey : jDimKeys.items()) {
                            if (jDimKey.isString())
                            dimKeys.push_back(jDimKey.asString());
                        }
                    }

                    m_depDimKeyMapD1.insert(DimKeyMap::value_type(it.first.asString(), dimKeys));
                }
            }
        }
    }
    // load pre-analyzed keys
    loadKeysFromDefault(defaultDimKey, "volatile",      m_volatileKeys);
    loadKeysFromDefault(defaultDimKey, "perAppDbType",  m_perAppKeys);
    loadKeysFromDefault(defaultDimKey, "mixedDbType",   m_mixedPerAppKeys);
    loadKeysFromDefault(defaultDimKey, "exceptionDbType", m_exceptionAppKeys);
    loadKeysFromDefault(defaultDimKey, "hasCountryVar",   m_countryVarKeys);
    loadKeysFromDefault(defaultDimKey, "strictValueCheck", m_strictValueCheckKeys);

    return true;
}

bool PrefsKeyDescMap::setDimensionKeyValueList(void)
{
    /* collect available value list of dimension to generate
     * all available categories(category$dimension format) */
    std::vector<std::string> dims = getDimensionInfo();

    for (const std::string& dim : dims) {
        DimKeyValueListMap::iterator value_list = m_dimKeyValueListMap.find(dim);

        if ( value_list == m_dimKeyValueListMap.end() ) {
            std::set<std::string> vl;
            m_dimKeyValueListMap.insert(DimKeyValueListMap::value_type(dim, vl));
            value_list = m_dimKeyValueListMap.find(dim);
        }

        pbnjson::JValue descObj = genDescFromCache(dim);
        if (descObj.isNull()) continue;

        pbnjson::JValue values = descObj["values"];
        if (values.isNull()) {
            continue;
        }

        pbnjson::JValue array = values["arrayExt"];
        if (!array.isArray() || array.arraySize() == 0) {
            continue;
        }

        for (pbnjson::JValue valueObj : array.items()) {
            pbnjson::JValue value = valueObj["value"];
            if (value.isString()) {
                value_list->second.insert(value.asString());
            }
        }

    } /* for all dimension keys */

    return true;
}

void PrefsKeyDescMap::stackCategoryDimStr(CategoryDimKeyListMap &map, const std::string &a_key, const std::string &a_category, const std::string &fmt, const std::string &str) const
{
    if ( fmt.empty() ) {
        CategoryDimKeyListMap::iterator cat = map.find(a_category+"$"+str);
        if ( cat == map.end() ) {
            std::set<std::string> key_list;
            key_list.insert(a_key);
            map.insert(CategoryDimKeyListMap::value_type(a_category+"$"+str, key_list));
        } else {
            cat->second.insert(a_key);
        }
        return;
    }

    unsigned pos = fmt.find(".");
    std::string cur_dim;
    std::string next_dim;

    if ( pos != std::string::npos ) {
        cur_dim = fmt.substr(0, pos);
        /* Always be some string after '.' */
        next_dim = fmt.substr(pos+1);
    } else {
        cur_dim = fmt;
        next_dim = "";
    }

    if ( cur_dim == "x" ) {
        stackCategoryDimStr(map, a_key, a_category, next_dim, str+".x");
        return;
    }

    DimKeyValueListMap::const_iterator vl = m_dimKeyValueListMap.find(cur_dim);
    if ( vl == m_dimKeyValueListMap.end() || vl->second.empty() ) {
        SSERVICELOG_WARNING(MSGID_KEYDESC_DIMENSION_ERR, 2,
                PMLOGKS("key", a_key.c_str()), PMLOGKS("dimension",cur_dim.c_str()),
                "Incorrect dimension info");
        return;
    }
    std::set<std::string> dim_value_list = vl->second;

    for (const std::string& dim_value : dim_value_list)
    {
        std::string work_str = str;
        if ( !work_str.empty() )
            work_str += ".";
        work_str += dim_value;
        stackCategoryDimStr(map, a_key, a_category, next_dim, work_str);
    }
}

CategoryDimKeyListMap PrefsKeyDescMap::getCategoryKeyListMapAll(const std::string &a_category, const std::set<std::string>& inKeyList) const
{
    CategoryDimKeyListMap retMap;
    std::set<std::string> keyList;

    if ( m_dimKeyValueListMap.empty() ) {
        /* empty map means error */
        return retMap;
    }

    DimFormatMap::const_iterator itDimFormat = m_dimFormatMap.find(a_category);
    if ( itDimFormat == m_dimFormatMap.end() ) {
        /* The a_category which has no dimension */
        retMap.insert(CategoryDimKeyListMap::value_type(a_category, inKeyList));
        return retMap;
    }

    // find keys in the required a_category
    CategoryMap::const_iterator itCategoryMap = m_categoryMap.find(a_category);
    if(itCategoryMap != m_categoryMap.end()) {
        for(const std::string& itList1 : itCategoryMap->second) {
            if(!inKeyList.empty()) {
                if (std::find(inKeyList.begin(), inKeyList.end(), itList1) != inKeyList.end()) {
                    keyList.insert(itList1);
                }
            }
            else {
                keyList.insert(itList1);
            }
        }
    }

    for (const std::string& key : keyList)
    {
        std::vector<std::string> dims;
        getDimensionsByKey(key, &dims);
        std::string fmt;
        std::string start;

        /* If dims is empty, category dimension string must be category$x.x.x */

        for ( std::list<std::string>::const_iterator dim = itDimFormat->second.begin();
                dim != itDimFormat->second.end(); )
        {
            if ( std::find(dims.begin(), dims.end(), *dim) != dims.end() ) {
                fmt += *dim;
            } else {
                fmt += "x";
            }
            if ( ++dim != itDimFormat->second.end() ) fmt += ".";
        }

        /* this function inserts category$dimension string into the map as fmt.
         * For example, if fmt is input.pictureMode._3dStatus then
         * picture$dtv.vivid.2d string is inserted */
        stackCategoryDimStr(retMap, key, a_category, fmt, start);
    }

    return retMap;
}

/**
 * Make Category-Dimension:KeyList Map by given dimension, keys, appId.
 *
 * @param  category  category from request like 'picture'.
 * @param  dimObj    dimension from request.
 * @param  inKeyList keys from request.
 * @return           CategoryDimKeyListMap
 */
CategoryDimKeyListMap PrefsKeyDescMap::getCategoryKeyListMap(
    const std::string& category,
    pbnjson::JValue dimObj,
    const std::set<std::string>& inKeyList) const
{
    CategoryDimKeyListMap categoryDimKeyListMap;

    // no-dimension category

    if (m_dimFormatMap.count(category) == 0) {
        if (dimObj.isNull()) {
            // just use category name
            categoryDimKeyListMap[category] = inKeyList;
        }
        else {
            // If dimension is given for the category that doesn't include diemsnion. It's error.
            // return empty Map
        }

        return categoryDimKeyListMap;
    }

    DimKeyValueMap reqDimKeyValueMap;

    if (dimObj.isObject()) {
        // If dimObj is {}, create and use empty dimension.
        // FIXME: this cause incorrect data is inserted
        // FIXME: eg. set pictureMode with dimension = {}
        if (dimObj.objectSize() == 0) {
            pbnjson::JValue emptyDimObj = getEmptyDimObj(category);
            for (pbnjson::JValue::KeyValue it : emptyDimObj.children()) {
                reqDimKeyValueMap[it.first.asString()] = it.second.asString();
            }
        }
        else {
            for (pbnjson::JValue::KeyValue it : dimObj.children()) {
                reqDimKeyValueMap[it.first.asString()] = it.second.asString();
            }
        }
    }

    // find keys in the required category

    std::set<std::string> keyList;

    auto itCategory = m_categoryMap.find(category);
    if (itCategory != m_categoryMap.end()) {
        const std::set<std::string>& keySet = itCategory->second;

        if (inKeyList.empty()) {
            keyList.insert(keySet.begin(), keySet.end());
        }
        else {
            std::set_intersection(
                keySet.begin(), keySet.end(),
                inKeyList.begin(), inKeyList.end(),
                std::inserter(keyList, keyList.begin()));
        }
    }

    // get categoryDim of selected keys.

    for (auto& key : keyList) {
        std::string categoryDim;
        if (getCategoryDim(key, /*out*/categoryDim, reqDimKeyValueMap)) {
            categoryDimKeyListMap[categoryDim].insert(key);
        }
    }

    return categoryDimKeyListMap;
}

/**
 * Build 'picture$x.x.x' from the given category.
 *
 * @param  category    category
 * @param  categoryDim category-dimension output
 * @return             tru if category has dimension
 */
bool PrefsKeyDescMap::getCategoryDim(const std::string& category, std::string& categoryDim) const
{
    DimFormatMap::const_iterator itDimFormat = m_dimFormatMap.find(category);
    if (itDimFormat == m_dimFormatMap.end()) {
        return false;
    }
    const std::list<std::string>& dimFormatList = itDimFormat->second;
    std::string dimStr;
    for (auto& it : dimFormatList) {
        (void)it;
        if (!dimStr.empty()) {
            dimStr += ".";
        }
        dimStr += "x";
    }
    categoryDim = category + "$" + dimStr;
    return true;
}

bool PrefsKeyDescMap::matchedRequestDimensions(const DimKeyValueMap& a_dimKeyValues, const std::vector<std::string>& a_dimensions) const
{
    if ( a_dimKeyValues.size() != a_dimensions.size() )
        return false;

    for ( const std::string& dimension : a_dimensions )
        if ( a_dimKeyValues.find(dimension) == a_dimKeyValues.end() )
            return false;

    return true;
}

/**
 * Build 'picture$dtv.normal.2d' from the given key.
 *
 * @param  key               key name
 * @param  categoryDim       string to be written
 * @param  reqDimKeyValueMap a DimKeyValueMap (string:string map) that contains from request.dimension
 * @return                   true if success
 */
bool PrefsKeyDescMap::getCategoryDim(const std::string &key, std::string &categoryDim, const DimKeyValueMap &reqDimKeyValueMap) const
{
    std::vector<std::string> dimensionVector;
    if (!getDimensionsByKey(key, &dimensionVector)) {
        categoryDim = "";
        return false;
    }

    std::string category;
    getCategory(key, category);

    std::list<std::string> dimFormatList;
    DimFormatMap::const_iterator itDimFormat = m_dimFormatMap.find(category);
    if (itDimFormat != m_dimFormatMap.end()) {
        dimFormatList = itDimFormat->second;
    }

    std::lock_guard<std::mutex> lock(m_lock_desc_json);

    categoryDim = category; // default output

    // create dimension string
    // For the case, a category uses dimension.
    // create dimension key value map
    std::string dimStr;

    // for the key that has no dimension
    if (dimensionVector.empty()) {
        // if there is given dimension, just use that.
        if (!reqDimKeyValueMap.empty()) {
            for (const auto& itDimKeyValueMap : reqDimKeyValueMap) {
                if (!dimStr.empty()) {
                    dimStr += ".";
                }
                dimStr += itDimKeyValueMap.second;
            }
        }
        // if there is no given dimension, set x for the dimension length of category.
        else {
            for (size_t i = 0; i < dimFormatList.size(); i++) {
                if (!dimStr.empty()) {
                    dimStr += ".";
                }
                dimStr += "x";
            }
        }
    }
    // for the key has dimensions
    else {
        DimKeyValueMap filteredDimKeyMap;

        // if dimension is given, select only keys in the dimension.
        if (!reqDimKeyValueMap.empty() && matchedRequestDimensions(reqDimKeyValueMap, dimensionVector)) {
            for (const std::string& itDim : dimensionVector) {
                DimKeyValueMap::const_iterator itDimKeyValueMap = reqDimKeyValueMap.find(itDim);
                if (itDimKeyValueMap != reqDimKeyValueMap.end()) {
                    filteredDimKeyMap.insert({itDimKeyValueMap->first, itDimKeyValueMap->second});
                }
            }
        }
        else if (!reqDimKeyValueMap.empty()) {
            /* FIXME: if the number of dimension specified is not equal to descrption,
             * settings data is added into the incorrect category which has only cate-
             * gory name. */
            return false;
        }
        // with no given dimension, use current value.
        else {
            for (const std::string& itDim : dimensionVector) {
                DimKeyValueMap::const_iterator itDimKeyValueMap = m_dimKeyValueMap.find(itDim);
                if (itDimKeyValueMap != m_dimKeyValueMap.end()) {
                    filteredDimKeyMap.insert({itDimKeyValueMap->first, itDimKeyValueMap->second});
                }
            }
        }

        // create dimension string inserting '.' and 'x'
        //      if the category use three dimension and the length of given dimension is one.
        //      And the key use it of dimension.
        //      The dimension string to be "oooo.x.x", "x.ooo.x", or "x.x.ooo"
        if (!filteredDimKeyMap.empty()) {
            for (const std::string& itList : dimFormatList) {
                if (!dimStr.empty()) {
                    dimStr += ".";
                }

                DimKeyValueMap::const_iterator itDimKeyValueMap = filteredDimKeyMap.find(itList);
                dimStr += itDimKeyValueMap != filteredDimKeyMap.end() ? itDimKeyValueMap->second : "x";
            }
        }
    }

    if (!dimStr.empty()) {
        categoryDim = category + "$" + dimStr;
    }

    return true;
}

void PrefsKeyDescMap::initDimensionValues(const std::string& a_cat, const std::set<std::string>& a_keys)
{
    pbnjson::JValue valueObj = PrefsDb8Get::mergeLayeredRecords(a_cat, pbnjson::Array(), "", true);
    if (valueObj.isNull()) {
        SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 0, "merging Array Error");
        return;
    }

    for (const std::string& key : a_keys)
    {
        pbnjson::JValue kobj = valueObj[key];
        if (!kobj.isString())
            continue;

        DimKeyValueMap::iterator dim_key = m_dimKeyValueMap.find(key);
        if ( dim_key != m_dimKeyValueMap.end() ) {
            dim_key->second = kobj.asString();
        }
    }
}

void PrefsKeyDescMap::initDimensionValues(void)
{
    for ( DimKeyValueMap::value_type& dim_key : m_dimKeyValueMap)
    {
        dim_key.second.clear();
    }

    std::set<std::string> keys;

    getDimKeyList(DIMENSIONKEYTYPE_INDEPENDENT, keys);
    initDimensionValues("dimensionInfo", keys);

    getDimKeyList(DIMENSIONKEYTYPE_DEPENDENTD1, keys);
    for(const std::string& key : keys)
    {
        std::string category;
        std::set<std::string> select;

        getCategoryDim(key, category, DimKeyValueMap());
        select.insert(key);

        initDimensionValues(category, select);
    }
}

void PrefsKeyDescMap::handleVolatileKeyInDimension(std::set<std::string>& a_keyList)
{
    std::set<std::string> remainKeyList = a_keyList;

    /* load volatile settings at first */
    for(const std::string& key : a_keyList)
    {
        std::string categoryDim;

        getCategoryDim(key, categoryDim, DimKeyValueMap());

        /* app_id of dimension key is always empty */
        std::string val = PrefsVolatileMap::instance()->getVolatileValue(categoryDim, "", key);

        pbnjson::JValue valObj = pbnjson::JDomParser::fromString(val);
        if (!valObj.isString())
            continue;

        val = valObj.asString();

        if ( val.empty() )
            continue;

        DimKeyValueMap::iterator dim_key = m_dimKeyValueMap.find(key);
        if( dim_key != m_dimKeyValueMap.end() ) {
            dim_key->second = val;
            remainKeyList.erase(key);
        }
    }

    a_keyList = remainKeyList;
}

bool PrefsKeyDescMap::setDimensionValues() {
    std::set<std::string> keyList;

    initDimensionValues();

    getDimKeyList(DIMENSIONKEYTYPE_INDEPENDENT, keyList);
    handleVolatileKeyInDimension(keyList);
    sendDimensionValuesIndepRequest(keyList, SETTINGSSERVICE_KIND_MAIN);

    return true;
}

pbnjson::JValue PrefsKeyDescMap::createCountryCodeJsonQuery(const std::string& targetKind) const
{
    DimKeyMap::iterator itDimKeyMap;

    pbnjson::JObject replyRoot;
    pbnjson::JObject replyRootQuery;
    pbnjson::JArray replyRootSelectArray;
    pbnjson::JArray replyRootWhereArray;
    pbnjson::JObject replyRootItem1;
    pbnjson::JObject replyRootItem2;

/*
luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/search '{"query":{"from":"com.webos.settings.system:1", "select":["value.language", "value.brightness"] , "where":[{"prop":"category","op":"=","val":"HDMI"}, {"prop":"app_id","op":"=","val":"com.webos.BDP"}]}}'
*/

    replyRootSelectArray.append("value.country");
    replyRootSelectArray.append("value.localeCountryGroup");
    replyRootQuery.put("select", replyRootSelectArray);

    // add where
    //              add category
    replyRootItem1.put("prop", "category");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", "option");

    replyRootItem2.put("prop", "app_id");
    replyRootItem2.put("op", "=");
    replyRootItem2.put("val", "");

    replyRootWhereArray.append(replyRootItem1);
    replyRootWhereArray.append(replyRootItem2);
    if (targetKind == SETTINGSSERVICE_KIND_DEFAULT) {
        std::string cur_country = PrefsKeyDescMap::instance()->getCountryCode();
        pbnjson::JObject countryItem;
        pbnjson::JArray countryArray;

        countryArray.append("none");
        countryArray.append("default");
        countryArray.append(cur_country);

        countryItem.put("prop", "country");
        countryItem.put("op", "=");
        countryItem.put("val", countryArray);

        replyRootWhereArray.append(countryItem);
    }
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", targetKind);

    // add reply root
    replyRoot.put("query", replyRootQuery);

    return replyRoot;
}


pbnjson::JValue PrefsKeyDescMap::createIndepJsonQuery(const std::set<std::string>& keyList, const std::string& targetKind)
{
    m_targetKindIndep = targetKind;
    m_targetKeyListIndep = keyList;

    pbnjson::JObject replyRoot;
    pbnjson::JObject replyRootQuery;
    pbnjson::JArray replyRootWhereArray;
    pbnjson::JObject replyRootItem1;
    pbnjson::JObject replyRootItem2;
/*
luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/search '{"query":{"from":"com.webos.settings.system:1", "select":["value.language", "value.brightness"] , "where":[{"prop":"category","op":"=","val":"HDMI"}, {"prop":"app_id","op":"=","val":"com.webos.BDP"}]}}'
*/

    // add where
    //              add category
    replyRootItem1.put("prop", "category");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", "dimensionInfo");

    replyRootItem2.put("prop", "app_id");
    replyRootItem2.put("op", "=");
    replyRootItem2.put("val", "");

    replyRootWhereArray.append(replyRootItem1);
    replyRootWhereArray.append(replyRootItem2);
    if (targetKind == SETTINGSSERVICE_KIND_DEFAULT) {
        std::string cur_country = PrefsKeyDescMap::instance()->getCountryCode();
        pbnjson::JObject countryItem;
        pbnjson::JArray countryArray;

        countryArray.append("none");
        countryArray.append("default");
        countryArray.append(cur_country);

        countryItem.put("prop", "country");
        countryItem.put("op", "=");
        countryItem.put("val", countryArray);

        replyRootWhereArray.append(countryItem);
    }
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", targetKind);

    // add reply root
    replyRoot.put("query", replyRootQuery);

    return replyRoot;
}


pbnjson::JValue PrefsKeyDescMap::createDepJsonQueryD1(const std::set<std::string>& keyList, const std::string& targetKind)
{
    std::string categoryDim;

    m_targetKindD1 = targetKind;
    m_targetKeyListD1 = keyList;

    pbnjson::JObject batchRoot;
    pbnjson::JArray batchRootArray;

    for(const std::string& key : keyList) {
        pbnjson::JObject replyRoot;
        pbnjson::JObject replyRootQuery;
        pbnjson::JArray replyRootWhereArray;
        pbnjson::JObject replyRootItem1;
        pbnjson::JObject replyRootItem2;
        pbnjson::JObject batchRootItem;

        getCategoryDim(key, categoryDim, DimKeyValueMap());

        /*
           luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/search '{"query":{"from":"com.webos.settings.system:1", "select":["value.language", "value.brightness"] , "where":[{"prop":"category","op":"=","val":"HDMI"}, {"prop":"app_id","op":"=","val":"com.webos.BDP"}]}}'
         */

        // add where
        //              add category
        replyRootItem1.put("prop", "category");
        replyRootItem1.put("op", "=");
        replyRootItem1.put("val", categoryDim);

        replyRootItem2.put("prop", "app_id");
        replyRootItem2.put("op", "=");
        replyRootItem2.put("val", "");

        replyRootWhereArray.append(replyRootItem1);
        replyRootWhereArray.append(replyRootItem2);
        if (targetKind == SETTINGSSERVICE_KIND_DEFAULT) {
            std::string cur_country = PrefsKeyDescMap::instance()->getCountryCode();
            pbnjson::JObject countryItem;
            pbnjson::JArray countryArray;

            countryArray.append("none");
            countryArray.append("default");
            countryArray.append(cur_country);

            countryItem.put("prop", "country");
            countryItem.put("op", "=");
            countryItem.put("val", countryArray);

            replyRootWhereArray.append(countryItem);
        }
        replyRootQuery.put("where", replyRootWhereArray);

        // add from
        replyRootQuery.put("from", targetKind);

        // add reply root
        replyRoot.put("query", replyRootQuery);

        batchRootItem.put("method", "find");
        batchRootItem.put("params", replyRoot);

        batchRootArray.append(batchRootItem);
    }

    batchRoot.put("operations", batchRootArray);

    return batchRoot;
}

pbnjson::JValue PrefsKeyDescMap::createCountryJsonQuery() const
{
    pbnjson::JObject replyRoot;
    pbnjson::JValue replyMain = createCountryCodeJsonQuery(SETTINGSSERVICE_KIND_MAIN);
    pbnjson::JValue replyDefault = createCountryCodeJsonQuery(SETTINGSSERVICE_KIND_DEFAULT);
    pbnjson::JArray batchArrayObj;

    // Add default first.
    {
        pbnjson::JObject batchOp;
        batchOp.put("method", "find");
        batchOp.put("params", replyDefault);
        batchArrayObj.append(batchOp);
    }

    // Then, add main.
    {
        pbnjson::JObject batchOp;
        batchOp.put("method", "find");
        batchOp.put("params", replyMain);
        batchArrayObj.append(batchOp);
    }

    replyRoot.put("operations", batchArrayObj);

    return replyRoot;
}

bool PrefsKeyDescMap::sendCountryCodeRequest()
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot = createCountryJsonQuery();

    bool result = DB8_luna_call(m_serviceHandle, "luna://com.webos.service.db/batch", replyRoot.stringify().c_str(), cbCountryCodeRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Request country code");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    return true;
}

bool PrefsKeyDescMap::sendDimensionValuesIndepRequest(const std::set<std::string>& keyList, const char* targetKind)
{
    LSError lsError;
    LSErrorInit(&lsError);

    // send a request for all keys in 'dimensionInfo' category, then parsing them.
    pbnjson::JValue replyRoot = createIndepJsonQuery(keyList, targetKind);

    bool result = DB8_luna_call(m_serviceHandle, "luna://com.webos.service.db/find", replyRoot.stringify().c_str(), cbDimensionValuesIndepRequest, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Find indep-dimension");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    return true;
}

bool PrefsKeyDescMap::sendDimensionValuesDepD1Request(const std::set<std::string>& keyList, const char* targetKind)
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JValue replyRoot = createDepJsonQueryD1(keyList, targetKind);

    bool result = DB8_luna_call(m_serviceHandle, "luna://com.webos.service.db/batch", replyRoot.stringify().c_str(), cbDimensionValuesDepD1Request, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Find d1-dimension");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    return true;
}

bool PrefsKeyDescMap::cbCountryCodeRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    std::string countryCode;
    std::string countryGroupCode;

    bool success = false;
    bool isAllResultArrayEmtpy = true;

    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        /*
           {"returnValue":true,"results":[{"_id":"++IWeVoPs7+jKUDf","_kind":"com.webos.settings.default:1","_rev":125910,"_sync":true,"app_id":"","category":"dimensionInfo","value":{"_3dStatus":"off","input":"dtv"}}]}
         */

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PARSE_ERR, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (!label.isBoolean() || !label.asBool()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue batchResultArray = root["responses"];
        if (!batchResultArray.isArray()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 1, PMLOGJSON("payload", payload), "responses Array Error");
            break;
        }

        for(pbnjson::JValue batchResultObj : batchResultArray.items()) {
            label = batchResultObj["returnValue"];
            if (!label.isBoolean() || !label.asBool()) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
                continue;
            }

            pbnjson::JValue resultArray = batchResultObj["results"];
            if (!resultArray.isArray()) {
                SSERVICELOG_WARNING(MSGID_KYEDESC_NO_RESULTS, 0, "payload is : %s", payload);
                continue;
            }

            if (resultArray.isArray() && resultArray.arraySize() > 0) {
                isAllResultArrayEmtpy = false;
            } else {
                continue;
            }

            for (pbnjson::JValue arrayObj : resultArray.items())
            {
                if (arrayObj.isNull()) {
                    SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 1, PMLOGJSON("payload", payload), "Array Item Error");
                    continue;
                }

                label = arrayObj["value"];
                if (label.isNull())
                    continue;

                pbnjson::JValue valueObj = label["country"];
                if (valueObj.isString())
                {
                    countryCode = valueObj.asString();
                }

                valueObj = label["localeCountryGroup"];
                if (valueObj.isString())
                {
                    countryGroupCode = valueObj.asString();
                }
            }
        }

        if (isAllResultArrayEmtpy) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 1, PMLOGJSON("payload", payload), "results Array Error");
        }

        if ( !countryCode.empty() && !countryGroupCode.empty() )
        {
            success = true;
        }
    } while (false);

    // set default value for the keys that has no return with default kind
    if(!success) {
        replyInfo->setCountryCode(DEFAULT_COUNTRY_STR);
        replyInfo->setCountryGroupCode(DEFAULT_COUNTRY_GROUP_STR);
        replyInfo->sendLoadKeyDescDefaultKindRequest();
    }
    else {
        /* If modified data is exist in the main kind,
         * even there is country specific data and country is changed,
         * the country specific data is not provided
         *
         * After overwriting data in main kind as country default,
         * Loading description data is proceed.
         * Both description and data are included in targets of this procedure */
        replyInfo->setCountryCode(countryCode);
        replyInfo->setCountryGroupCode(countryGroupCode);
        replyInfo->sendLoadKeyDescDefaultKindRequest();
    }

    return success;
}

bool PrefsKeyDescMap::cbDimensionValuesIndepRequest(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool endCallChainFlag = true;

    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) data;

    const char *payload = LSMessageGetPayload(message);

    // set target key List to tempory key list
    std::set<std::string> remainKeyList = replyInfo->m_targetKeyListIndep;

    do {
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        /*
           {"returnValue":true,"results":[{"_id":"++IWeVoPs7+jKUDf","_kind":"com.webos.settings.default:1","_rev":125910,"_sync":true,"app_id":"","category":"dimensionInfo","value":{"_3dStatus":"off","input":"dtv"}}]}
         */

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PARSE_ERR, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (!label.isBoolean() || !label.asBool()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue resultArray = root["results"];
        if (!resultArray.isArray()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 1, PMLOGJSON("payload", payload), "results Array Error");
            break;
        }

        /* first category parameter is ignored if app_id is GLOBAL */
        pbnjson::JValue valueObj = PrefsDb8Get::mergeLayeredRecords("", resultArray, "", true);
        if (valueObj.isNull()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 1, PMLOGJSON("payload", payload), "merging Array Error");
            break;
        }

        for(const std::string& it : replyInfo->m_targetKeyListIndep) {
            pbnjson::JValue keyObj = valueObj[it];
            if (!keyObj.isString()) {
                continue;
            }

            DimKeyValueMap::iterator itKeyValueMap = replyInfo->m_dimKeyValueMap.find(it);
            if(itKeyValueMap != replyInfo->m_dimKeyValueMap.end()) {
                itKeyValueMap->second = keyObj.asString();
                remainKeyList.erase(it);
            }
        }
    } while(false);

    // set default value for the keys that has no return with default kind
    if(!remainKeyList.empty()) {
        if(replyInfo->m_targetKindIndep == SETTINGSSERVICE_KIND_DEFAULT) {
            for(const std::string& it : remainKeyList) {
                DimKeyValueMap::iterator itKeyValueMap = replyInfo->m_dimKeyValueMap.find(it);
                if(itKeyValueMap != replyInfo->m_dimKeyValueMap.end()) {
                    if ( itKeyValueMap->second.empty() ) {
                        SSERVICELOG_DEBUG("No '%s' dimension value", it.c_str());
                        itKeyValueMap->second = "x";
                    }
                }
            }

            // TODO: we need to refind exception scenario.
            SSERVICELOG_WARNING(MSGID_DIMENSION_KEY_VAL_ERR,0,"ERROR!! in getting independent dimension key values");
            //replyInfo->setDimensionValues();
        }
        // send request again to default kind
        else {
            replyInfo->sendDimensionValuesIndepRequest(remainKeyList, SETTINGSSERVICE_KIND_DEFAULT);
            endCallChainFlag = false;
        }
    }

    if(endCallChainFlag) {
        replyInfo->getDimKeyList(DIMENSIONKEYTYPE_DEPENDENTD1, remainKeyList);
        replyInfo->handleVolatileKeyInDimension(remainKeyList);
        replyInfo->sendDimensionValuesDepD1Request(remainKeyList, SETTINGSSERVICE_KIND_MAIN);
    }

    return true;
}

bool PrefsKeyDescMap::cbDimensionValuesDepD1Request(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool endCallChainFlag = true;

    PrefsKeyDescMap *replyInfo = (PrefsKeyDescMap*) data;

    const char *payload = LSMessageGetPayload(message);

    // set target key List to tempory key list
    std::set<std::string> remainKeyList = replyInfo->m_targetKeyListD1;

    do {
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        /*
           {"returnValue":true,"results":[{"_id":"++IWeVoPs7+jKUDf","_kind":"com.webos.settings.default:1","_rev":125910,"_sync":true,"app_id":"","category":"dimensionInfo","value":{"_3dStatus":"off","input":"dtv"}}]}
         */

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_PARSE_ERR, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (!label.isBoolean() || !label.asBool()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
            break;
        }

        pbnjson::JValue batchResultArray = root["responses"];
        if (batchResultArray.isNull()) {
            SSERVICELOG_WARNING(MSGID_KYEDESC_NO_RESPONSES,0, "payload is : %s", payload);
            break;
        }
        if (!batchResultArray.isArray()) {
            SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 1, PMLOGJSON("payload", payload), "responses Array Error");
            break;
        }

        for(pbnjson::JValue batchResultObj : batchResultArray.items()) {
            label = batchResultObj["returnValue"];
            if (!label.isBoolean() || !label.asBool()) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_DB_RETURNS_FAIL, 0, "payload is : %s", payload);
                continue;
            }

            pbnjson::JValue resultArray = batchResultObj["results"];
            if (!resultArray.isArray()) {
                SSERVICELOG_WARNING(MSGID_KYEDESC_NO_RESULTS, 0, "payload is : %s", payload);
                continue;
            }

            /* first category parameter is ignored if app_id is GLOBAL */
            pbnjson::JValue valueObj = PrefsDb8Get::mergeLayeredRecords("", resultArray, "", true);
            if (valueObj.isNull()) {
                SSERVICELOG_WARNING(MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR, 1, PMLOGJSON("payload", payload), "merging Array Error");
                continue;
            }

            for(const std::string& it : replyInfo->m_targetKeyListD1) {
                pbnjson::JValue keyObj = valueObj[it];
                if (!keyObj.isString()) {
                    continue;
                }

                DimKeyValueMap::iterator itKeyValueMap = replyInfo->m_dimKeyValueMap.find(it);
                if(itKeyValueMap != replyInfo->m_dimKeyValueMap.end()) {
                    itKeyValueMap->second = keyObj.asString();
                    remainKeyList.erase(it);
                }
            }
        }
    } while (false);
    // set default value for the keys that has no return with default kind
    if(!remainKeyList.empty()) {
        if(replyInfo->m_targetKindD1 == SETTINGSSERVICE_KIND_DEFAULT) {
            for(const std::string& it : remainKeyList) {
                DimKeyValueMap::iterator itKeyValueMap = replyInfo->m_dimKeyValueMap.find(it);
                if(itKeyValueMap != replyInfo->m_dimKeyValueMap.end()) {
                    if ( itKeyValueMap->second.empty() ) {
                        SSERVICELOG_DEBUG("No '%s' dimension value",it.c_str());
                        itKeyValueMap->second = "";
                    }
                }
            }

            // TODO: we need to refind exception scenario.
            SSERVICELOG_WARNING(MSGID_DEP_DIMENSION_KEY_VAL_ERR,0,"ERROR!! in getting dependent dimension key values");
            //g_critical("[%s:%d] Try again to get values for independent dimension key \n", __FUNCTION__, __LINE__);
            //replyInfo->setDimensionValues();
        }
        // send request again to default kind
        else {
            replyInfo->sendDimensionValuesDepD1Request(remainKeyList, SETTINGSSERVICE_KIND_DEFAULT);
            endCallChainFlag = false;
        }
    }
    else {
        SSERVICELOG_DEBUG("SettingsService init DONE");
    }

    if(endCallChainFlag) {
        if ( !replyInfo->m_initByDimChange ) {
            /* init by settingsservice start or country change */
            replyInfo->sendUpdatePreferenceFiles();
        } else {
            /* don't need to update preferences, just reset the flag */
            replyInfo->m_initByDimChange = false;
        }

        // set Description Map ready
        //      this part should be before calling sendResultReply
        //      when m_finalize is deleted, taskInfo is removed and paused task is called.
        replyInfo->setInitFlag();

        // send reply for setSettings and remove it.
        if(replyInfo->m_finalize) {
            PrefsFinalize *prefsFinalizeTask = replyInfo->m_finalize;
            replyInfo->m_finalize = NULL;
            prefsFinalizeTask->finalize();
        }

        // set SettingsService ready
        PrefsFactory::instance()->serviceReady();
        PrefsFactory::instance()->serviceReady();

        PrefsFactory::instance()->serviceStart();
    }
//  for debuging
//    replyInfo->tmpFileLog("/tmp/ss/descInfo");
//    replyInfo->tmpFileLogDimInfo("/tmp/ss/dimInfo");

    return true;
}

void PrefsKeyDescMap::setCountryCode(const std::string& a_country)
{
    m_countryCode = a_country;
}

const std::string& PrefsKeyDescMap::getCountryCode() const
{
    return m_countryCode;
}

void PrefsKeyDescMap::setCountryGroupCode(const std::string& a_countryGroup)
{
    m_countryGroupCode = a_countryGroup;
}

const std::string& PrefsKeyDescMap::getCountryGroupCode() const
{
    return m_countryGroupCode;
}

const DimKeyValueMap& PrefsKeyDescMap::getCurrentDimensionValues() const
{
    return m_dimKeyValueMap;
}

bool PrefsKeyDescMap::isCurrentDimension(pbnjson::JValue a_dimObj) const
{
    std::string dont_care_dim = "x";

    if (a_dimObj.isNull())
        return true;

    for (const std::pair<std::string, std::string>& citer : m_dimKeyValueMap)
    {
        pbnjson::JValue o = a_dimObj[citer.first];
        if (!o.isString() || dont_care_dim == o.asString())
            continue;
        if (citer.second != o.asString())
            return false;
    }

    return true;
}

void PrefsKeyDescMap::removeNotUsedDimension(pbnjson::JValue a_dimension)
{
    std::vector<std::string> delList;

    if (!a_dimension.isObject())
        return;

    for (pbnjson::JValue::KeyValue it : a_dimension.children()) {
        pbnjson::JValue value = it.second;
        std::string valueString = value.isString() ? value.asString() : "";
        if (valueString == "x") {
            delList.push_back(it.first.asString());
        }
    }

    for (const std::string& dim : delList) {
        a_dimension.remove(dim);
    }
}

struct PrefsDebugUtil {
    static void printKeyValueMap();
    static void printKeyValueListMap();
};

void PrefsDebugUtil::printKeyValueMap()
{
    for (const auto& citer : PrefsKeyDescMap::instance()->m_dimKeyValueMap) {
        printf("%s:%s\n", citer.first.c_str(), citer.second.c_str());
    }
}

void PrefsDebugUtil::printKeyValueListMap()
{
    for (const auto& citer : PrefsKeyDescMap::instance()->m_dimKeyValueListMap) {
        printf("%s:", citer.first.c_str());
        for (const std::string& v : citer.second) {
            printf("%s,", v.c_str());
        }
        printf("\n");
    }
}

std::string PrefsKeyDescMap::categoryDim2category(std::string &a_category_dim)
{
    if ( a_category_dim.find("$") == std::string::npos )
        return a_category_dim;

    std::istringstream iss(a_category_dim);
    std::string token;
    std::getline(iss, token, '$');

    return token;
}

pbnjson::JValue PrefsKeyDescMap::categoryDim2dimObj(std::string &a_category_dim)
{
    if ( a_category_dim.find("$") == std::string::npos )
        return pbnjson::JValue();

    std::istringstream iss(a_category_dim);
    std::string category, token;
    std::list<std::string> dims;
    DimFormatMap::const_iterator dim_fmt;

    std::getline(iss, category, '$');
    while ( std::getline(iss, token, '.') ) {
        dims.push_back(token);
    }

    dim_fmt = m_dimFormatMap.find(category);
    if ( dim_fmt == m_dimFormatMap.end() || dim_fmt->second.size() != dims.size() )
        return pbnjson::JValue();

    std::list<std::string>::const_iterator d_name = dim_fmt->second.begin();
    std::list<std::string>::const_iterator d_value = dims.begin();

    pbnjson::JObject dim_obj;

    for ( ; d_name != dim_fmt->second.end() && d_value != dims.end();
            ++d_name, ++d_value)
    {
        if ( *d_value == "x" )
            continue;

        dim_obj.put(*d_name, *d_value);
    }

    return dim_obj;
}

ConservativeButler::ConservativeButler()
{
    propertyNames.push_back("value.localeInfo.locales.UI");
    propertyNames.push_back("value.localeInfo.keyboards");

    keepObj = pbnjson::Object();
}

ConservativeButler::~ConservativeButler()
{
}

void ConservativeButler::clear()
{
    keepObj = pbnjson::Object();
}

std::vector<std::string> split(std::string str) {
    std::vector<std::string> strings;
    std::istringstream f(str);
    std::string s;
    while (std::getline(f, s, '.')) {
        strings.push_back(s);
    }
    return strings;
}

/**
 * Find specific key/value in SETTINGSSERVICE_LOCALEINFO_PATH and keep it
 */
void ConservativeButler::keep()
{
    pbnjson::JValue localeInfoObj;

    std::ifstream ifs(SETTINGSSERVICE_LOCALEINFO_PATH);
    if (ifs.is_open()) {
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        localeInfoObj = pbnjson::JDomParser::fromString(content);
        if (localeInfoObj.isNull()) {
            SSERVICELOG_ERROR(MSGID_LOCALE_FILE_PARSE_ERR, 0, "Failed to parse locale file contents into json");
            return;
        }
        ifs.close();
    } else {
        SSERVICELOG_ERROR(MSGID_LOCALE_FILES_LOAD_FAILED, 0, "Failed to load locale files");
        return;
    }

    for (const std::string& dotKeyName : propertyNames) {
        std::vector<std::string> keyNames = split(dotKeyName);
        pbnjson::JValue current = localeInfoObj;

        for (const std::string& keyToken : keyNames) {
            if (keyToken.compare(KEYSTR_VALUE) == 0) {
                continue; // skip first 'value' node because localeInfo file's root is 'value' obj
            }
            pbnjson::JValue child = current[keyToken];
            if (child.isNull()) {
                break;
            }
            std::string childValue;
            switch (child.getType()) {
            case JV_OBJECT:
                current = child; // dig into child
                continue;
            case JV_STR:
                childValue = child.asString();
                keepObj.put(dotKeyName, childValue);
                break;
            case JV_ARRAY:
                keepObj.put(dotKeyName, child);
                break;
            default:
                break;
            }
        }
    }
}

/**
 * This preserves(not be merged) the specific property
 * when the user changes the location of TV.
 *
 * If the value of parameter's object is empty,
 * key/values are created from this.keepObj.
 *
 * For only empty(null) category is applied.
 *
 * @param obj A JSON object like '{category: "", value: {localeInfo: {locales: {UI: "us-US"}}}}'
 */
void ConservativeButler::recoverKeepedProperty(pbnjson::JValue obj)
{
    SSERVICELOG_DEBUG("METHOD ENTRY: recoverKeepedProperty()");
    std::string targetKeyName;
    pbnjson::JValue categoryObj = obj[KEYSTR_CATEGORY];
    if (string("").compare(categoryObj.asString()) != 0)
        return;

    for(pbnjson::JValue::KeyValue kit : keepObj.children()) {
        pbnjson::JValue current = obj;
        pbnjson::JValue child;
        pbnjson::JValue keptVal(kit.second);
        std::string keptKey(kit.first.asString());
        std::vector<std::string> keys = split(keptKey);
        for (const std::string& it : keys) {
            child = current[it];
            if (child.isNull())
                break;
            if (child.isString()) {
                targetKeyName = it; // the child is aimed target. keep current node.
                break;
            }
            if (child.isArray()) {
                targetKeyName = it; // the child is aimed target. keep current node.
                break;
            }
            if (child.isObject()) {
                current = child; // dig into child
                continue;
            }
            break; // might be 'array', 'int', 'double'
        }
        if (child.isString()) {
            std::string oldval = keptVal.asString();
            current.put(targetKeyName, oldval);
        }
        else if (child.isArray()) {
            current.put(targetKeyName, keptVal);
        }
    }
    SSERVICELOG_DEBUG("keep obj() recovered successfully to form mergquery to change country");
    SSERVICELOG_DEBUG("METHOD EXIT: recoverKeepedProperty()");
}
