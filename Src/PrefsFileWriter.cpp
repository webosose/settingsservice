// Copyright (c) 2014-2023 LG Electronics, Inc.
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

#include <string.h>
#include <fstream>
#include <iostream>
#include <algorithm>

#include <openssl/sha.h>
#include <openssl/md5.h>

#include "Utils.h"
#include "JSONUtils.h"
#include "SettingsService.h"
#include "PrefsFileWriter.h"
#include "PrefsFactory.h"
#include "PrefsKeyDescMap.h"
#include "Logging.h"

using namespace std;

static const char* s_prefsFileWriterRuleFile = "/etc/palm/settings/prefsFileWriterRule.json";

string bin2hex(const unsigned char *bin, size_t len) {
    const char hex[] = "0123456789abcdef";

    string res;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char) bin[i];
        res += hex[(unsigned char) (c >> 4)];
        res += hex[(unsigned char) (c & 0xf)];
    }

    return res;
}

//
// Single Instance
//
PrefsFileWriter *PrefsFileWriter::_instance = NULL;
PrefsFileWriter *PrefsFileWriter::instance()
{
    if (_instance == NULL) {
        _instance = new PrefsFileWriter();
    }
    return _instance;
}

/**
@page Lock_Category Lock Category
@section Encryption_Condition Encryption Condition
Encryption applied on 'systemPin' key of 'lock' category
@sa PrefsFileWriter::PrefsFileWriter
*/

/**
initialize mutex for file IO, and
rules about keys to be written when it changed

@sa \ref SettingsCache
*/
PrefsFileWriter::PrefsFileWriter()
{
    std::string contents;
    if (!Utils::readFile(s_prefsFileWriterRuleFile,contents))
        return;

    pbnjson::JValue prefsFileWriterRule = pbnjson::JDomParser::fromString(contents);
    if (prefsFileWriterRule.isNull())
        return;

    std::list<RuleSchema> ruleSchemas;
    if(!parseRuleSchema(prefsFileWriterRule, ruleSchemas))
        return;

    for(const auto& item: ruleSchemas)
    {
        PrefsFileWriterRule r;
        r.init();

        for(const auto& rule : item.rules)
        {
            r.m_keyCategoryPairs.push_back(std::pair<std::string, std::string>(rule.key.c_str(), rule.category.c_str()));
            this->m_categories.insert(rule.category);
            if (std::find(rule.postProcess.begin(), rule.postProcess.end(), "encrypt") != rule.postProcess.end())
                r.m_postHandler.push_back(encryptLockData);

            if (std::find(rule.postProcess.begin(), rule.postProcess.end(), "disableCache") != rule.postProcess.end())
                r.m_disableCache.insert(rule.key.c_str());
        }

        m_rules.insert(std::pair<std::string, PrefsFileWriterRule>(item.path, r));
    }

    loadLocaleInfo();
}

void PrefsFileWriter::invalidatePreferences(void)
{
    std::lock_guard<std::mutex> lock (m_rules_lock);

    for ( std::map<std::string,PrefsFileWriterRule>::value_type& r : m_rules )
        r.second.clearContentCache();
}

bool writeMD5Checksum(const string &path, const char *str, size_t strlen)
{
    unsigned char checksum[MD5_DIGEST_LENGTH];
    memset(checksum, 0, MD5_DIGEST_LENGTH);
    MD5((unsigned char *)str, strlen, checksum);

    ofstream outMd5(path + ".md5", ios_base::out | ios_base::trunc);
    if (outMd5.good()) {
        outMd5 << bin2hex(checksum, MD5_DIGEST_LENGTH) << "  " << path;
        outMd5.close();
        return true;
    }

    int err = remove(path.c_str());
    SSERVICELOG_ERROR(MSGID_LOCALEINFO_FILE_OPEN_FAILED, 2,
        PMLOGKS("Target FilePath", path.c_str()),
        PMLOGKFV("Remove file returns", "%d", err),
        "Fail to write cache md5 file");

    return false;
}
//
// write files in m_rules if required
//
void PrefsFileWriter::flush()
{
    map<string, PrefsFileWriterRule>::iterator it;

    // for each PrefsFileWriterRules
    for (it = m_rules.begin(); it != m_rules.end(); ++it) {
        const string & path = it->first;
        PrefsFileWriterRule & rule = it->second;

        if (rule.m_isNeedFlush == false) {
            continue;
        }

        rule.postProcessing();

        std::string FlushBuf = rule.m_jsonFlushBuf.stringify();
        if (rule.m_content.empty() || rule.m_content != FlushBuf) {

            // file content is not empty
            // and it is need to be update
            pbnjson::JValue jContentObj = pbnjson::JDomParser::fromString(rule.m_content);
            if (!jContentObj.isObject()) {
                jContentObj = pbnjson::Object();
            }
            for(pbnjson::JValue::KeyValue it : rule.m_jsonFlushBuf.children()) {
                jContentObj.put(it.first, it.second);
            }

            if (jContentObj != rule.getContentCache()) {
                writeJSONObjectStr(path, jContentObj);
                rule.cacheContent(jContentObj);
            }
        }
        else if (false == Utils::doesExistOnFilesystem(string(path + ".md5").c_str())) {
            (void)writeMD5Checksum(path, rule.m_content.c_str(), rule.m_content.length());
        }
        rule.init();
    }
}

