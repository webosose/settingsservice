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

//->Start of API documentation comment block
/**
@page com_webos_settingsservice com.webos.settingsservice

@brief Service component for Setting. Provides APIs to set/get/manage settings

@{
@}
*/
//->End of API documentation comment block

#include <thread>
#include <algorithm>
#include <functional>

#include "Logging.h"
#include "PrefsFactory.h"
#include "PrefsFactory.h"
#include "PrefsNotifier.h"
#include "PrefsPerAppHandler.h"
#include "SettingsService.h"
#include "SettingsServiceApi.h"
#include "Utils.h"

const std::string subscriptionValueKey = ".Value";
const std::string PrefsNotifier::Element::emptyDimension = "";

PrefsNotifier* PrefsNotifier::_instance = NULL;
PrefsNotifier *PrefsNotifier::instance()
{
    if (!PrefsNotifier::_instance)
    {
        PrefsNotifier::_instance = new PrefsNotifier;
    }

    return PrefsNotifier::_instance;
}

PrefsNotifier::PrefsNotifier()
{
}

PrefsNotifier::~PrefsNotifier()
{
}

void PrefsNotifier::initialize()
{
    using namespace std::placeholders;

    PrefsFactory::SubsCancelFunc func = std::bind(&PrefsNotifier::cbSubscriptionCancel, _1, _2, this);
    PrefsFactory::instance()->registerSubscriptionCancel(func);
}

void PrefsNotifier::insert(MessageContainer& a_container, LSMessage * a_message, PrefsNotifier::Element& a_element)
{
    LSMessageElem messageElem(a_message);

    MessageContainer::iterator it = a_container.find(messageElem);
    if (it != a_container.end())
    {
        it->second.insert(a_element);
    }
    else
    {
        std::set<Element> elemSet;
        elemSet.insert(a_element);
        a_container.insert( MessageContainer::value_type(messageElem, elemSet) );
    }
}

bool PrefsNotifier::find(LSMessageElem& a_message) const
{
    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
    for (ContainerType::value_type citer : m_container)
    {
        if ( citer.second.find(a_message) != citer.second.end() )
        {
            return true;
        }
    }

    return false;
}

void PrefsNotifier::getValueReply(LSMessage* a_message, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_outSettings, pbnjson::JValue a_inSettings) const
{
    std::string dimension_json = a_dimObj.isNull() ? Element::emptyDimension : a_dimObj.stringify();

    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
    for (ContainerType::value_type citer : m_container)
    {
        MessageContainer::const_iterator msgCiter = citer.second.find(LSMessageElem(a_message));
        if ( msgCiter == citer.second.end() )
            continue;

        const std::set<Element>& elemSet = msgCiter->second;
        for (pbnjson::JValue::KeyValue it : a_inSettings.children()) {
            std::string keyString(it.first.asString());
            if (elemSet.find(Element(a_category, keyString, a_appId, dimension_json, Element::eKey)) != elemSet.end())
            {
                a_outSettings.put(keyString, it.second);
            }
        }
    }
}

void PrefsNotifier::addSubscriptionPerApp(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage)
{
    PrefsPerAppHandler::instance().addSubscription(category, key, appId, lsMessage);
}

void PrefsNotifier::addDescSubscriptionPerApp(const std::string& category, const std::string& key, const std::string& appId, LSMessage* lsMessage)
{
    PrefsPerAppHandler::instance().addDescSubscription(category, key, appId, lsMessage);
}

void PrefsNotifier::addSubscriptionImpl(LSHandle *a_handle, const std::string& a_category, const std::string& a_key,  const std::string& a_appId, LSMessage * a_message, Element::Type a_type)
{
    std::set<std::string> dependentDims = PrefsKeyDescMap::instance()->findDependentDimensions(a_key);

    std::set<std::string> subscribeKeyList;
    
    Element ObjElement(a_category, a_key, a_appId, Element::emptyDimension, a_type);
    for ( const std::string& k : dependentDims )
        subscribeKeyList.insert(k + subscriptionValueKey);

    for ( const std::string& k : subscribeKeyList )
    {
        std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
        ContainerType::iterator it = m_container.find(k);

        if (it != m_container.end())
        {
            MessageContainer& msgContainer = it->second;

            insert(msgContainer, a_message, ObjElement);
        }
        else
        {
            MessageContainer msgContainer;
            insert(msgContainer, a_message, ObjElement);
            m_container.insert( ContainerType::value_type(k, msgContainer) );
        }

        if (a_type == Element::eKey)
        {
            Utils::subscriptionAdd(a_handle, k.c_str(), a_message);
        }
        else
        {
            // TODO: Add subscription if needed.
        }
    }
}

