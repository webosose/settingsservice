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
#include "PrefsDb8Init.h"
#include "PrefsKeyDescMap.h"
#include "SettingsService.h"
#include "Utils.h"
#include <boost/algorithm/string/replace.hpp>
/* SettingsService initialization sequence
    .
    |
   initKind
    |
   delKind
    |
   regKind
    |
   delKind
    |
    +-- initSubscribers -- ---
    |
   checkingDescKind
    |
   loadingDefaultSettings
    |
   PrefsKeyDescMap::initKeyDescMap
    |
    O
*/
PrefsDb8Init* PrefsDb8Init::s_instance = nullptr;
const static char* APPLICATION_MANAGER = "com.webos.applicationManager";

PrefsDb8Init *PrefsDb8Init::instance()
{
    if (!s_instance) {
        s_instance = new PrefsDb8Init;
    }
    return s_instance;
}

PrefsDb8Init::PrefsDb8Init() :
      m_kindInfo(nullptr)
    , m_serviceHandlePrivate(nullptr)
    , m_dbInitDone(false)
    , m_updateType(UpdateType_eNone)
    , m_tokenForegroundApp(0)
    , m_tokenListApps(0)
{
}

PrefsDb8Init::~PrefsDb8Init()
{
    LSError lsError;
    LSErrorInit(&lsError);

    s_instance = 0;

    releaseKindInfo();

    for (auto& cook : m_serviceCookies ) {
        if( !LSCancelServerStatus(m_serviceHandlePrivate, cook.second, &lsError) ) {
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
        }
    }
}


bool PrefsDb8Init::loadKindInfo(bool delRegFlag) {
    bool result = true;

    if(m_kindInfo) {
        delete(m_kindInfo);
    }

    m_kindInfo = new KindNameInfo;
    if(m_dbInitDone) {
        SSERVICELOG_DEBUG("%s: Init for Volatile kind", __FUNCTION__);
        result = m_kindInfo->loadVolatileKindName();
    }
    else {
        SSERVICELOG_DEBUG("%s: Init for %s kind", __FUNCTION__, m_updateType == UpdateType_eDefault ? "default" : "all");
        if(!delRegFlag) {
            SSERVICELOG_DEBUG("%s: to delKind", __FUNCTION__);
            result = m_kindInfo->loadAllKindNameToDel(m_updateType == UpdateType_eDefault);
        }
        else {
            SSERVICELOG_DEBUG("%s: to regKind", __FUNCTION__);
            result = m_kindInfo->loadAllKindNameToReg(m_updateType == UpdateType_eDefault);
        }
    }

    return result;
}


bool PrefsDb8Init::loadPermissionInfo()
{
    if(m_kindInfo) {
        delete(m_kindInfo);
    }

    m_kindInfo = new KindNameInfo;
    return m_kindInfo->loadPermission();
}

void PrefsDb8Init::getKindInfoNext() {
    if(m_kindInfo) {
        m_kindInfo->getKindInfoNext(m_targetKindName, m_targetKindFilePath);
    }
    else {
        m_targetKindName = "";
        m_targetKindFilePath = "";
    }
    SSERVICELOG_DEBUG("%s: kindfilepath : %s targetkindname : %s", __FUNCTION__,
                      m_targetKindFilePath.c_str(), m_targetKindName.c_str());
}

void PrefsDb8Init::getPermissionInfoNext() {
    if(m_kindInfo) {
        m_kindInfo->getPermissionInfoNext(m_targetPermissionFilePath);
    }
    else {
        m_targetPermissionFilePath = "";
    }
    SSERVICELOG_DEBUG("%s: targetPermissionFilePath : %s", __FUNCTION__, m_targetPermissionFilePath.c_str());
}

void PrefsDb8Init::releaseKindInfo() {
    if(m_kindInfo) {
        delete m_kindInfo;
        m_kindInfo = NULL;
    }
}