/**
@page Lock_Category Lock Category
@section Encryption
Encryption uses SHA256
@sa PrefsFileWriter::updateFilesIfTargetExistsInSettingsObj
*/

/**
If argument is matched for specified rules,
write it to specified file via buffer(PrefsFileWriterRule.m_jsonFlushBuf)
if it need to be written.
@param catName category
@param keysObj json key/value objects updated previous stage already
@sa \ref Lock_Category
*/
void PrefsFileWriter::updateFilesIfTargetExistsInSettingsObj(const string& catName, pbnjson::JValue keysObj)
{
    SSERVICELOG_TRACE(
        "PrefsFileWriter::updateFilesIfTargetExistsInSettingsObj() input: %s, %s",
        catName.c_str(),
        keysObj.stringify().c_str());

    std::lock_guard<std::mutex> lock(m_rules_lock);

    loadLocaleInfo();

    // check argument is need to be processed?
    for (pair<const string, PrefsFileWriterRule>& itRule : m_rules) {
        PrefsFileWriterRule &rule = itRule.second;
        const list<pair<string, string> > &keyCatPairs = rule.m_keyCategoryPairs;

        for (const pair<string, string>& itKey : keyCatPairs) {
            // check category is equal?
            if (catName == itKey.second) {
                pbnjson::JValue jsonObj = keysObj[itKey.first];
                // get the value from argument for that key, the key in specified in rules
                if (!jsonObj.isNull()) {

                    // hash the value if the category is lock
                    // this creates new jsonObj
                    // add key:value to flush object
                    rule.m_jsonFlushBuf.put(itKey.first, jsonObj);
                    rule.m_isNeedFlush = true;
                }
                if (itKey.first.empty()) {
                    for(pbnjson::JValue::KeyValue it : keysObj.children()) {
                        rule.m_jsonFlushBuf.put(it.first, it.second);
                        rule.m_isNeedFlush = true;
                    }
                }
            }
        }
    }

    flush();
}

/**
@page SettingsCache Settings Cache
@section SettingsCache_File Cache File and Hash File
SettingsCache provides the key and its value before SettingsService is launched
via some files (called 'cache') in /var/luna/preferences/{categoryName,something}.
SettingsCache Rule is defined in PrefsFileWriter::m_rules, so changes of values
are dropped into cache files through the SettingsCache rules(some value is
encrypted like 'systemPin').
For integrity of Settings Cache, MD5 hash and JSON content validator are operated
with 'bootd' service.
@sa PrefsFileWriter::PrefsFileWriter
@sa PrefsFileWriter::writeJSONObjectStr
@sa http://hlm.lge.com/issue/browse/WEBOSDWBS-2660
*/