void PrefsNotifier::addSubscription(LSHandle *a_handle, const std::string &a_cat, pbnjson::JValue a_dimObj, const std::string &a_key, LSMessage *a_msg)
{
    LSError lsError;
    LSErrorInit(&lsError);
    ContainerType::iterator it;

    /* TODO: extend subscribe key to support app_id subscription */

    std::string subscribe_key = a_dimObj.isNull() ? Element::emptyDimension : a_dimObj.stringify();

    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
    it = m_container.find(subscribe_key);
    Element ObjElement(a_cat, a_key, subscribe_key, Element::eKey);
    if (it != m_container.end()) {
        MessageContainer& msgContainer = it->second;
        insert(msgContainer, a_msg, ObjElement);
    } else {
        MessageContainer msgContainer;
        insert(msgContainer, a_msg, ObjElement);
        m_container.insert( ContainerType::value_type(subscribe_key, msgContainer) );
    }

    Utils::subscriptionAdd(a_handle, subscribe_key.c_str(), a_msg);
}

void PrefsNotifier::addSubscriptionPerDimension(LSHandle *a_handle, const std::string& a_category, const std::string& a_key, const std::string& a_appId, LSMessage * a_message)
{
    addSubscriptionImpl(a_handle, a_category, a_key, a_appId, a_message, Element::eKey);
}

void PrefsNotifier::addDescSubscription(LSHandle *a_handle, const std::string& a_category, const std::string& a_key, const std::string& a_appId, LSMessage * a_message)
{
    LSError lsError;
    LSErrorInit(&lsError);
    std::string subscribe_key;
    ContainerType::iterator it;

    subscribe_key = Element::emptyDimension;

    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
    it = m_container.find(subscribe_key);
    Element ElementObj(a_category, a_key, a_appId, subscribe_key, Element::eDesc);
    if (it != m_container.end()) {
        MessageContainer& msgContainer = it->second;
        insert(msgContainer, a_message, ElementObj);
    } else {
        MessageContainer msgContainer;
        insert(msgContainer, a_message, ElementObj);
        m_container.insert( ContainerType::value_type(subscribe_key, msgContainer) );
    }
}

void PrefsNotifier::addDescSubscriptionPerDimension(LSHandle *a_handle, const std::string& a_category, const std::string& a_key, const std::string& a_appId, LSMessage * a_message)
{
    addSubscriptionImpl(a_handle, a_category, a_key, a_appId, a_message, Element::eDesc);
}

void PrefsNotifier::removeSubscription(LSMessage * a_message)
{
    PrefsPerAppHandler::instance().delSubscription(a_message);

    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
    for (ContainerType::iterator it = m_container.begin();
        it != m_container.end();
        )
    {
        /* erase cached key description if no subscriber */
        MessageContainer::const_iterator elements = it->second.find(LSMessageElem(a_message));
        if ( elements != it->second.end() ) {
            for ( const Element& elem : elements->second ) {
                if ( elem.getType() != Element::Type::eDesc ) {
                    /* We don't need to check if it isn't for description.
                     * This loop is for clearKeyDescription() */
                    continue;
                }

                std::string subscribeKey = SUBSCRIBE_STR_KEYDESC(elem.getKey(), elem.getAppId());
                if ( PrefsFactory::instance()->noSubscriber(subscribeKey.c_str(), a_message) ) {
                    clearKeyDescription(elem.getKey(), elem.getAppId());
                }
            }
        }

        /* erase subscripiton elements */
        it->second.erase( LSMessageElem(a_message) );
        if (it->second.empty())
        {
            m_container.erase(it++);
        }
        else
        {
            it++;
        }
    }
}

