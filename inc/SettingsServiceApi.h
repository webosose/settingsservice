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

#ifndef __SETTINGSSERVICESAPI_H__
#define __SETTINGSSERVICESAPI_H__

#include "JSONUtils.h"
#include "PrefsTaskMgr.h"

#define SETTINGSSERVICE_METHOD_BATCH                        "batch"
#define SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGS            "getSystemSettings"
#define SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGS            "setSystemSettings"
#define SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGFACTORYVALUE "getSystemSettingFactoryValue"
#define SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYVALUE "setSystemSettingFactoryValue"
#define SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUES       "getSystemSettingValues"
#define SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGVALUES       "setSystemSettingValues"
#define SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGDESC         "getSystemSettingDesc"
#define SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGDESC         "setSystemSettingDesc"
#define SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGFACTORYDESC  "setSystemSettingFactoryDesc"
#define SETTINGSSERVICE_METHOD_GETCURRENTSETTINGS           "getCurrentSettings"
#define SETTINGSSERVICE_METHOD_DELETESYSTEMSETTINGS         "deleteSystemSettings"
#define SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGS          "resetSystemSettings"
#define SETTINGSSERVICE_METHOD_RESETSYSTEMSETTINGDESC       "resetSystemSettingDesc"
#define SETTINGSSERVICE_METHOD_REQUESTGETSYSTEMSETTINGS     "requestGetSystemSettings"
#define SETTINGSSERVICE_METHOD_CHANGE_APP                   "changeApp"
#define SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGSPRIV        "getSystemSettingsPriv"
#define SETTINGSSERVICE_METHOD_SETSYSTEMSETTINGSPRIV        "setSystemSettingsPriv"
#define SETTINGSSERVICE_METHOD_GETSYSTEMSETTINGVALUESPRIV   "getSystemSettingValuesPriv"
#define SETTINGSSERVICE_METHOD_UNINSTALL_APP                "removeApp"

LSMethod* SettingsServiceApi_GetMethods();

#define JSON_SCHEMA_DESC_VTYPE_ENUM   ", \"enum\":[\"Array\", \"ArrayExt\", \"Range\", \"Date\", \"Callback\", \"File\"]"
#define JSON_SCHEMA_VALUES__OP_ENUM   ", \"enum\":[\"set\", \"add\", \"remove\", \"update\"]"
#define BATCH_OPERATION_ITEM_V2       ", \"items\":" SCHEMA_V2_2(",\"required\":[\"method\"]", SCHEMA_V2_STRING(method), SCHEMA_V2_OBJECT(params) )

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_batch batch

    Retrieves the values for keys specified in a passed array on system settings.

    @par Parameters
    Name        | Required | Type  | Description
    ------------|----------|-------|-------------------------
    operations  | yes      | Array | Item is operation object

    @par Item Object
    Name        | Required | Type   | Description
    ------------|----------|--------|---------------------------
    method      | yse      | String | method name
    params      | yes      | Object | any objects are acceptable

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|----------------
    returnValue | yes      | Boolean | True
    result      | yes      | Object  | settings object

    @par Returns(Subscription)
    not supported
@}
*/
#define JSON_SCHEMA_BATCH_PARAM_V2 \
    SCHEMA_V2_2( \
        ",\"required\":[\"operations\"]" , \
        SCHEMA_V2_PROP(operations, array, BATCH_OPERATION_ITEM_V2) , \
        SCHEMA_V2_PROP(subscribe, boolean) \
    )
bool cbBatchProcess(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_getSystemSettings getSystemSettings

    Retrieves the values for keys specified in a passed array on system settings.

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|----------------------------------------------
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object. Any properties are allowed.
    keys        | no       | Array   | Key-string array
    app_id      | no       | Array   | Application Id
    currunt_app | no       | Boolean | If true, refer foreground App
    subscribe   | no       | Boolean | subscription option

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|-----------------
    returnValue | yes      | Boolean | True
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object
    app_id      | no       | Array   | Application Id
    settings    | yes      | Object  | settings object

    @par Returns(Subscription)
    Name        | Required | Type    | Description
    ------------|----------|---------|-----------------
    returnValue | yes      | Boolean | True
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object
    app_id      | no       | Array   | Application Id
    settings    | yes      | Object  | settings object
@}
*/
#define JSON_SCHEMA_GET_SYSTEM_SETTINGS_PARAM_V2 \
    SCHEMA_V2_7( \
        "" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_OBJECT(dimension) , \
        SCHEMA_V2_ARRAY(keys, string) , \
        SCHEMA_V2_STRING(key) , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_PROP(current_app, boolean) , \
        SCHEMA_V2_PROP(subscribe, boolean) \
    )