bool PrefsDb8Init::cbDelKind(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    std::string errorText;

    PrefsDb8Init *replyInfo = (PrefsDb8Init *) data;

    const char *payload = LSMessageGetPayload(message);
    do {
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }
        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_INIT_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean()) {
            success = label.asBool();
            if (!success) {
                pbnjson::JValue label2(root["errorText"]);
                if (label.isString()) {
                    errorText = label.asString();
                    boost::replace_all(errorText, "\"", "'");
                }
                else
                    errorText = "unexpected payload";
            }
        }
    } while (false);

    if(!success) {
        SSERVICELOG_ERROR(MSGID_INIT_DELKIND_ERR, 2, PMLOGKS("kind",replyInfo->m_targetKindName.c_str()),
                             PMLOGKS("Reason",errorText.c_str()), "");
    }

    // for delKind, error is discarded.
    replyInfo->getKindInfoNext();
    if(replyInfo->m_targetKindName.size()) {
        replyInfo->delKind();
    }
    else {
        // start again in kind name list
        if(replyInfo->loadKindInfo(true)) {
            replyInfo->getKindInfoNext();
            if(replyInfo->m_targetKindFilePath.size()) {
                replyInfo->regKind();
            }
            else {
                SSERVICELOG_ERROR(MSGID_INIT_KIND_REG_ERR, 0, "There is no kind info to register");
                replyInfo->releaseKindInfo();   // Don't need KindInfo anymore.
                PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
            }
        }
        else {
            SSERVICELOG_ERROR(MSGID_INIT_KIND_LOAD_ERR, 0, " ");
            replyInfo->releaseKindInfo();   // Don't need KindInfo anymore.
            PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
        }
    }

    return true;
}

bool PrefsDb8Init::cbRegKind(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    std::string errorText;

    PrefsDb8Init *replyInfo = (PrefsDb8Init *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            errorText = "missing payload";
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_INIT_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "couldn't parse json";
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean())
            success = label.asBool();
    } while (false);

    if (success) {
        // If there is another kind fine to register, call regKind again.
        replyInfo->getKindInfoNext();
        if(replyInfo->m_targetKindFilePath.size()) {
            replyInfo->regKind();
        }
        // Otherwise, goto next step.
        else {
            SSERVICELOG_DEBUG("Success to register Kind(s)");
            replyInfo->initSubscribers();
            if(replyInfo->m_dbInitDone) {
                SSERVICELOG_DEBUG("Start to Load Description Kind");
                PrefsKeyDescMap::instance()->initKeyDescMap();
            }
            else {
                // try to register Permissions
                if(replyInfo->loadPermissionInfo()) {
                    replyInfo->getPermissionInfoNext();
                    if(replyInfo->m_targetPermissionFilePath.size()) {
                        replyInfo->regPermission();
                    }
                    else {
                        SSERVICELOG_ERROR(MSGID_INIT_PERM_REG_ERR, 0, "There is no Permission info to register");
                        replyInfo->releaseKindInfo();   // Don't need KindInfo anymore.
                        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
                    }
                }
                else {
                    SSERVICELOG_ERROR(MSGID_INIT_PERM_LOAD_ERR, 0, " ");
                    replyInfo->releaseKindInfo();   // Don't need KindInfo anymore.
                    PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
                }
            }
        }
    }
    else {
        SSERVICELOG_ERROR(MSGID_INIT_DB8_CONF_ERR, 1, PMLOGKS("Kind_File",replyInfo->m_targetKindFilePath.c_str()), "");
        replyInfo->releaseKindInfo();
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }

    return true;
}


bool PrefsDb8Init::cbRegPermission(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    std::string errorText;

    PrefsDb8Init *replyInfo = (PrefsDb8Init *) data;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_INIT_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean())
            success = label.asBool();
    } while(false);

    if (success) {
        // Initialize a subscriber, all subscribe routines are called here.
        // Before calling this routing, volatile kind must have been iniialized.
        // TODO: It would be good to create appropriate class for subscriber.

        // If there is another permission file to register, call regPermission again.
        replyInfo->getPermissionInfoNext();
        if(replyInfo->m_targetPermissionFilePath.size()) {
            replyInfo->regPermission();
        }
        else {
            SSERVICELOG_DEBUG("Success to register Permission(s)");

            replyInfo->loadDefaultSettings();

            replyInfo->releaseKindInfo();   // Don't need KindInfo anymore.
        }
    }
    else {
        SSERVICELOG_ERROR(MSGID_INIT_DB8_CONF_ERR, 1,
                             PMLOGKS("Permission_File",replyInfo->m_targetPermissionFilePath.c_str()), "");
        replyInfo->releaseKindInfo();
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }

    return true;
}

std::pair<std::string, std::string> PrefsDb8Init::getMajorMinorVersion(const std::string& a_version)
{
    std::string major, minor;

    std::size_t pos = a_version.find(".");
    if (std::string::npos == pos || pos == 0 || (pos+1) == a_version.size() )
    {
        major = a_version;
        minor = "";
    }
    else
    {
        major = a_version.substr(0, pos);
        minor = a_version.substr(pos+1);
    }

    return std::pair<std::string, std::string>(major, minor);
}

