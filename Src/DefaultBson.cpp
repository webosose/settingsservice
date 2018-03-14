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

#include <iostream>
#include <sstream>

#include "DefaultBson.h"
#include "JSONUtils.h"
#include "Logging.h"
#include "SettingsService.h"
#include "Utils.h"

using namespace std;

class SettingsObject {
    public:
        SettingsObject(pbnjson::JValue a_obj, const std::set<std::string>& a_select)
        {
            m_json_object = a_obj;

            if ( a_select.empty() )
                return;

            pbnjson::JValue values_obj = m_json_object["value"];
            if ( !values_obj.isObject() )
                return;

            pbnjson::JValue new_obj(pbnjson::Object());

            for (const std::string& sel : a_select )
            {
                pbnjson::JValue key_obj = values_obj[sel];
                if (!key_obj.isNull())
                    new_obj.put(sel, key_obj);
            }

            m_json_object.put("value", new_obj);
        }

        const std::string getPropString(const std::string& a_prop) const
        {
            std::string value;

            if (!m_json_object.isNull()) {
                pbnjson::JValue prop_obj = m_json_object[a_prop];

                if (prop_obj.isString())
                    value = prop_obj.asString();
            }

            return value;
        }

        pbnjson::JValue get(void) const
        {
            return m_json_object;
        }

    private:
        pbnjson::JValue m_json_object;
};


// DefaultBson::Indexer ///////////////////////////////////////////////////////

DefaultBson::Indexer::Indexer(const string &a_idx_prop)
{
    m_key_name = a_idx_prop;
}

DefaultBson::Indexer::~Indexer()
{
}

set<unsigned int> DefaultBson::Indexer::findIDs(const set<string> &valueSet) const
{
    set<unsigned int> found_ids;

    for ( set<string>::const_iterator val = valueSet.begin();
            val != valueSet.end();
            ++val ) {
        IDMap::const_iterator ids = m_ids.find(*val);

        if ( ids != m_ids.end() ) {
            found_ids.insert(ids->second.begin(), ids->second.end());
        }
    }

    return found_ids;
}

void DefaultBson::Indexer::indexing(const unsigned int a_id, bson_iter_t &itArrayItem)
{
    bson_iter_t itArrayItemSub;
    bson_iter_recurse(&itArrayItem, &itArrayItemSub);

    bson_iter_t itDescendant;
    if (!bson_iter_find_descendant(&itArrayItemSub, m_key_name.c_str(), &itDescendant)) {
        return;
    }

    const bson_value_t *bsonValue = bson_iter_value(&itDescendant);
    if (bsonValue->value_type != BSON_TYPE_UTF8) {
        return;
    }

    string value = bsonValue->value.v_utf8.str;

    IDMap::iterator ids = m_ids.find(value);
    if (ids != m_ids.end()) {
        ids->second.insert(a_id);
    }
    else {
        set<unsigned int> new_ids;
        new_ids.insert(a_id);
        m_ids.insert(IDMap::value_type(value, new_ids));
    }
}

void DefaultBson::Indexer::indexing(const unsigned int a_id, pbnjson::JValue a_obj)
{
    if ( !a_obj.isObject() )
        return;

    pbnjson::JValue key_obj(a_obj[m_key_name]);

    /* current indexed data is always string */
    if ( !key_obj.isString() )
        return;

    std::string key = key_obj.asString();

    IDMap::iterator ids = m_ids.find(key);
    if ( ids != m_ids.end() ) {
        ids->second.insert(a_id);
    } else {
        std::set<unsigned int> new_ids;
        new_ids.insert(a_id);
        m_ids.insert(IDMap::value_type(key, new_ids));
    }
}

void DefaultBson::Indexer::sort(list<SettingsObject>& a_objs) const
{
    compareKeyName = m_key_name;
    a_objs.sort(compare);
}

string DefaultBson::Indexer::compareKeyName;

bool DefaultBson::Indexer::compare(const SettingsObject& first, const SettingsObject& second)
{
    return first.getPropString(compareKeyName) < second.getPropString(compareKeyName);
}

bool DefaultBson::Indexer::compareImpl(const SettingsObject &first, const SettingsObject &second)
{
    return first.getPropString(compareKeyName) < second.getPropString(compareKeyName);
}

// DefaultBson::TokenizedIndexer //////////////////////////////////////////////

