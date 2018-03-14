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

#include <list>

#include "Logging.h"
#include "PrefsDb8Del.h"
#include "PrefsDb8DelDesc.h"
#include "PrefsDb8Get.h"
#include "PrefsDb8GetValues.h"
#include "PrefsDb8Set.h"
#include "PrefsDb8SetValues.h"
#include "PrefsFactory.h"
#include "PrefsFactory.h"
#include "SettingsService.h"
#include "SettingsServiceApi.h"
#include "Utils.h"
#include "AccessChecker.h"

extern MethodTaskMgr methodTaskMgr;

static LSMethod s_methods[] = {
        // public:
    {SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS, cbGetSystemSettings},
    {SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES, cbGetSystemSettingValues},
    {SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS, cbSetSystemSettings},
        // private:
    {SETTINGSSERVICE_METHOD_BATCH, cbBatchProcess},
    {SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGFACTORYVALUE, cbGetSystemSettingFactoryValue},
    {SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYVALUE, cbSetSystemSettingFactoryValue},
    {SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGVALUES, cbSetSystemSettingValues},
    {SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC, cbGetSystemSettingDesc},
    {SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGDESC, cbSetSystemSettingDesc},
    {SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYDESC, cbSetSystemSettingFactoryDesc},
    {SETTINGSSERVICE_METHOD_GETCURRENTSETTINGS, cbGetCurrentSettings},
    {SETTINGSSERVICE_METHOD_DELETESYSTEMSETTINGS, cbDelSystemSettings},
    {SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGS, cbResetSystemSettings},
    {SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGDESC, cbResetSystemSettingDesc},
        // end
    {0, 0}
};

LSMethod* SettingsServiceApi_GetMethods()
{
    return s_methods;
}

void sendErrorReply(LSHandle * lsHandle, LSMessage * message, pbnjson::JValue replyObj)
{
    LSError lsError;

    LSErrorInit(&lsError);

    bool retVal = LSMessageReply(lsHandle, message, replyObj.stringify().c_str(), &lsError);
    if (!retVal) {
        LSErrorFree(&lsError);
        SSERVICELOG_TRACE("Error reply in %s: %s", __FUNCTION__, replyObj.stringify().c_str());
    }
}

void sendErrorReply(LSHandle * lsHandle, LSMessage * message, const std::string& method, const std::string& errorText, bool subscribe)
{
    LSError lsError;

    LSErrorInit(&lsError);

    pbnjson::JValue reply(pbnjson::Object());

    reply.put("returnValue", false);
    reply.put("errorText", errorText);
    reply.put("method", method);
    if(subscribe)
        reply.put("subscribed", false);

    bool retVal = LSMessageReply(lsHandle, message, reply.stringify().c_str(), &lsError);
    if (!retVal) {
        LSErrorFree(&lsError);
        SSERVICELOG_WARNING(MSGID_SEND_ERR_REPLY_FAIL, 1, PMLOGKS("ErrorText",errorText.c_str()), "");
    }
}

bool cbBatchProcess(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    LSError lsError;
    bool root_subscribe = false;
    bool success = false;
    std::string errorText;

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_BATCH_PARAM_V2)

    const char *payload = LSMessageGetPayload(message);
    if (!payload) {
        SSERVICELOG_WARNING(MSGID_BATCH_PAYLOAD_MISSING, 0, " ");
        errorText = "LunaBus Msg Fail!";
        return false;
    }

    LSErrorInit(&lsError);

    SSERVICELOG_TRACE("Entering function : %s", __FUNCTION__);

    do {
        pbnjson::JValue root = pbnjson::JDomParser::fromString(payload);
        if (root.isNull()) {
            SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
            errorText = "Parsing Fail!";
            break;
        }

        pbnjson::JValue operationArray = root["operations"];
        if (!operationArray.isArray() || operationArray.arraySize() <= 0) {
            errorText = "no operations specified";
            break;
        }

        pbnjson::JValue label = root["subscribe"];
        if (label.isBoolean()) {
            root_subscribe = label.asBool();
        }

        std::list<tBatchParm> batchParmList;

        // parsing request msg and create task list
        bool done = false;
        for (pbnjson::JValue obj : operationArray.items()) {
            tBatchParm item;

            pbnjson::JValue label = obj["method"];
            if (!label.isString()) {
                errorText = "no method specified";
                done = true;
                break;
            }
            std::string method = label.asString();

            label = obj["params"];
            if (label.isNull()) {
                errorText = "no params specified";
                done = true;
                break;
            }
            item.params = label;

            label = label["subscribe"];
            if (label.isBoolean() && label.asBool() == true && root_subscribe == false)
            {
                errorText = "Incorrect subscribe option";
                done = true;
                break;
            }

            item.method = method;

            SSERVICELOG_DEBUG("method:%s / params: %s\n", method.c_str(), label.stringify().c_str());
            batchParmList.push_back(item);
        }
        if (done)
            break;

        success = methodTaskMgr.pushBatchMethod(lsHandle, message, batchParmList);
        if(!success) {
            errorText = "Error!! to insert Batch method to the task queue";
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (!success) {
        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_BATCH, errorText, true);
    }

    return true;
}