void PrefsDb8Init::checkUpdateType(bool a_db_init, const std::string &a_dbVersion, const std::string &a_confVersion)
{
    if (a_db_init == false)
    {
        m_dbInitDone = false;
        m_updateType = UpdateType_eAll;
        return;
    }

    m_updateType = UpdateType_eNone;
    if (a_dbVersion==a_confVersion)
    {
        m_dbInitDone = true;
    }
    else
    {
        std::pair<std::string, std::string> dbVersion;
        std::pair<std::string, std::string> confVersion;

        dbVersion = getMajorMinorVersion(a_dbVersion);
        confVersion = getMajorMinorVersion(a_confVersion);
        m_updateType = UpdateType_eAll;
        if (dbVersion.first != confVersion.first)
        {
            m_updateType = UpdateType_eAll;
        }
        else if (dbVersion.second != confVersion.second)
        {
            m_updateType = UpdateType_eDefault;
        }
        else
        {
            m_dbInitDone = true;
        }

        /* TODO: This is temporary code. We should always keeps user-modified settings.
         *       If MAIN kind schema is changed (m_updateType should be UpdateType_eAll),
         *       we would implement new function to migrate user modified settings. */
        m_updateType = UpdateType_eDefault;
    }
}

bool PrefsDb8Init::cbCheckInitKey(LSHandle * lsHandle, LSMessage * message, void *data)
{
    std::string errorText;
    bool db_init_flag = false;
    std::string db_version_str;

    PrefsDb8Init *replyInfo = (PrefsDb8Init *) data;

    do {
          const char *payload = LSMessageGetPayload(message);
          if (!payload) {
              SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
              errorText = "missing payload";
              break;
          }

          SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

          pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
          if (root.isNull()) {
              SSERVICELOG_WARNING(MSGID_INIT_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
              errorText = "couldn't parse json";
              break;
          }

          pbnjson::JValue label(root["returnValue"]);
          if (!label.isBoolean() || !label.asBool()) {
              SSERVICELOG_WARNING(MSGID_INIT_DB_RETURNS_FAIL, 0, "payload : %s",payload);
              errorText = "DB8 error";
              break;
          }

          pbnjson::JValue resultsArray(root["results"]);
          if (!resultsArray.isArray()) {
              SSERVICELOG_WARNING(MSGID_INIT_JSON_TYPE_ARRAY_ERR, 0, "payload : %s",payload);
              errorText = "Json object error";
              break;
          }

          if(resultsArray.arraySize() != 1) {
              /* if duplicated data object exist */
              SSERVICELOG_WARNING(MSGID_INIT_JSON_TYPE_ARRLEN_ERR, 1, PMLOGKS("Reason","unexpected db8 resultsArray"),
                                  "payload : %s",payload);
              break;
          }

          pbnjson::JValue result_obj(resultsArray[0]);
          /* Simple validation */
          label = result_obj["value"];
          if (label.isNull()) {
              SSERVICELOG_WARNING(MSGID_INIT_NO_VALUE, 1, PMLOGKS("Reason","Category has no keys"),
                                  "payload : %s",payload);
              errorText = "Category has no keys";
              break;
          }

          pbnjson::JValue keyObj = label[SETTINGSSERVICE_DBVER_KEY];
          if (!keyObj.isString()) {
              SSERVICELOG_WARNING(MSGID_INIT_NO_DBVERSION, 0, "payload : %s",payload);
              errorText = "Ver Key is not in DB";
              break;
          }
          db_version_str = keyObj.asString();

          keyObj = label[SETTINGSSERVICE_INIT_KEY];
          if (!keyObj.isBoolean()) {
              SSERVICELOG_WARNING(MSGID_INIT_NO_INITKEY, 1, PMLOGKS("db_version",db_version_str.c_str()), "");
              errorText = "Init Key is not in DB";
              break;
          }
          db_init_flag = keyObj.asBool();
    } while(false);

    if(errorText.size()) {
        SSERVICELOG_DEBUG("%s: errortext : %s", __FUNCTION__, errorText.c_str());
    }

    replyInfo->checkUpdateType(db_init_flag, db_version_str, Settings::settings()->dbVersion);
    if ( UpdateType_eAll == replyInfo->m_updateType )
    {
        // Workaround - just upgrade default kind, not user kind.
        //
        replyInfo->m_updateType = UpdateType_eDefault;
    }

    if(replyInfo->loadKindInfo(false)) {
        replyInfo->getKindInfoNext();
        if(replyInfo->m_targetKindName.size()) {
            replyInfo->delKind();
        }
        else {
            SSERVICELOG_ERROR(MSGID_INIT_DB8_CONF_ERR, 0, "There is no kind info to delete");
            PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
        }
    }
    else {
        SSERVICELOG_ERROR(MSGID_ERR_LOADING_KIND_INFO, 0, " ");
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }

    return true;
}

bool PrefsDb8Init::cbLoadDefaultSettingsDB8(LSHandle * lsHandle, LSMessage * message, void *data)
{
    bool success = false;
    std::string errorText;
    pbnjson::JValue label;

    PrefsDb8Init *replyInfo = (PrefsDb8Init *) data;

    const char *payload = LSMessageGetPayload(message);
    do {
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            errorText = std::string("missing payload");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_INIT_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = std::string("couldn't parse json");
            break;
        }

        label = root["returnValue"];
        if (label.isBoolean())
            success = label.asBool();

        label = root["count"];

    } while(false);

    if (success && label.isNumber()) {
        SSERVICELOG_DEBUG("Loading default values(%d records)", label.asNumber<int>());
        replyInfo->mergeInitKey();
    } else {
        /* TODO: We need to recover this critical error
           SettingsService alwyas return fails for any request in this error */
        SSERVICELOG_ERROR(MSGID_INIT_DB_LOAD_FAIL, 0, "payload : %s",payload);
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }

    return true;
}