bool cbGetSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_setSystemSettings setSystemSettings

    Save the specified settings in parameter.

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|----------------------------------------------
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object. Any properties are allowed.
    settings    | yes      | Object  | Settings
    app_id      | no       | Array   | Application Id
    currunt_app | no       | Boolean | If true, refer foreground App
    notify      | no       | Boolean | If false, disable notification
    store       | no       | Boolean | If false, no db8 operation
    valueCheck  | no       | Boolean | If false, any data can be set

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_SET_SYSTEM_SETTINGS_PARAM_V2 \
    SCHEMA_V2_10( \
        ",\"required\":[\"settings\"]" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_OBJECT(dimension) , \
        SCHEMA_V2_OBJECT(settings) , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_PROP(notifySelf, boolean) , \
        SCHEMA_V2_PROP(current_app, boolean) , \
        SCHEMA_V2_PROP(setAll, boolean) , \
        SCHEMA_V2_PROP(notify, boolean) , \
        SCHEMA_V2_PROP(store, boolean) , \
        SCHEMA_V2_PROP(valueCheck, boolean) \
    )
bool cbSetSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_getSystemSettingFactoryValue getSystemSettingFactoryValue

    Retrieve the list of default values for the specified key.

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object. Any properties are allowed.
    keys        | no       | Array   | Key-string array
    app_id      | no       | Array   | Application Id
    currunt_app | no       | Boolean | If true, refer foreground App

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object
    app_id      | no       | Array   | Application Id
    settings    | yes      | Object  | settings object

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_GET_SYSTEM_SETTING_FACTORY_VALUE_PARAM_V2 \
    SCHEMA_V2_5( \
        "" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_OBJECT(dimension) , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_ARRAY(keys, string) , \
        SCHEMA_V2_STRING(key) \
    )
bool cbGetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_setSystemSettingFactoryValue setSystemSettingFactoryValue

    Save the specified settings in parameter.

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|------------------------------------------------
    app_id      | no       | string  | Application Id for Per-App
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object. Any properties are allowed.
    settings    | yes      | Object  | Settings
    setAll      | no       | boolean | update all values regardless dimension if true.
    country     | no       | String  | Country variation
    valueCheck  | no       | Boolean | If false, any data can be set

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_SET_SYSTEM_SETTING_FACTORY_VALUE_PARAM_V2 \
    SCHEMA_V2_7( \
        ",\"required\":[\"settings\"]" , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_OBJECT(dimension) , \
        SCHEMA_V2_OBJECT(settings) , \
        SCHEMA_V2_PROP(setAll, boolean) , \
        SCHEMA_V2_STRING(country) , \
        SCHEMA_V2_PROP(valueCheck, boolean) \
    )
bool cbSetSystemSettingFactoryValue(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_getSystemSettingValues getSystemSettingValues

    Retrieve the list of valid values for the specified key

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|--------------------
    app_id      | no       | string  | Application Id for Per-App
    category    | no       | String  | Category name
    key         | yes      | String  | Key-string
    subscribe   | no       | Boolean | Subscription option

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|-------------------------------------------
    returnValue | yes      | Boolean | True
    category    | no       | String  | Category name
    vtype       | yes      | String  | Array, ArrayExt, Range, Callback, and Date
    values      | yes      | Object  | Values object

    @par "Values" Object
    Name        | Required | Type    | Description
    ------------|----------|---------|---------------------------
    array       | no       | Array   | Items (string only)
    arrayExt    | no       | Array   | Items (object)
    range       | no       | Object  | Min, max, and interval
    date        | no       | String  | ISO-8601
    callback    | no       | Object  | uri, method, and parameter

    @par Returns(Subscription)
    Name        | Required | Type    | Description
    ------------|----------|---------|-------------------------------------------
    returnValue | yes      | Boolean | True
    category    | no       | String  | Category name
    vtype       | yes      | String  | Array, ArrayExt, Range, Callback, and Date
    values      | yes      | Object  | Values object
@}
*/
#define JSON_SCHEMA_GET_SYSTEM_SETTING_VALUES_PARAM_V2 \
    SCHEMA_V2_4( \
        ",\"required\":[\"key\"]" , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_STRING(key) , \
        SCHEMA_V2_PROP(subscribe, boolean) \
    )
bool cbGetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_setSystemSettingValues setSystemSettingValues

    Update the list of valid values for the specified key

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|---------------------
    category    | no       | String  | Category name
    key         | yes      | String  | Key-string
    vtype       | yes      | String  | Values object type
    op          | yes      | String  | Add, remove, and set
    values      | yes      | Object  | Values object

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par "Values" Object
    Name        | Required | Type    | Description
    ------------|----------|---------|---------------------------
    array       | no       | Array   | Items (string)
    arrayExt    | no       | Array   | Items (object)
    range       | no       | Object  | Min, max, and interval
    date        | no       | String  | ISO-8601
    callback    | no       | Object  | uri, method, and parameter

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_SET_SYSTEM_SETTING_VALUES_PARAM_V2 \
    SCHEMA_V2_5( \
        ",\"required\":[\"key\",\"vtype\",\"op\",\"values\"]" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_STRING(key) , \
        SCHEMA_V2_PROP(vtype, string, JSON_SCHEMA_DESC_VTYPE_ENUM) , \
        SCHEMA_V2_PROP(op, string, JSON_SCHEMA_VALUES__OP_ENUM) , \
        SCHEMA_V2_OBJECT(values) \
    )
