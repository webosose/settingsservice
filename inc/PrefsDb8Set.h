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

#ifndef PREFSDB8SET_H
#define PREFSDB8SET_H

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

#include "PrefsKeyDescMap.h"
#include "SettingsService.h"

class PrefsDebugUtil;

class PrefsDb8Set : public PrefsRefCounted, public PrefsFinalize {

 private:
    LSMessage *m_replyMsg;
    MethodCallInfo* m_taskInfo;
    unsigned int m_totalN;

    pbnjson::JValue m_keyListObj;
    pbnjson::JValue m_dimensionObj;
    pbnjson::JValue m_successKeyListObj;
    pbnjson::JValue m_successKeyListVolatileObj;
    std::string m_app_id;
    std::string m_category;
    std::string m_country;
    std::string m_subscribeAppId;

    /* Following keyList is created by KeyDescription Analyzer.
     * All Keys are categorized into non-volatile and volatile at first.
     *   m_successKeyList keep non-volatile settings
     *   m_successKeyListVolatile keep volatile settings
     *   m_errorKeyList keep invalid settings
     * Two CategoryDimKeyListMap are used while updating (merge and put)
     * After updating, processed result is categories into following
     *   m_mergeFailKeyList contains all failed value
     *   m_storeDb8KeyList contains all stored value */
    std::set < std::string > m_successKeyList;

    std::set<std::string> m_globalKeysFromPerAppRequest;

    /**
     * Contains KeyDescInfo for volatile keys, this will be filled in parsingKeyDescInfo().
     */
    std::set < std::string > m_successKeyListVolatile;
    std::set < std::string > m_errorKeyList;
    std::set < std::string > m_mergeFailKeyList;
    std::set < std::string > m_toBeNotifiedKeyList;
    CategoryDimKeyListMap m_requestCategoryDimKeysMap;
    CategoryDimKeyListMap m_storedCategoryDimKeyMap;

#ifdef USE_MEMORY_KEYDESC_KIND
    pbnjson::JValue m_keyDescRoot;
#endif // USE_MEMORY_KEYDESC_KIND

    bool m_setAll;
    bool m_storeFlag;
    bool m_notifyFlag;
    bool m_defKindFlag;
    bool m_notifySelf;
    bool m_valueCheck;

    // for reply
    std::string m_errorText;
    LSHandle* m_lsHandle;

    LSMessage *getLSMsgHandle() {
        return m_replyMsg;
    }

    bool isForGlobalSetting() {
        return m_app_id.empty();
    }

    bool sendGetValuesRequestMemory(LSHandle * lsHandle);
    bool sendGetValuesRequest(LSHandle * lsHandle);
    bool sendMergeRequest(LSHandle * lsHandle);
    bool sendPutRequest(LSHandle * lsHandle, const char* kind, LSFilterFunc callback);
    bool sendMergeRequestDefKind(LSHandle * lsHandle);
    bool sendResultReply(LSHandle * lsHandle, std::string & errorText);
    void postSubscription(const char *a_sender);
    void postSubscriptionSingle(const char *a_senderToken, const char *a_senderId);
    void postSubscriptionBulk(const char *a_senderToken, const char *a_senderId);
    bool parsingKeyDescInfo(pbnjson::JValue root, std::string &errorText);
    bool analyzeMergeBatchResult(pbnjson::JValue root);
    void updateModifiedKeyInfo(void);
    void storeDone(const CategoryDimKeyListMap::value_type& done);
    pbnjson::JValue jsonMergeBatchItem(const std::string &categoryName, const std::set < std::string >& keyList, const std::string& targetKindName, pbnjson::JValue allKeyListObj, const std::string& appId);
    pbnjson::JValue jsonPutObject(const std::string &categoryName, const std::set < std::string >& keyList, const std::string& targetKindName, pbnjson::JValue allKeyListObj, const std::string& appId);
    void fillPutRequest(const char* kind, pbnjson::JValue jsonObjParam);
    /**
     * For each key from CategoryDimKeyListMap based on the requst,
     * save the key/value
     * and save the key into m_toBeNotifiedKeyList to post subscription
     * (the value for subscription is from m_keyListObj from the request)
     *
     * @return true always
     */
    bool handleVolatileKey();

    static bool cbGetValuesRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbMergeRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbPutRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbMergeRequestDefKind(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbPutRequestDefKind(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbMergeRequestVolatile(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbPutRequestVolatile(LSHandle * lsHandle, LSMessage * message, void *data);

public:
    PrefsDb8Set(pbnjson::JValue inKeyList, const std::string &inAppId, const std::string &inCategory, pbnjson::JValue inDimension, LSMessage * inMessage) :
        m_replyMsg(inMessage),
        m_taskInfo(NULL),
        m_keyListObj(inKeyList),
        m_dimensionObj(inDimension),
        m_successKeyListObj(pbnjson::Object()),
        m_successKeyListVolatileObj(pbnjson::Object()),
        m_app_id(inAppId),
        m_category(inCategory),
        m_setAll(false),
        m_storeFlag(true),
        m_notifyFlag(true),
        m_defKindFlag(false),
        m_notifySelf(true),
        m_valueCheck(true),
        m_lsHandle(NULL)
    {
        m_totalN = m_keyListObj.objectSize();
        PrefsKeyDescMap::removeNotUsedDimension(m_dimensionObj);
        LSMessageRef(m_replyMsg);
    };

    ~PrefsDb8Set()
    {
        if(m_taskInfo) PrefsFactory::instance()->releaseTask(&m_taskInfo);

        m_successKeyList.clear();
        m_successKeyListVolatile.clear();
        m_errorKeyList.clear();

        LSMessageUnref(m_replyMsg);
    }

    void reference(void);
    void finalize(void);
    std::string getSettingValue(const char *);

    void sendResultReply();     // for callback

    bool sendRequest(LSHandle * lsHandle);
    void setNotifyFlag(bool inNotify) {
        m_notifyFlag = inNotify;
    }
    void setStoreFlag(bool inStore) {
        m_storeFlag = inStore;
    }
    void setAllFlag(bool setAll) {
        m_setAll = setAll;
    }
    void setDefKindFlag() {
        m_defKindFlag = true;
    }
    void setCountry(std::string country) {
        m_country = country;
    }

    void setNotifySelf(bool a_flag)
    {
        m_notifySelf = a_flag;
    }

    void setValueCheck(bool a_flag)
    {
        m_valueCheck = a_flag;
    }

    void setTaskInfo(MethodCallInfo* p) { m_taskInfo = p; };

    friend class PrefsDebugUtil;
};
#endif                          // PREFSDB8GET_H
