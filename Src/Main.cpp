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

#include <luna-service2/lunaservice.h>

#include "Logging.h"
#include "PrefsDb8Condition.h"
#include "PrefsFactory.h"
#include "PrefsNotifier.h"
#include "PrefsPerAppHandler.h"
#include "SettingsServiceApi.h"
#include "Utils.h"
#include "GCovHandler.h"


int main(int argc, char **argv)
{
    SSERVICELOG_TRACE("Entering function %s", __FUNCTION__);
    register_gcov_handler();

    try {
        std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> mainLoop(
            g_main_loop_new(nullptr, FALSE),
            &g_main_loop_unref);

        LSError lsError;
        bool result;

        LSErrorInit(&lsError);

        // Register the service com.lge.settingsservice
        LSHandle *serviceHandleLge = nullptr;
        result = LSRegister("com.lge.settingsservice", &serviceHandleLge, &lsError);
        if (!result) {
            SSERVICELOG_ERROR(MSGID_WEBOS_SRVC_REGISTER_FAILED, 0, "Failed to register service: com.lge.settingsservice");
            LSErrorFree(&lsError);
            throw std::runtime_error("Failed to register service: com.lge.settingsservice");
        }

        result = LSGmainAttach(serviceHandleLge, mainLoop.get(), &lsError);
        if (!result) {
            SSERVICELOG_ERROR(MSGID_WEBOS_SRVC_ATTACH_FAIL, 0, "Failed to attach service handle to main loop");
            LSErrorFree(&lsError);
            throw std::runtime_error("Failed to attach service handle to main loop");
        }
        PrefsFactory::instance()->setServiceHandle(serviceHandleLge);
        Utils::Instrument::addCurrentServiceName("com.lge.settingsservice");

        // Register the service com.webos.service.settings
        LSHandle *serviceHandleNew = nullptr;
        result = LSRegister("com.webos.service.settings", &serviceHandleNew, &lsError);
        if (!result) {
            SSERVICELOG_ERROR(MSGID_WEBOS_SRVC_REGISTER_FAILED, 0, "Failed to register service: com.webos.service.settings");
            LSErrorFree(&lsError);
            throw std::runtime_error("Failed to register service: com.webos.service.settings");
        }

        result = LSGmainAttach(serviceHandleNew, mainLoop.get(), &lsError);
        if (!result) {
            SSERVICELOG_ERROR(MSGID_WEBOS_SRVC_ATTACH_FAIL, 0, "Failed to attach service handle to main loop");
            LSErrorFree(&lsError);
            throw std::runtime_error("Failed to attach service handle to main loop");
        }
        PrefsFactory::instance()->setServiceHandle(serviceHandleNew);
        Utils::Instrument::addCurrentServiceName("com.webos.service.settings");

        // Register the service
        LSHandle *serviceHandle = nullptr;
        result = LSRegister(PrefsFactory::service_name.c_str(), &serviceHandle, &lsError);
        if (!result) {
            SSERVICELOG_ERROR(MSGID_WEBOS_SRVC_REGISTER_FAILED, 0, ("Failed to register service: " + PrefsFactory::service_name).c_str());
            LSErrorFree(&lsError);
            throw std::runtime_error("Failed to register service: " + PrefsFactory::service_name);
        }

        result = LSGmainAttach(serviceHandle, mainLoop.get(), &lsError);
        if (!result) {
            SSERVICELOG_ERROR(MSGID_WEBOS_SRVC_ATTACH_FAIL, 0, "Failed to attach service handle to main loop");
            LSErrorFree(&lsError);
            throw std::runtime_error("Failed to attach service handle to main loop");
        }
        PrefsFactory::instance()->setServiceHandle(serviceHandle);
        PrefsFactory::instance()->loadCoreServices(CORE_SERVICE_CONF_PATH);
        Utils::Instrument::addCurrentServiceName(PrefsFactory::service_name);

        // Initailze the notifier module.
        PrefsNotifier::instance()->initialize();
        PrefsKeyDescMap::instance()->initialize();

        // initialize a kind for volatile
        PrefsFactory::instance()->initKind();

        PrefsDb8Condition::instance()->loadEnvironmentCondition();
        PrefsKeyDescMap::instance()->loadExceptionAppList();

        Utils::Instrument::addPostfixToApiMap(".SettingsService_KeyDesc",  SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC);
        Utils::Instrument::addPostfixToApiMap(".SettingsService_KeyValue", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES);
        Utils::Instrument::addPostfixToApiMap("",                          SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS);

        PrefsPerAppHandler::instance().setLSHandle(PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE));

        // Run the main loop
        g_main_loop_run(mainLoop.get());
        return 0;
    }
    catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        PrefsFactory::instance()->serviceFailed(__PRETTY_FUNCTION__);
        return -1;
    }
}