bool PrefsDb8Init::loadDefaultSettings()
{
    LSError lsError;
    bool result = false;

    LSErrorInit(&lsError);

    //luna-send -n 1 -a com.palm.configurator luna://com.webos.service.db/load '{"path":"/etc/palm/defaultSettings.json"}'

    result =  DB8_luna_call(m_serviceHandlePrivate,
        "luna://com.webos.service.db/load",
        "{\"path\":\"/etc/palm/defaultSettings.json\"}",
        PrefsDb8Init::cbLoadDefaultSettingsDB8, this, NULL, &lsError);

    if ( !result ) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Load default json");
        LSErrorFree(&lsError);
    }

    return result;
}

bool PrefsDb8Init::checkInitKey()
{
    pbnjson::JValue replyRoot(pbnjson::Object());
    pbnjson::JValue replyRootQuery(pbnjson::Object());
    pbnjson::JValue replyRootWhereArray(pbnjson::Array());
    pbnjson::JValue replyRootItem1(pbnjson::Object());
    LSError lsError;
    LSErrorInit(&lsError);
    bool result = false;

    // add category for where
    replyRootItem1.put("prop", "category");
    replyRootItem1.put("op", "=");
    replyRootItem1.put("val", SETTINGSSERVICE_INIT_CATEGORY);

    // build where
    replyRootWhereArray.append(replyRootItem1);
    replyRootQuery.put("where", replyRootWhereArray);

    // add from
    replyRootQuery.put("from", SETTINGSSERVICE_KIND_DEFAULT);

    // add to root
    replyRoot.put("query", replyRootQuery);

    // add reply root
    result = DB8_luna_call(m_serviceHandlePrivate, "luna://com.webos.service.db/find", replyRoot.stringify().c_str(), PrefsDb8Init::cbCheckInitKey, this, NULL, &lsError);

    if ( !result ) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Check init key");
        LSErrorFree(&lsError);
    }

    return result;
}

bool PrefsDb8Init::delKind()
{
    LSError lsError;
    LSErrorInit(&lsError);
    std::string dbQuery;
    bool result = false;

    if(!m_kindInfo) {
        SSERVICELOG_ERROR(MSGID_INIT_NO_KINDINFO, 0, " error occured during DB initialization for settingsservice");
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
        return false;
    }

    dbQuery = "{\"id\":\"" + m_targetKindName + "\"}";

    result = DB8_luna_call(m_serviceHandlePrivate, "luna://com.webos.service.db/delKind", dbQuery.c_str(), PrefsDb8Init::cbDelKind, this, NULL, &lsError);

    if ( !result ) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "delKind for init");
        LSErrorFree(&lsError);
    }

    return result;
}

bool PrefsDb8Init::regKind()
{
    bool result = false;
    LSError lsError;
    LSErrorInit(&lsError);

    std::string kindData;

    if(Utils::readFile(m_targetKindFilePath, kindData)) {
        result = DB8_luna_call(m_serviceHandlePrivate, "luna://com.webos.service.db/putKind", kindData.c_str(), PrefsDb8Init::cbRegKind, this, NULL, &lsError);

        if ( !result ) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reg kind");
            LSErrorFree(&lsError);
            PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
        }
    }
    else {
        SSERVICELOG_ERROR(MSGID_INIT_READ_DB8_CONF_ERR, 1, PMLOGKS("Kind_file",m_targetKindFilePath.c_str()), "");
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }

    return result;
}