//
// getRequestList
//    Retrieve category - keyList container.
//
void PrefsNotifier::getRequestList(CatKeyContainer& a_list, const std::string& a_dimension, Element::Type a_type) const
{
    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
    ContainerType::const_iterator citer = m_container.find(a_dimension);
    if (citer == m_container.end())
    {
        return;
    }

    const MessageContainer& msgContainer = citer->second;
    for (MessageContainer::const_iterator msgCiter = msgContainer.begin();
        msgCiter != msgContainer.end();
        ++msgCiter)
    {
        const std::set<Element>& keySet = msgCiter->second;
        for (std::set<Element>::const_iterator keyCiter = keySet.begin();
            keyCiter != keySet.end();
            ++keyCiter)
        {
            const std::string& category = keyCiter->getCategory();
            const std::string& key = keyCiter->getKey();

            if (a_type != keyCiter->getType())
                continue;

            CatKeyContainer::iterator it = a_list.find( { category, keyCiter->getAppId() } );
            if (it != a_list.end())
            {
                it->second.insert(key);
            }
            else
            {
                std::set<std::string> keySet;
                keySet.insert(key);
                a_list.insert( CatKeyContainer::value_type( { category, keyCiter->getAppId() }, keySet) );
            }
        }
    }
}

void PrefsNotifier::notifyEarly(const std::string& a_category, const std::set<std::string>& a_keyList)
{
    // Save previous dimension values here.
    //
    m_dimensionValues = PrefsKeyDescMap::instance()->getCurrentDimensionValues();
}

bool PrefsNotifier::isSameDimensionValues(const std::set<std::string>& a_keyList) const
{
    for (const std::string& citer : a_keyList)
    {
        std::string keyAll = citer + subscriptionValueKey;

        std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
        if (m_container.find(keyAll) != m_container.end())
        {
            const DimKeyValueMap& newDimensionValues = PrefsKeyDescMap::instance()->getCurrentDimensionValues();
            return m_dimensionValues == newDimensionValues;
        }
    }

    return true;
}

void PrefsNotifier::notifyAllSettings(void)
{
    CatKeyContainer requestList, requestDescList;
    std::vector<std::string> all_subscribe_keys;
    std::vector<std::string> dimensionList;
    std::vector<std::string> dimensionListInContainer;

    dimensionList = PrefsKeyDescMap::instance()->getDimensionInfo();

    std::transform(dimensionList.begin(), dimensionList.end(), dimensionList.begin(),
            std::bind2nd(std::plus<std::string>(), subscriptionValueKey));

    /* make request list based on entire subscriptions */

    {
        std::lock_guard<std::recursive_mutex> lock(m_container_mutex);
        for (auto it : m_container) {
            const std::string & dimensionStr = it.first;
            dimensionListInContainer.push_back(dimensionStr);
        }
    }

    for ( std::vector<std::string>::const_iterator citer = dimensionListInContainer.begin();
            citer != dimensionListInContainer.end(); ++citer )
    {
        const std::string & dimensionStr = *citer;

        /* TODO: Make SUBSCRIPTION_VALUE_KEY and SUBSCRIPTION_DIM_KEY be separate.
         * skip subscription for dimension */
        if ( std::find(dimensionList.begin(), dimensionList.end(), dimensionStr) != dimensionList.end() )
            continue;

        /* If subscription request has dimension object,
         * subscribe key is registered as dimension json string */
        pbnjson::JValue dimension_obj = pbnjson::JDomParser::fromString(dimensionStr);
        if (dimension_obj.isNull()) {
            dimension_obj = pbnjson::JValue();
        }

        requestList.clear();
        requestDescList.clear();
        all_subscribe_keys.clear();

        getRequestList(requestList, dimensionStr.c_str(), Element::eKey);
        getRequestList(requestDescList, dimensionStr.c_str(), Element::eDesc);
        all_subscribe_keys.push_back(dimensionStr);

        if (requestList.size())
        {
            // TODO - TaskRequestInfo shall be reference counted.
            //
            TaskRequestInfo* requestInfo = new TaskRequestInfo;
            requestInfo->requestList = requestList;
            requestInfo->requestDimObj = dimension_obj;
            requestInfo->requestCount = requestList.size() + REQUEST_GETSYSTEMSETTINGS_REF_CNT;
            requestInfo->subscribeKeys = all_subscribe_keys;
            requestInfo->cbFunc = reinterpret_cast<void *>(&PrefsNotifier::cbGetSettings);
            requestInfo->thiz_class = reinterpret_cast<void *>(const_cast<PrefsNotifier *>(this));
            SSERVICELOG_TRACE("%s: %s", __FUNCTION__, dimension_obj.stringify().c_str());
            PrefsFactory::instance()->getTaskManager().pushUserMethod(METHODID_REQUEST_GETSYSTEMSETTIGNS, PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE), NULL, reinterpret_cast<void *>(requestInfo), TASK_PUSH_FRONT);
        }

        if (requestDescList.size())
        {
            // TODO: check this is needed. Notify the description now.
            //
            TaskRequestInfo requestInfo;
            requestInfo.requestList = requestDescList;
            requestInfo.requestDimObj = pbnjson::JValue();
            requestInfo.requestCount = requestDescList.size() + REQUEST_GETSYSTEMSETTINGS_REF_CNT;
            requestInfo.subscribeKeys = all_subscribe_keys;
            requestInfo.cbFunc = NULL;
            requestInfo.thiz_class = NULL;

            doNotifyDescription(PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE), &requestInfo);
        }
    }
}

