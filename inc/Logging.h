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

#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#include <PmLogLib.h>

/* Logging for settingsservice context ********
 * The parameters needed are
 * msgid - unique message id
 * kvcount - count for key-value pairs
 * ... - key-value pairs and free text. key-value pairs are formed using PMLOGKS or PMLOGKFV
 * e.g.)
 * SSERVICELOG_WARNING(msgid, 2, PMLOGKS("key1", "value1"), PMLOGKFV("key2", "%d", value2), "free text message");
 ** use these for key-value pair printing*/

#define SSERVICELOG_INFO(...)     (void)PmLogInfo(get_settings_service_context(), ##__VA_ARGS__)
#define SSERVICELOG_DEBUG(...)    (void)PmLogDebug(get_settings_service_context(), ##__VA_ARGS__)
#define SSERVICELOG_WARNING(...)  (void)PmLogWarning(get_settings_service_context(), ##__VA_ARGS__)
#define SSERVICELOG_ERROR(...)    (void)PmLogError(get_settings_service_context(), ##__VA_ARGS__)
#define SSERVICELOG_CRITICAL(...) (void)PmLogCritical(get_settings_service_context(), ##__VA_ARGS__)
#define SSERVICELOG_TRACE(...)    PMLOG_TRACE(__VA_ARGS__)

/*msgids*/
#define MSGID_SYSTEM_SETTING_CHANGED                   "SYSTEM_SETTING_CHANGED" /** Info msgid representing change in system settings */

#define MSGID_CHAINCALL_NULL_HANDLE                    "CHAINCALL_NULL_HANDLE" /** Null LS handle returned */
#define MSGID_JSON_ERR                                 "JSON_ERR" /** Json parse error */
#define MSGID_JSON_PARSE_ERR                           "JSON_PARSE_ERR" /** Json parse error */
#define MSGID_JSON_TO_STRING_FAIL                      "JSON_TO_STRING_FAIL" /** Failed to serialize Json reply */
#define MSGID_LGE_SRVC_REGISTER_FAILED                 "LGE_SRVC_REGISTER_FAIL" /** Failed to register service on luna bus */
#define MSGID_JSON_INVALID_SCHEMA                      "JSON_INVALID_SCHEMA" /** message is not a valid JSON message against schema*/
#define MSGID_JSON_MSG_SCHEMA_VALIDATION_ERR           "JSON_MSG_SCHEMA_VALIDATION_ERR" /** could not validate JSON message */
#define MSGID_LGE_SRVC_ATTACH_FAIL                     "LGE_SRVC_ATTACH_FAIL" /** Failed to attach service handle to mainloop */
#define MSGID_WEBOS_SRVC_REGISTER_FAILED               "WEBOS_SRVC_REGISTER_FAILED" /** Failed to register service on luna bus */
#define MSGID_WEBOS_SRVC_ATTACH_FAIL                   "WEBOS_SRVC_ATTACH_FAIL" /** Failed to attach service handle to mainloop */

#define MSGID_KEYDESC_DB_RETURNS_FAIL                  "KEYDESC_DB_RETURNS_FAIL" /** DB8 returns fail */
#define MSGID_DEL_DB_RETURNS_FAIL                      "DEL_DB_RETURNS_FAIL" /** PrefsDb8Del - DB8 returns fail */
#define MSGID_GET_DB_RETURNS_FAIL                      "GET_DB_RETURNS_FAIL" /** PrefsDb8Get - DB8 returns fail */
#define MSGID_GETVAL_DB_RETURNS_FAIL                   "GETVAL_DB_RETURNS_FAIL" /** PrefsDb8GetValues - DB8 returns fail */
#define MSGID_SET_DB_RETURNS_FAIL                      "SET_DB_RETURNS_FAIL" /** PrefsDb8Set - DB8 returns fail */
#define MSGID_SETVAL_DB_RETURNS_FAIL                   "SETVAL_DB_RETURNS_FAIL" /** PrefsDb8Set - DB8 returns fail */
#define MSGID_INIT_DB_RETURNS_FAIL                     "INIT_DB_RETURNS_FAIL" /** DB8 returns fail */

#define MSGID_BATCH_DEL_FAIL                           "BATCH_DEL_FAIL" /** batch operation delete failed */
#define MSGID_DEL_JSON_TYPE_ARRAY_ERR                  "DEL_JSON_TYPE_ARRAY_ERR" /** PrefsDb8Del - batch operation delete failed */

