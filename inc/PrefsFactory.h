// Copyright (c) 2013-2024 LG Electronics, Inc.
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

#ifndef PREFSFACTORY_H
#define PREFSFACTORY_H

#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <functional>

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

#include "PrefsHandler.h"

class MethodCallInfo;
class MethodTaskMgr;

class PrefsFactory {
public:
    enum ServiceId
    {
        COM_LGE_SERVICE = 0,
        COM_WEBOS_SERVICE = 1
    };
    static const std::string service_name;
    static const std::string service_root_uri;

    static PrefsFactory *instance();

    const std::vector<LSHandle*>& getServiceHandles() const;
    LSHandle* getServiceHandle(ServiceId id) const
    {
        return getServiceHandles().at(id);
    }
    void setServiceHandle(LSHandle*);
    void serviceReady(void);
    void serviceFailed(const char * name);
    void serviceStart(void);
    bool isReady(void);
    void initKind();

    void loadCoreServices(const std::string& a_confPath);
    bool isCoreService(LSMessage* a_message) const;
    bool isAvailableCache(const std::string& a_method, LSHandle* a_handle, LSMessage* a_mesg) const;
    void blockCacheValue(LSMessage* a_message);

    std::shared_ptr<PrefsHandler> getPrefsHandler(const std::string& key) const;

    void postPrefChange(const char *subscribeKey, pbnjson::JValue replyRoot, const char *a_sender=NULL, const char *a_senderId=NULL) const;
    void postPrefChange(const char *subscribeKey, const std::string& a_reply, const char *a_sender=NULL) const;
    void postPrefChangeEach(LSHandle *lsHandle, const char *subscribeKey, const std::string& a_reply, const char *a_sender=NULL) const;
    void postPrefChanges(const std::string &category, pbnjson::JValue dimObj, const std::string &app_id, pbnjson::JValue keyValueObj, bool result, bool storeFlag = true, const char *a_sender=NULL, const char *a_senderId=NULL) const;
    void postPrefChangeCategory(LSHandle *lsHandle, const std::string &category, pbnjson::JValue dimObj, const std::string &app_id, pbnjson::JValue keyValueObj, bool result, bool storeFlag, const char *a_sender=NULL, const char *a_senderId=NULL) const;


    bool noSubscriber    (const char *a_key, LSMessage *a_exception) const;
    unsigned int subscribersCount(LSHandle *a_handle, const char *a_key, LSMessage *a_exception) const;

    // current app id
    void setCurrentAppId(const std::string& inCurAppId);
    const char* getCurrentAppId(void) { return m_currentAppId.c_str(); }

    void releaseTask(MethodCallInfo ** p, pbnjson::JValue replyObj = pbnjson::JValue());

    MethodTaskMgr* getTaskManager() const;

    /**
    @param lsHandle LS2 Handle
    @param lsMessage LS2 Message from request
    @return true If the message can access the service.
    @sa \ref Public_API_Rectriction
    */
    bool hasAccess(LSHandle *lsHandle, LSMessage *lsMessage);

    /**
    @page Public_API_Rectriction Public API Restriction
    @brief Public API Restriction
    @section Public_API_Rectriction_Sec1 Public API Restriction
    Only two APIs, getSystemSettings and getSystemSettingValues, are allowed on public bus.
    And also, available keys are restricted. Only following keys are readable on public bus.

    | Category    | Key                      |
    | :---------: | :----------------------: |
    | &nbsp;      | localeInfo               |
    | option      | country                  |
    | option      | smartServiceCountryCode2 |
    | option      | smartServiceCountryCode3 |

    @sa PrefsFactory::PublicAPIGuard
    @sa http://jira2.lgsvl.com/browse/ANG-1733
    */

    /**
    @sa \ref Public_API_Rectriction
    */
    class PublicAPIGuard {
    public:
        PublicAPIGuard();
        bool allowMessage(LSMessage *message);
    private:
        std::set<std::string> m_readMethods;
        std::set<std::string> m_writeMethods;

        enum AccessPerm {
            ACPERM_N = 0x00000000,
            ACPERM_R = 0x00000001,
            ACPERM_W = 0x00000002
        };

        std::map<std::pair<std::string, std::string>, int> m_accessControlTable;

        int permissionMask(const std::string &perm);
        bool allowReadMessage(LSMessage *message);
        bool allowWriteMessage(LSMessage *message);
    };

    typedef std::function< void (LSHandle *sh, LSMessage *reply) > SubsCancelFunc;

    void setSubscriptionCancel();
    static bool cbSubscriptionCancel(LSHandle * a_handle, LSMessage * a_message, void * a_data);
    void registerSubscriptionCancel(SubsCancelFunc a_func);

protected:
     PrefsFactory();
    ~PrefsFactory();

private:
    typedef std::map<std::string, std::shared_ptr<PrefsHandler>> PrefsHandlerMap;

    void registerPrefHandler(std::shared_ptr<PrefsHandler> handler);

    bool m_batchFlag;
    bool m_serviceReady;
    std::string m_currentAppId;
    std::vector<LSHandle*> m_serviceHandles;
    PrefsHandlerMap m_handlersMaps;
    PublicAPIGuard m_publicAPIGuard;

    std::set< std::string > m_coreServices;
    std::set< std::pair<std::string,std::string> > m_blockCache;
    std::list< SubsCancelFunc > m_cbSubsCancel;
};

class PrefsNoCopy {
public:
    PrefsNoCopy() {}

private:
    PrefsNoCopy(const PrefsNoCopy&);
    PrefsNoCopy& operator=(const PrefsNoCopy&);
};

class PrefsRefCounted : private PrefsNoCopy {
public:
    PrefsRefCounted();

    void ref();
    bool unref();
    int count() { return m_refCount; }

protected:
    virtual ~PrefsRefCounted() {}

private:
    int m_refCount;
};

class PrefsFinalize {
public:
    virtual ~PrefsFinalize();
    virtual void reference() = 0;
    virtual void finalize() = 0;
    virtual std::string getSettingValue(const char *) = 0;
};

#endif                          /* PREFSFACTORY_H */
