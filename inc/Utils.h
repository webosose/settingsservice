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

#ifndef UTILS_H
#define UTILS_H

#undef min
#undef max

#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <luna-service2/lunaservice.h>

#include "PrefsFactory.h"

#define SS_DEBUG_INFO	100
#define SS_DEBUG_WARN	50
#define SS_DEBUG_ERR	10

namespace Utils {

    template < class T > std::string toSTLString(const T & arg) {
        std::ostringstream out;
        out << arg;
        return (out.str());
    }

    bool readFile(const std::string& filePath, std::string& res);
    void freeFile(char * a_buffer);

    std::string trimWhitespace(const std::string & s, const std::string & drop = "\r\n\t ");
    void trimWhitespace_inplace(std::string & s_mod, const std::string & drop = "\r\n\t ");

    bool getNthSubstring(unsigned int n, std::string & target, const std::string & str, const std::string & delims = " \t\n\r");
    int splitFileAndPath(const std::string & srcPathAndFile, std::string & pathPart, std::string & filePart);
    int splitFileAndExtension(const std::string & srcFileAndExt, std::string & filePart, std::string & extensionPart);
    int splitStringOnKey(std::vector < std::string > &returnSplitSubstrings, const std::string & baseStr, const std::string & delims);
    int splitStringOnKey(std::list < std::string > &returnSplitSubstrings, const std::string & baseStr, const std::string & delims);

    bool doesExistOnFilesystem(const char *pathAndFile);
    int fileCopy(const char *srcFileAndPath, const char *dstFileAndPath);

    int filesizeOnFilesystem(const char *pathAndFile);

    /**
     * Read file list in directory
     * @param path    directory
     * @param postfix extension like '.json'. that matches last part of full filename
     * @param outList filename list to be added
     */
    void readDirEntry(const std::string &path, const std::string &postfix, std::list<std::string> &outList);

    int urlDecodeFilename(const std::string & encodedName, std::string & decodedName);
    int urlEncodeFilename(std::string & encodedName, const std::string & decodedName);

    std::string base64_encode(unsigned char const *, unsigned int len);
    std::string base64_decode(std::string const &s);

#define ERRMASK_POSTSUBUPDATE_PUBLIC	1
#define ERRMASK_POSTSUBUPDATE_PRIVATE	2
    bool processSubscription(LSHandle * serviceHandle, LSMessage * message, const std::string & key);
    bool subscriptionAdd(LSHandle *a_sh, const char *a_key, LSMessage *a_message);

// Build an std::string using printf-style formatting
    std::string string_printf(const char *format, ...) G_GNUC_PRINTF(1, 2);

// Append a printf-style string to an existing std::string
    std::string & append_format(std::string & str, const char *format, ...) G_GNUC_PRINTF(2, 3);

    void string_to_lower(std::string & str);

    void json_object_array_into_string_list(pbnjson::JValue o, std::list<std::string>& container);

    std::list<std::string> set_to_list(const std::set<std::string>& a_set);
    std::set<std::string> list_to_set(const std::list<std::string>& a_set);

    namespace Instrument {

        struct SubscriptionDumpItem {
            std::string sender;
            std::string method;
            std::string message;
        };

        class CurrentSubscriptions : public PrefsRefCounted
        {
            typedef std::function<void()>                       requestCallBack;
            typedef std::map<std::string, SubscriptionDumpItem> SubscriptionMap;

            public:
                CurrentSubscriptions();
                void                   request (requestCallBack cb);
                const SubscriptionMap& subscriptionMap() const {return m_subscriptionMap;}
                bool                   error()           const {return !errorText().empty();}
                std::string            errorText()       const;
                const std::string&     errorContext()    const {return m_errorContext;}

            private: // method members
                ~CurrentSubscriptions();
                void        requestNext();
                static bool requestProcessReply(LSHandle* handle, LSMessage* msg, void* context);

            private: // field members
                requestCallBack                  m_requestCallBack;
                std::string                      m_requestURI;
                std::list<std::string>::iterator m_g_serviceNamesIt;
                SubscriptionMap                  m_subscriptionMap;
                LSError                          m_lsError;
                std::string                      m_errorText;
                std::string                      m_errorContext;
        };

        void start (std::function<void(const std::string& errorText,
                                       const std::string& errorContext)> callBack);
        void stop();
        bool isStarted();
        void writeRequest(LSMessage *msg);
        void addPostfixToApiMap(const std::string &postfix, const std::string &api);
        void addCurrentServiceName(const std::string &serviceName);
    }
}

#endif                          /* UTILS_H */