#define MSGID_DEL_BATCH_OPER_ERR                       "DEL_BATCH_OPER_ERR" /** PrefsDb8Del -  complete batch operations not done */
#define MSGID_DEL_BATCH_RETURNS_FAIL                   "DEL_BATCH_RETURNS_FAIL" /** PrefsDb8Del -  batch item delete failed */

#define MSGID_DB_RESP_ARRAY_ERR                        "DB_RESP_ARRAY_ERR" /** DB resp_array error */
#define MSGID_DB_COUNT_ERR                             "DB_COUNT_ERR" /** Batch result count error */

#define MSGID_GET_BATCH_ERR                            "GET_BATCH_ERR" /** Batch result error */
#define MSGID_GET_NO_KINDSTR                           "GET_NO_KINDSTR" /** Batch result has no kind string */
#define MSGID_GET_DATA_SIZE_ERR                        "GET_DATA_SIZE_ERR" /** Failed to merge categorized data */
#define MSGID_GET_CACHE_FAIL                           "GET_CACHE_FAIL"   /* no data in file cache */
#define MSGID_GETVAL_NO_VALUES                         "GETVAL_NO_VALUES" /** no values in the result */
#define MSGID_GETVAL_FILE_VTYPE_ERR                    "GETVAL_FILE_VTYPE_ERR" /** Failed to parse locale file */
#define MSGID_INIT_NO_VALUE                            "INIT_NO_VALUE" /** Values property missing in payload */
#define MSGID_INIT_NO_INITKEY                          "INIT_NO_INITKEY" /** No Init Key in DB */
#define MSGID_LSCALL_SAM_FAIL                          "LSCALL_SAM_FAIL" /** Failed to call applicationManager to get current app id */
#define MSGID_INIT_NO_APPID                            "INIT_NO_APPID" /** SAM returned incorrect message */
#define MSGID_INIT_SAM_RETURNS_ERR                     "INIT_SAM_RETURNS_ERR" /** SAM returns error for forground App ID */
#define MSGID_INIT_STATUS_UPDATE_FAIL                  "INIT_STATUS_UPDATE_FAIL" /** Failed to update the status of DB init */
#define MSGID_INIT_DELKIND_ERR                         "INIT_DELKIND_ERR" /** Failed to delete kind */
#define MSGID_INIT_KIND_REG_ERR                        "INIT_KIND_REG_ERR" /** NO kind info for the kind to be deleted */
#define MSGID_INIT_KIND_LOAD_ERR                       "INIT_KIND_LOAD_ERR" /** Could not load kind info */
#define MSGID_INIT_PERM_REG_ERR                        "INIT_PERM_REG_ERR" /** NO permission info for the kind to be registered */
#define MSGID_INIT_PERM_LOAD_ERR                       "INIT_PERM_LOAD_ERR" /** Failed to load permission for kind */
#define MSGID_INIT_DB8_CONF_ERR                        "INIT_DB8_CONF_ERR" /** Could not register kind */
#define MSGID_ERR_LOADING_KIND_INFO                    "ERR_LOADING_KIND_INFO" /** Could not load kind info */
#define MSGID_INIT_DB_LOAD_FAIL                        "INIT_DB_LOAD_FAIL" /** Failed to initialize DESC db */
#define MSGID_INIT_NO_KINDINFO                         "INIT_NO_KINDINFO" /** error occured during DB initialization for settingsservice */
#define MSGID_INIT_READ_DB8_CONF_ERR                   "INIT_READ_DB8_CONF_ERR" /** Couldn't read kind file */
#define MSGID_INIT_JSON_TYPE_ARRLEN_ERR                "INIT_JSON_TYPE_ARRLEN_ERR" /** Json object error */
#define MSGID_INIT_NO_DBVERSION                        "INIT_NO_DBVERSION"             /* no dbVersion key */
#define MSGID_INIT_DBINFO_ERR                          "INIT_DBINFO_ERR"  /* Incorrect DB info */

/* PrefsTaskMgr.cpp */
#define MSGID_WRONG_METHODID                           "WRONG_METHODID"        /* Method Id is wrong. method call is ignored */
#define MSGID_PTHREAD_CREATE_ERR                       "PTHREAD_CREATE_ERR"    /* Error!! to create task thred */
#define MSGID_EMPTY_BATCH_RESULT                       "EMPTY_BATCH_RESULT"          /* NULL object in the result */
#define MSGID_TASK_ERROR                               "TASK_ERROR"            /* unexpected error while managing task */