bool doResetSystemSettingDesc(LSHandle *lsHandle, LSMessage *message, MethodCallInfo* pTaskInfo)
{
    std::string errorText;
    std::string app_id = GLOBAL_APP_ID;
    std::string category;
    std::list<std::string> keyList;
    PrefsDb8DelDesc *delReq = NULL;
    bool success = false;
    pbnjson::JValue root;

    do {
      if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root["category"];
        if (label.isString()) {
            category = label.asString();
        }

        label = root[KEYSTR_APPID];
        if (label.isString()) {
            app_id = label.asString();
        }

        pbnjson::JValue keyArray = root["keys"];
        if (!keyArray.isValid()) {
             errorText = "no keys specified";
             break;
        }
        if (!keyArray.isArray() || keyArray.arraySize() <= 0) {
            errorText = "invalid key array";
            break;
        }
        for (pbnjson::JValue obj : keyArray.items()) {
            if (obj.isString())
                keyList.push_back(obj.asString());
        }

        // Send LS2 response only if success in PrefsDb8DelDesc
        delReq = new PrefsDb8DelDesc(keyList, category, pTaskInfo, message);
        if (delReq) {
            delReq->ref();
            delReq->setAppId(app_id);
            success = delReq->sendRequest(lsHandle, errorText);
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (success) {
        // do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGDESC);

        if (!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (delReq) {
        // Constructed with refCount=1.
        delReq->unref();
    }

    return true;
}

bool cbGetSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ((PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }

#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_GET_SYSTEM_SETTINGS_PARAM_V2)

    static AccessChecker accessChecker(lsHandle, PrefsFactory::service_root_uri + SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGSPRIV);

    bool checked = accessChecker.check(message, [user_data, lsHandle](LSMessage* msg, bool allowed)
    {
        if (!allowed && !static_cast<PrefsFactory*>(user_data)->hasAccess(lsHandle, msg))
        {
            sendErrorReply(lsHandle, msg, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS, "Access denied", true);
        } else if (!methodTaskMgr.push(METHODID_GETSYSTEMSETTINGS, lsHandle, msg))
        {
            sendErrorReply(lsHandle, msg, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS, "Failed to insert method to the task queue", true);
        }
    });

    if (!checked)
    {
        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS, "Error checking access rights", true);
        return true;
    }

    return true;
}

bool cbSetSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_SET_SYSTEM_SETTINGS_PARAM_V2)

    static AccessChecker accessChecker(lsHandle, PrefsFactory::service_root_uri + SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGSPRIV);

    bool checked = accessChecker.check(message, [user_data, lsHandle](LSMessage* msg, bool allowed)
    {
        if (!allowed && !static_cast<PrefsFactory*>(user_data)->hasAccess(lsHandle, msg))
        {
            sendErrorReply(lsHandle, msg, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS, "Access denied", false);
        } else if (!methodTaskMgr.push(METHODID_SETSYSTEMSETTINGS, lsHandle, msg))
        {
            sendErrorReply(lsHandle, msg, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS, "Failed to insert method to the task queue", true);
        }
    });

    if (!checked)
    {
        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS, "Error checking access rights", true);
        return true;
    }

    return true;
}

bool cbGetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_GETSYSTEMSETTINGFACTORYVALUE;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_GET_SYSTEM_SETTING_FACTORY_VALUE_PARAM_V2)

    if (!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";
        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGFACTORYVALUE, errorText, false);
    }

    return true;
}

bool cbSetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_SETSYSTEMSETTINGFACTORYVALUE;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_SET_SYSTEM_SETTING_FACTORY_VALUE_PARAM_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYVALUE, errorText, false);
    }

    return true;

}

bool cbGetCurrentSettings(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_GETCURRENTSETTINGS;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_GET_CURRENT_SETTINGS_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_GETCURRENTSETTINGS, errorText, true);
    }

    return true;

}

bool cbGetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_GET_SYSTEM_SETTING_VALUES_PARAM_V2)

    static AccessChecker accessChecker(lsHandle, PrefsFactory::service_root_uri + SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUESPRIV);

    bool checked = accessChecker.check(message, [user_data, lsHandle](LSMessage* lsMsg, bool allowed)
    {
        if (!allowed && !static_cast<PrefsFactory*>(user_data)->hasAccess(lsHandle, lsMsg))
        {
            sendErrorReply(lsHandle, lsMsg, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES, "Access denied", true);
        } else if (!methodTaskMgr.push(METHODID_GETSYSTEMSETTINGVALUES, lsHandle, lsMsg))
        {
            sendErrorReply(lsHandle, lsMsg, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES, "Failed to insert method to the task queue", true);
        }
    });

    if (!checked)
    {
        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES, "Error checking access rights", true);
        return true;
    }

    return true;
}

bool cbSetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_SETSYSTEMSETTINGVALUES;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*) user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_SET_SYSTEM_SETTING_VALUES_PARAM_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGVALUES, errorText, false);
    }

    return true;

}

bool cbGetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_GETSYSTEMSETTINGDESC;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_GET_SYSTEM_SETTING_DESC_PARAM_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC, errorText, true);
    }

    return true;

}

bool cbSetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_SETSYSTEMSETTINGDESC;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_SET_SYSTEM_SETTING_DESC_PARAM_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGDESC, errorText, false);
    }

    return true;

}

bool cbSetSystemSettingFactoryDesc(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_SETSYSTEMSETTINGFACTORYDESC;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    /* the schema is same with setSystemSettingDesc */
    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_SET_SYSTEM_SETTING_DESC_PARAM_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYDESC, errorText, false);
    }

    return true;

}

bool cbDelSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_DELETESYSTEMSETTINGS;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_DEL_SYSTEM_SETTINGS_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_DELETESYSTEMSETTINGS, errorText, false);
    }

    return true;

}

bool cbResetSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_RESETSYSTEMSETTINGS;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_RESET_SYSTEM_SETTINGS_V2)

    if(!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGS, errorText, false);
    }

    return true;

}

bool cbResetSystemSettingDesc(LSHandle *lsHandle, LSMessage *message, void *user_data)
{
    Utils::Instrument::writeRequest(message);

    MethodId methodId = METHODID_RESETSYSTEMSETTINGDESC;

#ifdef CHECK_LEGACY_SERVICE_USAGE
    if ( (PrefsFactory*)user_data != PrefsFactory::instance() ) {
        SSERVICELOG_DEBUG("Legacy service is called by '%s'", LSMessageGetApplicationID(message) == NULL ?
                LSMessageGetSender(message) : LSMessageGetApplicationID(message));
    }
#endif

    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, JSON_SCHEMA_RESET_SYSTEM_SETTING_DESC_V2)

    if (!methodTaskMgr.push(methodId, lsHandle, message)) {
        std::string errorText;

        errorText = "Error!! to insert method " + methodTaskMgr.getMethodName(methodId) + " to task que";

        sendErrorReply(lsHandle, message, SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGDESC, errorText, false);
    }

    return true;
}

