// Copyright (c) 2013-2019 LG Electronics, Inc.
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

#ifndef TASKMGR_H
#define TASKMGR_H

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <luna-service2/lunaservice.h>

#include "JSONUtils.h"
#include "PrefsFactory.h"

typedef enum {
    TASK_PUSH_FRONT,
    TASK_PUSH_BACK
} TaskPushMode;

typedef enum {
    METHODID_MIN,
    METHODID_GETSYSTEMSETTINGS,
    METHODID_SETSYSTEMSETTINGS,
    METHODID_GETSYSTEMSETTINGFACTORYVALUE,
    METHODID_SETSYSTEMSETTINGFACTORYVALUE,
    METHODID_GETSYSTEMSETTINGVALUES,
    METHODID_SETSYSTEMSETTINGVALUES,
    METHODID_GETSYSTEMSETTINGDESC,
    METHODID_SETSYSTEMSETTINGDESC,
    METHODID_SETSYSTEMSETTINGFACTORYDESC,
    METHODID_GETCURRENTSETTINGS,
    METHODID_DELETESYSTEMSETTINGS,
    METHODID_RESETSYSTEMSETTINGS,
    METHODID_RESETSYSTEMSETTINGDESC,
    METHODID_REQUEST_GETSYSTEMSETTIGNS,
    METHODID_REQUEST_GETSYSTEMSETTIGNS_SINGLE,
    METHODID_INTERNAL_GENERAL,
    METHODID_CHANGE_APP,
    METHODID_UNINSTALL_APP,
    METHODID_MAX
} MethodId;

// for batch method
typedef struct {
    std::string method;
    pbnjson::JValue params;
} tBatchParm;

class BatchMethodInfo {
    LSHandle *m_lsHandle;
    LSMessage *m_message;
    std::vector<pbnjson::JValue> m_replyObjs;
    int m_totalN;
    int m_replyCnt;

private:
    bool isSubscribedAll() const;

public:
    BatchMethodInfo(LSHandle *inHandle, LSMessage *inMessage, unsigned int inTotalN);
    ~BatchMethodInfo();
    void releaseBatchMethod(pbnjson::JValue obj, int index);
};

class BatchInfo {
    int m_index;
    pbnjson::JValue m_paramObj;
    std::shared_ptr<BatchMethodInfo> m_pBatchMethodInfo;

public:
    BatchInfo(int index, const std::shared_ptr<BatchMethodInfo> &batchMethodInfo, const pbnjson::JValue &param)
       : m_index(index)
       , m_paramObj(param)
       , m_pBatchMethodInfo(batchMethodInfo)
    {
    }

    void releaseBatchInfo(pbnjson::JValue replyObj) { m_pBatchMethodInfo->releaseBatchMethod(replyObj, m_index); }

    pbnjson::JValue getParam() { return m_paramObj; }
};

//<-- for batch method

class MethodCallInfo : public PrefsRefCounted {
    unsigned int m_taskId;
    MethodId   m_methodId;
    LSHandle    *m_lsHandle;
    LSMessage   *m_message;
    // batch method
    BatchInfo   *m_pBatchInfo;
    void *m_userData;
    bool m_inQueue;

public:
    MethodCallInfo(unsigned int taskId, MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo *inBatchInfo = nullptr) :
        m_taskId(taskId),
        m_methodId(inMethodId),
        m_lsHandle(inlsHandle),
        m_message(inMessage),
        m_pBatchInfo(inBatchInfo),
        m_userData(nullptr),
        m_inQueue(false)
    {
        if (m_message)
            LSMessageRef(m_message);
    }

    unsigned int getTaskId() const { return m_taskId; }

    ~MethodCallInfo()
    {
        if (m_message)
            LSMessageUnref(m_message);

        if(m_pBatchInfo)
            delete m_pBatchInfo;
        m_pBatchInfo = nullptr;
    }

    void setUserData(void *a_userData)
    {
        m_userData = a_userData;
    }

