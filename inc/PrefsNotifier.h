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
// LICENSE@@@

#ifndef PREFSNOTIFIER_H
#define PREFSNOTIFIER_H

#include <map>
#include <mutex>

#include <JSONUtils.h>
#include <luna-service2/lunaservice.h>

#include "PrefsKeyDescMap.h"
#include "PrefsTaskMgr.h"

class LSMessageElem {
public:
    LSMessageElem(LSMessage *a_message)
        : m_message(a_message)
    {
        if (a_message)
            LSMessageRef(a_message);
    }

    LSMessageElem(const LSMessageElem& a_rhs)
    {
        m_message = a_rhs.m_message;
        if (a_rhs.m_message)
            LSMessageRef(a_rhs.m_message);
    }

    LSMessageElem& operator=(const LSMessageElem& a_rhs)
    {
        if (m_message != a_rhs.m_message) {
            if (m_message)
                LSMessageUnref(m_message);
            m_message = a_rhs.m_message;
            LSMessageRef(m_message);
        }

        return *this;
    }

    ~LSMessageElem()
    {
        if (m_message)
        {
            LSMessageUnref(m_message);
        }
    }

    bool operator < (const LSMessageElem& a_rhs) const
    {
        return (const unsigned long)m_message < (const unsigned long)(a_rhs.m_message);
    }

    LSMessage* get(void) const { return m_message; }

private:
    LSMessageElem();
    LSMessage* m_message;
};

class PrefsNotifier {
public:
    static PrefsNotifier *instance();

    // Initialize the PrefsNotifier instance.
    //
    void initialize();

    /**
     * Add Subscription for PerApp Request
     * @sa PrefsPerAppHandler::addSubscription()
     */
    void addSubscriptionPerApp(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage);

    /**
     * Add Subscription for PerApp Request
     * @sa PrefsPerAppHandler::addDescSubscription()
     */
    void addDescSubscriptionPerApp(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage);

    // Add or remove subscription for dimension changes.
    // Called from PrefsDb8Get class.
    //
    void addSubscriptionPerDimension(LSHandle *a_handle, const std::string& a_category, const std::string& a_key, const std::string& a_appId, LSMessage * a_message);
    void addSubscription(LSHandle *a_handle, const std::string &a_cat, pbnjson::JValue a_dimObj, const std::string &a_key, LSMessage *a_msg);
    void addDescSubscriptionPerDimension(LSHandle *a_handle, const std::string& a_category, const std::string& a_key, const std::string& a_appId, LSMessage * a_message);
    void addDescSubscription(LSHandle *a_handle, const std::string& a_category, const std::string& a_key, const std::string& a_appId, LSMessage * a_message);
    void removeSubscription(LSMessage * a_message);

    // Notify key values are changed whatever a_keyList contains dimension values or not.
    // notifyEarly() will be called prior to updating the dimension values.
    //
    void notifyEarly(const std::string& a_category, const std::set<std::string>& a_keyList);
    void notifyAllSettings(void);
    void notifyByDimension(const std::string& a_category, const std::set<std::string>& a_keyList, pbnjson::JValue a_changedList);

    static pbnjson::JValue jsonSubsReturn(const std::string& a_category, const std::string &a_appId, bool a_result, pbnjson::JValue a_dimension, pbnjson::JValue a_settings);

    // cbGetSettings will be called via PrefsDb8Get.
    //
    static bool cbGetSettings(void *a_thiz_class, void *a_userdata, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_result);

protected:
    PrefsNotifier();
    ~PrefsNotifier();

private:
    class Element {
    public:
        typedef enum {
            eKey,
            eDesc
        } Type;

        static const std::string emptyDimension;

        Element(const std::string& a_category, const std::string& a_key, const std::string &a_dim, Type a_type)
            : m_category(a_category), m_key(a_key), m_dim_json(a_dim), m_appId(GLOBAL_APP_ID), m_type(a_type)
        {}