bool PrefsDb8Init::regPermission()
{
    bool result = false;
    std::string permissionData;
    LSError lsError;
    LSErrorInit(&lsError);

    if(Utils::readFile(m_targetPermissionFilePath, permissionData)) {
        pbnjson::JValue dbQuery = pbnjson::Object();
        pbnjson::JValue permissions = pbnjson::JDomParser::fromString(permissionData);
        dbQuery.put("permissions", permissions);
        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, dbQuery.stringify().c_str());
        result = DB8_luna_call(m_serviceHandlePrivate, "luna://com.webos.service.db/putPermissions", dbQuery.stringify().c_str(), PrefsDb8Init::cbRegPermission, this, NULL, &lsError);

        if ( !result ) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reg permission");
            LSErrorFree(&lsError);
        }
    }
    else {
        SSERVICELOG_ERROR(MSGID_INIT_READ_DB8_CONF_ERR, 1, PMLOGKS("Permissions_File",m_targetPermissionFilePath.c_str()), "");
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }

    return result;
}


bool PrefsDb8Init::initKind()
{
    return checkInitKey();
}

bool PrefsDb8Init::initSubscribers()
{
    return callwithSubscribes();
}

bool PrefsDb8Init::cbCurAppIdSubscribers(LSHandle * sh, LSMessage * message, void *ctx)
{
    std::string curAppId;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        /* expected message format
            {"appId":"com.palm.app.enyo2sampler","returnValue":true,"windowId":"","processId":"2198"}
         */

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            break;
        }
        pbnjson::JValue label = root["returnValue"];
        if (!label.isBoolean() || !label.asBool()) {
            SSERVICELOG_WARNING(MSGID_INIT_SAM_RETURNS_ERR, 1, PMLOGKS("Reason","SAM returns error for forground App ID"),
                                "payload : %s",payload);
            break;
        }

        /* subscribe message doesn't include returnValue */
        label = root["appId"];
        if (!label.isString()) {
            SSERVICELOG_WARNING(MSGID_INIT_NO_APPID, 0, " ");
            break;
        }

        curAppId = label.asString();
        if (!curAppId.empty())
        {
            std::unique_ptr<std::string> appId(new std::string(curAppId));
            if (PrefsFactory::instance()->getTaskManager().pushUserMethod(
                METHODID_CHANGE_APP,
                PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE),
                nullptr,
                appId.get(),
                TASK_PUSH_BACK))
            {
                appId.release();
            }
        }

        SSERVICELOG_DEBUG("%s: currnet app id: %s", __FUNCTION__, curAppId.c_str());
    }while(false);

    return true;
}

bool PrefsDb8Init::cbListAppsSubscribers(LSHandle * sh, LSMessage * message, void *ctx)
{
    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        /* expected message format for list apps
         * {
         *    "app": { "id":"youtube.leanback.v2" } <- subscribed reply,
         *    "apps": [ {}, {} ] <- first reply ,
         *    "change": "removed",
         *    "changeReason": "appUninstalled",
         *    "returnValue": true,
         *    "subscribed": true
         * }
         */

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isNull() || !label.asBool()) {
            SSERVICELOG_WARNING(MSGID_INIT_SAM_RETURNS_ERR, 1, PMLOGKS("Reason","SAM returns error for app list"),
                  "payload : %s",payload);
            break;
        }

        label = root["change"];
        if (!label.isString() || label.asString() != "removed")
        {
            break;
        }

        label = root["changeReason"];
        if (!label.isString() || label.asString() != "appUninstalled")
        {
            break;
        }

        /* Ignore full list of apps which is in first reply */
        label = root["app"];
        if (!label.isObject())
            break;

        label = label["id"];
        if (!label.isString())
            break;

        std::string removedAppId = label.asString();
        SSERVICELOG_DEBUG("%s: removed app id: %s", __FUNCTION__, removedAppId.c_str());
        {
            std::unique_ptr<std::string> appId(new std::string(removedAppId));
            if (PrefsFactory::instance()->getTaskManager().pushUserMethod(
                    METHODID_UNINSTALL_APP,
                    PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE),
                    nullptr,
                    appId.get(),
                    TASK_PUSH_BACK))
            {
                appId.release();
            }
       }

    } while(false);

    return true;
}

void PrefsDb8Init::cancelForegroundApp()
{
    LSError lsError;
    LSErrorInit(&lsError);

    if ( m_tokenForegroundApp ) {
        if ( !LSCallCancel(m_serviceHandlePrivate, m_tokenForegroundApp, &lsError) ) {
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
        }
        m_tokenForegroundApp = 0;
    }
}

