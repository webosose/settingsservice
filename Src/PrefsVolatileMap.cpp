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

#include "PrefsVolatileMap.h"

using namespace std;

static PrefsVolatileMap *s_instance = 0;

PrefsVolatileMap *PrefsVolatileMap::instance()
{
    if (!s_instance) {
        s_instance = new PrefsVolatileMap();
    }

    return s_instance;
}

/**
 * Save key/value about volatile key with category, appId into
 * the m_volatileValues nested map.
 *
 * This replace the functionality saving volatile key/value into DB8.
 */
bool PrefsVolatileMap::setVolatileValue(const string &categoryDimension, const string &appId, const string &key, const string &jsonValue)
{
    pair<N3Map::iterator, bool> ret_category;
    pair<N2Map::iterator, bool> ret_appId;
    pair<N1Map::iterator, bool> ret_key;
    bool retval = true;

    // map.insert() ignores new value if already exists(check return_value.second == false)
    ret_category = m_volatileValues.insert(pair<string, N2Map>(categoryDimension, N2Map()));
    ret_appId = ret_category.first->second.insert(pair<string, N1Map>(appId, N1Map()));
    ret_key = ret_appId.first->second.insert(pair<string, string>(key, jsonValue));
    if (ret_key.second == false) {
        if (jsonValue.compare(ret_key.first->second) == 0) {
            retval = false;
        }
        ret_key.first->second = jsonValue;
    }

    return retval;
}

/**
 * Get value for volatile key with category, appId from
 * the m_volatileValues nested map.
 *
 * This replace the functionality getting volatile key/value into DB8.
 */
string PrefsVolatileMap::getVolatileValue(const string &categoryDimension, const string &appId, const string &key)
{
    string retVal;

    N3Map::iterator itCategory = m_volatileValues.find(categoryDimension);
    if (itCategory == m_volatileValues.end()) {
        return retVal;
    }

    N2Map::iterator itAppId = itCategory->second.find(appId);
    if (itAppId == itCategory->second.end()) {
        return retVal;
    }

    N1Map::iterator itKey = itAppId->second.find(key);
    if (itKey == itAppId->second.end()) {
        return retVal;
    }

    retVal = itKey->second;
    return retVal;
}

/**
 * Delete key/value about volatile key with category, appId in
 * the m_volatileValues nested map.
 *
 * This replace the functionality deleting volatile key/value into DB8.
 */
bool PrefsVolatileMap::delVolatileValue(const string &categoryDimension, const string &appId, const string &key)
{
    N3Map::iterator itCategory = m_volatileValues.find(categoryDimension);
    if (itCategory == m_volatileValues.end()) {
        return false;
    }

    N2Map::iterator itAppId = itCategory->second.find(appId);
    if (itAppId == itCategory->second.end()) {
        return false;
    }

    N1Map::iterator itKey = itAppId->second.find(key);
    if (itKey == itAppId->second.end()) {
        return false;
    }

    itAppId->second.erase(key);
    return true;
}

set<string> PrefsVolatileMap::delVolatileKeysByCategory(const string &category)
{
    set<string> ret;

    for (N3Map::iterator itCategory = m_volatileValues.begin(); itCategory != m_volatileValues.end(); itCategory++) {
        const string & dimCategory = itCategory->first;
        size_t pos = dimCategory.find(category);
        if (pos != 0) {
            continue;
        }

        N2Map::iterator itAppId = itCategory->second.find(""); // empty app id
        if (itAppId == itCategory->second.end()) {
            continue;
        }

        for (N1Map::iterator itKey = itAppId->second.begin(); itKey != itAppId->second.end(); itKey++) {
            ret.insert(itKey->first);
        }
    }

    return ret;
}
