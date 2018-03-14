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

#ifndef PREFSDB8GETVALUES_H
#define PREFSDB8GETVALUES_H

#include <set>
#include <string>

#include <luna-service2/lunaservice.h>

#include "PrefsTaskMgr.h"
#include "SettingsService.h"

class PrefsDb8GetValues : public PrefsRefCounted {

 private:
    LSMessage * m_replyMsg;
    mutable MethodCallInfo* m_taskInfo;

    bool m_subscribe;
    bool m_descFlag;
    int m_totalN;
    pbnjson::JValue m_keyArrayObj;
    std::string m_returnStr;
    std::string m_key;
    std::string m_category;
    std::string m_appId;

#ifdef USE_MEMORY_KEYDESC_KIND
    pbnjson::JValue m_keyDescRoot;

    void setKeyDescInfoObj(pbnjson::JValue obj) {
        m_keyDescRoot = obj;
    }

    pbnjson::JValue getKeyDescInfoObj() { return m_keyDescRoot; };
#endif // USE_MEMORY_KEYDESC_KIND

    static bool cbSendQueryGetOneKey(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbSendQueryGetKeys(LSHandle * lsHandle, LSMessage * message, void *data);

    bool sendRequestOneKey(LSHandle * lsHandle);
    bool sendRequestKeys(LSHandle * lsHandle);

    void regSubscription(LSHandle * lsHandle, const std::string &key, const std::string& a_appId);
    std::set <std::string> getKeyList(pbnjson::JValue keyArrayObj);

 public:
    /**
    @brief Construct instance for handling API request of '/getSystemSettingValues'
    @param inKey A key from the request
    @param inCategory A category from the request
    @param inSubscribe A Subscribe from the request
    @param inMsg Whole request message
    */
    PrefsDb8GetValues(const std::string &inKey, const std::string &inCategory, bool inSubscribe, LSMessage * inMsg) {
        m_key = inKey;
        m_totalN = 1;
        m_subscribe = inSubscribe;
        m_replyMsg = inMsg;
        m_category = inCategory;
        m_descFlag = false;

        LSMessageRef(m_replyMsg);

        m_taskInfo = NULL;
    }

    /**
    @brief Construct instance for handling API request of '/getSystemSettingDesc'
    @param inKeyListObj A json_object for 'keys' from the request
    @param inCategory A category from the request
    @param inSubscribe A Subscribe from the request
    @param inMsg Whole request message
    */
    PrefsDb8GetValues(pbnjson::JValue inKeyListObj, const std::string &inCategory, bool inSubscribe, LSMessage * inMsg) {
        m_keyArrayObj = inKeyListObj.isArray() ? inKeyListObj : pbnjson::Array();

        m_totalN = m_keyArrayObj.arraySize();

        m_subscribe = inSubscribe;
        m_replyMsg = inMsg;
        m_category = inCategory;
        m_descFlag = true;

        LSMessageRef(m_replyMsg);

        m_taskInfo = NULL;
    }

    ~PrefsDb8GetValues() {
        if(m_taskInfo) PrefsFactory::instance()->releaseTask(&m_taskInfo);

        LSMessageUnref(m_replyMsg);
    }

    void printPrefMsg() {
        LSMessagePrint((LSMessage *) m_replyMsg, stdout);
    }

    void sendHandledValues(LSHandle *a_handle, pbnjson::JValue a_values) const;

    bool sendRequest(LSHandle * lsHandle);
    bool isDescCall() {
        return m_descFlag;
    }
    void setTaskInfo(MethodCallInfo* p) { m_taskInfo = p; };
    void setAppId(const std::string& appId) { m_appId = appId; }
};

#endif                          // PREFSDB8GET_H
