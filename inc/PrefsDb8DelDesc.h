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

#ifndef PREFSDB8DELDESC_H
#define PREFSDB8DELDESC_H

#include <list>
#include <map>
#include <string>

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

#include "PrefsTaskMgr.h"

class PrefsDb8DelDesc: public PrefsRefCounted
{
private:
    std::list<std::string> m_keyList;
    std::map<std::string, std::string> m_keyValueListAfterFind; // key:_id

    MethodCallInfo* m_taskInfo;
    std::string m_category;
    LSMessage *m_message;
    std::string m_appId;

    void sendResultReply(LSHandle *lsHandle, bool success, const std::string &errorText);
    bool handleErrorWithRef(LSHandle *lsHandle, const std::string &strErr);

    bool doFindKeys(LSHandle *lsHandle, std::string &errorText);
    static bool cbFindKeys(LSHandle *lsHandle, LSMessage *message, void *data);

    bool doFindKeyValuesDefault(LSHandle *lsHandle, std::string &errorText);
    static bool cbFindKeyValuesDefault(LSHandle *lsHandle, LSMessage *message, void *data);

    bool doDelKeys(LSHandle *lsHandle, std::string &errorText);
    static bool cbDelKeys(LSHandle *lsHandle, LSMessage *message, void *data);

    // for handling Subscription
    std::map<std::string, std::string>* getKeyValueListForSubscription();
    void postSubscription();

public:

    //
    // @brief Constructor of PrefsDb8DelDesc class
    //
    // @param inKeyList
    // @param inCategory
    // @param taskInfo
    // @param message
    //
    PrefsDb8DelDesc(const std::list<std::string> &inKeyList, const std::string &inCategory, MethodCallInfo *taskInfo, LSMessage *message)
    : m_keyList(inKeyList)
    , m_taskInfo(taskInfo)
    , m_category(inCategory)
    , m_message(message)
    {
    }

    //
    // @brief sendRequest
    //
    // @param lsHandle      Instance of LSHandle.
    // @param errorText     if any error is occured, errorText will contain the description of error.
    //
    // @retval              false if any error is occured.
    //
    bool sendRequest(LSHandle *lsHandle, std::string &errorText);

    void setAppId(const std::string& appId) { m_appId = appId; }
};

#endif // PREFSDB8DELDESC_H
