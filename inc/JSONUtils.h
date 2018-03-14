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

#ifndef JSONUTILS_H
#define JSONUTILS_H

#include <set>
#include <luna-service2/lunaservice.h>
#include <pbnjson.hpp>

#include "Settings.h"

/*
 * Helper macros to build schemas in a more reliable, readable & editable way in C++
 */

#define STR(x) #x

extern const char *STANDARD_JSON_SUCCESS;

/**
  * Json Online Schema Validator : http://jsonlint.com/
  * http://davidwalsh.name/json-validation
  */

// Build a standard reply as a const char * string consistently
#define STANDARD_JSON_SUCCESS 						"{\"returnValue\":true}"
#define STANDARD_JSON_ERROR(errorCode, errorText)	"{\"returnValue\":false,\"errorCode\":" STR(errorCode) ",\"errorText\":\"" errorText "\"}"
#define MISSING_PARAMETER_ERROR(name, type)			"{\"returnValue\":false,\"errorCode\":2,\"errorText\":\"Missing '" STR(name) "' " STR(type) " parameter.\"}"
#define INVALID_PARAMETER_ERROR(name, type)			"{\"returnValue\":false,\"errorCode\":3,\"errorText\":\"Invalid '" STR(name) "' " STR(type) " parameter value.\"}"

// Deprecated Macro comments
#define DEPRECATED_SERVICE_MSG() g_critical("THIS METHOD IS DEPRECATED. PLEASE REVISIT THE CODE.")

#define SCHEMA_V2_PROP(name, type, ...)                  "\"" #name "\":{\"type\":\"" #type "\"" __VA_ARGS__ "}"
#define SCHEMA_V2_STRING(name)                           SCHEMA_V2_PROP(name, string, ",\"minLength\":1")
#define SCHEMA_V2_ARRAY(name,type)                       SCHEMA_V2_PROP(name, array, ",\"items\":{\"type\":\"" #type "\",\"minLength\":1}")
#define SCHEMA_V2_OBJECT(name)                           SCHEMA_V2_PROP(name, object)
#define SCHEMA_V2_SYSTEM_PARAMETERS                      SCHEMA_V2_OBJECT($activity)
#define SCHEMA_V2_ANY                                         "{}"
#define SCHEMA_V2_0                                           "{\"type\":\"object\",\"additionalProperties\":false,\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "}}"
#define SCHEMA_V2_1(required,p1)                              "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "}}"
#define SCHEMA_V2_2(required,p1,p2)                           "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "}}"
#define SCHEMA_V2_3(required,p1,p2,p3)                        "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "}}"
#define SCHEMA_V2_4(required,p1,p2,p3,p4)                     "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "," p4 "}}"
#define SCHEMA_V2_5(required,p1,p2,p3,p4,p5)                  "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "," p4 "," p5 "}}"
#define SCHEMA_V2_6(required,p1,p2,p3,p4,p5,p6)               "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "," p4 "," p5 "," p6 "}}"
#define SCHEMA_V2_7(required,p1,p2,p3,p4,p5,p6,p7)            "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," p7 "}}"
#define SCHEMA_V2_8(required,p1,p2,p3,p4,p5,p6,p7,p8)         "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," p7 "," p8 "}}"
#define SCHEMA_V2_9(required,p1,p2,p3,p4,p5,p6,p7,p8,p9)      "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," p7 "," p8 "," p9 "}}"
#define SCHEMA_V2_10(required,p1,p2,p3,p4,p5,p6,p7,p8,p9,p10) "{\"type\":\"object\",\"additionalProperties\":false" required ",\"properties\":{" SCHEMA_V2_SYSTEM_PARAMETERS "," p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," p7 "," p8 "," p9 "," p10 "}}"

class JsonValue {
 public:
    JsonValue(const pbnjson::JValue & value):mValue(value) {
    } 

    pbnjson::JValue & get() {
        return mValue;
    }

    bool get(const char *name, std::string & str) {
        return mValue[name].asString(str) == CONV_OK;
    }
    bool get(const char *name, bool & boolean) {
        return mValue[name].asBool(boolean) == CONV_OK;
    }
    template < class T > bool get(const char *name, T & number) {
        return mValue[name].asNumber < T > (number) == CONV_OK;
    }
    pbnjson::JValue get(const char *name) {
        return mValue[name];
    }

 private:
    pbnjson::JValue mValue;
};

/*
 * Helper class to parse a json message using a schema (if specified)
 */
class JsonMessageParser {
 public:
    JsonMessageParser(const std::string& json, const char *schema):mJson(json), mSchema(schema) {
    } 

    bool parse(const char *callerFunction);
    pbnjson::JValue get() {
        return mParser.getDom();
    }

    // convenience functions to get a parameter directly.
    bool get(const char *name, std::string & str) {
        return get()[name].asString(str) == CONV_OK;
    }
    bool get(const char *name, bool & boolean) {
        return get()[name].asBool(boolean) == CONV_OK;
    }
    template < class T > bool get(const char *name, T & number) {
        return get()[name].asNumber < T > (number) == CONV_OK;
    }
    pbnjson::JValue get(const char *name) {
        return get()[name];
    }