void PrefsDb8Init::cancelListApps()
{
    LSError lsError;
    LSErrorInit(&lsError);

    if ( m_tokenListApps ) {
        if ( !LSCallCancel(m_serviceHandlePrivate, m_tokenListApps, &lsError) ) {
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
        }
        m_tokenListApps = 0;
    }
}

bool PrefsDb8Init::subscribeForegroundApp()
{
    LSError lsError;
    LSErrorInit(&lsError);

    bool ret = LSCall(m_serviceHandlePrivate, "luna://com.webos.applicationManager/getForegroundAppInfo", "{\"subscribe\":true}",
            PrefsDb8Init::cbCurAppIdSubscribers, this, &m_tokenForegroundApp, &lsError);

    SSERVICELOG_DEBUG("%s: Db8 call %s %s", __FUNCTION__, "getForegroundAppInfo", "{\"subscribe\":true|");

    if (!ret) {
        SSERVICELOG_WARNING(MSGID_LSCALL_SAM_FAIL, 0, "Failed to call applicationManager to get current app id");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        m_tokenForegroundApp = 0;
    }

    return ret;
 }


bool PrefsDb8Init::subscribeListApps()
{
    LSError lsError;
    LSErrorInit(&lsError);

    bool ret = LSCall(m_serviceHandlePrivate, "luna://com.webos.applicationManager/listApps", "{\"subscribe\":true}",
            PrefsDb8Init::cbListAppsSubscribers, this, &(m_tokenListApps), &lsError);

    SSERVICELOG_DEBUG("%s: Db8 call %s %s", __FUNCTION__, "listApps", "{\"subscribe\":true|");

    if (!ret) {
        SSERVICELOG_WARNING(MSGID_LSCALL_SAM_FAIL, 0, "Failed to call applicationManager to get current app id");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        m_tokenListApps = 0;
    }

    return ret;
}

bool PrefsDb8Init::_callwithSubscribes(LSHandle *a_handle, const char *a_service, bool a_connected, void *ctx)
{
    LSError lsError;
    LSErrorInit(&lsError);

    if ( !a_handle || !ctx ) {
        SSERVICELOG_WARNING(MSGID_LSCALL_SAM_FAIL, 3,
                PMLOGKS("service", a_service),
                PMLOGKS("connected", a_connected ? "True" : "False"),
                PMLOGKFV("ctx", "\"%p\"", ctx),
                "Unexpected sam status");
        return false;
    }

    PrefsDb8Init* thiz = static_cast<PrefsDb8Init*>(ctx);

    if ( !a_connected ) {
        SSERVICELOG_WARNING(MSGID_LSCALL_SAM_FAIL, 0, "SAM Disconnected");
        thiz->cancelForegroundApp();
        thiz->cancelListApps();
        return true;
    }

    bool subsResultForeApp = thiz->subscribeForegroundApp();
    bool subsResultListApp = thiz->subscribeListApps();

    if ( !subsResultForeApp || !subsResultListApp ) {
        SSERVICELOG_WARNING(MSGID_LSCALL_SAM_FAIL, 0, "SAM Connected, but fail to subscribe");
    }

    return subsResultForeApp && subsResultListApp;
}

bool PrefsDb8Init::callwithSubscribes(void)
{
    LSError lsError;
    LSErrorInit(&lsError);
    void* cookie;

    bool ret = LSRegisterServerStatusEx(m_serviceHandlePrivate,
                        APPLICATION_MANAGER, PrefsDb8Init::_callwithSubscribes,
                        this, &cookie, &lsError);

    if ( !ret || !cookie ) {
        SSERVICELOG_WARNING(MSGID_LSCALL_SAM_FAIL, 0, "Failed to monitor sam status");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        return false;
    }

    m_serviceCookies.insert( { APPLICATION_MANAGER, cookie } );

    return true;
}