DefaultBson::TokenizedIndexer::TokenizedIndexer(const string &a_key_name)
    : DefaultBson::Indexer(a_key_name)
{
    m_delim = ',';
}

void DefaultBson::TokenizedIndexer::indexing(const unsigned int a_id, bson_iter_t &itArrayItem)
{
    // TODO:
    // This is special for 'country'.
    // So, this need to be generalized.

    string value = "none";

    bson_iter_t itArrayItemSub;
    bson_iter_recurse(&itArrayItem, &itArrayItemSub);

    bson_iter_t itDescendant;
    if (bson_iter_find_descendant(&itArrayItemSub, m_key_name.c_str(), &itDescendant)) {
        const bson_value_t *bsonValue = bson_iter_value(&itDescendant);
        if (bsonValue->value_type == BSON_TYPE_UTF8) {
            value = bsonValue->value.v_utf8.str;
        }
    }

    istringstream keys(value);

    set<string> tokens;
    string token;

    while (getline(keys, token, m_delim)) {
        token = token.erase(token.find_last_not_of(" ") + 1);
        token = token.erase(0, token.find_first_not_of(" "));
        if (!token.empty()) {
            tokens.insert(token);
        }
    }

    for (set<string>::const_iterator key = tokens.begin(); key != tokens.end(); ++key ) {
        IDMap::iterator ids = m_ids.find(*key);
        if ( ids != m_ids.end() ) {
            ids->second.insert(a_id);
        }
        else {
            set<unsigned int> ids;
            ids.insert(a_id);
            m_ids.insert(IDMap::value_type(*key, ids));
        }
    }
}

bool DefaultBson::TokenizedIndexer::compareImpl(const SettingsObject &first, const SettingsObject &second)
{
    if ( compareKeyName != "country" ) {
        return first.getPropString(compareKeyName) < second.getPropString(compareKeyName);
    } else {
        string first_country = first.getPropString("country");
        string second_country = second.getPropString("country");

        if ( first_country == second_country ) {
            return false;
        } else if ( first_country == "" ) {
            return true;
        } else if ( second_country == "" ) {
            return false;
        } else {
            return false; /* country string has no order */
        }
    }
}

void DefaultBson::searchIDs(const string &a_key_name, const set<string>& valueSet, set<unsigned int>& a_result) const
{
    a_result.clear();

    if ( !m_bsonDoc ) {
        return;
    }

    bson_iter_t itBsonArr;
    if ( !bson_iter_init_find(&itBsonArr, m_bsonDoc, "BSON") ||
            !BSON_ITER_HOLDS_ARRAY(&itBsonArr) )
    {
        return;
    }

    unsigned int arrayIndex = 0;
    bson_iter_t itArrayItem;

    if ( !bson_iter_recurse(&itBsonArr, &itArrayItem) ) {
        return;
    }

    while ( bson_iter_next(&itArrayItem) ) {
        bson_iter_t itDesc;

        if ( BSON_ITER_HOLDS_DOCUMENT(&itArrayItem) &&
                bson_iter_recurse(&itArrayItem, &itDesc) &&
                bson_iter_find(&itDesc, a_key_name.c_str()) &&
                BSON_ITER_HOLDS_UTF8(&itDesc) )
        {
            uint32_t strlen = 0;
            const char *value = bson_iter_utf8(&itDesc, &strlen);

            if ( !bson_utf8_validate(value, strlen, false) )
                continue;

            string country(value);
            bool country_matched = false;
            typedef set<string>::const_iterator Iter;
            for ( Iter v = valueSet.begin(); v != valueSet.end(); ++v ) {
                if ( country.find(*v) != string::npos ) {
                    country_matched = true;
                    break;
                }
            }

            if ( country_matched )
                a_result.insert(arrayIndex);
        }

        arrayIndex++;
    }

    return;
}

// DefaultBson ////////////////////////////////////////////////////////////////

DefaultBson::DefaultBson() : m_bsonDoc(NULL), m_bsonDocReader(NULL), m_loadCompleted(false)
{
}

DefaultBson::DefaultBson(const string &bsonPath) : m_bsonDoc(NULL), m_bsonDocReader(NULL), m_loadCompleted(false)
{
    m_bsonPath = bsonPath;

    m_indexes.insert(IndexerMap::value_type("category", new DefaultBson::Indexer("category")));
    m_indexes.insert(IndexerMap::value_type("app_id",   new DefaultBson::Indexer("app_id")));
    m_indexes.insert(IndexerMap::value_type("country",  new DefaultBson::TokenizedIndexer("country")));
}