    void *getUserData() const
    {
        return m_userData;
    }

    MethodId getMethodId() const { return m_methodId; }
    const std::string& getMethodName() const;
    void run();
    bool isBatchCall() const { return m_pBatchInfo != nullptr; }
    const BatchInfo* getBatchInfo() const { return m_pBatchInfo; }
    void releaseBatchTask(pbnjson::JValue replyObj) { m_pBatchInfo->releaseBatchInfo(replyObj); }
    pbnjson::JValue getBatchParam() { return m_pBatchInfo->getParam(); }

    void taskInQueue() { m_inQueue = true; }
    bool isTaskInQueue() const { return m_inQueue; }
};

class MethodCallQueue {
    private:
        std::list<MethodCallInfo*> m_methodCallInfoList;
        std::mutex m_mutex_lock_methodInfo;
        std::condition_variable m_mutex_cond_methodInfo;

        bool pushImpl(unsigned int taskId, MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo *pBatchInfo, void *a_userData, TaskPushMode a_mode);

    public:
        MethodCallQueue(void);
        ~MethodCallQueue() { m_methodCallInfoList.clear(); }
        void releaseBlockedQueue(void);
        bool push(unsigned int taskId, MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo* batchInfo);
        bool pushUser(unsigned int taskId, MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, void *a_userData, TaskPushMode a_mode);
        MethodCallInfo* pop();
};

class MethodTaskMgr {
    private:
        const static unsigned int TaskCache = 0;
        const static unsigned int TaskIdStart = 10;

        MethodCallQueue m_methodCallQueue;
        std::thread* m_p_thread;
        static std::mutex m_mutex_lock_taskMap;
        static std::condition_variable m_mutex_cond_taskMap;;

        bool m_threadRunFlag;
        gint m_taskCnt;
        unsigned int m_taskId;

        std::map<int, BatchInfo*> batchInfoMap;

        static void methodCallThread(void* data);

        MethodCallInfo* pop()
        {
            return m_methodCallQueue.pop();
        }

        void upTaskCnt();
        void downTaskCnt();
        int getTaskCnt();
        bool isTaskEmpty();

    public:
        MethodTaskMgr(void);
        ~MethodTaskMgr(void);
        MethodTaskMgr(const MethodTaskMgr&) = delete;
        MethodTaskMgr& operator=(const MethodTaskMgr&) = delete;

        void stopTaskThread();

        bool push(MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo* p = nullptr)
        {
            m_taskId++;
            return m_methodCallQueue.push(m_taskId, inMethodId, inlsHandle, inMessage, p);
        }

        bool pushUserMethod(MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, void *a_userData, TaskPushMode a_mode)
        {
            m_taskId++;
            return m_methodCallQueue.pushUser(m_taskId, inMethodId, inlsHandle, inMessage, a_userData, a_mode);
        }

        bool execute(MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo* p = nullptr);

        bool pushBatchMethod(LSHandle *lsHandle, LSMessage *message, const std::list<tBatchParm> &batchParmList);

        MethodId getMethodId(const std::string& name);
        bool createTaskThread();
        void releaseTask(MethodCallInfo** p, pbnjson::JValue replyObj);
        const std::string& getMethodName(unsigned int methodId);
};

// RequestInfo
//   @desc: Request information sent to Task manager.
//
#define REQUEST_GETSYSTEMSETTINGS_REF_CNT 1
typedef std::map< std::pair<std::string, std::string> /* category,appId */, std::set<std::string> /* keyList */ > CatKeyContainer;
struct TaskRequestInfo {
    CatKeyContainer requestList;                ///< category - Key list container.
    pbnjson::JValue requestDimObj;
    std::string requestAppId; /* TODO: should be supported in the future */
    int requestCount;

    std::vector<std::string> subscribeKeys;  ///< relative subscribe keys
    void *cbFunc;                               ///< Callback function
    void *thiz_class;                           ///< Callback context
};

#endif                          /* TASKMGR_H */
