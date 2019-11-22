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

#ifndef PREFSKEYDESCMAP_H
#define PREFSKEYDESCMAP_H

#include <list>
#include <map>
#include <string>
#include <mutex>

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

#include "DefaultBson.h"
#include "PrefsTaskMgr.h"
#include "SettingsService.h"

class PrefsDb8Set;
class PrefsKeyDescMap;

/**
 * Identifier for cache map.
 * Old one is using only key, but new is using both key and AppId.
 */
class DescriptionCacheId {
public:
    std::string m_key;
    std::string m_appId;

    bool operator< (const DescriptionCacheId& rhs) const
    {
        return m_key != rhs.m_key ? m_key < rhs.m_key : m_appId < rhs.m_appId;
    }
};

/**
 * Cache map for default/system kinds
 *
 * @sa DescriptionCacheId
 */
class DescriptionCacheMap : public std::map<DescriptionCacheId, pbnjson::JValue> {
public:
    const std::string THE_VERY_FIRST_APP_ID = "";

    /**
     * Check key existence in the cache map regardless of appId
     */
    bool hasKey(const std::string& key) const
    {
        DescriptionCacheMap::const_iterator it = lower_bound({key, THE_VERY_FIRST_APP_ID});
        return it == end() ? false : it->first.m_key == key;
    }

    /**
     * Erase items in the cache map by key regardless of appId
     *
     * @return false if any item was not deleted, otherwise true.
     */
    bool eraseByKey(const std::string& key)
    {
        bool deleted = false;
        for (;;) {
            DescriptionCacheMap::iterator it = lower_bound({key, THE_VERY_FIRST_APP_ID});
            if (it == end() || it->first.m_key != key)
                break;
            deleted = true;
            erase(it);
        }
        return deleted;
    }
};

//typedef std::map <std::set<std::string>, std::set<std::string> > DimSetKeyListMap;
//typedef std::map <std::string, DimSetKeyListMap > CategoryMap;
typedef std::map <std::string /*key*/, std::vector<std::string> /*dimensionList*/> KeyDimensionMap;
typedef std::map <std::string, std::set<std::string> > CategoryMap;
typedef std::map <std::string, pbnjson::JValue>            KeyMap;
typedef std::map <std::string, std::list<std::string> >  DimKeyMap;     // dimension keys and it's keys in dimension
typedef std::map <std::string, std::string>             DimKeyValueMap; // dimension keys and it's value
typedef std::map <std::string, std::set<std::string> > DimKeyValueListMap;
typedef std::map <std::string, std::set<std::string> > CategoryDimKeyListMap;
typedef std::map <std::string, std::list<std::string> > DimFormatMap;
// This type is for parsing description kind by country.
typedef std::map<std::string, pbnjson::JValue>     CountryJsonMap;
typedef std::map<DescriptionCacheId, CountryJsonMap > DescInfoMap;

class PrefsDebugUtil;

class ConservativeButler;

class PrefsKeyDescMap {
    public:
        typedef bool (*Callback)(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results);

        static PrefsKeyDescMap *instance();

        PrefsKeyDescMap();
        ~PrefsKeyDescMap();
        PrefsKeyDescMap(const PrefsKeyDescMap&) =delete;
        PrefsKeyDescMap& operator=(const PrefsKeyDescMap&) =delete;

        bool isInit() { return m_initFlag; }
        void initialize();

        // manipulate description cache
        bool addKeyDesc(const std::string &key, pbnjson::JValue inItem, bool a_def, const std::string& appId = GLOBAL_APP_ID); // insert or up
        bool addKeyDescForce(const std::string &key, pbnjson::JValue inItem, bool a_def);
        bool delKeyDesc(const std::string &key, const std::string& appId = GLOBAL_APP_ID); // remove item.
        bool resetKeyDesc(const std::string& key, const std::string& appId = GLOBAL_APP_ID);

