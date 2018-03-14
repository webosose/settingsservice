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

#ifndef SERVICECHECKER_H
#define SERVICECHECKER_H

#include <luna-service2/lunaservice.h>

#include <functional>
#include <string>
#include <memory>
#include <boost/utility.hpp>

class AccessChecker : boost::noncopyable
{
public:
    typedef std::function<void(LSMessage*, bool)> CallbackType;

    AccessChecker(LSHandle* handle, const std::string &uri_to_check);
    ~AccessChecker();

    /**
     * \brief   Checks if the call inside \a message is allowed to pass
     * \param message   Correctly filled in message to check to
     * \param callback  Callback to call after check is done. Might be called synchronously!
     * \return  true if check was successful, false otherwise. No callback should be called in case of false returned */
    bool check(LSMessage* message, CallbackType callback) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif                          /* SERVICECHECKER_H */
