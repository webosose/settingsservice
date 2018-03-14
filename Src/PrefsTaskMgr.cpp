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

//->Start of API documentation comment block
/**
  @page com_webos_settingsservice com.webos.settingsservice

  @brief Service component for Setting. Provides APIs to set/get/manage settings

  @{
  @}
 */
//->End of API documentation comment block

#include "Logging.h"
#include "PrefsTaskMgr.h"
#include "PrefsInternalCategory.h"
#include "PrefsPerAppHandler.h"
#include "SettingsService.h"
#include "SettingsServiceApi.h"

std::mutex MethodTaskMgr::m_mutex_lock_taskMap;
std::condition_variable MethodTaskMgr::m_mutex_cond_taskMap;;

// these functions are in PrefsFactory.cpp
extern bool doGetSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doSetSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doGetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doSetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doGetCurrentSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doGetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doSetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doGetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doSetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doSetSystemSettingFactoryDesc(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doDelSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doResetSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool doResetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
extern bool requestGetSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);

bool requestChangeAppId(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    std::string* appId = static_cast<std::string *>(pTaskInfo->getUserData());

    PrefsPerAppHandler::instance().setTaskInfo(pTaskInfo);
    PrefsFactory::instance()->setCurrentAppId(*appId);

    return true;
}

bool requestRemovePerAppSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    std::string* appId = static_cast<std::string*>(pTaskInfo->getUserData());

    PrefsPerAppHandler::instance().setTaskInfo(pTaskInfo);
    PrefsPerAppHandler::instance().removePerAppSettings(*appId);

    return true;
}

// MethodId, MethodName, MethodCallback
typedef struct {
    MethodId id;
    std::string name;
    bool (*function)(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo);
} MethodInfo ;

MethodInfo methodInfo[] = {
    {METHODID_MIN, "", NULL},
    {METHODID_GETSYSTEMSETTINGS, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS, doGetSystemSettings },
    {METHODID_SETSYSTEMSETTINGS, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS, doSetSystemSettings },
    {METHODID_GETSYSTEMSETTINGFACTORYVALUE, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGFACTORYVALUE, doGetSystemSettingFactoryValue },
    {METHODID_SETSYSTEMSETTINGFACTORYVALUE, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYVALUE, doSetSystemSettingFactoryValue },
    {METHODID_GETSYSTEMSETTINGVALUES, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES, doGetSystemSettingValues},
    {METHODID_SETSYSTEMSETTINGVALUES, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGVALUES, doSetSystemSettingValues},
    {METHODID_GETSYSTEMSETTINGDESC, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC, doGetSystemSettingDesc },
    {METHODID_SETSYSTEMSETTINGDESC, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGDESC, doSetSystemSettingDesc },
    {METHODID_SETSYSTEMSETTINGFACTORYDESC, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYDESC, doSetSystemSettingFactoryDesc },
    {METHODID_GETCURRENTSETTINGS, SETTINGSSERVICE_METHOD_GETCURRENTSETTINGS, doGetCurrentSettings },
    {METHODID_DELETESYSTEMSETTINGS, SETTINGSSERVICE_METHOD_DELETESYSTEMSETTINGS, doDelSystemSettings },
    {METHODID_RESETSYSTEMSETTINGS, SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGS, doResetSystemSettings },
    {METHODID_RESETSYSTEMSETTINGDESC, SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGDESC, doResetSystemSettingDesc },
    {METHODID_REQUEST_GETSYSTEMSETTIGNS, SETTINGSSERVICE_METHOD_REQUESTGETSYSTEMSETTINGS, requestGetSystemSettings },
    {METHODID_REQUEST_GETSYSTEMSETTIGNS_SINGLE, SETTINGSSERVICE_METHOD_REQUESTGETSYSTEMSETTINGS, requestGetSystemSettings },
    {METHODID_INTERNAL_GENERAL, "internal/(general)", doInternalCategoryGeneralMethod },
    {METHODID_CHANGE_APP, SETTINGSSERVICE_METHOD_CHANGE_APP, requestChangeAppId },
    {METHODID_UNINSTALL_APP, SETTINGSSERVICE_METHOD_UNINSTALL_APP, requestRemovePerAppSettings },
    {METHODID_MAX, "", NULL}
};

MethodCallQueue::MethodCallQueue(void)
{
}

void MethodCallInfo::run()
{
    methodInfo[m_methodId].function(m_lsHandle, m_message, this);
}

const std::string& MethodCallInfo::getMethodName() const
{
    return methodInfo[m_methodId].name;
}

const std::string& MethodTaskMgr::getMethodName(unsigned int methodId)
{
    return methodInfo[methodId].name;
}