bool doGetSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    std::set < std::string > keyList;
    bool subscribe = false;
    bool success = false;
    std::string errorText;
    std::string app_id;
    std::string category;
    bool current_app = false;
    pbnjson::JValue root;
    pbnjson::JValue keyArray;
    pbnjson::JValue dimension;

    PrefsDb8Get *getReq = NULL;

    do {
          if (pTaskInfo->isTaskInQueue() && !PrefsFactory::instance()->isReady()) {
              SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
              errorText = "Service is not ready";
              break;
          }

          if(!pTaskInfo->isBatchCall()) {
              const char *payload = LSMessageGetPayload(message);
              if (!payload) {
                  SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                  errorText = "LunaBus Msg Fail!";
                  break;
              }

              root = pbnjson::JDomParser::fromString(payload);
              if (root.isNull()) {
                  SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
                  errorText = "Parsing Fail!";
                  break;
              }
          }
          else {
              root = pTaskInfo->getBatchParam();
          }

          SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

          pbnjson::JValue label = root["subscribe"];
          if (label.isBoolean())
              subscribe = label.asBool();

          label = root["category"];
          if (label.isString())
              category = label.asString();

          label = root["dimension"];
          if (!label.isNull())
              dimension = label;

          label = root["current_app"];
          if (label.isBoolean())
              current_app = label.asBool();

          if(current_app) {
              SSERVICELOG_DEBUG("set current AppId: %s in %s\n", PrefsFactory::instance()->getCurrentAppId(), __FUNCTION__);
              app_id = PrefsFactory::instance()->getCurrentAppId();
          }
          else {
              label = root["app_id"];
              if (label.isString())
                  app_id = label.asString();
          }

          label = root["keys"];
          if (label.isNull()) {
              label = root["key"];
              if (label.isString()) {
                  keyArray = pbnjson::Array();
                  keyArray.append(label);
              } else if (category.empty()) {
                  errorText = "Error!! Both 'category' and 'keys' should not be empty!";
                  break;
              }
              else {
                  // category only search
              }
          } else {
              if (!label.isArray()) {
                  errorText = std::string("'keys' should be set json_array type");
                  break;
              }

              keyArray = label;
          }

          if (keyArray.isArray()) {
              if (keyArray.arraySize() <= 0) {
                  errorText = "invalid key array";
                  break;
              } else {
                  for (pbnjson::JValue obj : keyArray.items()) {
                      if (obj.isString())
                          keyList.insert(obj.asString());
                  }
              }
          } else {
              if (category.empty()) {
                  errorText = "One of keys or category shouldn't be NULL";
                  break;
              }
              // for all keys
          }

      #ifdef LEGACY_LOCK_REQ_SUPPORT
          /* TODO: this code should be removed after lock key movement is finished
           * This code insert appropriate categroy name if user omit changed new category */
          if (std::find(keyList.begin(), keyList.end(), "applockPerApp")!=keyList.end() ||
              std::find(keyList.begin(), keyList.end(), "parentalControl")!=keyList.end() ||
              std::find(keyList.begin(), keyList.end(), "lockByAge")!=keyList.end() ||
              std::find(keyList.begin(), keyList.end(), "systemPin")!=keyList.end())
          {
              if (category.empty()) category = "lock";
          }
          else if (std::find(keyList.begin(), keyList.end(), "allowMobileDeviceAccess")!=keyList.end())
          {
              if (category.empty()) category = "network";
          }
      #endif

          getReq = new PrefsDb8Get(keyList, app_id, category, dimension, subscribe, message);
          if(getReq) {
              getReq->ref();
              getReq->setTaskInfo(pTaskInfo);
              success = getReq->sendRequest(lsHandle);
          }
          else {
              errorText = "ERROR!! send a request to DB";
          }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (success) {
        SSERVICELOG_DEBUG("Waiting for process.");
        //              do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS);
        replyObj.put("subscribed", false);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (getReq)
    {
        // Constructed with refCount=1.
        //
        getReq->unref();
    }

    return true;
}

bool doSetSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    bool success = false;
    std::string errorText;
    std::string app_id;
    std::string category;

    PrefsDb8Set *setReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root[KEYSTR_STORE];
        bool store = label.isBoolean() ? label.asBool() : true;

        label = root["valueCheck"];
        bool valueCheck = label.isBoolean() ? label.asBool() : true;

        label = root["notify"];
        bool notify = label.isBoolean() ? label.asBool() : true;

        if (store == false && notify == false) {
            errorText = "Both store and notify couldn't be false";
            break;
        }

        label = root["notifySelf"];
        bool notifySelf = label.isBoolean() ? label.asBool() : true;

        label = root["current_app"];
        bool current_app = label.isBoolean() ? label.asBool() : false;

        if(current_app) {
            SSERVICELOG_DEBUG("set current AppId: %s in %s\n", PrefsFactory::instance()->getCurrentAppId(), __FUNCTION__);
            app_id = PrefsFactory::instance()->getCurrentAppId();
        }
        else {
            label = root["app_id"];
            if (label.isString())
                app_id = label.asString();
        }

        label = root["category"];
        if (label.isString())
            category = label.asString();

        pbnjson::JValue dimension = root["dimension"];

        label = root["setAll"];
        bool setAll = label.isBoolean() ? label.asBool() : false;

        if (setAll && !dimension.isNull()) {
            errorText = std::string("Used both setAll and dimension");
            break;
        }

        pbnjson::JValue keyList = root["settings"];
        if (keyList.isNull()) {
            errorText = "no settings specified";
            break;
        }
        if (!keyList.isObject() || keyList.objectSize() <= 0) {
            errorText = "'Key' shouldn't be NULL";
            break;
        }

    #ifdef LEGACY_LOCK_REQ_SUPPORT
        /* TODO: this code should be removed after lock key movement is finished
         * This code insert appropriate categroy name if user omit changed new category */
        if (!keyList["applockPerApp"].isNull() ||
            !keyList["parentalControl"].isNull()||
            !keyList["lockByAge"].isNull()||
            !keyList["systemPin"].isNull())
        {
            if (category.empty()) category = "lock";
        }
        else if (!keyList["allowMobileDeviceAccess"].isNull())
        {
            if (category.empty()) category = "network";
        }
    #endif

        // incress reference count
        setReq = new PrefsDb8Set(keyList, app_id, category, dimension, message);
        if(setReq) {
            setReq->ref();
            setReq->setTaskInfo(pTaskInfo);
            setReq->setStoreFlag(store);
            setReq->setNotifyFlag(notify);
            setReq->setNotifySelf(notifySelf);
            setReq->setValueCheck(valueCheck);
            setReq->setAllFlag(setAll);
            success = setReq->sendRequest(lsHandle);
            if ( success == false ) {
                errorText = "Fail to send request";
            }
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    if (!success) {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (setReq) {
        // Constructed with refCount=1.
        //
        setReq->unref();
    }

    return true;
}

bool doGetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    pbnjson::JValue keyArray;
    pbnjson::JValue dimension;
    std::set < std::string > keyList;
    bool success = false;
    std::string errorText;
    std::string app_id;
    std::string category;
    bool current_app = false;

    PrefsDb8Get *getReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "function : %s, payload : %s", __FUNCTION__, payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root["category"];
        if (label.isString())
            category = label.asString();

        dimension = root["dimension"];
        if (dimension.isNull())
            {}

        label = root["current_app"];
        if (label.isBoolean())
            current_app = label.asBool();

        if(current_app) {
            SSERVICELOG_DEBUG("set current AppId: %s in %s\n", PrefsFactory::instance()->getCurrentAppId(), __FUNCTION__);
            app_id = PrefsFactory::instance()->getCurrentAppId();
        }
        else {
            label = root["app_id"];
            if (label.isString())
                app_id = label.asString();
        }

        label = root["keys"];
        if (label.isNull()) {
            label = root["key"];
            if (label.isString()) {
                keyArray = pbnjson::Array();
                keyArray.append(label);
            } else if (category.empty()) {
                errorText = "Error!! Both 'category' and 'keys' should not be empty!";
                break;
            }
            else {
                // category only search
            }
        } else {
            if (!label.isArray()) {
                errorText = std::string("'keys' should be set json_array type");
                break;
            }

            keyArray = label;
        }

        if (keyArray.isArray()) {
            if (keyArray.arraySize() <= 0) {
                errorText = "invalid key array";
                break;
            } else {
                for (pbnjson::JValue obj : keyArray.items()) {
                    if (!obj.isString())
                        continue;
                    keyList.insert(obj.asString());
                }
            }
        } else {
            if (category.empty()) {
                errorText = "One of keys or category shouldn't be NULL";
                break;
            }
            // for all keys
        }

    #ifdef LEGACY_LOCK_REQ_SUPPORT
        /* TODO: this code should be removed after lock key movement is finished
         * This code insert appropriate categroy name if user omit changed new category */
        if (std::find(keyList.begin(), keyList.end(), "applockPerApp")!=keyList.end() ||
            std::find(keyList.begin(), keyList.end(), "parentalControl")!=keyList.end() ||
            std::find(keyList.begin(), keyList.end(), "lockByAge")!=keyList.end() ||
            std::find(keyList.begin(), keyList.end(), "systemPin")!=keyList.end())
        {
            if (category.empty()) category = "lock";
        }
        else if (std::find(keyList.begin(), keyList.end(), "allowMobileDeviceAccess")!=keyList.end())
        {
            if (category.empty()) category = "network";
        }
    #endif

        getReq = new PrefsDb8Get(keyList, app_id, category, dimension, false, message);
        if(getReq) {
            getReq->ref();
            getReq->setTaskInfo(pTaskInfo);
            getReq->setFactoryValueRequest(true);
            success = getReq->sendRequest(lsHandle);
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (success) {
        SSERVICELOG_DEBUG("Waiting for process.");
        //              do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGFACTORYVALUE);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (getReq)
    {
        // Constructed with refCount=1.
        //
        getReq->unref();
    }

    return true;
}

bool doSetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    pbnjson::JValue keyList;
    pbnjson::JValue dimension;
    bool success = false;
    bool setAll = false;
    std::string errorText;
    std::string app_id;
    std::string category;
    std::string country;
    bool valueCheck = false;

    PrefsDb8Set *setReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR , 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        // appId
        pbnjson::JValue label = root[KEYSTR_APPID];
        if (label.isString()) {
            app_id = label.asString();
        }

        label = root["category"];
        if (label.isString())
            category = label.asString();

        label = root["dimension"];
        if (!label.isNull())
            dimension = label;

        label = root["setAll"];
        if (label.isBoolean()) {
            setAll = label.asBool();
        } else {
            setAll = false;
        }

        if ( setAll && !dimension.isNull() ) {
            errorText = std::string("Used both setAll and dimension");
            break;
        }

        label = root["valueCheck"];
        if (label.isBoolean()) {
            valueCheck = label.asBool();
        } else {
            valueCheck = false;
        }

        label = root["country"];
        if (label.isString())
            country = label.asString();

        keyList = root["settings"];
        if (keyList.isNull()) {
            errorText = "no settings specified";
            break;
        }
        if (!keyList.isObject()) {
            errorText = "'settings' should be set json_object type";
            break;
        }
        if (keyList.objectSize() == 0) {
            errorText = "'Key' shouldn't be NULL";
            break;
        }

    #ifdef LEGACY_LOCK_REQ_SUPPORT
        /* TODO: this code should be removed after lock key movement is finished
         * This code insert appropriate categroy name if user omit changed new category */
        if (!keyList["applockPerApp"].isNull() ||
            !keyList["parentalControl"].isNull() ||
            !keyList["lockByAge"].isNull() ||
            !keyList["systemPin"].isNull())
        {
            if (category.empty()) category = "lock";
        }
        else if (!keyList["allowMobileDeviceAccess"].isNull())
        {
            if (category.empty()) category = "network";
        }
    #endif

        // incress reference count
        setReq = new PrefsDb8Set(keyList, app_id, category, dimension, message);
        if(setReq) {
            setReq->ref();
            setReq->setTaskInfo(pTaskInfo);
            setReq->setDefKindFlag();
            setReq->setAllFlag(setAll);
            setReq->setValueCheck(valueCheck);
            if(!country.empty()) {
                setReq->setCountry(country);
            }
            success = setReq->sendRequest(lsHandle);
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    if (success) {
        //      sent reply in PrefsDb8Set
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYVALUE);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (setReq) {
        // Constructed with refCount=1.
        //
        setReq->unref();
    }

    return true;
}

bool doGetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    bool subscribe = false;
    bool success = false;
    std::string errorText;
    std::string key;
    std::string app_id = GLOBAL_APP_ID;
    std::string category;
    pbnjson::JValue root;

    PrefsDb8GetValues *getValuesReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }


            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root["key"];
        if (!label.isString()) {
            errorText = "no key specified";
            break;
        } else {
            key = label.asString();
            if (key.empty()) {
                errorText = "no key specified";
                break;
            }
        }

        label = root[KEYSTR_APPID];
        if (label.isString()) {
            app_id = label.asString();
        }

        label = root[KEYSTR_CATEGORY];
        if (label.isString()) {
            category = label.asString();
        }

        // some requests omits category field
        // try to fill category based on key
        if (category.empty()) {
            PrefsKeyDescMap::instance()->getCategory(key, category);
        }

        label = root["subscribe"];
        if (label.isBoolean())
            subscribe = label.asBool();

        getValuesReq = new PrefsDb8GetValues(key, category, subscribe, message);
        if(getValuesReq) {
            getValuesReq->ref();
            getValuesReq->setTaskInfo(pTaskInfo);
            getValuesReq->setAppId(app_id);
            success = getValuesReq->sendRequest(lsHandle);
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (success) {
        //              do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES);
        replyObj.put("subscribed", false);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (getValuesReq) {
        // Constructed with refCount=1.
        //
        getValuesReq->unref();
    }

    return true;
}

bool doSetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    bool success = false;
    std::string errorText;
    std::string key;
    std::string vtype;
    std::string op;
    std::string vtypeRef;
    pbnjson::JValue values;
    bool notifySelf = true;

    PrefsDb8SetValues *setValuesReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        /* TODO: category should be supported */

        // op
        pbnjson::JValue label = root["op"];
        if (!label.isString()) {
            errorText = "no op specified";
            break;
        } else {
            op = label.asString();
            if (op != "add" && op != "remove" && op != "set" && op != "update") {
                errorText = "op should be one of add, remove, or set";
                break;
            }
        }

        // key
        label = root["key"];
        if (!label.isString()) {
            errorText = "no key specified";
            break;
        }
        key = label.asString();

        // vtype
        label = root["vtype"];
        if (!label.isString()) {
            errorText = "no vtype specified";
            break;
        }
        vtype = label.asString();
        if (vtype == "Array") {
            vtypeRef = "array";
        } else if (vtype == "ArrayExt") {
            vtypeRef = "arrayExt";
        } else if (vtype == "Range") {
            vtypeRef = "range";
        } else if (vtype == "Date") {
            vtypeRef = "date";
        } else if (vtype == "Callback") {
            vtypeRef = "callback";
        } else if (vtype == "File") {
            vtypeRef = "file";
        } else {
            errorText = "vtype should be one of Array, Range or Date";
            break;
        }

        label = root["notifySelf"];
        if (label.isBoolean())
        {
            notifySelf = label.asBool();
        }

        // value
        values = root["values"];
        if (!values.isObject()) {
            errorText = "no values specified";
            break;
        }
        // checking format of values
        label = values[vtypeRef];
        if (label.isNull()) {
            errorText = "The format of value is wrong. 'value':{$ref:'VaildValues'}. $ref is one of 'array', 'range', 'date', 'callback', or 'file'";
            break;
        }

        if (vtype == "Array" || vtype == "ArrayExt") {
            if (!label.isArray()) {
                errorText = "array can be set with array.";
                break;
            }
        } else if (vtype == "Range") {
            pbnjson::JValue minObj, maxObj, intervalObj;

            minObj = label["min"];
            maxObj = label["max"];
            intervalObj = label["interval"];

            if (minObj.isNull() || maxObj.isNull() || intervalObj.isNull()) {
                errorText = "Range type should have min, max, interval properties.";
                break;
            } else if (!minObj.isNumber() || !maxObj.isNumber() || !intervalObj.isNumber()) {
                errorText = "min, max, and interval can be set with intiger.";
                break;
            }
        } else if (vtype == "Callback") {
            pbnjson::JValue uriObj, methodObj, parameterObj;

            uriObj = label["uri"];
            methodObj = label["method"];
            parameterObj = label["parameter"];

            if (uriObj.isNull() || methodObj.isNull() || parameterObj.isNull()) {
                errorText = "Callback type should have uri, method, parameter properties.";
                break;
            } else if (!uriObj.isString() || !methodObj.isString() || !parameterObj.isString()) {
                errorText = "uri, method, and parameter can be set with string.";
                break;
            }
        }
        // it's just string
        else if (vtype == "Date" || vtype == "File") {
            if (!label.isString()) {
                errorText = "Date or File could set String type";
                break;
            }
        } else {
            errorText = "The format of value is wrong. 'value':{$ref:'VaildValues'}. $ref is one of 'array', 'range', or 'date'";
            break;
        }

        setValuesReq = new PrefsDb8SetValues(key, vtype, op, values, message);
        if(setValuesReq) {
            setValuesReq->ref();
            setValuesReq->setTaskInfo(pTaskInfo);
            setValuesReq->setNotifySelf(notifySelf);
            success = setValuesReq->sendRequest(lsHandle);
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (success) {
        //              do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGVALUES);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (setValuesReq) {
        // Constructed with refCount=1.
        //
        setValuesReq->unref();
    }

    return true;
}

/**
 * Callback from TaskMgr for METHODID_GETSYSTEMSETTINGDESC task
 *
 * @sa cbGetSystemSettingDesc()
 * @param  lsHandle  Luna service handler from request
 * @param  message   Luna service message about request
 * @param  pTaskInfo Reference of MethodCallInfo task information
 * @return           true if success
 */
bool doGetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    bool subscribe = false;
    bool success = false;
    std::string errorText;
    std::string category;
    std::string appId;

    PrefsDb8GetValues *getValuesReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root["category"];
        if (label.isString())
            category = label.asString();

        label = root["subscribe"];
        if (label.isBoolean())
            subscribe = label.asBool();

        pbnjson::JValue keyArrayObj = root["keys"];
        if (keyArrayObj.isNull()) {
            pbnjson::JValue keyArg = root["key"];
            if (keyArg.isString()) {
                keyArrayObj = pbnjson::Array();
                keyArrayObj.append(keyArg);
            } else if (category.empty()) {
                errorText = "Error!! Both 'category' and 'keys' should not be empty!";
                break;
            }
            else {
                errorText = "no keys (array) or key (string) specified";
            }
        } else {
            pbnjson::JValue keyArg = root["key"];
            if(keyArg.isString()) {
                errorText = std::string("Specify just one of key or keys properties");
                break;
            }
            if (!keyArrayObj.isArray() || keyArrayObj.arraySize() <= 0) {
                errorText = "invalid key array";
                break;
            }
        }

        {
            const pbnjson::JValue jAppId = root[KEYSTR_APPID];
            if (jAppId.isString()) {
                appId = jAppId.asString();
            }
            if (appId.empty()) {
                pbnjson::JValue  jCurrentApp = root[KEYSTR_CURRENT_APP];
                if (jCurrentApp.isBoolean()) {
                    if (jCurrentApp.asBool()) {
                        appId = PrefsFactory::instance()->getCurrentAppId();
                    }
                }
            }
        }

        getValuesReq = new PrefsDb8GetValues(keyArrayObj, category, subscribe, message);
        if(getValuesReq) {
            getValuesReq->ref();
            getValuesReq->setTaskInfo(pTaskInfo);
            getValuesReq->setAppId(appId);
            success = getValuesReq->sendRequest(lsHandle);
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    if (success) {
        //              do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC);
        replyObj.put("subscribed", false);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (getValuesReq) {
        // Constructed with refCount=1.
        //
        getValuesReq->unref();
    }

    return true;
}

/**
 * Handle a task for setSystemSettingDesc or setSystemSettingFactoryDesc
 *
 * @param  a_def     true  if this handles 'setSystemSettingFactoryDesc',
 *                   false if this handles 'setSystemSettingDesc'.
 */
bool _setSystemSettingDescImpl(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo, bool a_def)
{
    bool success = false;
    bool valueCheckFlag = false;
    bool valueCheck = false;
    bool notifySelf = true;
    std::string errorText;
    std::string vtype;
    std::string op;
    std::string vtypeRef;
    bool categoryFlag = false;
    std::string appId = GLOBAL_APP_ID;
    std::string category;
    std::string dbtype;

    PrefsDb8SetValues *setValuesReq = NULL;

    do {
        pbnjson::JValue root;
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        // key
        pbnjson::JValue label = root["key"];
        if (!label.isString()) {
            errorText = "no key specified";
            break;
        }
        std::string key = label.asString();

        // ui
        pbnjson::JValue ui = root["ui"];
        if (ui.isNull()) {
            errorText = "no ui specified";
            //                              break;
        }
        // category
        label = root["category"];
        if (label.isString()) {
            category = label.asString();
            categoryFlag = true;
        } else {
            errorText = "no category specified";
            //                              break;
        }

        /* TODO: remove dimension handling code. This API doesn't support dimension parameter */
        // dimension
        pbnjson::JValue dimension = root["dimension"];
        if (dimension.isNull()) {
            errorText = "no dimension specified";
        }    //                              break;
        else if (!dimension.isArray()) {
            errorText = "ERROR!! dimension should be array type.";
            break;
        }
        // appId
        label = root[KEYSTR_APPID];
        if (label.isString()) {
            appId = label.asString();
        }

        // dbtype
        label = root[KEYSTR_DBTYPE];
        if (label.isString()) {
            dbtype = label.asString();
        } else {
            pbnjson::JValue cached_desc;
            std::string def_dbtype = DBTYPE_GLOBAL;

            cached_desc = PrefsKeyDescMap::instance()->genDescFromCache(key);
            if (!cached_desc.isNull()) {
                pbnjson::JValue dbtype_obj = cached_desc[KEYSTR_DBTYPE];
                if (dbtype_obj.isString()) {
                    def_dbtype = dbtype_obj.asString();
                }
            }

            dbtype = def_dbtype;
        }

        // TODO: "arcPerApp" filter going to be deleted as of webOS 4.0 if no problem
        if (((DBTYPE_MIXED == dbtype || DBTYPE_EXCEPTION == dbtype) &&
             "arcPerApp" == key ) && appId.empty() ) {
                errorText = "appId must be specified if dbType is M or E ,and key is arcPerApp";
                break;
        }

        label = root["volatile"];
        if (!label.isNull()) {
            SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, "Not allowed to modify volatile flag");
        }

        label = root["notifySelf"];
        if (label.isBoolean())
        {
            notifySelf = label.asBool();
        }

        // valueCheck
        label = root["valueCheck"];
        if (label.isBoolean()) {
            valueCheck = label.asBool();
            valueCheckFlag = true;
        }

        // vtype
        label = root["vtype"];
        if (label.isString()) {
            vtype = label.asString();
            if (vtype == "Array") {
                vtypeRef = "array";
            } else if (vtype == "ArrayExt") {
                vtypeRef = "arrayExt";
            } else if (vtype == "Range") {
                vtypeRef = "range";
            } else if (vtype == "Date") {
                vtypeRef = "date";
            } else if (vtype == "Callback") {
                vtypeRef = "callback";
            } else if (vtype == "File") {
                vtypeRef = "file";
            } else {
                errorText = "vtype should be one of Array, Range or Date";
                break;
            }
        } else {
            errorText = "no vtype specified";
            //                              break;
        }

        // value
        pbnjson::JValue values = root["values"];
        if (!values.isObject()) {
            errorText = "no values specified";
            //                              break;
        } else {
            // check value format
            label = values[vtypeRef];

            if (label.isNull()) {
                errorText = "The format of value is wrong. 'value':{$ref:'VaildValues'}. $ref is one of 'array', 'range', or 'date'";
                break;
            }

            if (vtype == "Array" || vtype == "ArrayExt") {
                if (!label.isArray()) {
                    errorText = "array can be set with array.";
                    break;
                }
            } else if (vtype == "Range") {
                pbnjson::JValue minObj, maxObj, intervalObj;

                minObj = label["min"];
                maxObj = label["max"];
                intervalObj = label["interval"];

                if (minObj.isNull() || maxObj.isNull()|| intervalObj.isNull()) {
                    errorText = "Range type should have min, max, interval properties.";
                    break;
                } else if (!minObj.isNumber() || !maxObj.isNumber()|| !intervalObj.isNumber()) {
                    errorText = "min, max, and interval can be set with intiger.";
                    break;
                }
            } else if (vtype == "Callback") {
                pbnjson::JValue uriObj, methodObj, parameterObj;

                uriObj = label["uri"];
                methodObj = label["method"];
                parameterObj = label["parameter"];

                if (uriObj.isNull() || methodObj.isNull() || parameterObj.isNull()) {
                    errorText = "Callback type should have uri, method, parameter properties.";
                    break;
                } else if (!uriObj.isString() || !methodObj.isString() || !parameterObj.isString()) {
                    errorText = "uri, method, and parameter can be set with string.";
                    break;
                }
            } else if (vtype == "Date" || vtype == "File") {
                if (!label.isString()) {
                    errorText = "Date or File could set String type";
                    break;
                }
            } else {
                errorText = "The format of value is wrong. 'value':{$ref:'VaildValues'}. $ref is one of 'array', 'range', or 'date'";
                break;
            }
        }                           // values

        // check if the key is for dimension.
    #if 0
        if(categoryFlag || dimension) {
            std::list < std::string > keyList;
            keyList.push_back(key);
            if(PrefsKeyDescMap::instance()->isInDimKeyList(keyList))  {
                errorText = "The key used for dimension couldn't be changed category and dimension!";
                break;
            }
        }
    #endif

        // send a request to DB
        setValuesReq = new PrefsDb8SetValues(key, vtype, op, values, message);
        if(setValuesReq) {
            setValuesReq->ref();
            setValuesReq->setTaskInfo(pTaskInfo);
            setValuesReq->setDescProperties(dimension, dbtype, appId, ui);
            if (a_def)
                setValuesReq->setDefKindFlag();
            if (categoryFlag)
                setValuesReq->setDescPropertiesCategory(category);
            if (valueCheckFlag)
                setValuesReq->setDescPropertiesValueCheck(valueCheck);
            setValuesReq->setNotifySelf(notifySelf);
            success = setValuesReq->sendRequest(lsHandle);
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (!success) {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);

        if ( a_def )
            replyObj.put("method", SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYDESC);
        else
            replyObj.put("method", SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGDESC);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (setValuesReq) {
        // Constructed with refCount=1.
        //
        setValuesReq->unref();
    }

    return true;
}

bool doSetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    return _setSystemSettingDescImpl(lsHandle, message, pTaskInfo, false);
}

bool doSetSystemSettingFactoryDesc(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    return _setSystemSettingDescImpl(lsHandle, message, pTaskInfo, true);
}

bool doGetCurrentSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    pbnjson::JValue dimension;
    pbnjson::JValue keyArray;
    std::set < std::string > keyList;
    bool subscribe = false;
    bool success = false;
    std::string errorText;
    std::string app_id;
    std::string category;

    PrefsDb8Get *getReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root["subscribe"];
        if (label.isBoolean())
            subscribe = label.asBool();

        // set current appid
        app_id = PrefsFactory::instance()->getCurrentAppId();
        if ( app_id.empty() ) app_id = DEFAULT_APP_ID;

        label = root["category"];
        if (label.isString())
            category = label.asString();
        dimension = root["dimension"];

        keyArray = root["keys"];
        if (keyArray.isNull()) {
            errorText = "no keys specified";
            break;
        }
        if (keyArray.isArray() && keyArray.arraySize() > 0) {
            for (pbnjson::JValue obj : keyArray.items()) {
                if (!obj.isString())
                    continue;

                keyList.insert(obj.asString());
            }
        } else {
                errorText = "invalid key array";
        }

    #ifdef LEGACY_LOCK_REQ_SUPPORT
        /* TODO: this code should be removed after lock key movement is finished
         * This code insert appropriate categroy name if user omit changed new category */
        if (std::find(keyList.begin(), keyList.end(), "applockPerApp")!=keyList.end() ||
            std::find(keyList.begin(), keyList.end(), "parentalControl")!=keyList.end() ||
            std::find(keyList.begin(), keyList.end(), "lockByAge")!=keyList.end() ||
            std::find(keyList.begin(), keyList.end(), "systemPin")!=keyList.end())
        {
            if (category.empty()) category = "lock";
        }
        else if (std::find(keyList.begin(), keyList.end(), "allowMobileDeviceAccess")!=keyList.end())
        {
            if (category.empty()) category = "network";
        }
    #endif

        getReq = new PrefsDb8Get(keyList, app_id, category, dimension, subscribe, message);
        if(getReq) {
            getReq->ref();
            getReq->setTaskInfo(pTaskInfo);
            success = getReq->sendRequest(lsHandle);
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (success) {
        //              do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_GETCURRENTSETTINGS);
        replyObj.put("subscribed", false);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (getReq) {
        // Constructed with refCount=1.
        //
        getReq->unref();
    }

    return true;
}

bool doDelSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    pbnjson::JValue dimension;
    pbnjson::JValue keyArray;
    std::set < std::string > globalKeys, perAppKeys;
    std::set < std::string > keyList;
    bool success = false;
    std::string errorText;
    std::string app_id;
    std::string category;
    bool existKeys = false;

    PrefsDb8Del *delReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root["app_id"];
        if (label.isString())
            app_id = label.asString();

        label = root["category"];
        if (label.isString())
            category = label.asString();

        dimension = root["dimension"];

        keyArray = root["keys"];
        if (keyArray.isNull()) {
            errorText = "no keys specified";
            break;
        }

        existKeys = true;
        if (keyArray.isArray()) {
            if (keyArray.arraySize() <= 0) {
                errorText = "invalid key array";
                break;
            }
            for (pbnjson::JValue obj : keyArray.items()) {
                if (!obj.isString())
                    continue;
                keyList.insert(obj.asString());
            }
        }

        delReq = new PrefsDb8Del(keyList, app_id, category, dimension, true, message);
        if(delReq) {
            PrefsKeyDescMap::instance()->splitKeysIntoGlobalOrPerAppByDescription(keyList, category, app_id, globalKeys, perAppKeys);

            //Check invalid keys(perappkey with globalkey)
            if (!globalKeys.empty() && !perAppKeys.empty() && existKeys) {
                errorText = "enter same type keys";
                break;
            }

            delReq->ref();
            delReq->setTaskInfo(pTaskInfo);
            delReq->setGlobalAndPerAppKeys(globalKeys,perAppKeys);
            if (!delReq->sendRequest(lsHandle)) {
                errorText = "ERROR!! send a request to DB";
                break;
            }
        }
        else {
            errorText = "ERROR!! send a request to DB";
            break;
        }

        success = true;
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (!success) {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_DELETESYSTEMSETTINGS);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (delReq) {
        // Constructed with refCount=1.
        //
        delReq->unref();
    }

    return success;
}

bool doResetSystemSettings(LSHandle * lsHandle, LSMessage * message, MethodCallInfo* pTaskInfo)
{
    pbnjson::JValue root;
    pbnjson::JValue dimension;
    pbnjson::JValue keyArray;
    std::set < std::string > keyList;
    std::set < std::string > globalKeys, perAppKeys;
    bool success = false;
    std::string errorText;
    std::string app_id;
    std::string category;
    bool reset_all = false;
    bool existKeys = false;

    PrefsDb8Del *delReq = NULL;

    do {
        if (!PrefsFactory::instance()->isReady()) {
            SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
            errorText = "Service is not ready";
            break;
        }

        if(!pTaskInfo->isBatchCall()) {
            const char *payload = LSMessageGetPayload(message);
            if (!payload) {
                SSERVICELOG_WARNING(MSGID_API_NO_ARGS, 0, " ");
                errorText = "LunaBus Msg Fail!";
                break;
            }

            root = pbnjson::JDomParser::fromString(payload);
            if (root.isNull()) {
                SSERVICELOG_WARNING(MSGID_API_ARGS_PARSE_ERR, 0, "payload : %s",payload);
                errorText = "Parsing Fail!";
                break;
            }
        }
        else {
            root = pTaskInfo->getBatchParam();
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, root.stringify().c_str());

        pbnjson::JValue label = root["resetAll"];
        reset_all = label.isBoolean() ? label.asBool() : false;

        label = root["app_id"];
        if (label.isString())
            app_id = label.asString();

        label = root["category"];
        if (label.isString())
            category = label.asString();

        dimension = root["dimension"];

        keyArray = root["keys"];
        if (keyArray.isNull()) {
            if (category.empty() && reset_all == false) {
                errorText = "no keys specified";
                break;
            }
        }

        if (keyArray.isArray()) {
            if (keyArray.arraySize() <= 0) {
                errorText = "invalid key array";
                break;
            }
            existKeys = true;
            for (pbnjson::JValue obj : keyArray.items()) {
                if (!obj.isString())
                    continue;
                keyList.insert(obj.asString());
            }
        }

        delReq = new PrefsDb8Del(keyList, app_id, category, dimension, false, message);

        if(delReq) {
            PrefsKeyDescMap::instance()->splitKeysIntoGlobalOrPerAppByDescription(keyList, category, app_id, globalKeys, perAppKeys);

            //Check invalid keys(perappkey with globalkey)
            if (!globalKeys.empty() && !perAppKeys.empty() && existKeys) {
                errorText = "enter same type keys";
                break;
            }

            delReq->ref();
            delReq->setTaskInfo(pTaskInfo);
            delReq->setGlobalAndPerAppKeys(globalKeys,perAppKeys);

            if ( reset_all ) {
                success = delReq->sendRequestResetAll(lsHandle);
            } else {
                success = delReq->sendRequest(lsHandle);
            }
        }
        else {
            errorText = "ERROR!! send a request to DB";
        }
    } while(false);

    SSERVICELOG_TRACE("in Done: %s", __func__);

    if (success) {
        //              do nothing. return is sent by cb.
    } else {
        pbnjson::JValue replyObj(pbnjson::Object());

        replyObj.put("returnValue", false);
        replyObj.put("errorText", errorText);
        replyObj.put("method", SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGS);

        if(!pTaskInfo->isBatchCall()) {
            sendErrorReply(lsHandle, message, replyObj);
        }

        PrefsFactory::instance()->releaseTask(&pTaskInfo, replyObj);
    }

    if (delReq) {
        // Constructed with refCount=1.
        //
        delReq->unref();
    }

    return true;
}

//
// requestGetSystemSettings
//
// This function is NOT explicit interface, but internal function to get current settings
// For example, to 'dimension' notification, if app subscribes 'picture' WITHOUT any 'dimension',
// and only if 'dimension' is changed, then this will notify the app.
//
bool requestGetSystemSettings(LSHandle * a_handle, LSMessage * a_message, MethodCallInfo* a_taskInfo)
{
    std::string errorText;
    bool success;

    if (!PrefsFactory::instance()->isReady()) {
        SSERVICELOG_WARNING(MSGID_SERVICE_NOT_READY, 0, "in %s", __FUNCTION__);
        goto ErrExit;
    }

    {
        TaskRequestInfo* requestInfo = static_cast<TaskRequestInfo *>(a_taskInfo->getUserData());
        if (!requestInfo)
        {
            goto ErrExit;
        }

        CatKeyContainer& requestList = requestInfo->requestList;
        for (CatKeyContainer::iterator it = requestList.begin();
            it != requestList.end();
            ++it)
        {
            std::string category = it->first.first;
            std::string appId = it->first.second;
            // TODO: Change std::list<> keyList into std::set<>.
            //
            PrefsDb8Get* getReq = new PrefsDb8Get(it->second, appId, category, requestInfo->requestDimObj);
            if(getReq) {
                PrefsDb8Get::Callback db_get_cbfunc = reinterpret_cast<PrefsDb8Get::Callback>(requestInfo->cbFunc);
                // Intensionally, each PrefsDb8Get instance has ref. count to taskInfo.
                // So, once all the PrefsDb8Get is destroyed, then next command will be executed.
                //
                a_taskInfo->ref();
                getReq->ref();
                getReq->setTaskInfo(a_taskInfo);
                getReq->Connect(db_get_cbfunc, requestInfo->thiz_class, requestInfo);
                getReq->forceDbSync();
                success = getReq->sendRequest(a_handle);
                getReq->unref();
            }
            else {
                errorText = "not enough memory.";
                goto ErrExit;
            }

            if (!success)
            {
                goto ErrExit;
            }
        }

        if (g_atomic_int_dec_and_test(&requestInfo->requestCount) == TRUE)
        {
            delete requestInfo;
        }
    }

ErrExit:
    // Here we have to release task, since each PrefsDb8Get has ref 1.
    //
    PrefsFactory::instance()->releaseTask(&a_taskInfo, pbnjson::Object());

    return true;
}