        Element(const std::string& a_category, const std::string& a_key, const std::string &a_appId, const std::string &a_dim, Type a_type)
            : m_category(a_category), m_key(a_key), m_dim_json(a_dim),  m_appId(a_appId), m_type(a_type)
        {}


        ~Element()
        {}

        bool operator < (const Element& a_rhs) const
        {
            if ( this->m_category < a_rhs.m_category )
            {
                return true;
            }
            if ( this->m_category == a_rhs.m_category && this->m_dim_json < a_rhs.m_dim_json )
            {
                return true;
            }
            if ( this->m_category == a_rhs.m_category && this->m_dim_json == a_rhs.m_dim_json &&
                    this->m_key < a_rhs.m_key )
            {
                return true;
            }
            if (this->m_category == a_rhs.m_category && this->m_dim_json == a_rhs.m_dim_json &&
                    this->m_key == a_rhs.m_key && this->m_type < a_rhs.m_type)
            {
                return true;
            }
            if (this->m_category == a_rhs.m_category && this->m_dim_json == a_rhs.m_dim_json &&
                    this->m_key == a_rhs.m_key && this->m_type == a_rhs.m_type &&
                    this->m_appId < a_rhs.m_appId)
            {
                return true;
            }

            return false;
        }

        const std::string& getCategory() const { return m_category; }
        const std::string& getKey() const { return m_key; }
        const std::string& getDimensionJson() const { return m_dim_json; }
        const std::string& getAppId() const { return m_appId; }
        const Type getType() const { return m_type; }

    private:
        Element();

        std::string m_category;
        std::string m_key;
        std::string m_dim_json;
        std::string m_appId;
        Type m_type;
    };

    typedef std::map< LSMessageElem, std::set<Element> > MessageContainer;
    typedef std::map< std::string /*key: dimension*/, MessageContainer > ContainerType;
    typedef std::map<LSMessageElem, pbnjson::JValue> SubKeyMap;
    typedef std::map< std::pair<std::string/*key*/, std::string/*appId*/>, std::string/*description*/ > KeyDescContainer;

    // Insert message, category and key into container.
    //
    void insert(MessageContainer& a_container, LSMessage * a_message, PrefsNotifier::Element a_element);

    // Checks whether specified message is in the container.
    //
    bool find(LSMessageElem& a_message) const;

    // Format the json object for notification. Mainly, filter-out inSettings objects.
    //
    void getValueReply(LSMessage* a_message, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_outSettings, pbnjson::JValue a_inSettings) const;

    // Get request information(category - key lists map) for PrefsDb8Get.
    //
    void getRequestList(CatKeyContainer& a_list, const std::string& a_dimension, Element::Type a_type) const;

    // Checks whether same dimension values.
    //
    bool isSameDimensionValues(const std::set<std::string>& a_keyList) const;

    // Subscription cancel handler for luna-service2
    //
    static bool cbSubscriptionCancel(LSHandle * a_handle, LSMessage * a_message, void *a_data);

    // Notification impl. which do actual notifications.
    //
    void doNotifyValue(LSHandle* a_handle, const TaskRequestInfo* a_requestInfo, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_result) const;
    void doNotifyDescription(LSHandle* a_handle, const TaskRequestInfo* a_requestInfo);

    void addSubscriptionImpl(LSHandle *a_handle, const std::string& a_category, const std::string& a_key, const std::string& a_appId, LSMessage * a_message, Element::Type a_type);

    // Checks whether same description and even save it into m_keyDesc.
    //
    bool isSameDescription(const std::string& a_key, const std::string& a_appId, const std::string& a_result);
    // Clear description in m_keyDesc.
    //
    void clearKeyDescription(const std::string& a_key, const std::string &a_appId);

    KeyDescContainer m_keyDesc;
    DimKeyValueMap m_dimensionValues;
    ContainerType m_container;
    mutable std::recursive_mutex m_container_mutex;
    static PrefsNotifier *_instance;
};

#endif                          /* PREFSNOTIFIER_H */