MethodTaskMgr::MethodTaskMgr() :
    m_p_thread(nullptr)
    , m_threadRunFlag(false)
    , m_taskId(TaskIdStart)  // is incressed in push
{
    g_atomic_int_set(&m_taskCnt, 0);
}

MethodTaskMgr::~MethodTaskMgr(void)
{
    stopTaskThread();
}

void MethodTaskMgr::stopTaskThread()
{
    if(m_p_thread  && m_threadRunFlag) {
        m_threadRunFlag = false;
        m_methodCallQueue.releaseBlockedQueue();
        m_mutex_cond_taskMap.notify_all();

        try {
            m_p_thread->join();
            m_p_thread->detach();
        } catch(std::exception& e) {
            SSERVICELOG_WARNING(MSGID_TASK_ERROR, 0, "Exception Received form thread join() or detach() is: %s", e.what());

        }

        delete m_p_thread;
        m_p_thread = nullptr;
    }
}

void MethodCallQueue::releaseBlockedQueue(void)
{
    m_mutex_cond_methodInfo.notify_all();
}

bool MethodCallQueue::pushImpl(unsigned int taskId, MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo *pBatchInfo, void *a_userData, TaskPushMode a_mode)
{
    bool result = false;

    // inset new task
    if(inMethodId > METHODID_MIN && inMethodId < METHODID_MAX) {
        MethodCallInfo* item = new MethodCallInfo(taskId, inMethodId, inlsHandle, inMessage, pBatchInfo);
        item->setUserData(a_userData);
        item->ref();

        std::lock_guard<std::mutex> lock(m_mutex_lock_methodInfo);
        //
        // In case of user method, push front so that run just after current method.
        //
        if (a_mode == TASK_PUSH_FRONT)
        {
            // In case of user method, do not increament reference count.
            // It will be handled by user.
            //
            m_methodCallInfoList.push_front(item);
        }
        else
        {
            m_methodCallInfoList.push_back(item);
        }

        item->taskInQueue();

        SSERVICELOG_DEBUG("push a item, now total: %zd ", m_methodCallInfoList.size());
        m_mutex_cond_methodInfo.notify_all();

        result = true;
    }
    else {
        SSERVICELOG_WARNING(MSGID_WRONG_METHODID, 0, "Method Id is wrong %d. This method call is ignored", inMethodId);
        // do nothing
    }

    return result;
}

bool MethodCallQueue::push(unsigned int taskId, MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo *pBatchInfo)
{
    return pushImpl(taskId, inMethodId, inlsHandle, inMessage, pBatchInfo, NULL, TASK_PUSH_BACK);
}

bool MethodCallQueue::pushUser(unsigned int taskId, MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, void *a_userData, TaskPushMode a_mode)
{
    return pushImpl(taskId, inMethodId, inlsHandle, inMessage, NULL, a_userData, a_mode);
}

MethodCallInfo* MethodCallQueue::pop() {
    MethodCallInfo* item;

    std::unique_lock<std::mutex> lock(m_mutex_lock_methodInfo);

    // check and wait for a pushed item
    if(m_methodCallInfoList.empty()) {
        SSERVICELOG_DEBUG("pthread_cond_wait");
        m_mutex_cond_methodInfo.wait(lock);
    }

    // empty should be checked before call list::front,
    if(!m_methodCallInfoList.empty()) {
        item = m_methodCallInfoList.front();
        m_methodCallInfoList.pop_front();
        SSERVICELOG_DEBUG("pop a item, now remain: %zd", m_methodCallInfoList.size());
    }
    // if there is no item in m_methodCallQueue, set item NULL.
    else {
        item = nullptr;
    }

    return item;
}

void MethodTaskMgr::upTaskCnt() {
#if USE_ATOMIC
    g_atomic_int_add(&m_taskCnt, 1);
#else
    m_taskCnt++;
#endif
}

void MethodTaskMgr::downTaskCnt() {
#if USE_ATOMIC
    if(g_atomic_int_get(&m_taskCnt) > 0) {
        g_atomic_int_add(&m_taskCnt, -1);
    }
    else {
        g_atomic_int_set(&m_taskCnt, 0);
    }
#else
    if(m_taskCnt > 0) {
        m_taskCnt--;
    }
    else {
        m_taskCnt = 0;
    }
#endif
}

bool MethodTaskMgr::isTaskEmpty() {
#if USE_ATOMIC
    return (g_atomic_int_get(&m_taskCnt))? false : true;
#else
    return (m_taskCnt)? false : true;
#endif
}

