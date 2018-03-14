// Copyright (c) 2010-2018 LG Electronics, Inc.
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
#ifndef __Settings_h__
#define __Settings_h__

#include <glib.h>
#include <string>

class Settings {
 public:

    static inline Settings *settings() {
        if (G_LIKELY(s_settings))
            return s_settings;

        s_settings = new Settings();
        return s_settings;
    } bool m_turnNovacomOnAtStartup;
    bool m_saveLastBackedUpTempDb;
    bool m_saveLastRestoredTempDb;
    std::string m_logLevel;

    bool m_useComPalmImage2;
    bool m_image2svcAvailable;
    std::string m_comPalmImage2BinaryFile;

    int schemaValidationOption;
    bool supportAppSwitchNotify;
    bool loadDefaultJson;
    bool loadPerAppJson;
    std::string dbVersion;

 private:
    Settings();
    ~Settings();
    bool initValues();
    bool load(const char *settingsFile);
    bool postLoad();
    bool createNeededFolders();
    static Settings *s_settings;
};

#endif                          // Settings
