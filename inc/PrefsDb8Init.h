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

#ifndef PREFSDB8INIT_H
#define PREFSDB8INIT_H

#include <list>
#include <string>
#include <map>

#include <luna-service2/lunaservice.h>

class PrefsDb8Init {
    private:
        typedef enum {
            UpdateType_eNone,       // Update nothing.
            UpdateType_eDefault,    // Update default only.
            UpdateType_eAll
        } UpdateType;

        class KindNameInfo {
            private:
                std::list<std::string> kindNameList;
                std::list<std::string>::iterator itCurKindName;
                int itemN;

            public:
                KindNameInfo() {
                    itCurKindName = kindNameList.end();
                    itemN = 0;
                }

                // TODO: loading file lists from /etc/palm/db/kinds
                bool loadAllKindNameToDel(bool a_defaultOnly);
                bool loadAllKindNameToReg(bool a_defaultOnly);
                bool loadVolatileKindName();
                bool loadPermission();
                void getKindInfoNext(std::string& kindName, std::string& kindPath);
                void getPermissionInfoNext(std::string& permissionPath);
        };

    public:
        static PrefsDb8Init *instance();
        void setServiceHandle(LSHandle* serviceHandle) { m_serviceHandlePrivate = serviceHandle; };
        bool initKind();
        static std::pair<std::string, std::string> getMajorMinorVersion(const std::string& a_version);

        void cancelForegroundApp();
        void cancelListApps();
        bool subscribeForegroundApp();
        bool subscribeListApps();

    private:
        PrefsDb8Init();
        ~PrefsDb8Init();

    private:
        KindNameInfo*  m_kindInfo;
        LSHandle*      m_serviceHandlePrivate;
        bool           m_dbInitDone;
        UpdateType     m_updateType;

        LSMessageToken m_tokenForegroundApp;
        LSMessageToken m_tokenListApps;
        std::map<std::string, void*> m_serviceCookies;

        std::string    m_targetKindName;
        std::string    m_targetKindFilePath;
        std::string    m_targetPermissionFilePath;

        static PrefsDb8Init*  s_instance;

        bool loadKindInfo(bool);
        bool loadPermissionInfo();
        void getKindInfoNext();
        void getPermissionInfoNext();
        void releaseKindInfo();
        void checkUpdateType(bool a_db_init, const std::string &a_dbVersion, const std::string &a_confVersion);

        bool mergeInitKey();  // save dbInitDone flag to DB
        bool putInitKey();  // save dbInitDone flag to DB

        bool initSubscribers();
        bool callwithSubscribes();
        static bool _callwithSubscribes(LSHandle *a_handle, const char *a_service, bool a_connected, void *ctx);

        bool delKind();
        bool regKind();
        bool regPermission();
        bool checkInitKey();

        bool loadDefaultSettings();

        static bool cbMergeInitKey(LSHandle * sh, LSMessage * message, void *ctx);
        static bool cbPutInitKey(LSHandle * sh, LSMessage * message, void *ctx);
        static bool cbFindNullCategory(LSHandle * sh, LSMessage * message, void *ctx);
        static bool cbCreateNullCategory(LSHandle * sh, LSMessage * message, void *ctx);

        static bool cbSubscribers(LSHandle * sh, LSMessage * message, void *ctx);
        static bool cbCurAppIdSubscribers(LSHandle * sh, LSMessage * message, void *ctx);
        static bool cbListAppsSubscribers(LSHandle * sh, LSMessage * message, void *ctx);

        static bool cbDelKind(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbRegKind(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbRegPermission(LSHandle * lsHandle, LSMessage * message, void *data);

        static bool cbCheckInitKey(LSHandle * lsHandle, LSMessage * message, void *data);
        static bool cbLoadDefaultSettingsDB8(LSHandle * lsHandle, LSMessage * message, void *data);
};

#endif                          /* PREFSFACTORY_H */