int MethodTaskMgr::getTaskCnt() {
#if USE_ATOMIC
    return (int) g_atomic_int_get(&m_taskCnt);
#else
    return m_taskCnt;
#endif
}

void MethodTaskMgr::methodCallThread(void* data)
{
    MethodTaskMgr* taskMgr;
    unsigned int taskId;
    MethodCallInfo *item = nullptr;
    MethodId methodId = METHODID_MIN;
    bool need_to_wait = false;

    taskMgr = (MethodTaskMgr*) data;

    do {
        item = taskMgr->pop();
        if (!item)
        {
            continue;
        }

        methodId = item->getMethodId();

        need_to_wait = false;
        // if the task is for writing, key or description, waiting for all tasks are finished.
        {
            std::unique_lock<std::mutex> lock(m_mutex_lock_taskMap);
            if(methodId == METHODID_SETSYSTEMSETTINGS
                        || methodId == METHODID_SETSYSTEMSETTINGFACTORYVALUE
                        || methodId == METHODID_SETSYSTEMSETTINGVALUES
                        || methodId == METHODID_SETSYSTEMSETTINGDESC
                        || methodId == METHODID_SETSYSTEMSETTINGFACTORYDESC
                        || methodId == METHODID_DELETESYSTEMSETTINGS
                        || methodId == METHODID_REQUEST_GETSYSTEMSETTIGNS_SINGLE
                        || methodId == METHODID_CHANGE_APP
                        || methodId == METHODID_UNINSTALL_APP
                        || methodId == METHODID_RESETSYSTEMSETTINGS
                        || methodId == METHODID_RESETSYSTEMSETTINGDESC)
            {
                if (!taskMgr->isTaskEmpty()) {
                    SSERVICELOG_DEBUG("waiting for finishing other takes. remain tasks: %d", taskMgr->getTaskCnt());

                    // waiting for finishing other task.
                    //      The signal is sent from release function, when no task is remain.
                    m_mutex_cond_taskMap.wait(lock);
                }
                need_to_wait = true;
            }

            // incress task count
            taskMgr->upTaskCnt();
            taskId = item->getTaskId();
            SSERVICELOG_DEBUG(" TotalTask:#%d, NewTask: %s (task id:%d) is running", taskMgr->getTaskCnt(), item->getMethodName().c_str(), taskId);
        }

        // do function
        item->run();

        if (need_to_wait)
        {
            std::unique_lock<std::mutex> lock(m_mutex_lock_taskMap);
            if (!taskMgr->isTaskEmpty())
            {
                m_mutex_cond_taskMap.wait(lock);
            }
        }
    } while(taskMgr->m_threadRunFlag);
}

void MethodTaskMgr::releaseTask(MethodCallInfo** p, pbnjson::JValue replyObj)
{
    MethodCallInfo *taskInfo = *p;

    if(taskInfo->isBatchCall()) {
        taskInfo->releaseBatchTask(replyObj);
    }

    unsigned int taskId = taskInfo->getTaskId();

    SSERVICELOG_DEBUG("%s task is released [taskId %d]", taskInfo->getMethodName().c_str(), taskId);

    *p = nullptr;
    if (taskInfo->unref())
    {
        std::lock_guard<std::mutex> lock(m_mutex_lock_taskMap);
        SSERVICELOG_DEBUG("count down the number of running task. current running task: #%u", getTaskCnt());
        downTaskCnt();
        // only if no task is remain, send signal.
        if(!getTaskCnt()) {
            // wake the thread that is waiting for finishing other tasks.
            m_mutex_cond_taskMap.notify_all();
        }
    }
}

bool MethodTaskMgr::createTaskThread(void)
{
    // TODO:: move to serivceReady with registering service
    //      For this we need another bus handle to initialize SettingsService
    // create thread to handle reuqests
    if(!m_p_thread) {

        m_threadRunFlag = true;
        try {
            m_p_thread = new std::thread(MethodTaskMgr::methodCallThread, this);
        }
        catch(...) {
            m_threadRunFlag = false;
            SSERVICELOG_ERROR(MSGID_PTHREAD_CREATE_ERR, 0, "Error!! to create task thread");
            exit(0);
        }
        SSERVICELOG_DEBUG("MethodCallThread is running");
    }

    return true;
}


MethodId MethodTaskMgr::getMethodId(const std::string& name)
{
    for(int i = METHODID_MIN + 1; i < METHODID_MAX; i++) {
        if(methodInfo[i].name == name) {
            return methodInfo[i].id;
        }
    }

    return METHODID_MIN;
}



