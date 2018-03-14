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

#ifndef DB8_FIND_CHAIN_CALL_H
#define DB8_FIND_CHAIN_CALL_H

#include <list>

#include <JSONUtils.h>

#include "PrefsFactory.h"

class Db8FindChainCall;
class Db8FindChainCall : public PrefsRefCounted {
public:
    typedef bool (*Callback)(void *a_thiz_class, void *a_userdata, const std::list<pbnjson::JValue>& a_results );
    typedef int ConnectionId;

    // Constructor
    Db8FindChainCall();

    static bool sendRequest(Db8FindChainCall *a_thiz_class, LSHandle *a_handle, pbnjson::JValue a_findQuery);
    bool Connect(Callback a_func, void *thiz_class, void *userdata);
    bool Disconnect();

    static bool cbDb8FindCall(LSHandle *a_handle, LSMessage *a_message, void *a_userdata);

private:
    bool sendFindRequest(pbnjson::JValue a_page = pbnjson::JValue());

    LSHandle *m_serviceHandle;      // Service handle for luna-service2. (LSHandle)

    Callback m_callback;
    void* m_thiz_class;
    void* m_user_data;

    pbnjson::JValue m_findQuery;
    std::list<pbnjson::JValue> m_results;
};

#endif // DB8_FIND_CHAIN_CALL_H