DefaultBson::~DefaultBson()
{
    for (IndexerMap::iterator idxer = m_indexes.begin(); idxer != m_indexes.end(); ++idxer) {
        DefaultBson::Indexer *indexer = idxer->second;
        delete indexer; // TODO: shared_ptr
    }
    m_indexes.clear();

    if (m_bsonDocReader != NULL) {
        bson_reader_destroy(m_bsonDocReader);
        m_bsonDoc = NULL;
    }
}

static DefaultBson *s_instance = 0;

DefaultBson *DefaultBson::instance()
{
    if (!s_instance) {
        s_instance = new DefaultBson(DEFAULT_LOADING_FILEPATH_BSON);
    }

    return s_instance;
}

/**
 * Load and parse 'path' file and make index of its content.
 * Let m_entireIDs have all sequencial ids of items.
 * Check success log with 'INFO' level, failure log with 'WARNING' level.
 *
 * @param  path     defaultSettings.bson or for per-app bson path
 * @param  isAppend if true, appended items are all in cache not in m_bsonDoc
 *                  Because of difficulty of appending new items into m_bsonDoc.
 *                  Just make indexes and loaded into cache directly.
 * @return          false if this cannot read the file or cannot parse the file as bson
 */
bool DefaultBson::loadAndDoIndexing(std::string &path, bool isAppend)
{
    bson_error_t error;

    // reader would be not free by bson_reader_destroy() until
    // bson document is used no longer.
    bson_reader_t *reader = bson_reader_new_from_file(path.c_str(), &error);
    if (reader == NULL) {
        SSERVICELOG_WARNING(MSGID_DEFAULTSETTINGS_LOAD_FOR_APP, 1,
            PMLOGKS("Cannot Load - Read File Error", path.c_str()),
            MSGID_DEFAULTSETTINGS_LOAD_FOR_APP);
        return false;
    }

    const bson_t *bsonDoc = NULL;
    const bson_t *bsonDocRead = NULL;
    while ((bsonDocRead = bson_reader_read(reader, NULL))) { // result of bson_reader_read() should not be modified or freed.
        bson_iter_t bson_iter;
        bson_iter_init_find (&bson_iter, bsonDocRead, "BSON");

        if (BSON_ITER_HOLDS_ARRAY(&bson_iter)) {
            bsonDoc = bsonDocRead;
        }
    }

    if (NULL == bsonDoc) {
        bson_reader_destroy(reader);
        return false;
    }

    if (!isAppend) {
        m_bsonDoc = bsonDoc;
        m_bsonDocReader = reader;
    }

    // At this time m_bsonDoc holds BSON document like
    // { "BSON": [
    //   {"0":{...}},
    //   {"1":{...}},
    //   {"2":{...}},
    //   {"3":{...}}
    // ]}
    //
    // Array in BSON has index number - this is not exited in JSON - starts from 0

    bson_iter_t itBsonArr;
    bson_iter_init_find(&itBsonArr, bsonDoc, "BSON");

    if (BSON_ITER_HOLDS_ARRAY(&itBsonArr)) {

        bson_iter_t itArrayItem;
        bson_iter_recurse(&itBsonArr, &itArrayItem);
        while (bson_iter_next(&itArrayItem)) {
            const bson_value_t *itemValue = bson_iter_value(&itArrayItem);
            if (itemValue->value_type == BSON_TYPE_DOCUMENT) {
                for (IndexerMap::iterator idxer = m_indexes.begin(); idxer != m_indexes.end(); ++idxer) {
                    idxer->second->indexing(m_entireIDs.size(), itArrayItem);
                }

                if (isAppend) {
                    size_t len = 0;
                    bson_t *doc = bson_new_from_data(itemValue->value.v_doc.data, itemValue->value.v_doc.data_len);
                    char *str = bson_as_json(doc, &len);
                    storeIntoCache(m_entireIDs.size(), str);
                    bson_free(str);
                    bson_destroy(doc);
                }

                m_entireIDs.insert(m_entireIDs.size());
            }
        }
    }

    if (isAppend) {
        bson_reader_destroy(reader);
    }

    SSERVICELOG_INFO(MSGID_DEFAULTSETTINGS_LOAD_FOR_APP, 1,
        PMLOGKS("Load", path.c_str()),
        MSGID_DEFAULTSETTINGS_LOAD_FOR_APP);

    return true;
}

