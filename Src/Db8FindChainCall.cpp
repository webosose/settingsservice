// Copyright (c) 2013-2023 LG Electronics, Inc.
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

#include <algorithm>

#include "Db8FindChainCall.h"
#include "Logging.h"
#include "SettingsService.h"

Db8FindChainCall::Db8FindChainCall()
    : m_serviceHandle(NULL), m_callback(NULL), m_thiz_class(NULL), m_user_data(NULL)
{
}

bool Db8FindChainCall::sendRequest(Db8FindChainCall* a_thiz_class, LSHandle *a_handle, pbnjson::JValue a_findQuery)
{
    PRF_REQUIRE(a_handle);
    PRF_REQUIRE(!a_findQuery.isNull());

    if (!a_handle || a_findQuery.isNull())
    {
        SSERVICELOG_WARNING(MSGID_CHAINCALL_NULL_HANDLE, 0, " ");
        return false;
    }

    a_thiz_class->m_serviceHandle = a_handle;
    a_thiz_class->m_findQuery = a_findQuery;

    bool rc = a_thiz_class->sendFindRequest();

    return rc;
}

bool Db8FindChainCall::sendFindRequest(pbnjson::JValue a_page)
{
    LSError lsError;
    LSErrorInit(&lsError);
    bool result;

    pbnjson::JValue findRequest = pbnjson::Object();
    pbnjson::JValue queryItem = pbnjson::Object();
    for (pbnjson::JValue::KeyValue item : m_findQuery.children()) {
        queryItem << item;
    }

    if (!a_page.isNull()) {
        queryItem.put("page", a_page);
    }
    findRequest.put("query", queryItem);
    findRequest.put("count", true);

    /*
       The data of com.webos.settings.desc.default.country is also retrieved,
       because the desc.default.country kind is extended from desc.default kind
     */
    ref();
    result = DB8_luna_call(m_serviceHandle, "luna://com.webos.service.db/find", findRequest.stringify().c_str(), Db8FindChainCall::cbDb8FindCall, this, NULL, &lsError);
    if (!result) {
        SSERVICELOG_WARNING(MSGID_LSCALL_DB_FIND_FAIL, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        unref();
    }

    return result;
}

bool Db8FindChainCall::Connect(Callback a_func, void *thiz_class, void *userdata)
{
    m_callback = a_func;
    m_thiz_class = thiz_class;
    m_user_data = userdata;

    return true;
}

bool Db8FindChainCall::Disconnect()
{
    m_callback = NULL;
    m_thiz_class = NULL;
    m_user_data = NULL;

    return true;
}

bool Db8FindChainCall::cbDb8FindCall(LSHandle *a_handle, LSMessage *a_message, void *a_userdata)
{
    pbnjson::JValue root;
    bool success = false;
    bool completed = true;
    int count = -1;

    Db8FindChainCall *thiz_class = (Db8FindChainCall*) a_userdata;
    do {
        const char *payload = LSMessageGetPayload(a_message);
        if (!payload) {
            SSERVICELOG_WARNING(MSGID_CHAINCALL_PAYLOAD_MISSING, 0, " ");
            break;
        }

        if (!thiz_class) {
            SSERVICELOG_DEBUG("%s, NULL thiz_class", __func__);
            break;
        }

        SSERVICELOG_TRACE("%s: %s", __FUNCTION__, payload);
        root = pbnjson::JDomParser::fromString(payload);
        if(root.isNull()) {
            SSERVICELOG_WARNING(MSGID_CHAINCALL_PARSE_ERR, 0, "payload : %s", payload);
            break;
        }

        pbnjson::JValue label(root["returnValue"]);
        if (label.isBoolean()) {
            success = label.asBool();
        }
        //all_db8find.push_back(json_object_get(root));

        if (success)
        {
            label = root["results"];
            if (label.isArray())
            {
                count = label.arraySize();
                if (count > 0) {
                    pbnjson::JValue next_object(root["next"]);
                    pbnjson::JValue count_object(root["count"]);

                    if (next_object.isString())
                    {
                        if (count_object.isNumber())
                        {
                            if (count_object.asNumber<int>() <= 500)
                            {
                                SSERVICELOG_DEBUG("%s: count should be larger than 500, but it's NOT", __func__);
                            }
                        }

                        // If 'next' field is there and it's valid one. we need to send it again with page.
                        completed = false;
                        thiz_class->sendFindRequest(next_object);
                    }
                    else
                    {
                        completed = true;
                    }
                }
            }
        }
        // Here we insert ANY return object even it's not successful.
        // Callee will check it.
        //
        thiz_class->m_results.push_back(root);
    } while (false);

    if (!success || completed) {
        // Call callback here even though it's failure.
        if (thiz_class && thiz_class->m_callback) {
            SSERVICELOG_DEBUG("%s : completed: %d, success: %d", __func__,
                    completed, success);
            thiz_class->m_callback(thiz_class->m_thiz_class,
                    thiz_class->m_user_data, thiz_class->m_results);
        }
    } else {
        SSERVICELOG_DEBUG("%s : completed: %d, success: %d", __func__,
                completed, success);
    }

    if (thiz_class)
        thiz_class->unref();

    return success;
}