void PrefsNotifier::notifyByDimension(const std::string& a_category, const std::set<std::string>& a_keyList, pbnjson::JValue a_changedList)
{
    if (isSameDimensionValues(a_keyList))
    {
        // In case same dimension values, then do nothing.
        //
        return;
    }

    std::vector<std::string> dimensionList, subscribeKeyList;
    CatKeyContainer requestList, requestDescList;

    dimensionList = PrefsKeyDescMap::instance()->getDimensionInfo();

    // TODO - 이 부분에서,
    //   - 내가 subscription하고 있는 dimension인 경우에만 처리하도록 추가 필요.
    //   - notifyEarly()에서 저장해둔 것과 다른 경우에만 처리.
    //
    for (const std::string& citer : dimensionList)
    {
        pbnjson::JValue obj = a_changedList[citer];
        if (obj.isNull())
            continue;

        // For the 'dimension' list, gather request lists.
        //
        std::string subsKey = citer + subscriptionValueKey;

        getRequestList(requestList, subsKey, Element::eKey);
        getRequestList(requestDescList, subsKey, Element::eDesc);

        subscribeKeyList.push_back(subsKey);
    }
    SSERVICELOG_TRACE("%s:requestList.size()=%d; requestDescList.size()=%d", __FUNCTION__, requestList.size(), requestDescList.size());
    if (requestList.size())
    {
        // TODO - TaskRequestInfo shall be reference counted.
        //
        TaskRequestInfo* requestInfo = new TaskRequestInfo;
        requestInfo->requestList = requestList;
        requestInfo->requestDimObj = pbnjson::JValue();
        requestInfo->requestCount = requestList.size() + REQUEST_GETSYSTEMSETTINGS_REF_CNT;
        requestInfo->subscribeKeys = subscribeKeyList;
        requestInfo->cbFunc = reinterpret_cast<void *>(&PrefsNotifier::cbGetSettings);
        requestInfo->thiz_class = reinterpret_cast<void *>(const_cast<PrefsNotifier *>(this));
        SSERVICELOG_TRACE("%s", __FUNCTION__);
        PrefsFactory::instance()->getTaskManager().pushUserMethod(METHODID_REQUEST_GETSYSTEMSETTIGNS, PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE), NULL, reinterpret_cast<void *>(requestInfo), TASK_PUSH_FRONT);
    }

    if (requestDescList.size())
    {
        // Notify the description now.
        //
        TaskRequestInfo requestInfo;
        requestInfo.requestList = requestDescList;
        requestInfo.requestDimObj = pbnjson::JValue();
        requestInfo.requestCount = requestDescList.size() + REQUEST_GETSYSTEMSETTINGS_REF_CNT;
        requestInfo.subscribeKeys = subscribeKeyList;
        requestInfo.cbFunc = NULL;
        requestInfo.thiz_class = NULL;

        doNotifyDescription(PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE), &requestInfo);
    }

    // FIXME - resetSystemSettings does not updates dimension values currently.
    // Clear dimension values until it is fixed, so that it notifies.
    //
    m_dimensionValues.clear();
}

