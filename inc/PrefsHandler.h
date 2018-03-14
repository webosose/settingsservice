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

#ifndef PREFSHANDLER_H
#define PREFSHANDLER_H

#include <list>
#include <map>
#include <string>

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

class PrefsHandler {
 public:

    PrefsHandler(LSHandle *handle):m_handle(handle) {}
    virtual ~PrefsHandler() {}

    virtual std::list < std::string > keys() const = 0;
    virtual bool validate(const std::string & key, pbnjson::JValue value) = 0;
    virtual bool validate(const std::string & key, pbnjson::JValue value, const std::string & originId) {
        return validate(key, value);
    }
    virtual void valueChanged(const std::string & key, pbnjson::JValue value) = 0;
    virtual void valueChanged(const std::string & key, const std::string & strval) {
        if (strval.empty())
            return;
        //WORKAROUND WRAPPER FOR USING valueChanged() internally.  //TODO: do this the proper way.
        // the way it is now makes a useless conversion at least once

        JsonMessageParser parser(strval, SCHEMA_V2_ANY);
        pbnjson::JValue jo(parser.get());
        if (!jo.isValid()) {
            return;
        }
        valueChanged(key, jo);
    }
    virtual pbnjson::JValue valuesForKey(const std::string & key) = 0;
    // FIXME: We very likely need a windowed version the above function
    virtual bool isPrefConsistent() {
        return true;
    }
    virtual void restoreToDefault() {
    }
    virtual bool shouldRefreshKeys(std::map < std::string, std::string > &keyvalues) {
        return false;
    }

    LSHandle *getServiceHandle() {
        return m_handle;
    }

 protected:
    LSHandle *m_handle;
};

#endif                          /* PREFSHANDLER_H */