#define MSGID_LSCALL_DB_MERGE_FAIL                     "LSCALL_DB_MERGE_FAIL"          /* DB8 luna call merge failed */
#define MSGID_LSCALL_DB_DEL_FAIL                       "LSCALL_DB_DEL_FAIL"            /* DB8 luna call del failed */
#define MSGID_LSCALL_DB_FIND_FAIL                      "LSCALL_DB_FIND_FAIL"           /* DB8 luna call find failed */
#define MSGID_LSCALL_DB_PUT_FAIL                       "LSCALL_DB_PUT_FAIL"            /* DB8 luna call put failed */
#define MSGID_LSCALL_DB_BATCH_FAIL                     "LSCALL_DB_BATCH_FAIL"          /* DB8 luna call batch failed */

/* PrefsFactory.cpp */
#define MSGID_SERVICE_NOT_READY                        "SERVICE_NOT_READY"          /* Service is not ready */
#define MSGID_CB_PAYLOAD_PARSE_ERR                     "PAYLOAD_PARSE_ERR"          /* Payload parse error in callback methods */
#define MSGID_NO_LS_HANLDE                             "NO_LS_HANLDE"          /* Init kind failed */
#define MSGID_SEND_ERR_REPLY_FAIL                      "SEND_ERR_REPLY_FAIL"        /* Sending Error reply failed */
#define MSGID_EMIT_UPSTART_EVENT_FAIL                  "EMIT_UPSTART_EVENT_FAIL"         /* fail to emit settingsservice-ready */
#define MSGID_FAIL_TO_INIT                             "FAIL_TO_INIT"                    /* fail to init db */

/* PrefsKeyDescMap.cpp */
#define MSGID_KEYDESC_PARSE_ERR                        "KEYDESC_PARSE_ERR"       /* DB8 Batch operation response - payload parse error */
#define MSGID_CHAINCALL_PARSE_ERR                      "CHAINCALL_PARSE_ERR"        /* Db8FindChainCall response - payload parse error */
#define MSGID_DEL_PARSE_ERR                            "DEL_PARSE_ERR"              /* PrefsDb8Del response - payload parse error */
#define MSGID_GET_PARSE_ERR                            "GET_PARSE_ERR"              /* PrefsDb8Get response - payload parse error */
#define MSGID_GETVAL_PARSE_ERR                         "GETVAL_PARSE_ERR"           /* PrefsDb8GetValues response - payload parse error */
#define MSGID_API_ARGS_PARSE_ERR                       "API_ARGS_PARSE_ERR"   /* */
#define MSGID_SET_PARSE_ERR                            "SET_PARSE_ERR"           /* PrefsDb8Set response - payload parse error */
#define MSGID_SETVAL_PARSE_ERR                         "SETVAL_PARSE_ERR"           /* PrefsDb8SetValues response - payload parse error */
#define MSGID_INIT_PARSE_ERR                           "INIT_PARSE_ERR"       /* PrefsDb8Init -payload parse error  */
#define MSGID_PAYLOAD_ERR                              "MSGID_PAYLOAD_ERR"

#define MSGID_DB_LUNA_CALL_FAIL                        "DB_LUNA_CALL_FAIL"          /* DB8 luna call failed */

#define MSGID_KEYDESC_PAYLOAD_MISSING                  "KEYDESC_PAYLOAD_MISSING"            /* Payload is missing in LS Message*/
#define MSGID_CHAINCALL_PAYLOAD_MISSING                "CHAINCALL_PAYLOAD_MISSING"  /* Payload is missing in LS Message - Db8FindChainCall */
#define MSGID_DEL_PAYLOAD_MISSING                      "DEL_PAYLOAD_MISSING"        /* Payload is missing in LS Message - PrefsDb8Del */
#define MSGID_GET_PAYLOAD_MISSING                      "GET_PAYLOAD_MISSING"        /* Payload is missing in LS Message - PrefsDb8Get */
#define MSGID_GETVAL_PAYLOAD_MISSING                   "GETVAL_PAYLOAD_MISSING"     /* Payload is missing in LS Message - PrefsDb8GetValues */
#define MSGID_BATCH_PAYLOAD_MISSING                    "BATCH_PAYLOAD_MISSING"     /* Payload is missing in LS Message - PrefsDb8GetValues */
#define MSGID_API_NO_ARGS                              "API_NO_ARGS"     /* Payload is missing in LS Message - PrefsDb8GetValues */
#define MSGID_SET_PAYLOAD_MISSING                      "SET_PAYLOAD_MISSING"     /* Payload is missing in LS Message - PrefsDb8Set */
#define MSGID_SETVAL_PAYLOAD_MISSING                   "SETVAL_PAYLOAD_MISSING"     /* Payload is missing in LS Message - PrefsDb8SetValues */
#define MSGID_INIT_PAYLOAD_MISSING                     "MSGID_INIT_PAYLOAD_MISSING"  /* Payload is missing in LS Message - PrefsDb8Init */