bool cbSetSystemSettingValues(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_getSystemSettingDesc getSystemSettingDesc

    Retrieve the description for the specified key

    @par Parameters
    Name         | Required | Type    | Description
    -------------|----------|---------|------------------------------------------
    category     | no       | String  | Category name
    keys         | no       | Array   | Key-string array
    key          | no       | String  | Key string (can be used instead of keys)
    app_id       | no       | String  | specify appId for finding per-app descriptions
    current_app  | no       | Boolean | use current appId for finding per-app descriptions
    subscribe    | no       | Boolean | subscription option

    @par Returns(Call)
    Name         | Required | Type    | Description
    -------------|----------|---------|------------
    returnValue  | yes      | Boolean | True
    category     | no       | String  | Category name
    descriptions | yes      | Array   | Array of Description objects
    app_id       | no       | String  | Application ID if request has app_id or current_app (means per-app request)

    @par "Description" Object
    Name         | Required | Type    | Description
    -------------|----------|---------|-------------------------------------------
    key          | yes      | String  | Key string
    category     | no       | String  | Category name
    ui           | no       | Object  | widget, displayname, visable, and active
    vtype        | no       | String  | Array, ArrayExt, Range, Date, and Callback
    values       | no       | Object  | Values object

    @par "Values" Object
    Name         | Required | Type    | Description
    -------------|----------|---------|---------------------------
    array        | no       | Array   | Items (string)
    arrayExt     | no       | Array   | Items (object)
    range        | no       | Object  | Min, max, and interval
    date         | no       | String  | ISO-8601
    callback     | no       | Object  | uri, method, and parameter

    @par Returns(Subscription)
    Name         | Required | Type    | Description
    -------------|----------|---------|-----------------------------
    returnValue  | yes      | Boolean | True
    category     | no       | String  | Category name
    descriptions | yes      | Array   | Array of Description objects
@}
*/
#define JSON_SCHEMA_GET_SYSTEM_SETTING_DESC_PARAM_V2 \
    SCHEMA_V2_6( \
        "" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_ARRAY(keys, string) , \
        SCHEMA_V2_STRING(key) , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_PROP(current_app, boolean) , \
        SCHEMA_V2_PROP(subscribe, boolean) \
    )
bool cbGetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_setSystemSettingDesc setSystemSettingDesc

    Update the description for the specified key

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|-------------------------------------------------
    category    | no       | string  | category name
    key         | no       | string  | key string (key can be used instead of keys)
    ui          | no       | Object  | widget, displayname, visable, and active
    vtype       | no       | String  | Array, ArrayExt, Range, Date, File, and Callback
    values      | no       | Object  | Values object
    volatile    | no       | Boolean | If true, data is stored in volatile kind
    valueCheck  | no       | Boolean | If false, any data can be set
    ext         | no       | Object  | Any extension object can be specified.

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par "ui" Object
    Name        | Required | Type    | Description
    ------------|----------|---------|--------------------
    widget      | no       | Array   | UI component name
    displayname | no       | Object  | UI title (i18n key)
    visable     | no       | String  | Visible flag
    active      | no       | Object  | Active flag

    @par "Values" Object
    Name        | Required | Type    | Description
    ------------|----------|---------|---------------------------
    array       | no       | Array   | Items (string)
    arrayExt    | no       | Array   | Items (object)
    range       | no       | Object  | Min, max, and interval
    date        | no       | String  | ISO-8601
    callback    | no       | Object  | uri, method, and parameter

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_SET_SYSTEM_SETTING_DESC_PARAM_V2 \
    SCHEMA_V2_10( \
        "" , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_STRING(key) , \
        SCHEMA_V2_OBJECT(ui) , \
        SCHEMA_V2_PROP(vtype, string, JSON_SCHEMA_DESC_VTYPE_ENUM) , \
        SCHEMA_V2_OBJECT(values) , \
        SCHEMA_V2_PROP(volatile, boolean) , \
        SCHEMA_V2_PROP(valueCheck, boolean) , \
        SCHEMA_V2_PROP(notifySelf, boolean) , \
        SCHEMA_V2_OBJECT(ext) \
    )
