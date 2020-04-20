// Copyright (c) 2013-2020 LG Electronics, Inc.
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

#ifndef PREFSDB8DEL_H
#define PREFSDB8DEL_H

#include <map>
#include <string>

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

#include "PrefsTaskMgr.h"
#include "PrefsKeyDescMap.h"
#include "SettingsRecord.h"
#include "SettingsService.h"

class PrefsDb8Del : public PrefsRefCounted, public PrefsFinalize {

 private:
    LSMessage *m_replyMsg;
    MethodCallInfo* m_taskInfo;

    tKindType m_targetKind;
    bool m_removeDefKindFlag;
    bool m_reset_all;
    pbnjson::JValue m_dimensionObj;
    pbnjson::JValue m_dimFilterObj;
    std::string m_app_id;
    std::string m_category;
    std::string m_returnStr;
    std::set < std::string > m_globalKeys;
    std::set < std::string > m_perAppKeys;
    std::set < std::string > m_keyList;
    std::set < std::string > m_errorKeyList;
    std::set<std::string> m_removedKeySet;  // for subscription

    std::string m_errorText;
    LSHandle* m_lsHandle;
    bool m_reply_success;

    std::map < int, SettingsRecord > m_currentSettings;

    bool isRemoveDefKind() { return m_removeDefKindFlag; }
    bool isForBaseKind() { return (m_targetKind == KINDTYPE_BASE); }
    bool isForVolatileKind() { return (m_targetKind == KINDTYPE_VOLATILE); }
    bool isForDefaultKind() { return (m_targetKind == KINDTYPE_DEFAULT); }
    void setTargetKind(tKindType type);
    char *getTargetKindStr();
    pbnjson::JValue getTargetKeyListObj();

    bool handleSettingsRecord(LSHandle *a_lsHandle, pbnjson::JValue a_records);

    /**
     * remove all settings if app_id is matched with specified a_app_id
     *
     * @param a_keys keys that would be removed.
     * @param a_app_id remove a_keys if only app_id is matched.
     * @return true if some key is removed
     */
    bool removeSettings(const std::set<std::string>& a_keys, const std::string& a_app_id);

    /**
     * If any volatile key is found in dimKeyMap(from request), delete it.
     *
     * @param dimKeyMap a map of dimension and keys that may be from request.
     */
    void handleVolatileKey(const CategoryDimKeyListMap &dimKeyMap);

    /**
     * If any volatile key is found in dimKeyMap(from request)
     * with specified category, delete it.
     *
     * @param category category like 'picture'
     */
    void handleVolatileKeyResetAll(const std::string &category);

    /**
     * This nested map contains key/values pairs for subscription.
     * In handleVolatileKey() will save the key only.
     * In cbFindRequestToDefKind()-saveVolatileKeyValueFromDefaultKindForSubscription()
     * will save the default value from DB8
     */
    std::map<std::string, std::map<std::string, std::string> > m_removedVolatileDimKeyMap;

#if (SUBSCRIPTION_TYPE == SUBSCRIPTION_TYPE_FOREACHKEY)
    void PrefsDb8Del::postSubscription();
    void postSubscriptionForEachKey(const std::string& key, pbnjson::JValue val, char* errorText);
#else
    void postSubscription();
    void postSubscriptionSingle(const char *a_caller);
    void postSubscriptionBulk(const char *a_caller);
    void removeMixedKeys(pbnjson::JValue obj, const char* a_caller, bool remove_keys = true);
#endif

    bool sendDelRecordRequest(LSHandle * lsHandle);
    bool sendPutRequest(LSHandle * lsHandle);
    void sendResultReply(LSHandle * lsHandle, bool success, std::string & errorText);
    bool sendFindRequestToDefKind(LSHandle * lsHandle);

    static bool cbFindRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbFindForResetAll(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results );
    static bool cbDelRecordRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbPutRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbFindRequestToDefKind(LSHandle * lsHandle, LSMessage * message, void *data);

 public:
    PrefsDb8Del(const std::set < std::string > &inKeyList, const std::string &inAppId, const std::string &inCategory, pbnjson::JValue inDimension, bool inRemoveDefKind, LSMessage * inMessage)
    {
        m_app_id = inAppId;
        m_category = inCategory;
        m_removeDefKindFlag = inRemoveDefKind;
        m_reset_all = false;
        m_replyMsg = inMessage;
        m_keyList = inKeyList;
        /* After deleting each key, corresponding item also will be removed
         * End of deletion process, unhandled(error) key is remained in m_errorKeyList */
        m_errorKeyList = inKeyList;

        m_dimensionObj = inDimension;
        PrefsKeyDescMap::removeNotUsedDimension(m_dimensionObj);

        setTargetKind(KINDTYPE_BASE);
        LSMessageRef(m_replyMsg);

        m_lsHandle = NULL;
        m_reply_success = false;

        m_taskInfo = NULL;
    }

    ~PrefsDb8Del() {
        if(m_taskInfo) PrefsFactory::instance()->releaseTask(&m_taskInfo);
        LSMessageUnref(m_replyMsg);
    }

    void reference(void);
    void finalize(void);
    std::string getSettingValue(const char *a_key);

    void sendResultReply();     // for callback

    bool sendRequest(LSHandle * lsHandle);
    bool sendRequestResetAll(LSHandle * lsHandle);

    void setGlobalAndPerAppKeys(const std::set < std::string >& globalKeys, const std::set < std::string >& PerAppKeys);
    static bool checkSettingsRecordDirty(const std::pair < int, SettingsRecord >& item);
    static bool checkSettingsRecordDirtyOrVolatile(const std::pair < int, SettingsRecord >& item);
    void setTaskInfo(MethodCallInfo* p) { m_taskInfo = p; };
};

#endif                          // PREFSDB8DEL_H