/**
 * Load default settings data from per-app json files
 */
void DefaultBson::loadDefaultSettingsFilesForEachApp()
{
    std::list<std::string> entries;

    Utils::readDirEntry(DEFAULT_LOADING_DIRECTORY, ".defaultSettings.bson", entries);

    for (std::list<std::string>::const_iterator entry = entries.begin(); entry != entries.end(); ++entry) {
        std::string path(DEFAULT_LOADING_DIRECTORY);
        path.append("/");
        path.append(*entry);

        loadAndDoIndexing(path);
    }
}

/**
 * Load default settings into DefaultBson
 *
 * @return  false if it is not able to load defaultSettings.bson
 */
bool DefaultBson::load(void)
{
    if (true == m_loadCompleted)
        return true;

    bool retVal = loadAndDoIndexing(m_bsonPath);
    m_loadCompleted = retVal;

    loadDefaultSettingsFilesForEachApp();

    return retVal;
}

bool DefaultBson::loadWithIndexerMap(const std::string &bsonPath, IndexerMap indexerMap)
{
    m_bsonPath = bsonPath;
    m_indexes = indexerMap;
    return loadAndDoIndexing(m_bsonPath);
}

/**
 * After this, Per-app contents are all in cache not in bson_t document.
 */
bool DefaultBson::loadAppendDirectory(const string &dirPath, const string &filePattern)
{
    list<string> entries;

    Utils::readDirEntry(DEFAULT_LOADING_DIRECTORY, filePattern, entries);

    for (list<string>::const_iterator entry = entries.begin(); entry != entries.end(); ++entry) {
        string path(DEFAULT_LOADING_DIRECTORY);
        path.append("/");
        path.append(*entry);

        loadAndDoIndexing(path, true);
    }
    return true;
}

bool DefaultBson::loadAppendDirectoryJson(const std::string &dirPath, const std::string &filePattern)
{
    list<string> entries;

    Utils::readDirEntry(DEFAULT_LOADING_DIRECTORY, filePattern, entries);

    for (list<string>::const_iterator entry = entries.begin(); entry != entries.end(); ++entry) {
        string path(DEFAULT_LOADING_DIRECTORY);
        path.append("/");
        path.append(*entry);

        std::string fileData;
        if (!Utils::readFile(path.c_str(),fileData)) {
            SSERVICELOG_WARNING(MSGID_DEFAULTSETTINGS_LOAD_FOR_APP, 1,
                PMLOGKS("Cannot Load - Read File Error", path.c_str()),
                MSGID_DEFAULTSETTINGS_LOAD_FOR_APP);
            continue;
        }

        pbnjson::JValue jObj = pbnjson::JDomParser::fromString(fileData);
        if (!jObj.isArray()) {
            SSERVICELOG_WARNING(MSGID_DEFAULTSETTINGS_LOAD_FOR_APP, 1,
                PMLOGKS("Cannot Load - Read File as JSON Error", path.c_str()),
                MSGID_DEFAULTSETTINGS_LOAD_FOR_APP);
            continue;
        }

        for (int i = 0; i < jObj.arraySize(); i++) {
            pbnjson::JValue jsonItem(jObj[i]);
            if (jsonItem.isObject()) {
                for (IndexerMap::iterator idxer = m_indexes.begin(); idxer != m_indexes.end(); ++idxer) {
                    idxer->second->indexing(m_entireIDs.size(), jsonItem);
                }
                std::string strJsonItem = jsonItem.stringify();
                storeIntoCache(m_entireIDs.size(), strJsonItem);
                m_entireIDs.insert(m_entireIDs.size());
            }
        }

    }
    return true;
}

void DefaultBson::storeIntoCache(unsigned int id, const string& jsonStr) const
{
    std::lock_guard<std::mutex> lock(m_lockCacheIdJsonString);
    m_cacheIdJsonString.insert(map<unsigned int, string>::value_type(id, jsonStr));
}

const string DefaultBson::hitFromCache(unsigned int id) const
{
    lock_guard<mutex> lock(m_lockCacheIdJsonString);
    map<unsigned int, string>::const_iterator it = m_cacheIdJsonString.find(id);
    return it != m_cacheIdJsonString.end() ? it->second : string();
}