pbnjson::JValue PrefsNotifier::jsonSubsReturn(const std::string& a_category, const std::string &a_appId, bool a_result, pbnjson::JValue a_dimension, pbnjson::JValue a_settings)
{
    // TODO: Merge with PrefsFactory:::postPrefChangeCategory()
    //
    pbnjson::JValue replyRoot(pbnjson::Object());
    std::string firstKey;

    if (a_result)
    {
        replyRoot.put("settings", a_settings);
        // store first key for subscription
        for(pbnjson::JValue::KeyValue it : a_settings.children()) {
            if(firstKey.empty()) {
                firstKey = it.first.asString();
                break;
            }
        }
    }
    else
    {
        pbnjson::JValue errorKeyArray(pbnjson::Array());
        for (pbnjson::JValue::KeyValue it : a_settings.children()) {
            // store first key to use subscription.
            if(firstKey.empty()) {
                firstKey = it.first.asString();
            }
            errorKeyArray.append(it.first);
        }
        replyRoot.put("errorKey", errorKeyArray);
    }

    replyRoot.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS);
    replyRoot.put("returnValue", a_result);
    replyRoot.put("app_id", a_appId);

    replyRoot.put("category", a_category);
    // For one key, use the dimension based on the key.
    pbnjson::JValue dimInfo;

    if (a_settings.isObject() && a_settings.objectSize() == 1 && !firstKey.empty()) {
        dimInfo = PrefsKeyDescMap::instance()->getDimKeyValueObj(a_category, a_dimension, firstKey);
    }
    // In case of sending multiple keys, and ONLY if it is related with dimensions,
    // the 'dimension' should be made using OR-ed all the possible dimension per each keys.
    //
    else {
        pbnjson::JValue categoryDim = PrefsKeyDescMap::instance()->getDimKeyValueObj(a_category, a_dimension);
        if (!categoryDim.isNull())
        {
            dimInfo = pbnjson::Object();
            for (pbnjson::JValue::KeyValue it : a_settings.children()) {
                PrefsKeyDescMap::instance()->getDimKeyValueObj(a_category, a_dimension, it.first.asString(), dimInfo);
            }
        }
    }
    if (!dimInfo.isNull()) {
        replyRoot.put("dimension", dimInfo);
    }

    return replyRoot;
}

void PrefsNotifier::clearKeyDescription(const std::string& a_key, const std::string &a_appId)
{
    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);

    m_keyDesc.erase({a_key, a_appId});
}

bool PrefsNotifier::isSameDescription(const std::string& a_key, const std::string &a_appId, const std::string& a_resultString)
{
    std::lock_guard<std::recursive_mutex> lock(m_container_mutex);

    // FIX-ME: Split into two function - save and check.
    //
    KeyDescContainer::iterator it = m_keyDesc.find({a_key, a_appId});
    if (it == m_keyDesc.end())
    {
        m_keyDesc.insert(KeyDescContainer::value_type({a_key, a_appId}, a_resultString));
        return false;
    }
    else
    {
        if (it->second == a_resultString)
        {
            return true;
        }
        m_keyDesc.erase(it);
        m_keyDesc.insert(KeyDescContainer::value_type({a_key, a_appId}, a_resultString));
    }

    return false;
}

void PrefsNotifier::doNotifyDescription(LSHandle* a_handle, const TaskRequestInfo* a_requestInfo)
{
    const CatKeyContainer& requestDescList = a_requestInfo->requestList;

    for (CatKeyContainer::const_iterator citer = requestDescList.begin();
        citer != requestDescList.end();
        ++citer)
    {
        std::string appId = citer->first.second;
        const std::set<std::string>& keyList = citer->second;
        for (const std::string& keyCiter : keyList)
        {
            pbnjson::JValue result_obj = PrefsKeyDescMap::instance()->genDescFromCache(keyCiter, appId);
            if (result_obj.isNull())
                continue;

            std::string subscribeKey = SUBSCRIBE_STR_KEYDESC(keyCiter, appId);
            result_obj.put(KEYSTR_APPID, appId);

            result_obj.put("method", SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC);
            result_obj.put("returnValue", true);

            std::string resultString = result_obj.stringify();
            if (isSameDescription(keyCiter, appId, resultString) == false)
            {
                PrefsFactory::instance()->postPrefChange(subscribeKey.c_str(), resultString);
            }
        }
    }
}