 private:
    const std::string& mJson;
    pbnjson::JSchemaFragment mSchema;
    pbnjson::JDomParser mParser;
};

/**
  * Schema Error Options
  */
enum ESchemaErrorOptions {
    EIgnore = 0,            /**< Ignore the schema */
    EValidateAndContinue,   /**< Validate, Log the error & Continue */
    EValidateAndError,      /**< Validate, Log the error & Reply with correct schema */
    EDefault                /**< Default, loads the value from settings (luna.conf) file  */
};

/*
 * Small wrapper around LSError. User is responsible for calling Print or Free after the error has been set.
 */
struct CLSError:public LSError {
    CLSError() {
        LSErrorInit(this);
    } 
    void Print(const char *where, int line, GLogLevelFlags logLevel = G_LOG_LEVEL_WARNING);
    void Free() {
        LSErrorFree(this);
    }
};

/*
 * Helper class to parse json messages coming from an LS service using pbnjson
 */
class LSMessageJsonParser {
 public:
    // Default using any specific schema. Will simply validate that the message is a valid json message.
    LSMessageJsonParser(LSMessage * message, const char *schema);

    /*!
     * \brief Parse the message using the schema passed in constructor.
     * \param callerFunction   -Name of the function
     * \param sender           - If 'sender' is specified, automatically reply in case of bad syntax using standard format.
     * \param errOption        - Schema error option
     * \return true if parsed successfully, false otherwise
     */
    bool parse(const char *callerFunction, LSHandle * sender = 0, ESchemaErrorOptions errOption = EIgnore);

    /*! \fn getMsgCategoryMethod
     * \brief function parses the message and creates a string with category & method appended to it
     * \return string with category and method appended
     */
     std::string getMsgCategoryMethod();

    /*! \fn getSender
     * \brief function retrieves the sender name from the message
     * \return sender name if available, empty string otherwise
     */
     std::string getSender();

     pbnjson::JValue get() {
        return mParser.getDom();
    } const char *getPayload() {
        return LSMessageGetPayload(mMessage);
    }

    // convenience functions to get a parameter directly.
    bool get(const char *name, std::string & str) {
        return get()[name].asString(str) == CONV_OK;
    }
    bool get(const char *name, bool & boolean) {
        return get()[name].asBool(boolean) == CONV_OK;
    }
    template < class T > bool get(const char *name, T & number) {
        return get()[name].asNumber < T > (number) == CONV_OK;
    }

 private:
    LSMessage * mMessage;
    const char *mSchemaText;
    pbnjson::JSchemaFragment mSchema;
    pbnjson::JDomParser mParser;
};

/**
  * Commonly used schema Macros
  */

/**
  * Main Validation Code
  */
#define VALIDATE_SCHEMA_AND_RETURN_OPTION(lsHandle, message, schema, schErrOption) {\
                                                                                        LSMessageJsonParser jsonParser(message, schema);                                                        \
                                                                                                                                                                                                \
                                                                                        if (EDefault == schErrOption)                                                                           \
                                                                                            schErrOption = static_cast<ESchemaErrorOptions>(Settings::settings()->schemaValidationOption);      \
                                                                                                                                                                                                \
                                                                                        if (!jsonParser.parse(__FUNCTION__, lsHandle, schErrOption))                                            \
                                                                                            return true;                                                                                        \
                                                                                    }

#define VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, schema)   {\
                                                                    ESchemaErrorOptions schErrOption = EDefault;                                \
                                                                    VALIDATE_SCHEMA_AND_RETURN_OPTION(lsHandle, message, schema, schErrOption); \
                                                                 }

/**
  * Subscribe Schema : {"subscribe":boolean}
  */
#define SUBSCRIBE_SCHEMA_RETURN(lsHandle, message)   VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, SCHEMA_1(OPTIONAL(subscribe, boolean)))

/**
  * Empty/Any Schema : {}
  */
#define EMPTY_SCHEMA_RETURN(lsHandle, message)    VALIDATE_SCHEMA_AND_RETURN(lsHandle, message, SCHEMA_V2_ANY)

// build a standard reply returnValue & errorCode/errorText if defined
pbnjson::JValue createJsonReply(bool returnValue = true, int errorCode = 0, const char *errorText = 0);

// build a standard json reply string without the overhead of using json schema
std::string createJsonReplyString(bool returnValue = true, int errorCode = 0, const char *errorText = 0);

// serialize a reply
std::string jsonToString(pbnjson::JValue & reply, const char *schema = SCHEMA_V2_ANY);

/**
@brief Pick only selected keys in JSON Object (Remove unspecified keys).
@param obj A JSON object.
@param inKeys set of keys to be remained.
*/
void json_object_object_pick(pbnjson::JValue obj, const std::set<std::string> &inKeys);

/**
@brief Keys in JSON object
@param obj A JSON object.
@param outKeys set of keys to be filled.
*/
void json_object_object_keys(pbnjson::JValue obj, std::set<std::string> &outKeys);

#endif                          // JSONUTILS_H
