// Copyright (c) 2015-2024 LG Electronics, Inc.
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

#include "AccessChecker.h"
#include "Logging.h"
#include <pbnjson.hpp>
#include <functional>
#include <map>

struct AccessChecker::Impl
{
private:
    typedef std::map<std::string, bool> CacheMap;
    struct ParamsGroup
    {
        const Impl* m_impl;
        LSMessage* m_message;
        CallbackType m_callback;
        std::string m_requester;
    };
public:
    Impl(LSHandle* handle, const std::string& uri_to_check);
    bool check(LSMessage* message, CallbackType callback) const;
private: // implementation methods
    static bool processReply(LSHandle* handle, LSMessage* reply, void* ctx);
private: // fields
    LSHandle* m_handle;
    std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> m_mainLoop;
    const std::string  m_uri_to_check;
    mutable CacheMap m_result_cache;
};

AccessChecker::Impl::Impl(LSHandle* handle, const std::string& uri_to_check)
    : m_handle(handle)
    , m_mainLoop(g_main_loop_new(nullptr, FALSE), &g_main_loop_unref)
    , m_uri_to_check(uri_to_check)
{
}

bool
AccessChecker::Impl::processReply(LSHandle* handle, LSMessage* reply, void* ctx)
{
    std::unique_ptr<ParamsGroup> params(static_cast<ParamsGroup*>(ctx));

    do {
        auto payload = LSMessageGetPayload(reply);
        if (!payload) {
            params->m_callback(params->m_message, false);
            break;
        }

        pbnjson::JValue parsed = pbnjson::JDomParser::fromString(payload);

        if (parsed["returnValue"].asBool()) {
            bool allowed = parsed["allowed"].asBool();
            params->m_callback(params->m_message, allowed);
            params->m_impl->m_result_cache[params->m_requester] = allowed;

        } else {
            params->m_callback(params->m_message, false);
        }
    } while (false);

    LSMessageUnref(params->m_message);
    return true;
}

bool
AccessChecker::Impl::check(LSMessage *message, CallbackType callback) const
{
    const char *serviceName = LSMessageGetSenderServiceName(message);

    if (!serviceName)
    {
        serviceName = LSMessageGetSender(message);
    }

    if (!serviceName)
    {
        return false;
    }

    std::string requester_service = serviceName;

    CacheMap::const_iterator it = m_result_cache.find(requester_service);

    if (it != m_result_cache.end())
    {
        callback(message, it->second);
        return true;
    }

    LSMessageToken token = LSMESSAGE_TOKEN_INVALID;

    const std::string url = "luna://com.webos.service.bus/isCallAllowed";
    pbnjson::JValue request = pbnjson::Object();
    request.put("requester", requester_service);
    request.put("uri", m_uri_to_check);

    std::unique_ptr<ParamsGroup> params (new ParamsGroup());
    params->m_impl = this;
    params->m_message = message;
    params->m_callback = callback;
    params->m_requester = requester_service;

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSCallOneReply(m_handle, url.c_str(), request.stringify().c_str(), processReply, params.release(), &token, &lsError))
    {
        SSERVICELOG_WARNING("ACCESSCHECKER_REQUEST_FAIL", 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorFree(&lsError);
        return false;
    }

    LSMessageRef(message);
    return true;
}

AccessChecker::AccessChecker(LSHandle* handle, const std::string &uri_to_check)
    : m_impl(new Impl(handle, uri_to_check))
{
}

AccessChecker::~AccessChecker()
{
}

bool
AccessChecker::check(LSMessage* message, CallbackType callback) const
{
    return m_impl->check(message, callback);
}
