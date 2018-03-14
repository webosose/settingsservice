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

#include <cstring>

#include "JSONUtils.h"
#include "Logging.h"
#include "Utils.h"

using namespace Utils;

bool JsonMessageParser::parse(const char *callerFunction)
{
    if (!mParser.parse(mJson, mSchema)) {
        const char *errorText = "Could not validate json message against schema";
        pbnjson::JSchemaFragment genericSchema(SCHEMA_V2_ANY);
        if (!mParser.parse(mJson, genericSchema))
            errorText = "Invalid json message";
        SSERVICELOG_ERROR(MSGID_JSON_PARSE_ERR, 3, PMLOGKS("Callerfunction",callerFunction), PMLOGKS("Reason",errorText),
                          PMLOGKS("mJson",mJson.c_str()), "");
        return false;
    }
    return true;
}

pbnjson::JValue createJsonReply(bool returnValue, int errorCode, const char *errorText)
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", returnValue);
    if (errorCode)
        reply.put("errorCode", errorCode);
    if (errorText)
        reply.put("errorText", errorText);
    return reply;
}

std::string createJsonReplyString(bool returnValue, int errorCode, const char *errorText)
{
    std::string reply;
    if (returnValue) {
        reply = STANDARD_JSON_SUCCESS;
    } else {
        reply = "{\"returnValue\":false";
        if (errorCode) {
            reply += Utils::string_printf(", \"errorCode\":%d", errorCode);
        }
        if (errorText) {
            reply += Utils::string_printf(", \"errorText\":\"%s\"", errorText);
        }
        reply += "}";
    }
    return reply;
}

std::string jsonToString(pbnjson::JValue & reply, const char *schema)
{
    pbnjson::JGenerator serializer(NULL);       // our schema that we will be using does not have any external references
    std::string serialized;
    pbnjson::JSchemaFragment responseSchema(schema);
    if (!serializer.toString(reply, responseSchema, serialized)) {
        SSERVICELOG_ERROR(MSGID_JSON_TO_STRING_FAIL, 0, "failed to generate json reply");
        return "{\"returnValue\":false,\"errorText\":\"error: Failed to generate a valid json reply...\"}";
    }
    return serialized;
}

LSMessageJsonParser::LSMessageJsonParser(LSMessage * message, const char *schema)
 : mMessage(message)
    , mSchemaText(schema)
    , mSchema(schema)
{
}

std::string LSMessageJsonParser::getMsgCategoryMethod()
{
    std::string context = "";

    if (mMessage) {
        if (LSMessageGetCategory(mMessage))
            context = "Category: " + std::string(LSMessageGetCategory(mMessage)) + " ";

        if (LSMessageGetMethod(mMessage))
            context += "Method: " + std::string(LSMessageGetMethod(mMessage));
    }

    return context;
}

std::string LSMessageJsonParser::getSender()
{
    std::string strSender = "";

    if (mMessage) {
        const char *sender = LSMessageGetSenderServiceName(mMessage);

        if ((sender && *sender) && (LSMessageGetSender(mMessage)))
            strSender = std::string(LSMessageGetSender(mMessage));
    }

    return strSender;
}

bool LSMessageJsonParser::parse(const char *callerFunction, LSHandle * lssender, ESchemaErrorOptions validationOption)
{
    if (EIgnore == validationOption)
        return true;

    const char *payload = getPayload();

    // Parse the message with given schema.
    if ((payload) && (!mParser.parse(payload, mSchema))) {
        // Unable to parse the message with given schema

        const char* errorText = "Could not validate json message against schema (for details please see the Log file) !";
        bool notJson = true;    // we know that, it's not a valid json message

        // Try parsing the message with empty schema, just to verify that it is a valid json message
        if (strcmp(mSchemaText, SCHEMA_V2_ANY) != 0) {
            pbnjson::JSchemaFragment genericSchema(SCHEMA_V2_ANY);
            notJson = !mParser.parse(payload, genericSchema);
        }

        if (notJson) {
            SSERVICELOG_WARNING(MSGID_JSON_INVALID_SCHEMA, 4, PMLOGKS("callerFunction",callerFunction),
                                PMLOGKS("Method",getMsgCategoryMethod().c_str()),
                                PMLOGKS("Sender",getSender().c_str()), PMLOGKS("Schema",mSchemaText),
                                "Payload : %s ", payload);
            errorText = "Not a valid json message";
        } else {
            SSERVICELOG_WARNING(MSGID_JSON_MSG_SCHEMA_VALIDATION_ERR, 4, PMLOGKS("callerFunction",callerFunction),
                                PMLOGKS("Method",getMsgCategoryMethod().c_str()),
                                PMLOGKS("Sender",getSender().c_str()), PMLOGKS("Schema",mSchemaText),
                                "Payload : %s ", payload);

            if (0 == get().objectSize()) {
              errorText = "Empty request";
            }
        }

        if (EValidateAndError == validationOption) {
            if (lssender) {
                std::string reply = createJsonReplyString(false, 1, errorText);
                CLSError lserror;
                if (!LSMessageReply(lssender, mMessage, reply.c_str(), &lserror))
                    lserror.Print(callerFunction, 0);
            }

            return false;       // throw the error back
        }
    }
    // Message successfully parsed with given schema
    return true;
}

void CLSError::Print(const char *where, int line, GLogLevelFlags logLevel)
{
    if (LSErrorIsSet(this)) {
        SSERVICELOG_DEBUG("%s(%d): Luna Service Error #%d \"%s\",\nin %s line #%d.", where, line, this->error_code, this->message, this->file, this->line);
        LSErrorFree(this);
    }
}

void json_object_object_pick(pbnjson::JValue obj, const std::set<std::string> &inKeys)
{
    if ( !obj.isObject() ) {
        SSERVICELOG_DEBUG("%s: Incorrect obj", __FUNCTION__);
        return;
    }

    std::set<std::string> keys;
    json_object_object_keys(obj, keys);

    for (auto &k : inKeys) { keys.erase(k); }
    for (auto &key : keys) { obj.remove(key); }
}

void json_object_object_keys(pbnjson::JValue obj, std::set<std::string> &outKeys)
{
    if ( !obj.isObject() ) {
        SSERVICELOG_DEBUG("%s: Incorrect obj", __FUNCTION__);
        return;
    }
    for (pbnjson::JValue::KeyValue it : obj.children()) {
        outKeys.insert(it.first.asString());
    }
}
