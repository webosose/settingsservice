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

#ifndef PREFSDB8SETVALUES_H
#define PREFSDB8SETVALUES_H

#include <string>

#include <luna-service2/lunaservice.h>

#include "PrefsKeyDescMap.h"
#include "PrefsTaskMgr.h"
#include "SettingsService.h"

class PrefsDb8SetValues : public PrefsRefCounted {

 private:
    LSMessage * m_replyMsg;
    MethodCallInfo* m_taskInfo;

    std::string m_key;
    std::string m_op;
    std::string m_vtype;
    pbnjson::JValue m_values;

    std::string m_appId;
    std::string m_dbtype;
    std::string m_category;
    bool m_categoryFlag;
    bool m_descFlag;
    bool m_valueCheck;
    bool m_valueCheckFlag;
    bool m_defKindFlag;
    bool m_notifySelf;
    pbnjson::JValue m_dimension;
    pbnjson::JValue m_ui;

    std::string m_targetKind;

    bool sendCheckUsedKeyRequest(LSHandle* lsHandle, const char* targetKind);
    bool sendRequestByVtype(LSHandle* lsHandle);
    bool sendFindRequest(LSHandle * lsHandle);
    bool sendDelRequest(LSHandle * lsHandle);
    bool sendMergeRequest(LSHandle * lsHandle);
    bool sendPutRequest(LSHandle * lsHandle);
    LSMessage *getLSMsgHandle() {
        return m_replyMsg;
    }
    void sendResultReply(LSHandle * lsHandle, bool success, std::string & errorText);
    void postSubscription();

    static bool cbCheckUsedKeyRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbAddRemoveRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbDelRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbMergeRequest(LSHandle * lsHandle, LSMessage * message, void *data);
    static bool cbPutRequest(LSHandle * lsHandle, LSMessage * message, void *data);

#ifdef USE_MEMORY_KEYDESC_KIND
    pbnjson::JValue m_replyRoot;

    void setKeyInfoObj(pbnjson::JValue obj) {
        m_replyRoot = obj;
    }

    bool addToKeyDescMap() {
        return PrefsKeyDescMap::instance()->addKeyDesc(m_key, m_replyRoot, m_defKindFlag, m_appId);
    }

    bool removeKeyDescMap() {
        return PrefsKeyDescMap::instance()->delKeyDesc(m_key);
    }

#endif // USE_MEMORY_KEYDESC_KIND


 public:
    PrefsDb8SetValues(const std::string &inKey, const std::string &inVtype, const std::string &inOp, pbnjson::JValue inValues, LSMessage * inMsg) :
        m_replyMsg(inMsg),
        m_taskInfo(nullptr),
        m_key(inKey),
        m_op(inOp),
        m_vtype(inVtype),
        m_values(inValues),
        m_categoryFlag(false),
        m_descFlag(false),
        m_valueCheck(false),
        m_valueCheckFlag(false),
        m_defKindFlag(false),
        m_notifySelf(true)
    {
        LSMessageRef(m_replyMsg);
    }

    ~PrefsDb8SetValues() {
        if(m_taskInfo) PrefsFactory::instance()->releaseTask(&m_taskInfo);
        LSMessageUnref(m_replyMsg);
    }

    void setDescProperties(pbnjson::JValue inDimension, const std::string &inDbtype, const std::string& appId, pbnjson::JValue inUi) {
        m_op.clear();
        m_appId = appId;
        m_dbtype = inDbtype;

        m_dimension = inDimension;

        m_ui = inUi;
    }

    void fillDescPropertiesForRequest(pbnjson::JValue jRootProps, bool handleNewKey);

    void setDefKindFlag() {
        m_defKindFlag = true;
    }

    void setDescPropertiesCategory(const std::string &inCategory) {
        m_categoryFlag = true;
        m_category = inCategory;
    }

    void setNotifySelf(bool a_flag)
    {
        m_notifySelf = a_flag;
    }

    bool getNotifySelf() const
    {
        return m_notifySelf;
    }

    void setDescPropertiesValueCheck(bool inValueCheck) {
        m_valueCheckFlag = true;
        m_valueCheck = inValueCheck;
    }

    void printPrefMsg() {
        LSMessagePrint((LSMessage *) m_replyMsg, stdout);
    }

    bool sendRequest(LSHandle * lsHandle);
    void setTaskInfo(MethodCallInfo* p) { m_taskInfo = p; };
};
#endif                          // PREFSDB8GET_H