bool PrefsDb8Init::putInitKey()
{
    LSError lsError;
    LSErrorInit(&lsError);
    bool result;

    pbnjson::JValue valueObj(pbnjson::Object());
    valueObj.put(SETTINGSSERVICE_INIT_KEY, true);
    valueObj.put(SETTINGSSERVICE_DBVER_KEY, Settings::settings()->dbVersion);

    pbnjson::JValue initObj(pbnjson::Object());
    initObj.put("_kind", SETTINGSSERVICE_KIND_DEFAULT);
    initObj.put("app_id", "");
    initObj.put("category", SETTINGSSERVICE_INIT_CATEGORY);
    initObj.put("volatile", false);
    initObj.put("value", valueObj);

    pbnjson::JValue objArray(pbnjson::Array());
    objArray.append(initObj);

    pbnjson::JValue replyRoot(pbnjson::Object());
    replyRoot.put("objects", objArray);

    result = DB8_luna_call(m_serviceHandlePrivate, "luna://com.webos.service.db/put", replyRoot.stringify().c_str(), PrefsDb8Init::cbPutInitKey, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_MERGE_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    return true;
}

bool PrefsDb8Init::mergeInitKey()
{
    LSError lsError;
    LSErrorInit(&lsError);
    bool result;

    pbnjson::JValue whereArray(pbnjson::Array());
    pbnjson::JValue whereItem(pbnjson::Object());
    whereItem.put("prop", "app_id");
    whereItem.put("op", "=");
    whereItem.put("val", "");
    whereArray.append(whereItem);
    whereItem = pbnjson::Object();
    whereItem.put("prop", "category");
    whereItem.put("op", "=");
    whereItem.put("val", SETTINGSSERVICE_INIT_CATEGORY);
    whereArray.append(whereItem);

    pbnjson::JValue queryObj(pbnjson::Object());
    queryObj.put("from", SETTINGSSERVICE_KIND_DEFAULT);
    queryObj.put("where", whereArray);

    pbnjson::JValue dbInitFlagObj(pbnjson::Object());
    dbInitFlagObj.put(SETTINGSSERVICE_INIT_KEY, true);

    pbnjson::JValue propsObj(pbnjson::Object());
    propsObj.put("value", dbInitFlagObj);

    pbnjson::JValue replyRoot(pbnjson::Object());
    replyRoot.put("query", queryObj);
    replyRoot.put("props", propsObj);

    result = DB8_luna_call(m_serviceHandlePrivate, "luna://com.webos.service.db/merge", replyRoot.stringify().c_str(), PrefsDb8Init::cbMergeInitKey, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_MERGE_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }

    LSErrorInit(&lsError);

    /* create null category in the system KIND
     * to avoid localeInfo object doesn't merge */
    result = DB8_luna_call(m_serviceHandlePrivate, "luna://com.webos.service.db/find",
            "{\"query\":{\"from\":\"com.webos.settings.system:1\", \"where\":[{\"prop\":\"category\",\"op\":\"=\",\"val\":\"\"}]}}",
            PrefsDb8Init::cbFindNullCategory, this, NULL, &lsError);

    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "create null category");
        LSErrorFree(&lsError);
    }

    return true;
}

bool PrefsDb8Init::cbFindNullCategory(LSHandle * sh, LSMessage * message, void *ctx)
{
    LSError lsError;
    LSErrorInit(&lsError);

    bool success = false;
    PrefsDb8Init *replyInfo = (PrefsDb8Init *)ctx;

    const char *payload = LSMessageGetPayload(message);
    if (!payload) {
        return false;
    }

    pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
    if (root.isNull()) {
        return false;
    }

    pbnjson::JValue label = root["returnValue"];
    if (label.isBoolean())
        success = label.asBool();
    if (success == false)
        return true;

    pbnjson::JValue null_category = root["results"];

    if (null_category.isArray() && null_category.arraySize() == 0 ) {
        bool result = DB8_luna_call(replyInfo->m_serviceHandlePrivate, "luna://com.webos.service.db/put",
                "{\"objects\":[{\"_kind\":\"com.webos.settings.system:1\", \"value\":{}, \"app_id\":\"\", \"category\":\"\", \"volatile\":false}]}",
                PrefsDb8Init::cbCreateNullCategory, NULL, NULL, &lsError);

        if (!result) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
            LSErrorFree(&lsError);
        }
    }

    return true;
}

bool PrefsDb8Init::cbCreateNullCategory(LSHandle * sh, LSMessage * message, void *ctx)
{
    /* nothing to do */
    return true;
}

bool PrefsDb8Init::cbMergeInitKey(LSHandle * sh, LSMessage * message, void *ctx)
{
    bool success = false;
    PrefsDb8Init *replyInfo = (PrefsDb8Init *)ctx;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        /* expected message format
           { "returnValue": true }
         */

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean())
            success = label.asBool();

        label = root["count"];
        if (!label.isNumber() || label.asNumber<int>() == 0 )
            success = false;

    } while(false);

    if (success != true) {
        SSERVICELOG_WARNING(MSGID_INIT_STATUS_UPDATE_FAIL, 0, " ");
        replyInfo->putInitKey();
    }
    else {
        SSERVICELOG_DEBUG("DB init status is set");
        PrefsKeyDescMap::instance()->initKeyDescMap();
    }

    return true;
}