#define MSGID_SETVAL_NO_RESULTS                        "SETVAL_NO_RESULTS"             /* Payload data - No "result" object */
#define MSGID_SETVAL_JSON_TYPE_ARRAY_ERR               "SETVAL_JSON_TYPE_ARRAY_ERR"  /*Payload data - No "result" array data  */
#define MSGID_SETVAL_JSON_TYPE_ARRAY_LEN_ERR           "SETVAL_JSON_TYPE_ARRAY_LEN_ERR"  /*Payload data - No "result" array length data  */
#define MSGID_INIT_NO_RESULTS                          "INIT_NO_RESULTS"      /* Payload data - No "result" object */
#define MSGID_KYEDESC_NO_RESULTS                       "KYEDESC_NO_RESULTS"             /* Payload data - No "result" object */
#define MSGID_INIT_JSON_TYPE_ARRAY_ERR                 "INIT_JSON_TYPE_ARRAY_ERR"  /* Payload data - No "result" array data */
#define MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR              "KEYDESC_JSON_TYPE_ARRAY_ERR"             /* Payload data - No "result" array data */
#define MSGID_KEYDESC_JSON_TYPE_ERR                    "KEYDESC_JSON_TYPE_ERR"       /* Unexpected json format */
#define MSGID_KEYDESC_CATEGORY_ERR                     "KEYDESC_CATEGORY_ERR"        /* Unexpected category error */
#define MSGID_KEYDESC_CACHE_ERR                        "KEYDESC_CACHE_ERR"           /* Unexpected cache error */
#define MSGID_KEYDESC_DIMENSION_ERR                    "KEYDESC_DIMENSION_ERR"      /* Incorrect dimension information */
#define MSGID_KEY_DESC_NOT_READY                       "KEY_DESC_NOT_READY"          /* KeyDesc is not ready */
#define MSGID_INCORRECT_OBJECT                         "INCORRECT_OBJECT"            /* Incorrect object in data */
#define MSGID_LOAD_DESC_DATA_FAIL                      "LOAD_DESC_DATA_FAIL"         /* Fail to load description data in internal memory */
#define MSGID_DEP_DIMENSION_KEY_VAL_ERR                "DEP_DIMENSION_KEY_VAL_ERR"       /* ERROR!! in getting dependent dimension key values */
#define MSGID_DIMENSION_KEY_VAL_ERR                    "DIMENSION_KEY_VAL_ERR"       /* ERROR!! in getting independent dimension key values */
#define MSGID_KEY_DESC_EMPTY                           "KEY_DESC_EMPTY"                  /* Keys dexcriptions are empty */
#define MSGID_SET_DESC_ERR                             "SET_DESC_ERR" /** KeyDescInfo parse fail */
#define MSGID_SET_MERGE_FAIL                           "SET_MERGE_FAIL" /** Error sending a request to DB */
#define MSGID_SET_BATCH_ERR                            "SET_BATCH_ERR" /** Incorrect batch result */
#define MSGID_SET_DATA_SIZE_ERR                        "SET_DATA_SIZE_ERR" /** Unexpected responses size in batch merge result */
#define MSGID_MERGE_BATCH_RESULT_FAIL                  "MERGE_BATCH_RESULT_FAIL" /* Unexpected batch result in MERGE responses */
#define MSGID_SEND_VOLATILE_PUT_REQ_ERR                "SEND_VOLATILE_PUT_REQ_ERR" /** Failed to sending put rquest to volatile kind*/
#define MSGID_DEFKIND_PUT_REQ_FAIL                     "DEFKIND_PUT_REQ_FAIL" /** put request to default kind failed */
#define MSGID_GETVAL_NO_VTYPE                          "GETVAL_NO_VTYPE"             /* no vtype in the results */
#define MSGID_SETVAL_NO_VTYPE                          "SETVAL_NO_VTYPE"             /* no vtype in the results */
#define MSGID_SETVAL_NO_COUNT                          "SETVAL_NO_COUNT"      /* No count property in return. DB Error */
#define MSGID_SETVAL_JSON_TYPE_INT_ERR                 "SETVAL_JSON_TYPE_INT_ERR"  /* */
#define MSGID_KEYDESC_JSON_TYPE_ARRAY_ERR              "KEYDESC_JSON_TYPE_ARRAY_ERR" /* */
#define MSGID_SETVAL_NO_VALUES                         "SETVAL_NO_VALUES"    /* No values property in return. DB Error */
#define MSGID_SETVAL_REQ_FAIL                          "SETVAL_REQ_FAIL"            /* Db put request failed */
#define MSGID_SETVAL_TYPE_MISMATCH                     "SETVAL_TYPE_MISMATCH"         /* Incorrect vtype */
#define MSGID_SETVAL_SUBS_ERROR                        "SETVAL_SUBS_ERROR"    /* Fail to subscription message */
#define MSGID_KYEDESC_NO_RESPONSES                     "KYEDESC_NO_RESPONSES" /* No "responses" object in DB response payload. */
#define MSGID_DEL_NO_RESPONSES                         "DEL_NO_RESPONSES"    /** PrefsDb8Del - No "responses" object in DB response payload. */
#define MSGID_SET_NO_RESPONSES                         "SET_NO_RESPONSES" /* No "responses" object in DB response payload. */
#define MSGID_SET_MERGE_ERR                            "SET_MERGE_ERR" /** Put request to merge left out keys  */
#define MSGID_KEYDESC_LOAD_FOR_APP                     "KEYDESC_LOAD_FOR_APP"