        bool isNewKey(const std::string& key) const;
        bool isVolatileKey(const std::string key) const;
        bool isInDimKeyList(const std::set<std::string>& keyList) const;
        bool isSameCategory(const std::string& key, const std::string& category) const;
        bool isSameDimension(const std::string& key, pbnjson::JValue dimObj) const;
        std::string getDbType(const std::string& key) const;
        std::set<std::string> getKeysInCategory(const std::string &category) const;
        bool existPerAppDescription(const std::string& a_category, const std::string& a_appId, const std::string a_key) const;
        void splitKeysIntoGlobalOrPerAppByDescription(const std::set<std::string>& allKeys, const std::string& category,
                const std::string& appId, /*out*/ std::set<std::string>& globalKeys, /*out*/ std::set<std::string>& perAppKeys) const;
        pbnjson::JValue getKeyDesc(const std::string& category, const std::set<std::string>& keyList, const std::string& appId) const;
        pbnjson::JValue getKeyDescByCategory(const std::string& category) const;
        pbnjson::JValue getKeyDescByKeys(std::set<std::string>& keyList) const;
        pbnjson::JValue getDimKeyValueObj(const std::string &category, pbnjson::JValue dimObj, const std::string &key="", pbnjson::JValue a_input = pbnjson::JValue());
        bool hasCountryVar(const std::string &a_key) const;
        bool needStrictValueCheck(const std::string &a_key) const;
        bool updateDimKeyValue(PrefsFinalize* a_finalize, const std::set<std::string>& keyList, pbnjson::JValue curDimObj);

        bool getCategory(const std::string &key, std::string &category) const;
        bool getCategoryDim(const std::string& category, std::string& categoryDim) const;
        bool getCategoryDim(const std::string &key, std::string& categoryDim, const DimKeyValueMap &reqDimKeyValueMap) const;
        CategoryDimKeyListMap getCategoryKeyListMap(const std::string& category, pbnjson::JValue dimObj, const std::set<std::string>& inKeyList) const;
        CategoryDimKeyListMap getCategoryKeyListMapAll(const std::string &category, const std::set<std::string>& inKeyList) const;

        void setServiceHandle(LSHandle* serviceHandle) { m_serviceHandle = serviceHandle; }
        LSHandle *getServiceHandle() const { return m_serviceHandle; }
        bool initKeyDescMap(void);
        void initKeyDescMapByCountry(void);

        void readCountryDesc(std::list<pbnjson::JValue > &a_result) const;
        bool populateCountryDesc(void);
        bool populateCountrySettings(void);
        bool createNullCategory(void);
        bool findModifiedCategory(void);
        bool gatherCountrySettingsJSON(const std::set<std::string>& a_categories, Callback a_func, void *a_type);
        bool gatherCountrySettings(const std::set<std::string> &a_categories, Callback a_func, void *a_type);
        bool mergeCountryDesc(pbnjson::JValue descArray);
        bool mergeCountrySettings(pbnjson::JValue descArray);

        void setCountryCode(const std::string& a_country);
        const std::string& getCountryCode() const;

        void setCountryGroupCode(const std::string& a_countryGroup);
        const std::string& getCountryGroupCode() const;

        void sett_populated(void) { m_cntr_sett_populated = true; }
        void desc_populated(void) { m_cntr_desc_populated = true; }
        const DimKeyValueMap& getCurrentDimensionValues() const;
        bool isCurrentDimension(pbnjson::JValue a_dimObj) const;
        pbnjson::JValue  genDescFromCache(const std::string &key, const std::string &appId = GLOBAL_APP_ID) const;
        std::vector<std::string> getDimensionInfo(void) const;
        bool getDimensionsByKey(const std::string &key, std::vector<std::string> *out) const;
        std::set<std::string> findDependentDimensions(const std::string& a_key) const;

        std::string categoryDim2category(std::string &a_category_dim);
        pbnjson::JValue categoryDim2dimObj(std::string &a_category_dim);

        void loadExceptionAppList();
        bool isExceptionAppList(const std::string&) const;
        std::string getAppIdforSubscribe(LSMessage* message);
        void removeSubscription(LSMessage * a_message);
        static bool cbSubscriptionCancel(LSHandle * a_handle, LSMessage * a_message, void *a_data);
        void setAppIdforSubscribe(LSMessage* message, std::string app_id);

        static void removeNotUsedDimension(pbnjson::JValue a_dimension);

        static bool cbUpdatePreferenceFiles(void *a_thiz_class, void *a_userdata, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_result);
        static pbnjson::JValue FilterForVolatile(pbnjson::JValue a_result);

