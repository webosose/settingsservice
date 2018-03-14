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

#include <cstdio>

#include "InfoLogger.h"
#include "Logging.h"

InfoLogger *InfoLogger::s_logger = NULL;

InfoLogger::InfoLogger(void)
{
    ExceptionKeys keys;

    m_e_cat.insert(ExceptionCategory::value_type("dimensionInfo",keys));

    keys.clear();
    keys.insert("systemPin");
    m_e_cat.insert(ExceptionCategory::value_type("lock",keys));

    keys.clear();
    keys.insert("deviceName");
    m_e_cat.insert(ExceptionCategory::value_type("network",keys));
}

void InfoLogger::logSetChange(const std::string &cat, pbnjson::JValue settings, const char* caller) const
{
    bool write_all = false;
    ExceptionCategory::const_iterator e_cat;

    e_cat = m_e_cat.find(cat);

    /* log exception, entire category or specific key */

    if ( e_cat == m_e_cat.end() )
        write_all = true;
    else if ( e_cat->second.size() == 0 )
        return;

    for (pbnjson::JValue::KeyValue it : settings.children()) {
        std::string keyStr(it.first.asString());
        std::string valStr(it.second.stringify());
        if ( write_all || (e_cat->second.find(keyStr) == e_cat->second.end()) ) {
            SSERVICELOG_INFO(MSGID_SYSTEM_SETTING_CHANGED, 4,
                    PMLOGKS("Caller", caller ? caller : "None"),
                    PMLOGKS("Category", cat.c_str()),
                    PMLOGKS("Key",keyStr.c_str()),
                    PMLOGJSON("Option", valStr.c_str()),
                    "Settings changed");
        } else if (e_cat->second.find(keyStr) != e_cat->second.end()) {
            SSERVICELOG_INFO(MSGID_SYSTEM_SETTING_CHANGED, 3,
                    PMLOGKS("Caller", caller ? caller : "None"),
                    PMLOGKS("Category", cat.c_str()),
                    PMLOGKS("Key", keyStr.c_str()),
                    "Settings changed");
        }
    }

    return;
}


