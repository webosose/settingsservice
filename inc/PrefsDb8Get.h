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

#ifndef PREFSDB8GET_H
#define PREFSDB8GET_H

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

#include "PrefsKeyDescMap.h"

class PrefsDb8Get : public PrefsRefCounted {
public:
    typedef bool (*Callback)(void *a_thiz_class, void *a_userdata, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_result );

private:
    LSMessage * m_replyMsg;

    // request values from app
    MethodCallInfo* m_taskInfo;
    std::string m_reqApp_id;
    std::string m_category;
    bool m_subscribe;
    bool m_forCategoryFlag;
    pbnjson::JValue m_dimensionObj;

    /**
     * Flag for handling getSystemSettingFactoryValue API.
     * If true, skip some main kind.
     */
    bool m_isFactoryValueRequest;
    /**
     * Flag for avoiding cache access.
     * This flag would be set true when cache update or dimension notify
     */
    bool m_forceDbSync;

    // bellow values will be modified in process.
    unsigned int m_itemN;
    unsigned int m_totalN;
    std::string m_app_id;
    std::set < std::string > m_keyList;
    std::set < std::string > m_successKeyList;
    std::set < std::string > m_successValList;
    std::set < std::string > m_errorKeyList;
    pbnjson::JValue m_successKeyListObj;
    CategoryDimKeyListMap m_mergeCategoryDimKeyMap;

    // For callback function.
    Callback m_callback;
    void* m_thiz_class;
    void* m_user_data;

    void sendCacheReply(LSHandle* a_handle, const std::set<std::string>& a_keys);
    void sendResultReply(LSHandle * lsHandle, bool success, const std::string &errorText = std::string());
    void sendConditionCategoryReply(LSHandle *lsHandle);
    static bool cbSendQueryGet(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbSendQueryGetDefault(LSHandle * lsHandle, LSMessage * message, void *data);
    void updateSuccessErrorKeyList();
    int updateSuccessValueObj(pbnjson::JValue valueObj, std::string & errorText);
    int parsingResult(pbnjson::JValue keyArray, std::string & errorText, bool a_filterMixed);


    void regSubscription(LSHandle * lsHandle, const std::string &key);
    bool sendRequestDefault(LSHandle * lsHandle);

    bool isForGlobalSettings() const
    {
        return (m_app_id.empty());
    }

    /**
     * For volatile key from m_mergeCategoryDimKeyMap, get the key's value and
     * set the key/value into m_successKeyListObj
     */
    void handleVolatileKey();

public:
    bool isKeyListSetting() const
    {
        return (m_forCategoryFlag);
    }

    void Init(const std::set< std::string >& inKeyList, const std::string& inAppId, const std::string& inCategory, pbnjson::JValue inDimension, bool inSubscribe, LSMessage * inMessage);

public:
    // Constructor
    PrefsDb8Get(const std::set < std::string >& inKeyList, const std::string& inAppId, const std::string& inCategory, pbnjson::JValue inDimension, bool inSubscribe=false, LSMessage * inMessage=NULL);

    // Destructor
    ~PrefsDb8Get();

    // Connect or disconnect the callback
    //
    void Connect(Callback a_func, void *thiz_class, void *userdata);
    void Disconnect();

    bool sendRequest(LSHandle * lsHandle);
    void setTaskInfo(MethodCallInfo* p) { m_taskInfo = p; }

    /**
     * Set flag to true for handling getSystemSettingFactoryValue API.
     * @param isFactoryValueRequest true for enable getSystemSettingFactoryValue API
     */
    void setFactoryValueRequest(bool isFactoryValueRequest) { m_isFactoryValueRequest = isFactoryValueRequest; };

    void forceDbSync(void) { m_forceDbSync = true; }
    bool isForceDbSync(void) const { return m_forceDbSync; }

    static pbnjson::JValue newKeyArrayfromBatch(pbnjson::JValue root);
    static std::set<std::string> keysLayeredRecords(pbnjson::JValue resultArray);
    static pbnjson::JValue mergeLayeredRecords(const std::string& a_category, pbnjson::JValue resultArray, const std::string &a_app_id, bool a_filterMixed, pbnjson::JValue reqDim = pbnjson::JValue());
    static pbnjson::JValue jsonFindBatchItem(const std::string &categoryName,
            bool isKeyListSetting, const std::set < std::string >& a_keyList, bool a_isSupportAppId, const std::string& a_appId, const std::string& targetKindName);
    static void filterWrongDimKey(const std::set<std::string>& perAppKeys, std::set<std::string>& filteredPerAppKeys, pbnjson::JValue reqDim);
};
#endif                          // PREFSDB8GET_H
