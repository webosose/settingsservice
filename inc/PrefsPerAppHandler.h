// Copyright (c) 2015-2018 LG Electronics, Inc.
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

#include <map>
#include <mutex>
#include <string>

#include <luna-service2/lunaservice.h>

#include "PrefsNotifier.h"

class PrefsPerAppHandler
{
private:
    PrefsPerAppHandler();
    ~PrefsPerAppHandler();

    enum class State { None, ForValue, ForDesc, RemovePerApp, FinishTask };

    LSHandle* m_lsHandle;
    LSError m_lsError;

    State m_nextFunc;
    bool next(); // task func runner

    std::string m_currAppId;
    std::string m_prevAppId;
    std::string m_removedAppId;

    MethodCallInfo * m_taskInfo;

    /**
     * Registered PerApp Subscriptions for Value
     */
    std::map<std::string, std::set<LSMessageElem>> m_categorySubscriptionMessagesMapForValue;

    /**
     * Registered PerApp Subscriptions for Description
     */
    std::map<std::string, std::set<LSMessageElem>> m_keySubscriptionMessagesMapForDesc;

    /**
     * mutex for m_categorySubscriptionMessagesMapForValue,
     *           m_keySubscriptionMessagesMapForDesc
     */
    mutable std::recursive_mutex m_container_mutex;

    // task func and its callbacks
    // naming rules:
    //   {do,cb}Send[Noun]() - for including db8 call - callback
    //   doHandle[Noun] - for not including db8 call - no callback
    bool        doSendValueQuery();
    bool        doHandleDescQuery();
    bool        doRemovePerApp();
    bool        doFinishTask();
    static bool cbSendValueQuery(LSHandle* lsHandle, LSMessage* message, void* ctx);
    static bool cbRemovePerApp(LSHandle* lsHandle, LSMessage* lsMessage, void* ctx);

    // ExcludeAppIdList

    std::set<std::string> m_excludeAppIdList;
    bool m_excludeAppIdListLoaded = false;
    std::set<std::string>& getExcludeAppIdList();
    void loadExcludeAppIdList();
    bool isMessageInExclude(LSMessage* lsMessage);

    // Find keys which is in a_category and dbtype is a_dbType.
    std::set<std::string> findKeys(const std::string& a_category, const std::string& a_dbType) const;

public:
    static PrefsPerAppHandler& instance();
    void setLSHandle(LSHandle* lsHandle) { m_lsHandle = lsHandle; }
    void handleAppChange(const std::string& currentAppId, const std::string& prevAppId);
    void removePerAppSettings(const std::string& app_id);

    // add/del subscription
    void addSubscription(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage);
    void addDescSubscription(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage);
    void delSubscription(LSMessage* lsMessage);

    void setTaskInfo(MethodCallInfo * a_taskInfo) { m_taskInfo = a_taskInfo; }
    MethodCallInfo* getTaskInfo() const { return m_taskInfo; }
};
