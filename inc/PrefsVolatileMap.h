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

#ifndef PREFSVOLATILEMAP_H
#define PREFSVOLATILEMAP_H

#include <map>
#include <set>
#include <string>

typedef std::map<std::string, std::string> N1Map;
typedef std::map<std::string, N1Map      > N2Map;
typedef std::map<std::string, N2Map      > N3Map;

/**
 * Manage volatile key/value in memory.
 */
class PrefsVolatileMap {
private:
    N3Map m_volatileValues;
public:
    static PrefsVolatileMap *instance();

    /**
     * Set a volatile value saved in memory.
     *
     * @param categoryDimension Like 'picture$dtv.normal.2d'
     * @param appId             AppId from Request.
     * @param key               A name of key like 'colorFilter'
     * @param value             A JSON string of the value of the key.
     *
     * @return                  true if the value is updated.
     */
    bool setVolatileValue(const std::string &categoryDimension, const std::string &appId, const std::string &key, const std::string &jsonValue);

    /**
     * Get a volatile value saved in memory.
     *
     * @param  categoryDimension Like 'picture$dtv.normal.2d'
     * @param  appId             AppId from Request.
     * @param  key               A name of key.
     *
     * @return                   A value for JSON Ojbect. Empty String if not found or error.
     */
    std::string getVolatileValue(const std::string &categoryDimension, const std::string &appId, const std::string &key);

    /**
     * Delete a volatile value saved in memory.
     *
     * @param categoryDimension Like 'picture$dtv.normal.2d'
     * @param appId             AppId from Request.
     * @param key               A name of key.
     *
     * @return                  true if key is existed and deleted.
     */
    bool delVolatileValue(const std::string &categoryDimension, const std::string &appId, const std::string &key);

    /**
     * Delete volatile key/value saved in memory based on category name.
     * This method for 'resetAll' flag.
     *
     * @param categoryDimension Like 'picture'
     *
     * @return                  set of string of deleted keys.
     */
    std::set<std::string> delVolatileKeysByCategory(const std::string &category);
};

#endif // PREFSVOLATILEMAP_H
