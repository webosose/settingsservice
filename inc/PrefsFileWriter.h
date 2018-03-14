// Copyright (c) 2014-2018 LG Electronics, Inc.
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

#include <list>
#include <map>
#include <string>
#include <functional>
#include <mutex>

struct Rules
{
    std::string key;
    std::string category;
    std::list<std::string> postProcess;
};

struct RuleSchema
{
    std::string path;
    std::list<Rules> rules;
};

class PrefsFileWriter
{

public:

    static PrefsFileWriter *instance();

    PrefsFileWriter();

    void updateFilesIfTargetExistsInSettingsObj(const std::string& catName, pbnjson::JValue keysObj);

    pbnjson::JValue getPreferences(const std::string &a_category, const std::string &a_key) const;
    bool isAvailablePreferences(const std::string& a_category, const std::set<std::string>& a_keys) const;
    void invalidatePreferences(void);
    bool parseRules(pbnjson::JValue objRoot, std::list<Rules>& rules);
    bool parseRuleSchema(pbnjson::JValue objRoot, std::list<RuleSchema>& ruleSchemas);
    const std::set<std::string>& getCategories() const { return m_categories; }

private:

    class PrefsFileWriterRule {

    public:

        std::list< std::pair<std::string, std::string> > m_keyCategoryPairs; // list of <key,category>
        std::list< std::function<void(pbnjson::JValue)> > m_postHandler;
        std::set< std::string > m_disableCache;

        std::string m_content;
        std::map< std::string, std::string > m_propContent;

        pbnjson::JValue m_jsonFlushBuf;
        pbnjson::JValue m_jsonContentBuf;

        bool m_isNeedFlush;

        PrefsFileWriterRule() : m_isNeedFlush(false) {
        }

        void init();

        void clear();

        bool match(const std::string& a_category, const std::set<std::string>& a_keys) const;
        void postProcessing();

        void cacheContent(pbnjson::JValue a_obj);
        pbnjson::JValue getContentCache() const;
        void clearContentCache();
        bool availableContentCache(const std::set<std::string>& a_props) const;
        pbnjson::JValue cloneContentCache(const std::string& a_prop) const;
    };

private:

    static PrefsFileWriter *_instance;

    mutable std::mutex m_rules_lock;

    std::map<std::string, PrefsFileWriterRule> m_rules;

    std::set<std::string> m_categories;

    void loadLocaleInfo();
    void flush();

    void writeJSONObjectStr(const std::string &filepath, pbnjson::JValue jKeyValueObj);

    static void encryptLockData(pbnjson::JValue a_buf);
};