bool PrefsDb8Init::cbPutInitKey(LSHandle * sh, LSMessage * message, void *ctx)
{
    bool success = false;

    do {
        const char *payload = LSMessageGetPayload(message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_INIT_PAYLOAD_MISSING, 0, " ");
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);

        /* expected message format
           { "returnValue": true }
         */

        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            break;
        }

        pbnjson::JValue label = root["returnValue"];
        if (label.isBoolean())
            success = label.asBool();
    } while(false);

    if (success != true) {
        SSERVICELOG_WARNING(MSGID_INIT_STATUS_UPDATE_FAIL, 0, " ");
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
    }
    else {
        SSERVICELOG_DEBUG("DB init status is set");
        PrefsKeyDescMap::instance()->initKeyDescMap();
    }

    return true;
}


// TODO: loading file lists from /etc/palm/db/kinds
bool PrefsDb8Init::KindNameInfo::loadAllKindNameToDel(bool a_defaultOnly)
{
    kindNameList.clear();

    if (a_defaultOnly)
    {
        kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN_VOLATILE);
        kindNameList.push_back(SETTINGSSERVICE_KIND_COUNTRY);           // FIXME: delete this?
        kindNameList.push_back(SETTINGSSERVICE_KIND_DEFAULT);
        kindNameList.push_back(SETTINGSSERVICE_KIND_DFLT_CTRY_DESC);    // FIXME: delete this?
        kindNameList.push_back(SETTINGSSERVICE_KIND_OVER_CTRY_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND_DFLT_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND_OVER_DESC);
    }
    else
    {
        kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN_VOLATILE);
        kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN);
        kindNameList.push_back(SETTINGSSERVICE_KIND_COUNTRY);           // For backward compatibility.
        kindNameList.push_back(SETTINGSSERVICE_KIND_DEFAULT);
        kindNameList.push_back(SETTINGSSERVICE_KIND_DFLT_CTRY_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND_OVER_CTRY_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND_DFLT_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND_OVER_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND_DESC);
        kindNameList.push_back(SETTINGSSERVICE_KIND);
    }

    itemN = kindNameList.size();
    itCurKindName = kindNameList.end();

    return true;
}

bool PrefsDb8Init::KindNameInfo::loadAllKindNameToReg(bool a_defaultOnly)
{
    kindNameList.clear();

    /* There are any side effects even
     * settingsservice try to create already existed kind */

    kindNameList.push_back(SETTINGSSERVICE_KIND);
    kindNameList.push_back(SETTINGSSERVICE_KIND_DESC);
    kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN_DESC);
    kindNameList.push_back(SETTINGSSERVICE_KIND_DFLT_DESC);
//    kindNameList.push_back(SETTINGSSERVICE_KIND_DFLT_CTRY_DESC);
    kindNameList.push_back(SETTINGSSERVICE_KIND_DEFAULT);
//    kindNameList.push_back(SETTINGSSERVICE_KIND_COUNTRY);
    kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN);
    kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN_VOLATILE);

    itemN = kindNameList.size();
    itCurKindName = kindNameList.end();

    return true;
}

bool PrefsDb8Init::KindNameInfo::loadVolatileKindName() {
    kindNameList.push_back(SETTINGSSERVICE_KIND_MAIN_VOLATILE);

    itemN = kindNameList.size();

    return true;
}

bool PrefsDb8Init::KindNameInfo::loadPermission() {
    kindNameList.clear();

    kindNameList.push_back(SETTINGSSERVICE_KIND_PERMISSION_MAIN);
    kindNameList.push_back(SETTINGSSERVICE_KIND_PERMISSION_DESC);

    itemN = kindNameList.size();
    itCurKindName = kindNameList.end();

    return true;
}

void PrefsDb8Init::KindNameInfo::getPermissionInfoNext(std::string& permissionPath) {

    if(itCurKindName == kindNameList.end()) {
        itCurKindName = kindNameList.begin();
    }
    else {
        itCurKindName++;
    }

    if(itCurKindName != kindNameList.end()) {
        permissionPath = PERMISSION_FILEPATH_BASE;
        permissionPath += *itCurKindName;
    }
    else {
        permissionPath = "";
    }
}

void PrefsDb8Init::KindNameInfo::getKindInfoNext(std::string& kindName, std::string& kindPath) {

    if(itCurKindName == kindNameList.end()) {
        itCurKindName = kindNameList.begin();
    }
    else {
        itCurKindName++;
    }

    if(itCurKindName != kindNameList.end()) {
        unsigned int pos;
        kindName = *itCurKindName;
        pos = kindName.find(":");
        kindPath = KINDFILEPATH_BASE;
        kindPath += kindName.substr(0,pos);
    }
    else {
        kindName = "";
        kindPath = "";
    }
}