bool cbSetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_setSystemSettingFactoryDesc setSystemSettingFactoryDesc

    Update the description for the specified key

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|-------------------------------------------------
    app_id      | no       | string  | Application Id for Per-App Description
    category    | no       | string  | category name
    key         | no       | string  | key string (key can be used instead of keys)
    ui          | no       | Object  | widget, displayname, visable, and active
    vtype       | no       | String  | Array, ArrayExt, Range, Date, File, and Callback
    values      | no       | Object  | Values object
    volatile    | no       | Boolean | If true, data is stored in volatile kind
    valueCheck  | no       | Boolean | If false, any data can be set
    ext         | no       | Object  | Any extension object can be specified.

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par "ui" Object
    Name        | Required | Type    | Description
    ------------|----------|---------|--------------------
    widget      | no       | Array   | UI component name
    displayname | no       | Object  | UI title (i18n key)
    visable     | no       | String  | Visible flag
    active      | no       | Object  | Active flag

    @par "Values" Object
    Name        | Required | Type    | Description
    ------------|----------|---------|---------------------------
    array       | no       | Array   | Items (string)
    arrayExt    | no       | Array   | Items (object)
    range       | no       | Object  | Min, max, and interval
    date        | no       | String  | ISO-8601
    callback    | no       | Object  | uri, method, and parameter

    @par Returns(Subscription)
    None
@}
*/
bool cbSetSystemSettingFactoryDesc(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_getCurrentSettings getCurrentSettings

    Returns settings of foreground App

    @par Parameters
    Name      | Required | Type    | Description
    ----------|----------|---------|----------------------------------------------
    category  | no       | String  | Category name
    dimension | no       | Object  | Dimension object. Any properties are allowed.
    keys      | no       | Array   | Key-string Array
    subscribe | no       | Boolean | Subscription option

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|-----------------
    returnValue | yes      | Boolean | True
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object
    app_id      | no       | Array   | Application Id
    settings    | yes      | Object  | settings object

    @par Returns(Subscription)
    Name        | Required | Type    | Description
    ------------|----------|---------|-----------------
    returnValue | yes      | Boolean | True
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object
    app_id      | no       | Array   | Application Id
    settings    | yes      | Object  | settings object
@}
*/
#define JSON_SCHEMA_GET_CURRENT_SETTINGS_V2 \
    SCHEMA_V2_4( \
        "" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_OBJECT(dimension) , \
        SCHEMA_V2_ARRAY(keys, string) , \
        SCHEMA_V2_PROP(subscribe, boolean) \
    )
bool cbGetCurrentSettings(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_deleteSystemSettings deleteSystemSettings

    Update the description for the specified key

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|----------------------------------------------
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object. Any properties are allowed.
    app_id      | no       | Array   | Application Id
    keys        | no       | Array   | Key-string array

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_DEL_SYSTEM_SETTINGS_V2 \
    SCHEMA_V2_4( \
        "" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_OBJECT(dimension) , \
        SCHEMA_V2_ARRAY(keys, string) , \
        SCHEMA_V2_STRING(app_id) \
    )
bool cbDelSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_resetSystemSettings resetSystemSettings

    Update the description for the specified key

    @par Parameters
    Name        | Required | Type    | Description
    ------------|----------|---------|-----------------------------------------------
    category    | no       | String  | Category name
    dimension   | no       | Object  | Dimension object. Any peroperties are allowed.
    app_id      | no       | String  | Application Id
    keys        | no       | Array   | Key-string array
    resetAll    | no       | boolean | If true, reset all settings

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_RESET_SYSTEM_SETTINGS_V2 \
    SCHEMA_V2_5( \
        "" , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_OBJECT(dimension) , \
        SCHEMA_V2_ARRAY(keys, string) , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_PROP(resetAll, boolean) \
    )
bool cbResetSystemSettings(LSHandle * lsHandle, LSMessage * message, void *user_data);

/**
@page com_webos_settingsservice com.webos.settingsservice
@{
    @section com_webos_settingsservice_resetSystemSettingDesc resetSystemSettingDesc

    Update the description for the specified key

    @par Parameters
    Name        | Required | Type   | Description
    ------------|----------|--------|-----------------
    app_id      | no       | String | Application Id
    category    | no       | String | Category name
    keys        | no       | Array  | Key-string array

    @par Returns(Call)
    Name        | Required | Type    | Description
    ------------|----------|---------|------------
    returnValue | yes      | Boolean | True

    @par Returns(Subscription)
    None
@}
*/
#define JSON_SCHEMA_RESET_SYSTEM_SETTING_DESC_V2 \
    SCHEMA_V2_3( \
        "" , \
        SCHEMA_V2_STRING(app_id) , \
        SCHEMA_V2_STRING(category) , \
        SCHEMA_V2_ARRAY(keys, string) \
    )
bool cbResetSystemSettingDesc(LSHandle * lsHandle, LSMessage * message, void *user_data);

#endif