#define MSGID_EXCEPTIONAPPLIST_FILE_NOTFOUND           "MSGID_EXCEPTIONAPPLIST_FILE_NOTFOUND" /** No exist exceptionapplist file */
#define MSGID_EXCEPTIONAPPLIST_FILE_FAILTO_LOAD        "MSGID_EXCEPTIONAPPLIST_FILE_FAILTO_LOAD" /** Failed to parse exceptionapplist contents into json */

/** LocalePrefsHandler.cpp */
#define MSGID_LOCALE_FILES_LOAD_FAILED                 "LOCALE_FILES_LOAD_FAILED" /** Failed to load locale files */
#define MSGID_LOCALE_FILE_PARSE_ERR                    "LOCALE_FILE_PARSE_ERR" /** Failed to parse locale file contents into json */
#define MSGID_COUNTRY_FILE_PARSE_ERR                   "COUNTRY_FILE_PARSE_ERR" /** Failed to parse locale file contents into json */
#define MSGID_LOCALE_GET_ENTRY_FAILED                  "LOCALE_GET_ENTRY_FAILED" /** Failed to get locale entry from locale file */
#define MSGID_COUNTRY_FILES_LOAD_FAILED                "COUNTRY_FILES_LOAD_FAILED" /** Failed to load country files */

// PrefsFileWriter.cpp
#define MSGID_LOCALEINFO_FILE_OPEN_FAILED              "LOCALEINFO_FILE_OPEN_FAILED" /* Failed to open localeinfo files */

// PrefsDb8Init.cpp
#define MSGID_LOCK_FILE_OPEN_FAILED                    "LOCK_FILE_OPEN_FAILED" /* Failed to open lock file */

// PrefsDb8Condition.cpp
#define MSGID_CONDITION_FILE_NOTFOUND                  "MSGID_CONDITION_FILE_NOTFOUND"
#define MSGID_CONDITION_FILE_FAILTO_LOAD               "MSGID_CONDITION_FILE_FAILTO_LOAD"

// DefaultJson.cpp
#define MSGID_DEFAULTSETTINGS_LOAD_FOR_APP             "DEFAULTSETTINGS_LOAD_FOR_APP"

#define MSGID_DIMENSION_FORMAT_LOAD                    "DIMENSION_FORMAT_LOAD"

// SettingsRecord
#define MSGID_AMBIGUOUS_RECORD                         "AMBIGUOUS_RECORD"

/** LS call Error log **/
#define MSGID_LSERROR_MSG                              "LSERROR_MSG"

PmLogContext get_settings_service_context();

#endif                          /* LOGGING_H */