void DefaultBson::Query::addWhere(const string &a_prop, const string &a_val)
{
    WhereMap::iterator w = m_where.find(a_prop);

    if (w == m_where.end()) {
        OrSet or_set;
        or_set.insert(a_val);
        m_where.insert(WhereMap::value_type(a_prop, or_set));
    }
    else {
        w->second.insert(a_val);
    }
}

void DefaultBson::Query::setSelect(const set<string> &a_keys)
{
    m_select.clear();
    m_select.insert(a_keys.begin(), a_keys.end());
}

void DefaultBson::Query::setOrder(const string &a_prop)
{
    /* TODO: order by should be supported */
    return; //m_order = a_prop;
}

pbnjson::JValue DefaultBson::Query::execute(void) const
{
    pbnjson::JValue outArr(pbnjson::Array());
    DefaultBson *bdata = DefaultBson::instance();
    executeEx(bdata, outArr);
    return outArr;
}

void DefaultBson::Query::executeFindIDs(const DefaultBson *bdata, std::set<unsigned int> &out) const
{
    // Find Data

    if (m_where.empty()) {
        out = bdata->m_entireIDs;
        return;
    }

    for (WhereMap::const_iterator w = m_where.begin(); w != m_where.end(); ++w) {
        const string &key = w->first;
        const set<string> &valueSet = w->second;
        set<unsigned int> partial;

        IndexerMap::const_iterator idxer = bdata->m_indexes.find(key);
        if (idxer != bdata->m_indexes.end()) {
            const Indexer *indexer = idxer->second;
            partial = indexer->findIDs(valueSet);
        } else {
            bdata->searchIDs(key, valueSet, partial);
        }


        if ( partial.empty() ) {
            out.clear();
            break;
        }

        if ( out.empty() ) {
            out = partial;
        }
        else {
            set<unsigned int> ids_intersection;
            set_intersection(
                out.begin(), out.end(),
                partial.begin(), partial.end(),
                inserter(ids_intersection, ids_intersection.end()));
            out = ids_intersection;
        }
    }
}

bool DefaultBson::Query::executeEx(const DefaultBson *bdata, pbnjson::JValue outJsonArr) const
{
    if (!bdata->m_bsonDoc)
        return false;

    set<unsigned int> found_ids;

    executeFindIDs(bdata, found_ids);

    list<SettingsObject> result;

    bson_iter_t itBsonArr;
    bson_iter_init_find(&itBsonArr, bdata->m_bsonDoc, "BSON");

    if (BSON_ITER_HOLDS_ARRAY(&itBsonArr)) {
        bson_iter_t itArrayItem;
        bson_iter_recurse(&itBsonArr, &itArrayItem);

        for (unsigned int id : found_ids) {
            const string cachedStr = bdata->hitFromCache(id);
            if (!cachedStr.empty()) {
                pbnjson::JValue jObj = pbnjson::JDomParser::fromString(cachedStr);
                result.push_back(SettingsObject(jObj, m_select));
                continue;
            }

            ostringstream oss;
            oss << id;
            string arrKey = oss.str();

            bson_iter_t itDescendant;
            if (bson_iter_find_descendant(&itArrayItem, arrKey.c_str(), &itDescendant)) {
                uint32_t bson_len = 0;
                const uint8_t *buf;
                bson_iter_document(&itDescendant, &bson_len, &buf);

                bson_t *doc = bson_new_from_data(buf, bson_len);
                size_t json_len = bson_len;
                char *str = bson_as_json(doc, &json_len);
                bdata->storeIntoCache(id, str);

                pbnjson::JValue jObj = pbnjson::JDomParser::fromString(str);
                result.push_back(SettingsObject(jObj, m_select));
                bson_free(str);
                bson_destroy(doc);
            }
        }
    }

    for (const SettingsObject& obj : result) {
        outJsonArr.append(obj.get());
    }

    // [
    //   { "_kind": "com.webos.settings.default:1", "app_id": "", "category": "picture$dtv.normal.2d", "value": { "backlight": "100" } },
    //   { "_kind": "com.webos.settings.default:1", "app_id": "", "category": "picture$dtv.normal.2d", "condition": { "panelType": "OLED" }, "value": { "backlight": "80" } }
    // ]
    return true;
}

size_t DefaultBson::Query::countEx(const DefaultBson *bdata) const
{
    set<unsigned int> found_ids;
    executeFindIDs(bdata, found_ids);
    return found_ids.size();
}