//
// @param jObj an instance of json_object that contains
//             localeInfo key and value. like '{"localeInfo": {
//               clock: "locale"
//               timezone: "",
//               keyboards: ["en"],
//               locales: [
//                 AUD: "en-US"
//                 ...
//               ]
//             }}' if the filepath is like '/var/luna/preference/localeInfo'
//
void PrefsFileWriter::writeJSONObjectStr(const string &filepath, pbnjson::JValue jKeyValueObj)
{
    //Filter for Volatile
    pbnjson::JValue nonVolatileKeyValueObj = PrefsKeyDescMap::instance()->FilterForVolatile(jKeyValueObj);
    const std::string strJson = nonVolatileKeyValueObj.stringify();

    // First, write checksum of the cache.
    if (false == writeMD5Checksum(filepath, strJson.c_str(), strJson.length())) {
        return;
    }
    // Second, write the cache.
    {
        ofstream outCache(filepath, ios_base::out | ios_base::trunc);
        if (outCache.good()) {
            outCache << strJson;
            outCache.close();
        }
        else {
            SSERVICELOG_ERROR(MSGID_LOCALEINFO_FILE_OPEN_FAILED, 1,
                PMLOGKS("Target FilePath", filepath.c_str()),
                "Fail to write cache file");
        }
    }
}