    protected:
        bool isCountryIncluded(const std::string& a_countryList, const std::string& a_country) const
        {
             return (a_countryList.find(a_country) != std::string::npos);
        }

    private:
        typedef enum {
            DescKindType_eFile, /* deprecated */
            DescKindType_eDefault,
            DescKindType_eSysMain,
            DescKindType_eOverride /* deprecated */
        } DescKindType;

        bool m_initFlag;
        bool m_doFirstFlag;        // for the first time to execute.

        LSHandle* m_serviceHandle;

        // tempory desc kind json_object
        std::list<pbnjson::JValue> m_descKindFileObj;
        std::list<pbnjson::JValue> m_descKindDefObj;
        std::list<pbnjson::JValue> m_descKindMainObj;
        std::list<pbnjson::JValue> m_descKindOverObj;
        std::list<pbnjson::JValue> m_batchParams;

        // description info
        KeyMap m_fileDescCache;
        KeyMap m_overrideDescCahce;
        mutable DefaultBson m_fileDescDefaultBson;
        DefaultBson m_overrideDescDefaultBson;

        // description cache new
        mutable DescriptionCacheMap m_defaultDescCache;
        mutable DescriptionCacheMap m_systemDescCache;

        // description info lock
        mutable std::mutex m_lock_desc_json;

        // subscription app info lock
        mutable std::mutex m_lock_subsAppId;

        CategoryMap m_categoryMap;                  // A cache for category:keyList
        mutable KeyDimensionMap m_keyDimensionMap;  // A cache for key:dimList
        mutable std::mutex m_lock_keyDimensionMap; // A mutex for m_keyDimensionMap

        // dimension info
        DimKeyValueMap  m_dimKeyValueMap;
        DimKeyValueListMap m_dimKeyValueListMap;
        DimKeyMap   m_indepDimKeyMap;
        DimKeyMap   m_depDimKeyMapD1;
        DimKeyMap   m_depDimKeyMapD2;
        DimFormatMap    m_dimFormatMap;

        // volatile keys
        std::set<std::string> m_volatileKeys;
        std::set<std::string> m_perAppKeys;
        std::set<std::string> m_mixedPerAppKeys;
        std::set<std::string> m_exceptionAppKeys;
        std::set<std::string> m_countryVarKeys;
        std::set<std::string> m_strictValueCheckKeys;

        // country info
        std::string m_countryCode;
        std::string m_countryGroupCode;
        bool m_cntr_desc_populated;
        bool m_cntr_sett_populated;
        bool m_initByDimChange;
        // categories and keys modified by user (on system:1 kind).
        // this key's value will be kept(skip on over-writing country default values)
        // must be initialized by initCategoryKeysMapInSystemKind
        std::map<std::string, std::set<std::string> > m_categoryKeysMapInSystemKind;

        // temporary date for DB query
        std::set<std::string> m_targetKeyListIndep;
        std::set<std::string> m_targetKeyListD1;
        std::string    m_targetKindIndep;
        std::string    m_targetKindD1;

        PrefsFinalize* m_finalize;

        ConservativeButler *m_conservativeButler;

        std::vector<std::string> m_exceptionApplist;
        std::map <LSMessage*, std::string> m_subsAppList;

        static void removeIdInArrayEx(pbnjson::JValue root); // remove '_id' in values for arrayExt type
        static void removeDB8Props(pbnjson::JValue root);

        void setInitFlag() { m_initFlag = true; }
        void resetInitFlag() { m_initFlag = false; }

        void setKeyDescData();			// parsing json_object to Memory.
        void updateKeyDescData();
        void insertOrUpdateDescKindObj(const std::string &key, const std::string &app_id, const std::string &country, DescInfoMap &keyMap, pbnjson::JValue newItemObj) const;

        void initCategoryKeysMapInSystemKind();

