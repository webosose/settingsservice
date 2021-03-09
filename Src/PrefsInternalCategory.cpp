// Copyright (c) 2015-2021 LG Electronics, Inc.
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

#include "PrefsInternalCategory.h"
#include "Utils.h"

#define INSTRUMENT_STR "instrument"
#define GETCURRENTSUBSCRIPTIONS_STR "getCurrentSubscriptions"

using namespace std;

void LSMessageReplyWrapper(LSHandle* handle, LSMessage* message, const char *msg)
{
    LSError lsError;
    LSErrorInit(&lsError);

    bool retVal = LSMessageReply(handle, message, msg, &lsError);
    if (!retVal) {
        LSErrorFree(&lsError);
    }
}

bool doInternalCategoryGeneralMethod(LSHandle* handle, LSMessage* message, MethodCallInfo* taskInfo)
{
    PrefsInternalCategory* internalReq = new PrefsInternalCategory();

    internalReq->ref();
    internalReq->setTaskInfo(taskInfo);
    internalReq->handleRequest(handle, message);

    return true;
}

PrefsInternalCategory::~PrefsInternalCategory()
{
    if (m_taskInfo) {
        PrefsFactory::instance()->releaseTask(&m_taskInfo);
    }
}

/**
 * Handle '/instrument' method to control instrument feature.
 *
 * API payload requires a 'control' property that should contain one of
 * 'start', 'stop', and 'status'.
 */
void PrefsInternalCategory::handleMethodInstrument()
{
    // VALIDATOR REQUIRED

    const char* payload = LSMessageGetPayload(m_message);
    if (payload == NULL) {
        LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":false, \"errorText\":\"no payload\"}");
        return;
    }

    pbnjson::JValue jsonRoot = pbnjson::JDomParser::fromString(payload);
    if (jsonRoot.isNull()) {
        LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":false, \"errorText\":\"not parsed json\"}");
        return;
    }

    pbnjson::JValue jsonControl = jsonRoot["control"];
    if (!jsonControl.isString()) {
        LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":false, \"errorText\":\"no control property\"}");
        return;
    }
    string control = jsonControl.asString();

    bool controlHandled = false;

    if (control == "start") {
        ref();
        Utils::Instrument::start([this](const std::string& errorText,
                                        const std::string& errorContext)
        {
            const bool isSuccess = errorText.empty();
            pbnjson::JValue jsonRoot(pbnjson::Object());
            jsonRoot.put("returnValue",      isSuccess);
            jsonRoot.put("instrumentStatus", isSuccess ? "started" : "stopped");
            if (!isSuccess)
            {
                jsonRoot.put("errorText",    errorText);
                jsonRoot.put("errorContext", errorContext);
            }
            LSMessageReplyWrapper(m_handle, m_message, jsonRoot.stringify().c_str());
            unref();
        });
        controlHandled = true;
    }

    if (control == "stop") {
        Utils::Instrument::stop();

        LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":true, \"instrumentStatus\":\"stopped\"}");
        controlHandled = true;
    }

    if (control.compare("status") == 0) {
        if (Utils::Instrument::isStarted()) {
            LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":true, \"instrumentStatus\":\"started\"}");
        }
        else {
            LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":true, \"instrumentStatus\":\"stopped\"}");
        }
        controlHandled = true;
    }

    if (control == "changeApp") {
        pbnjson::JValue params = jsonRoot["params"];
        if (params.isObject()) {
            pbnjson::JValue appId = params["app_id"];

            if (appId.isString()) {
                std::unique_ptr<std::string> appIdPtr(new std::string(appId.asString()));
                bool pushMethodResult = PrefsFactory::instance()->getTaskManager().pushUserMethod(
                                          METHODID_CHANGE_APP,
                                          PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE),
                                          nullptr, appIdPtr.get(), TASK_PUSH_BACK);
                if (pushMethodResult)
                {
                    appIdPtr.release();
                }
                LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":true}");
            }
            else {
                LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":false}");
            }
            controlHandled = true;
        }
    }

    if (control == "removeApp") {
        pbnjson::JValue params = jsonRoot["params"];
        pbnjson::JValue appId;
        if (params.isObject()) {
            pbnjson::JValue appId = params["app_id"];
            if (appId.isString()) {
                std::unique_ptr<std::string> appIdPtr(new std::string(appId.asString()));
                bool pushMethodResult = PrefsFactory::instance()->getTaskManager().pushUserMethod(
                        METHODID_UNINSTALL_APP,
                        PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE),
                        nullptr, appIdPtr.get(), TASK_PUSH_BACK);
                if (pushMethodResult)
                {
                    appIdPtr.release();
                }
                LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":true}");
            } else {
                LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":false}");
            }
            controlHandled = true;
         }
    }

    if (controlHandled == false) {
        LSMessageReplyWrapper(m_handle, m_message, "{\"returnValue\":false, \"errorText\":\"unexpected request on PrefsInternalCategory::handleMethodInstrument\"}");
    }
}

void PrefsInternalCategory::handleMethodGetCurrentSubscriptions()
{
    auto subscriptions = new Utils::Instrument::CurrentSubscriptions();
    ref();
    subscriptions->request([this, subscriptions]
    {
        const bool isSuccess = !subscriptions->error();
        pbnjson::JObject jsonRoot;
        jsonRoot.put("returnValue", isSuccess);
        if (isSuccess)
        {
            pbnjson::JArray jsonItemArray;
            for (auto it : subscriptions->subscriptionMap())
            {
                pbnjson::JObject jsonItem;
                jsonItem.put("sender",  it.second.sender);
                jsonItem.put("method",  it.second.method);
                jsonItem.put("message", it.second.message);
                jsonItemArray.append(jsonItem);
            }
            jsonRoot.put("subscriptions", jsonItemArray);

        }
        else
        {
            jsonRoot.put("errorText",        subscriptions->errorText());
            jsonRoot.put("errorContext",     subscriptions->errorContext());
            jsonRoot.put("instrumentStatus", "stopped");
        }
        LSMessageReplyWrapper(m_handle, m_message, jsonRoot.stringify().c_str());
        unref();
        subscriptions->unref();
    });
}

/**
 * Handle any request under 'internal' category.
 *
 * @param handle  LSHandle from the request
 * @param message LSMessage from the request
 */
void PrefsInternalCategory::handleRequest(LSHandle* handle, LSMessage* message)
{
    m_handle = handle;
    m_message = message;

    std::string method = LSMessageGetMethod(message);

    if (INSTRUMENT_STR == method) {
        handleMethodInstrument();
        unref();
        return;
    }

    if (GETCURRENTSUBSCRIPTIONS_STR == method) {
        handleMethodGetCurrentSubscriptions();
        unref();
        return;
    }

    LSMessageReplyWrapper(handle, message, "{\"returnValue\":false, \"errorText\":\"Unsupported method\"}");
    unref();
}