void PrefsNotifier::doNotifyValue(LSHandle* a_handle, const TaskRequestInfo* a_requestInfo, const std::string& a_appId, const std::string& a_category, pbnjson::JValue a_dimObj, pbnjson::JValue a_result) const
{
    const std::vector<std::string>& subscribe_keys = a_requestInfo->subscribeKeys;
    SubKeyMap subKeyMap;
    bool err;
    LSError lsError;
    LSSubscriptionIter *iter = NULL;

    // Prepare all the keys per messages.
    //
    for (const std::string& a_key : subscribe_keys)
    {
        LSErrorInit(&lsError);
        err = LSSubscriptionAcquire(a_handle, a_key.c_str(), &iter, &lsError);
        if (err == false)
        {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Acquire to notify");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            continue;
        }

        while (LSSubscriptionHasNext(iter)) {
            LSMessageElem lsMsg(LSSubscriptionNext(iter));

            if (find(lsMsg) == false)
                continue;

            SubKeyMap::iterator it = subKeyMap.find(lsMsg);
            if (it != subKeyMap.end())
            {
                getValueReply(lsMsg.get(), a_category, a_appId, a_dimObj, it->second, a_result);
            }
            else
            {
                pbnjson::JValue obj = pbnjson::Object();
                getValueReply(lsMsg.get(), a_category, a_appId, a_dimObj, obj, a_result);
                subKeyMap.insert(SubKeyMap::value_type(lsMsg, obj));
            }
        }

        LSSubscriptionRelease(iter);
        iter = NULL;
    }

    LSErrorInit(&lsError);
    pbnjson::JValue replyRoot = jsonSubsReturn(a_category, a_appId, true, a_dimObj, a_result);

    for (const std::pair<LSMessageElem, pbnjson::JValue>& it : subKeyMap)
    {
        if (it.second.isNull())
            continue;

        if (it.second.isObject() && it.second.objectSize() == 0)
            continue;

        if (it.second.isArray() && it.second.arraySize() == 0)
            continue;

        replyRoot.put("settings", it.second);

        LSErrorInit(&lsError);

        if (!LSMessageReply(a_handle, it.first.get(), replyRoot.stringify().c_str(), &lsError))
        {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reply to notify");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
        }
    }
}

bool PrefsNotifier::cbGetSettings(void *a_thiz_class, void *a_userdata, const std::string& a_category, const std::string& a_appId, pbnjson::JValue a_dimObj, pbnjson::JValue a_result)
{
    PrefsNotifier* thiz_class = static_cast<PrefsNotifier *>(a_thiz_class);
    TaskRequestInfo* requestInfo = static_cast<TaskRequestInfo *>(a_userdata);
    bool completed = false;

    if (!thiz_class || !requestInfo)
    {
        completed = true;
        return completed;
    }

    // Do notify per each handles.
    //
    for (auto handle: PrefsFactory::instance()->getServiceHandles())
    {
        thiz_class->doNotifyValue(handle, requestInfo, a_appId, a_category, a_dimObj, a_result);
    }

    if (g_atomic_int_dec_and_test(&requestInfo->requestCount) == TRUE)
    {
        // FIXME - If this callback is not called as 'requestCount' specified, this codes will not be reached.
        //
        delete requestInfo;
        completed = true;
    }

    return completed;
}

bool PrefsNotifier::cbSubscriptionCancel(LSHandle * a_handle, LSMessage * a_message, void *a_data)
{
    PrefsNotifier* thiz_class = static_cast<PrefsNotifier *>(a_data);
    if (!thiz_class)
        return false;

    thiz_class->removeSubscription(a_message);

    return true;
}