//
// load json content from file into m_content if file existed
// skip if already content is loaded
//
void PrefsFileWriter::loadLocaleInfo()
{
    map<string, PrefsFileWriterRule>::iterator it;

    // for each PrefsFileWriterRules
    for (it = m_rules.begin(); it != m_rules.end(); ++it) {
        ifstream ifs;

        // check already load the content of that file
        if (it->second.m_content.empty() == false) {
            continue;
        }

        // read file content
        ifs.open(it->first.c_str(), ios_base::in);
        if (ifs.fail()) {
            continue;
        }

        it->second.m_content.assign((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
        ifs.close();

        pbnjson::JValue content = pbnjson::JDomParser::fromString(it->second.m_content);
        it->second.cacheContent(content);
    }
}

//
// encode pin data in lock
//
void PrefsFileWriter::encryptLockData(pbnjson::JValue a_buf)
{
    vector<pair<string,string>> encrypted;
    unsigned char md[SHA256_DIGEST_LENGTH];

    for(pbnjson::JValue::KeyValue it : a_buf.children()) {
        if (!it.second.isString())
            continue;

        string val(it.second.asString());
        SHA256((unsigned char*)val.c_str(), val.length(), md);
        string hexMd = bin2hex(md, SHA256_DIGEST_LENGTH);

        encrypted.push_back( { string(it.first.asString()), hexMd } );
    }

    for ( const pair<string,string>& enc : encrypted ) {
        a_buf.put(enc.first, enc.second);
    }
}


//
// initialize pointer variables
//
void PrefsFileWriter::PrefsFileWriterRule::init()
{
    clear();

    m_jsonFlushBuf = pbnjson::Object();
}

//
// clear all member variables. dealloc if required
//
void PrefsFileWriter::PrefsFileWriterRule::clear()
{
    m_jsonFlushBuf = pbnjson::Object();

    m_content.clear();
    m_isNeedFlush = false;
    /* Do not clear m_jsonContentBuf. The Buf is used by getPreferences function */
}

//
// store the pbnjson object which is parsed cache file.
// cache is used by getReferences member function.
//
void PrefsFileWriter::PrefsFileWriterRule::cacheContent(pbnjson::JValue a_obj)
{
    if (!a_obj.isObject())
        return;

    m_jsonContentBuf = a_obj;

    /* For performance, stringify json in advance.
     * It is used when clone and return cache object */

    m_propContent.clear();

    for (pbnjson::JValue::KeyValue it : a_obj.children()) {
        if (it.second.isNull()) continue;
        m_propContent.insert( {it.first.asString(), it.second.stringify()} );
    }
}

//
// return cached object
//
pbnjson::JValue PrefsFileWriter::PrefsFileWriterRule::getContentCache(void) const
{
    return m_jsonContentBuf;
}

bool PrefsFileWriter::PrefsFileWriterRule::availableContentCache(const std::set<std::string>& a_props) const
{
    if (!m_jsonContentBuf.isObject())
        return false;

    for ( const std::string& k : a_props ) {
        pbnjson::JValue found = m_jsonContentBuf[k];
        if (found.isNull())
            return false;
    }

    return true;
}

void PrefsFileWriter::PrefsFileWriterRule::clearContentCache(void)
{
    m_jsonContentBuf = pbnjson::Object();
}

pbnjson::JValue PrefsFileWriter::PrefsFileWriterRule::cloneContentCache(const std::string& a_prop) const
{
    auto propertyIter = m_propContent.find(a_prop);

    if ( propertyIter == m_propContent.end() )
        return pbnjson::Object();

    pbnjson::JValue contentObj = pbnjson::JDomParser::fromString(propertyIter->second);

    if (contentObj.isNull())
        return pbnjson::Object();

    return contentObj;
}

//
// Find appropriate rule is defeind
//
bool PrefsFileWriter::PrefsFileWriterRule::match(const std::string& a_category, const std::set<std::string>& a_keys) const
{
    bool matched = false;
    std::set<std::string> matchedKeys;

    for ( const std::string& key : a_keys ) {
        if ( m_disableCache.find(key) != m_disableCache.end() )
            return false;
    }

    for ( const list< pair<string,string> >::value_type& key_cat : m_keyCategoryPairs ) {
        if ( a_category != key_cat.second )
            continue;

        if ( key_cat.first.empty() ) {
            matched = true;
            break;
        }

        if ( a_keys.find(key_cat.first) != a_keys.end() )
            matchedKeys.insert(key_cat.first);
    }

    if ( matched )
        return true;

    return matchedKeys.size() == a_keys.size();
}


//
// Modify cache data before flush
//
void PrefsFileWriter::PrefsFileWriterRule::postProcessing()
{
    for (const function<void(pbnjson::JValue)>& handle : m_postHandler ) {
        handle(m_jsonFlushBuf);
    }
}

//
// Check Cache is available
//
bool PrefsFileWriter::isAvailablePreferences(const std::string& a_category, const std::set<std::string>& a_keys) const
{
    if ( a_keys.empty() )
        return false;

    set<string> foundKeys;

    for (const map<string, PrefsFileWriterRule>::value_type& r : m_rules)
    {
        const PrefsFileWriterRule& rule = r.second;

        if ( !rule.match(a_category, a_keys) )
            continue;

        if ( !rule.availableContentCache(a_keys) )
            continue;

        return true;
    }

    return false;
}

//
// Retrieve specific settings from buffer which is equal to file
//
pbnjson::JValue PrefsFileWriter::getPreferences(const string &a_category, const string &a_key) const
{
    for (const pair<string, PrefsFileWriterRule>& rule_pair : m_rules)
    {
        const PrefsFileWriterRule& rule = rule_pair.second;
        if ( rule.m_disableCache.find(a_key) != rule.m_disableCache.end() )
            continue;

        for ( const pair<string,string>& key_cat : rule.m_keyCategoryPairs )
        {
            if ( key_cat.second == a_category &&
                  ( key_cat.first == a_key || key_cat.first.empty() )  )
            {
                pbnjson::JValue obj = rule.cloneContentCache(a_key);
                if (obj.isNull())
                    continue;
                return obj;
            }
        }
    }

    return pbnjson::JValue();
}

bool PrefsFileWriter::parseRules(pbnjson::JValue objRoot, std::list<Rules>& rules)
{
    if (objRoot.isNull())
        return false;

    if (!objRoot.isArray())
        return false;

    for (pbnjson::JValue objRule : objRoot.items()) {
        if (objRule.isNull())
            continue;

        pbnjson::JValue objKey = objRule["key"];
        if (!objKey.isString())
            continue;

        pbnjson::JValue objCategory = objRule["category"];
        if (!objCategory.isString())
            continue;

        pbnjson::JValue objPostProcess = objRule["postProcess"];
        if (!objPostProcess.isArray())
            continue;

        Rules rule;
        rule.key = objKey.asString();
        rule.category = objCategory.asString();
        Utils::json_object_array_into_string_list(objPostProcess, rule.postProcess);

        rules.push_back(rule);
    }
    return true;
}

bool PrefsFileWriter::parseRuleSchema(pbnjson::JValue objRoot, std::list<RuleSchema>& ruleSchemas)
{
    if (objRoot.isNull())
        return false;

    if (!objRoot.isArray())
        return false;

    for (pbnjson::JValue objRuleSchema : objRoot.items()) {
        if (objRuleSchema.isNull())
            return false;

        pbnjson::JValue objPath = objRuleSchema["path"];
        if (objPath.isNull())
            return false;

        pbnjson::JValue objRules = objRuleSchema["rules"];
        if (objRules.isNull())
            return false;

        RuleSchema ruleSchema;
        ruleSchema.path = objPath.asString();
        if (ruleSchema.path.empty())
            return false;

        if( !parseRules(objRules, ruleSchema.rules))
            return false;

        ruleSchemas.push_back(ruleSchema);
    }
    return true;
}
