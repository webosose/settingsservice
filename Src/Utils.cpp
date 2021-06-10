// Copyright (c) 2013-2021 LG Electronics, Inc.
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
#include <fstream>
#include <sys/stat.h>
#include <sys/time.h>

#include "Utils.h"
#include "Logging.h"

static std::map<std::string, Utils::Instrument::SubscriptionDumpItem> g_subscriptionMap;
namespace Utils {
    bool readFile(const std::string& filePath, std::string& res) {
        if (filePath.empty())
            return false;

        std::ifstream input(filePath);

        if (!input.is_open())
            return false;

        res.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());

        return true;
    }

    std::string trimWhitespace(const std::string & s, const std::string & drop) {
        std::string::size_type first = s.find_first_not_of(drop);
        std::string::size_type last = s.find_last_not_of(drop);

        if (first == std::string::npos || last == std::string::npos)
            return std::string("");
        return s.substr(first, last - first + 1);
    }

    bool getNthSubstring(unsigned int n, std::string & dest, const std::string & str, const std::string & delims) {
        if (n == 0)
            n = 1;

        std::string::size_type start = 0;
        std::string::size_type mark = 0;
        unsigned int i = 1;
        while (1) {
            //find the start of a non-delim
            start = str.find_first_not_of(delims, mark);
            if (start == std::string::npos)
                break;
            //find the end of the current substring (where the next instance of delim lives, or end of the string)
            mark = str.find_first_of(delims, start);
            if ((mark == std::string::npos) || (i == n))
                break;          //last substring, or Nth one found
            ++i;
        }

        if (i != n)
            return false;

        //extract
        dest = str.substr(start, mark - start);
        return true;

    }

    int splitFileAndPath(const std::string & srcPathAndFile, std::string & pathPart, std::string & filePart) {

        std::vector < std::string > parts;
        //printf("splitFileAndPath - input [%s]\n",srcPathAndFile.c_str());
        int s = splitStringOnKey(parts, srcPathAndFile, std::string("/"));
        if ((s == 1) && (srcPathAndFile.at(srcPathAndFile.length() - 1) == '/')) {
            //only path part
            pathPart = srcPathAndFile;
            filePart = "";
        } else if (s == 1) {
            //only file part
            if (srcPathAndFile.at(0) == '/') {
                pathPart = "/";
            } else {
                pathPart = "";
            }
            filePart = parts.at(0);
        } else if (s >= 2) {
            for (int i = 0; i < s - 1; i++) {
                if ((parts.at(i)).size() == 0)
                    continue;
                pathPart += std::string("/") + parts.at(i);
                //printf("splitFileAndPath - path is now [%s]\n",pathPart.c_str());
            }
            pathPart += std::string("/");
            filePart = parts.at(s - 1);
        }

        return s;
    }

    int splitFileAndExtension(const std::string & srcFileAndExt, std::string & filePart, std::string & extensionPart) {

        std::vector < std::string > parts;
        int s = splitStringOnKey(parts, srcFileAndExt, std::string("."));
        if (s == 1) {
            //only file part; no extension
            filePart = parts.at(0);
        } else if (s >= 2) {
            filePart += parts.at(0);
            for (int i = 1; i < s - 1; i++)
                filePart += std::string(".") + parts.at(i);
            extensionPart = parts.at(s - 1);
        }
        return s;
    }

    int splitStringOnKey(std::vector < std::string > &returnSplitSubstrings, const std::string & baseStr, const std::string & delims) {

        std::string::size_type start = 0;
        std::string::size_type mark = 0;
        std::string extracted;

        int i = 0;
        while (start < baseStr.size()) {
            //find the start of a non-delims
            start = baseStr.find_first_not_of(delims, mark);
            if (start == std::string::npos)
                break;
            //find the end of the current substring (where the next instance of delim lives, or end of the string)
            mark = baseStr.find_first_of(delims, start);
            if (mark == std::string::npos)
                mark = baseStr.size();

            extracted = baseStr.substr(start, mark - start);
            if (extracted.size() > 0) {
                //valid string...add it
                returnSplitSubstrings.push_back(extracted);
                ++i;
            }
            start = mark;
        }

        return i;
    }

    void trimWhitespace_inplace(std::string & s_mod, const std::string & drop) {
        std::string::size_type first = s_mod.find_first_not_of(drop);
        std::string::size_type last = s_mod.find_last_not_of(drop);

        if (first == std::string::npos || last == std::string::npos)
            s_mod = std::string("");
        else
            s_mod = s_mod.substr(first, last - first + 1);
    }

    int splitStringOnKey(std::list < std::string > &returnSplitSubstrings, const std::string & baseStr, const std::string & delims) {

        std::string::size_type start = 0;
        std::string::size_type mark = 0;
        std::string extracted;

        int i = 0;
        while (start < baseStr.size()) {
            //find the start of a non-delims
            start = baseStr.find_first_not_of(delims, mark);
            if (start == std::string::npos)
                break;
            //find the end of the current substring (where the next instance of delim lives, or end of the string)
            mark = baseStr.find_first_of(delims, start);
            if (mark == std::string::npos)
                mark = baseStr.size();

            extracted = baseStr.substr(start, mark - start);
            if (extracted.size() > 0) {
                //valid string...add it
                returnSplitSubstrings.push_back(extracted);
                ++i;
            }
            start = mark;
        }

        return i;
    }

    bool doesExistOnFilesystem(const char *pathAndFile) {

        if (pathAndFile == NULL)
            return false;

        struct stat buf;
        if (-1 ==::stat(pathAndFile, &buf))
            return false;
        return true;

    }

    int fileCopy(const char *srcFileAndPath, const char *dstFileAndPath) {
        if ((srcFileAndPath == NULL) || (dstFileAndPath == NULL))
            return -1;

        FILE *infp = fopen(srcFileAndPath, "rb");
        FILE *outfp = fopen(dstFileAndPath, "wb");
        if ((infp == NULL) || (outfp == NULL)) {
            if (infp)
                fclose(infp);
            if (outfp)
                fclose(outfp);
            return -1;
        }

        char buffer[2048];
        while (!feof(infp)) {
            size_t r = fread(buffer, 1, 2048, infp);
            if ((r == 0) && (ferror(infp))) {
                break;
            }
            size_t w = fwrite(buffer, 1, r, outfp);
            if (w < r) {
                break;
            }
        }

        fflush(infp);
        fflush(outfp);          //apparently our filesystem doesn't like to commit even on close
        fclose(infp);
        fclose(outfp);
        return 1;
    }

    int filesizeOnFilesystem(const char *pathAndFile) {
        if (pathAndFile == NULL)
            return 0;

        struct stat buf;
        if (-1 ==::stat(pathAndFile, &buf))
            return 0;
        return buf.st_size;
    }

    void readDirEntry(const std::string &path, const std::string &postfix, std::list<std::string> &outList)
    {
        DIR *hDir = opendir(path.c_str());
        if (hDir) {
            struct dirent *hFile;
            while ((hFile = readdir(hDir)) != NULL) {
                if (0 == strcmp(hFile->d_name, "." )) continue;
                if (0 == strcmp(hFile->d_name, "..")) continue;
                if (hFile->d_name[0] == '.') continue;

                std::string entryName(hFile->d_name);

                size_t pos = entryName.rfind(postfix);
                if (pos == std::string::npos) continue;

                if (pos + postfix.size() == entryName.size()) {
                    outList.push_back(entryName);
                }
            }
            closedir(hDir);
        }
    }

    bool processSubscription(LSHandle * serviceHandle, LSMessage * message, const std::string & key) {

        if ((serviceHandle == NULL) || (message == NULL))
            return false;

        LSError lsError;
        LSErrorInit(&lsError);

        if (LSMessageIsSubscription(message)) {
            if (!LSSubscriptionAdd(serviceHandle, key.c_str(), message, &lsError)) {
                LSErrorFree(&lsError);
                return false;
            } else
                return true;
        }
        return false;
    }

    void subscriptionRemove(LSHandle *sh, LSMessage *reply)
    {
        if(g_subscriptionMap.count(LSMessageGetSender(reply)))
        {
            g_subscriptionMap.erase(LSMessageGetSender(reply));
        }
    }

    bool subscriptionAdd(LSHandle *a_sh, const char *a_key, LSMessage *a_message)
    {
        pbnjson::JValue parsed = pbnjson::JDomParser::fromString(LSMessageGetPayload(a_message));
        std::string payload = pbnjson::JGenerator::serialize(parsed, pbnjson::JSchemaFragment("{}"));
        Instrument::SubscriptionDumpItem subscriber;

        subscriber.message = payload;
        subscriber.method = LSMessageGetMethod(a_message);
        if(LSMessageGetSenderServiceName(a_message)) {
            subscriber.sender = LSMessageGetSenderServiceName(a_message);
        }
        g_subscriptionMap[LSMessageGetSender(a_message)] = subscriber;

        LSError lsError;
        LSErrorInit(&lsError);

        if ( !LSSubscriptionAdd(a_sh, a_key, a_message, &lsError) ) {
            SSERVICELOG_WARNING(MSGID_LSERROR_MSG, 2, PMLOGKS("Function",lsError.func), PMLOGKS("Error",lsError.message), "Reg subscription");
            LSErrorFree(&lsError);
            return false;
        }

        return true;
    }

    void string_to_lower(std::string & str) {
        std::transform(str.begin(), str.end(), str.begin(), tolower);
    }

    std::string string_printf(const char *format, ...) {
        if (format == 0)
            return "";
        va_list args;
        va_start(args, format);
        char stackBuffer[1024];
        int result = vsnprintf(stackBuffer, G_N_ELEMENTS(stackBuffer), format, args);
        if (result > -1 && result < (int)G_N_ELEMENTS(stackBuffer)) {   // stack buffer was sufficiently large. Common case with no temporary dynamic buffer.
            va_end(args);
            return std::string(stackBuffer, result);
        }

        int length = result > -1 ? result + 1 : G_N_ELEMENTS(stackBuffer) * 3;
        char *buffer = 0;
        do {
            if (buffer) {
                delete[]buffer;
                length *= 3;
            }
            buffer = new char[length];
            result = vsnprintf(buffer, length, format, args);
        } while (result == -1 && result < length);
        va_end(args);
        std::string str(buffer, result);
        delete[]buffer;
        return str;
    }

    std::string & append_format(std::string & str, const char *format, ...) {
        if (format == 0)
            return str;
        va_list args;
        va_start(args, format);
        char stackBuffer[1024];
        int result = vsnprintf(stackBuffer, G_N_ELEMENTS(stackBuffer), format, args);
        if (result > -1 && result < (int)G_N_ELEMENTS(stackBuffer)) {   // stack buffer was sufficiently large. Common case with no temporary dynamic buffer.
            va_end(args);
            str.append(stackBuffer, result);
            return str;
        }

        int length = result > -1 ? result + 1 : G_N_ELEMENTS(stackBuffer) * 3;
        char *buffer = 0;
        do {
            if (buffer) {
                delete[]buffer;
                length *= 3;
            }
            buffer = new char[length];
            result = vsnprintf(buffer, length, format, args);
        } while (result == -1 && result < length);
        va_end(args);
        str.append(buffer, result);
        delete[]buffer;
        return str;
    }

    void json_object_array_into_string_list(pbnjson::JValue o, std::list<std::string>& container)
    {
        for (pbnjson::JValue oIt : o.items()) {
        if (oIt.isNull())
            continue;
        if (oIt.isString())
            container.push_back(oIt.asString());
        }
    }

    std::list<std::string> set_to_list(const std::set<std::string>& a_set)
    {
        std::list<std::string> container;
        for (std::set<std::string>::const_iterator citer = a_set.begin();
            citer != a_set.end();
            ++citer)
        {
            container.push_back(*citer);
        }
        return container;
    }

    std::set<std::string> list_to_set(const std::list<std::string>& a_list)
    {
        std::set<std::string> container;
        for (std::list<std::string>::const_iterator citer = a_list.begin();
            citer != a_list.end();
            ++citer)
        {
            container.insert(*citer);
        }
        return container;
    }

    namespace Instrument {

        enum DumpMode { NeedToCheck, Active, Inactive };

        DumpMode g_DumpMode = NeedToCheck;
        std::map<std::string, std::string> g_postfixApiMap; // map to guess api from subscription key name
        std::list<std::string> g_serviceNames; // service name is required to get current subscriptions

        void writeSubscriptionDumpItem(const std::map<std::string, SubscriptionDumpItem> &subscriptionMap);

        void start (std::function<void(const std::string& errorText,
                                       const std::string& errorContext)> callBack)
        {
            auto subscriptions = new CurrentSubscriptions();
            subscriptions->getLocalsubscriptionMap();
            if (Active == g_DumpMode)
            {
                callBack(subscriptions->errorText(), subscriptions->errorContext());
                subscriptions->unref();
                return;
            }

            writeSubscriptionDumpItem(subscriptions->subscriptionMap());
            g_DumpMode = Active;

            callBack(subscriptions->errorText(), subscriptions->errorContext());
            subscriptions->unref();

        }

        void stop()
        {
            g_DumpMode = Inactive;
        }

        bool isStarted()
        {
            return g_DumpMode == Active;
        }

        static const char* INSTRUMENT_CHECK_PATH       = "/var/com.webos.settingsservice.instrument";
        static const char* INSTRUMENT_TIME_FORMAT      = "%Y-%m-%d %H:%M:%S";
        static const char* INSTRUMENT_DUMP_PATH_FORMAT = "/var/log/com.webos.settingsservice-commands-%d.log";

        void stripNewLine(std::string &str)
        {
            str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());
            str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
        }

        FILE* openInstrumentFile()
        {
            static int pid = getpid();
            char path[128];
            snprintf(path, sizeof(path), INSTRUMENT_DUMP_PATH_FORMAT, pid);
            return fopen(path, "a+");
        }

        void fwriteInstrument(FILE* fp, const char* time, const char* sender, const char* appId, const char* method, const char* payload)
        {
            fprintf(fp, "%s\t%s\t%s\t%s\t%zd\t%s\n",
                time, sender, appId, method, strlen(payload), payload);
        }

        void writeRequest(LSMessage *msg)
        {
            if (g_DumpMode == NeedToCheck) {
                g_DumpMode = Inactive;

                struct stat buf;
                if (stat(INSTRUMENT_CHECK_PATH, &buf) == 0) {
                    g_DumpMode = Active;
                }
            }

            if (g_DumpMode == Inactive) {
                return;
            }

            if (msg == NULL) {
                return;
            }

            // write request

            struct timeval curTime;
            gettimeofday(&curTime, NULL);
            char timebuffer [80] = "";
            strftime(timebuffer, sizeof(timebuffer), INSTRUMENT_TIME_FORMAT, localtime(&curTime.tv_sec));
            char currentTime[84] = "";
            snprintf(currentTime, sizeof(currentTime), "%s.%03ld", timebuffer, curTime.tv_usec / 1000);

            FILE* file = openInstrumentFile();
            if (file == NULL) {
                return;
            }

            const char *msgSender = LSMessageGetSenderServiceName(msg);
            if (msgSender == NULL) msgSender = "";
            const char *msgAppId = LSMessageGetApplicationID(msg);
            if (msgAppId == NULL) msgAppId = "";
            const char *msgMethod = LSMessageGetMethod(msg);
            if (msgMethod == NULL) msgMethod = "";
            std::string msgPayloadString = "";
            const char *msgPayload = LSMessageGetPayload(msg);
            if (msgPayload != NULL) {
                msgPayloadString = msgPayload;
                stripNewLine(msgPayloadString);
            }

            fwriteInstrument(file, currentTime, msgSender, msgAppId, msgMethod, msgPayloadString.c_str());
            fclose(file);
        }

        bool endsWith(const std::string &fullString, const std::string &ending)
        {
            if (fullString.length() >= ending.length()) {
                return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
            }
            return false;
        }


        void addPostfixToApiMap(const std::string &postfix, const std::string &api)
        {
            g_postfixApiMap[postfix] = api;
        }

        std::string guessSubscribeMethodSubscriptionDumpItem(const std::string &key)
        {
            for (auto pair : g_postfixApiMap) {
                if (!pair.first.empty() && endsWith(key, pair.first)) {
                    return pair.second;
                }
            }
            return g_postfixApiMap[""];
        }

        void writeSubscriptionDumpItem(const std::map<std::string, SubscriptionDumpItem> &subscriptionMap)
        {
            FILE* file = openInstrumentFile();
            if (file == NULL) {
                return;
            }

            for (const std::pair<std::string,SubscriptionDumpItem> &it : subscriptionMap) {
                fwriteInstrument(file, "SUBSCRIBED", it.second.sender.c_str(), "", it.second.method.c_str(), it.second.message.c_str());
            }

            fclose(file);
        }

        /**
         * [parse description]
         * @param  payload         [description]
         * @param  subscriptionMap <UniqueName,SubscriptionDumpItem>
         * @return                 [description]
         */
        bool parseSubscriptionDumpItem(const std::string &payload, std::map<std::string, SubscriptionDumpItem> &subscriptionMap)
        {
            pbnjson::JDomParser parser;
            if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
                return false;
            }

            pbnjson::JValue jDom = parser.getDom();

            if (jDom["returnValue"].asBool() == false) {
                return false;
            }

            for (pbnjson::JValue subscription : jDom["subscriptions"].items()) {
                pbnjson::JValue subscribers = subscription["subscribers"];
                for (pbnjson::JValue subscriber : subscribers.items()) {
                    std::string message = subscriber["subscription_message"].asString();
                    std::string uniqueName = subscriber["unique_name"].asString() + message;
                    if (subscriptionMap.find(uniqueName) != subscriptionMap.end()) continue;
                    stripNewLine(message);
                    subscriptionMap[uniqueName] = {
                        subscriber["service_name"].asString(),
                        guessSubscribeMethodSubscriptionDumpItem(subscription["key"].asString()),
                        message
                    };
                }
            }

            return true;
        }

        void addCurrentServiceName(const std::string &serviceName)
        {
            g_serviceNames.push_back(serviceName);
        }

        //--------------------------------------------------------------------
        // CurrentSubscriptions class methods
        //--------------------------------------------------------------------

        CurrentSubscriptions::CurrentSubscriptions()
        {
            ref();
            LSErrorInit(&m_lsError);
        }

        CurrentSubscriptions::~CurrentSubscriptions()
        {
            if (LSErrorIsSet(&m_lsError)) LSErrorFree(&m_lsError);
        }

        std::string CurrentSubscriptions::errorText() const
        {
            if (m_errorText.empty() && LSErrorIsSet(const_cast<LSError*>(&m_lsError)))
                return "LUNASERVICE ERROR " + std::to_string(m_lsError.error_code) + ": "
                                            + m_lsError.message;
            return m_errorText;
        }

        void CurrentSubscriptions::getLocalsubscriptionMap()
        {
            m_subscriptionMap = g_subscriptionMap;
        }
        void CurrentSubscriptions::request (requestCallBack cb)
        {
            m_requestCallBack   = cb;
            m_g_serviceNamesIt  = g_serviceNames.begin();
            m_subscriptionMap.clear();
            m_errorText.clear();
            requestNext();
        }

        void CurrentSubscriptions::requestNext()
        {
            m_errorContext = __PRETTY_FUNCTION__;
            if (g_serviceNames.end() == m_g_serviceNamesIt)
            {
                m_requestCallBack();
                return;
            }
            m_requestURI = "luna://" + *m_g_serviceNamesIt
                                     + "/com/palm/luna/private/subscriptions";
            LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
            bool result = LSCallOneReply(
                PrefsFactory::instance()->getServiceHandle(PrefsFactory::COM_WEBOS_SERVICE),
                m_requestURI.c_str(), "{}", requestProcessReply, this, &token, &m_lsError);
            if (result)
            {
                m_g_serviceNamesIt++;
            }
            else
            {
                m_requestCallBack();
            }
        }

        bool CurrentSubscriptions::requestProcessReply(LSHandle*  handle,
                                                       LSMessage* message, void* context)
        {
            auto subscriptions = static_cast<CurrentSubscriptions*>(context);
            subscriptions->m_errorContext = __PRETTY_FUNCTION__;
            std::string payload = LSMessageGetPayload(message);
            if (parseSubscriptionDumpItem(payload, subscriptions->m_subscriptionMap))
            {
                subscriptions->requestNext();
            }
            else
            {
                subscriptions->m_errorText = "cannot parse result: "
                                           + subscriptions->m_requestURI + " '{}' --> " + payload;
                subscriptions->m_requestCallBack();
            }
            return true;
        }

    }                           //end Instrument namespace

}                               //end Utils namespace
