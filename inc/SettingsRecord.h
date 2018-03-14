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

#ifndef __SETTINGSRECORD__
#define __SETTINGSRECORD__

#include <map>
#include <set>
#include <string>

#include <pbnjson.hpp>
#include <luna-service2/lunaservice.h>

class SettingsRecord {
    private:
        pbnjson::JValue m_valuesObj;
        pbnjson::JValue m_dimensionObj;
        std::string m_id;
        std::string m_kindName;
        std::string m_appId;
        std::string m_country;

        std::string m_categoryDim;
        std::string m_category;
        std::set <std::string> m_removedKeyList;

        bool m_dirty;           // flag for a 'del' request to DB
        bool m_isVolatile;

    public:
        SettingsRecord(void) : m_dirty(false), m_isVolatile(false) {};

        pbnjson::JValue getValuesObj(void) const { return m_valuesObj; }
        pbnjson::JValue getDimObj(void) const { return m_dimensionObj; }
        const std::string &getKindName(void) const { return m_kindName; }
        const std::string &getAppId(void) const { return m_appId; }
        const std::string &getCountry(void) const { return m_country; }
        const std::string &getCategoryDim(void) const { return m_categoryDim; }
        const std::string &getCategory(void) const { return m_category; }
        const std::set<std::string> &getRemovedKeys(void) const { return m_removedKeyList; }
        bool isRemovedMixedType(void) const;
        void fixCategoryForMixedType(void);

        void clear(void);
        bool is_dirty(void) const { return m_dirty; };
        bool isVolatile(void) const { return m_isVolatile; };
        bool hasDimension(void) const { return (!m_dimensionObj.isNull()); }
        bool loadJsonObj(pbnjson::JValue );
        bool loadVolatileKeys(const std::string &categoryDim, const std::map<std::string, std::string> &volatileKeyMap);
        bool dimMatched(pbnjson::JValue dimObj) const;
        std::set < std::string > removeKeys(const std::set< std::string >& keys);
        std::set < std::string > removeAllKeys(void);
        pbnjson::JValue genDelQueryById(void) const;
        pbnjson::JValue genObjForPut(void) const;
        pbnjson::JValue genQueryForDefKind(void) const;
        int updateSuccessValueObj(pbnjson::JValue valueObj, std::string & errorText);
        int parsingResult(pbnjson::JValue resultArray, std::string & errorText, bool a_filterMixed);
};

#endif