        /**
         * Parse JSON object that contains multiple Description item into map by-country and by-category
         *
         * @param descKindObj    A JSON object, that has 'results' property,
         *                       from the DB8(desc.default, desc.system) or
         *                       from the cache(description.bson and override.bson)
         * @param tmpDescInfoMap Description items by-country and by-category
         * @param appId          If appId is passed(GLOBAL_APP_ID or else), PerApp filtering is enabled for bson cache.
         *                       If appId is not passed, PerApp filtering is disabled for db8 cache.
         */
        void parsingDescKindObj(pbnjson::JValue descKindObj, DescInfoMap& tmpDescInfoMap, const std::string& appId = UNSPECIFIED_APP_ID) const;
        bool setDescKindObj(const DescKindType a_type, const std::list<pbnjson::JValue>* inKeyDescInfo);
        bool overwriteDescKindObj(pbnjson::JValue inKeyDescInfo);
        void resetDescKindObj();
        bool setDimensionFormat();
        bool setDimensionKeyFromDefault();
        bool setDimensionKeyValueList(void);
        void initDimensionValues(const std::string& a_cat, const std::set<std::string>& a_keys);
        void initDimensionValues(void);
        bool setDimensionValues();
        void getDimKeyList(int dimKeyType, std::set<std::string>& keyList) const;
        bool matchedRequestDimensions(const DimKeyValueMap& a_dimKeyValues, const std::vector<std::string>& a_dimensions) const;
        pbnjson::JValue  getEmptyDimObj(const std::string &category) const;
        bool removeKeyInCategoryMap(const std::string &category, const std::string &key);
        bool insertKeyToCategoryMap(const std::string &category, const std::string &key);
        void stackCategoryDimStr(CategoryDimKeyListMap &map, const std::string &a_key, const std::string &a_category, const std::string &fmt, const std::string &str) const;

        std::string getOverrideKey(const std::string &key) const;

        void sendUpdatePreferenceFiles(void);

        // for setting dimension keys
        pbnjson::JValue  createCountryCodeJsonQuery(const std::string& targetKind) const;
        pbnjson::JValue  createCountryJsonQuery() const;

        pbnjson::JValue  getLocaleBatchItem(const std::string& kindName, const std::string& catName, const std::string& countryCode);
        pbnjson::JValue  createIndepJsonQuery(const std::set<std::string>& keyList, const std::string& targetKind);
        pbnjson::JValue createDepJsonQueryD1(const std::set<std::string>& keyList, const std::string& targetKind);
        void buildDescriptionCache(DescriptionCacheMap &dst, const std::list<pbnjson::JValue> &src, const std::string &country) /*const*/;
        void dumpDescriptionCache(KeyMap &key_map);
        bool sendLoadKeyDescDefaultKindRequest();
        bool sendLoadKeyDescMainKindRequest();
        void loadDescFilesForEachApp(pbnjson::JValue jArrayObj);
        bool sendCountryCodeRequest();
        void handleVolatileKeyInDimension(std::set<std::string>& a_keyList);
        bool sendDimensionValuesIndepRequest(const std::set<std::string>& keyList, const char* targetKind);
        bool sendDimensionValuesDepD1Request(const std::set<std::string>& keyList, const char* targetKind);

        // callback function
        //
        static bool cbLoadKeyDescDefaultKindRequest(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results );
        static bool cbLoadKeyDescMainKindRequest(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results );
        static bool cbCountryCodeRequest(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbDimensionValuesIndepRequest(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbDimensionValuesDepD1Request(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbFindCountryDesc(void *a_thiz_class, void *a_userdata, std::list<pbnjson::JValue>& a_results);
        static bool cbCreateNullCategory(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbFindCountrySettings(void *a_thiz_class, void *a_userdata, std::list<pbnjson::JValue>& a_results);
        static bool cbFindModifiedCategory(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results);
        static bool cbGatherCountrySettings(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results);
        static bool cbMergeCountryDesc(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbMergeCountrySettings(LSHandle * lsHandle, LSMessage * message, void *data);

        void tmpFileLogDimInfo(const char* fileName);
        void tmpFileLog(const char* fileName);

    private:
        pbnjson::JValue queryDescriptionByKey(const std::string &key, const std::string &country, const DefaultBson &m_fileDescDefaultBson, const std::string &appId = GLOBAL_APP_ID) const;

    friend class PrefsDebugUtil;
};

class ConservativeButler {
    public:
        ConservativeButler();
        ~ConservativeButler();
        pbnjson::JValue intervene();
        void keep();
        void recoverKeepedProperty(pbnjson::JValue obj);
        void clear();
    private:
        std::list<std::string> propertyNames;
        pbnjson::JValue keepObj;
};

#endif                          /* PREFSFACTORY_H */