BatchMethodInfo::BatchMethodInfo(LSHandle *inHandle, LSMessage *inMessage, unsigned int inTotalN) :
    m_lsHandle(inHandle)
    , m_message(inMessage)
    , m_totalN(inTotalN)        // inTotalN should be bigger than 0
{
    LSMessageRef(m_message);
    g_atomic_int_set(&m_replyCnt, inTotalN);

    m_replyObjs.resize(m_totalN);
}


BatchMethodInfo::~BatchMethodInfo() {
    LSMessageUnref(m_message);
}

bool MethodTaskMgr::execute(MethodId inMethodId, LSHandle *inlsHandle, LSMessage *inMessage, BatchInfo* pBatchInfo)
{
    if( pBatchInfo ) {
        SSERVICELOG_WARNING(MSGID_TASK_ERROR, 2,
                PMLOGKS("Method", methodInfo[inMethodId].name.c_str()),
                PMLOGKS("Message", LSMessageGetPayload(inMessage)),
                "Task execution error");
        return false;
    }

    MethodCallInfo *item = new MethodCallInfo(TaskCache, inMethodId, inlsHandle, inMessage, pBatchInfo);
    item->ref();

    item->run();

    return true;
}


bool MethodTaskMgr::pushBatchMethod(LSHandle *lsHandle, LSMessage *message, const std::list<tBatchParm>& batchParmList)
{
    int index = 0;

    unsigned int totalN = batchParmList.size();
    SSERVICELOG_DEBUG("total requests: %d", totalN);

    const auto pBatchMethodInfo = std::make_shared<BatchMethodInfo>(lsHandle, message, totalN);

    // call each callback function
    for (tBatchParm it : batchParmList) {

        MethodId id = getMethodId(it.method);

        if(id > METHODID_MIN) {
            std::unique_ptr<BatchInfo> pBatchInfo(new BatchInfo(index, pBatchMethodInfo, it.params));

            if (push(id, lsHandle, message, pBatchInfo.get()))
            {
                pBatchInfo.release();
                SSERVICELOG_DEBUG("pushed an item of batch method, now total is: %d ", index + 1);
            } else
            {
                SSERVICELOG_DEBUG("failed to push an item of batch method");
            }
        } else {
            pbnjson::JObject replyObj;

            std::string  errorText = "Method \'" + it.method + "\' is not supported";

            replyObj.put("returnValue", false);
            replyObj.put("errorText", errorText);
            replyObj.put("method", SETTINGSSERVICE_METHOD_BATCH);

            // add to replyObj for batchMethod
            pBatchMethodInfo->releaseBatchMethod(replyObj, index);
        }
        index++;
    }

    SSERVICELOG_DEBUG("#%d tasks are processed", totalN);
    return true;
}

bool BatchMethodInfo::isSubscribedAll(void) const
{
    for(int i = 0; i < m_totalN; i++) {
        if ( m_replyObjs[i].isNull() ) continue;

        pbnjson::JValue subscribed = m_replyObjs[i]["subscribed"];

        if (!subscribed.isBoolean() || !subscribed.asBool())
            return false;
    }

    return true;
}

void BatchMethodInfo::releaseBatchMethod(pbnjson::JValue obj, int index)
{
    // ignore NULL return. it will be return empty result
    if(obj.isNull()) {
        SSERVICELOG_WARNING(MSGID_EMPTY_BATCH_RESULT,0,"Error!! NULL object is sent for the result");
        m_replyObjs[index] = pbnjson::Object(); // this is released width replyArrayObj
    }
    else {
        m_replyObjs[index] = obj; // this is released width replyArrayObj
    }

    SSERVICELOG_DEBUG("ReleaseBatchmethod : %d m_replyObj[%d]: %s", m_replyCnt, index, m_replyObjs[index].stringify().c_str());

    if (g_atomic_int_dec_and_test(&m_replyCnt) == TRUE)
    {
        LSError lsError;

        LSErrorInit(&lsError);

        pbnjson::JArray replyArrayObj;
        for(pbnjson::JValue replyObj : m_replyObjs) {
            if(!replyObj.isNull()) {
                replyArrayObj.append(replyObj);
            }
        }

        SSERVICELOG_TRACE("Send result reply for batch method");

        pbnjson::JObject result;
        result.put("returnValue", true);
        result.put("results", replyArrayObj);
        result.put("subscribed", isSubscribedAll());

        SSERVICELOG_TRACE("reply string is %s", result.stringify().c_str());
        bool retVal = LSMessageReply(m_lsHandle, m_message, result.stringify().c_str(), &lsError);
        if (!retVal) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply for batch");
            LSErrorFree(&lsError);
        }
    }
}
