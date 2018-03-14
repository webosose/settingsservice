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

#ifndef PREFSDB8CONDITION_H
#define PREFSDB8CONDITION_H

#include <pbnjson.hpp>

class PrefsDb8Condition {
    public:

        /**
        @brief Get PrefsDb8Condition instance.
        @return PrefsDb8Condition instance
        */
        static PrefsDb8Condition *instance();

        void loadEnvironmentCondition();

        /**
        @brief Score the item from DB based on device's condition and condition of default value.

        First, If device's condition is empty, selected item should be
        emtpy condition of default value.

        Second, If device's condition is not empty, score the item case by case:

        - Condition has n same property : n + 1 (2 up to 15)
        - Condition is Empty            : 1
        - Condition is not equal        : 0

        @param item a json_object item from DB may contain 'condition' property
        @return score factor of item by condition.
                0: Not-matched,
                1: Non-condition,
                2 to 15: count of matched + 1. (Max up to 14 properties: score 15)
        */
        int scoreByCondition(pbnjson::JValue item);

        /**
        @brief Get current loaded condition.
        @return A condition object as json_object
        */
        pbnjson::JValue getEnvironmentCondition();

    private:
        pbnjson::JValue m_condition;
};

#endif
