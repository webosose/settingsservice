// Copyright (c) 2014-2018 LG Electronics, Inc.
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

#define ENVIRONMENTCONDITION_PATH "/var/luna/preferences/environmentCondition"

#include <fstream>

#include "JSONUtils.h"
#include "Logging.h"
#include "SettingsService.h"
#include "PrefsDb8Condition.h"

static PrefsDb8Condition *s_instance = 0;

PrefsDb8Condition *PrefsDb8Condition::instance()
{
    if (!s_instance) {
        s_instance = new PrefsDb8Condition;
    }
    return s_instance;
}

/**
@brief Load the system's condition to apply

This function should be run when SettingsService started.
The condition will be not changed device running. The change of condition
follows reboot, so the SettingsService will be restarted.
Now this function uses ENVIRONMENTCONDITION_PATH file to load the condition,
but another option like using arguments maybe also selected.
Anyway, the loaded condition is stored in this.m_condition as json_object.
*/
void PrefsDb8Condition::loadEnvironmentCondition()
{
    pbnjson::JValue condition;
    std::ifstream ifs(ENVIRONMENTCONDITION_PATH);
    if (ifs.is_open()) {
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        condition = pbnjson::JDomParser::fromString(content);
        if (!condition.isObject()) {
            SSERVICELOG_WARNING(MSGID_CONDITION_FILE_FAILTO_LOAD, 0, "Fail to parse condition file as JSON");
            return;
        }
    } else {
        SSERVICELOG_WARNING(MSGID_CONDITION_FILE_NOTFOUND, 0, "Not found condition file");
        return;
    }

    m_condition = condition;
    SSERVICELOG_DEBUG("PrefsDb8Condition::%s(%d): %s",
        __FUNCTION__, __LINE__, m_condition.stringify().c_str());
}

static int countOfEqualProperty(pbnjson::JValue a, pbnjson::JValue b)
{
    if (a.isNull() || b.isNull()) {
        return 0;
    }

    int equal_count_a = 0;
    {
        for(pbnjson::JValue::KeyValue it : a.children()) {
            pbnjson::JValue key(it.first);
            if (it.second == b[key.asString()]) {
                ++equal_count_a;
            }
        }
    }

    return equal_count_a;
}

/**
Example Case #1

- EnvironmentCondition: {Panel:OLED, UHD:true}
- NULL       scores 1
- {LCD}      scores 0
- {OLED}     scores 2
- {UHD}      scores 2
- {OLED,UHD} scores 3

Example Case #2

- EnvironmentCondition: {Panel:OLED, UHD:true, ClearPlus:on}
- NULL                                 scores 1
- {PDP}                                scores 0
- {OLED}                               scores 2
- {UHD}                                scores 2
- {OLED,UHD}                           scores 3
- {OLED,UHD,ClearPlusOn}               scores 4
- {OLED,UHD,ClearPlusOff}              scores 0
- {OLED,UHD,AnotherOption}             scores 0
- {OLED,UHD,ClearPlusOn,AnotherOption} scores 0
*/
int PrefsDb8Condition::scoreByCondition(pbnjson::JValue item)
{
    static const int NotMatch = 0, NonCond = 1;

    pbnjson::JValue envCondObj = getEnvironmentCondition();
    pbnjson::JValue itemCondObj = item[KEYSTR_CONDITION];

    if (!itemCondObj.isObject() || itemCondObj.objectSize() == 0) {
        return NonCond;
    }

    int score = countOfEqualProperty(itemCondObj, envCondObj);
    if (score == 0) {
        return NotMatch;
    }

    return score + NonCond; // Make Bigger than NonCond. Score Must > 0
}

pbnjson::JValue PrefsDb8Condition::getEnvironmentCondition()
{
    return m_condition;
}
