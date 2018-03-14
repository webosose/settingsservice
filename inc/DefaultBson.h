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

#ifndef __DEFAULTBSON__
#define __DEFAULTBSON__

#include <string>
#include <set>
#include <map>
#include <list>
#include <mutex>

#include <bson.h>
#include <pbnjson.hpp>

class SettingsObject;


/**
 * DefaultBson Class
 */
class DefaultBson {

    public:

        /**
         * BsonIndexer Class
         */
        class Indexer {

            protected:
                /**
                 * string - set<int> Map
                 */
                typedef std::map<std::string, std::set<unsigned int> > IDMap;

            protected:
                IDMap m_ids;
                std::string m_key_name;
                static std::string compareKeyName;

            public:
                Indexer(const std::string &a_idx_prop);
                virtual ~Indexer();

                std::set<unsigned int> findIDs(const std::set<std::string>& a_keys) const;
                void sort(std::list<SettingsObject>& a_objs) const;
                void dump(std::map<unsigned int, pbnjson::JValue> a_data);

                virtual void indexing(const unsigned int a_id, bson_iter_t &itArrayItem);
                void indexing(const unsigned int a_id, pbnjson::JValue a_obj);
                virtual bool compareImpl(const SettingsObject& first, const SettingsObject& second);

                static bool compare(const SettingsObject& first, const SettingsObject& second);
        };

        /**
         * TokenizedIndexer Class
         */
        class TokenizedIndexer : public Indexer {
            private:
                char m_delim;

            public:
                TokenizedIndexer(const std::string &a_key_name);

                void indexing(const unsigned int a_id, bson_iter_t &itArrayItem);
                bool compareImpl(const SettingsObject& first, const SettingsObject& second);
        };

        typedef std::map<std::string, DefaultBson::Indexer*> IndexerMap;

        class Query {
            private:
                typedef std::set<std::string> OrSet;
                typedef std::map<std::string, OrSet> WhereMap;

                WhereMap m_where;
                std::set<std::string> m_select;
                std::string m_order;

                void executeFindIDs(const DefaultBson *bdata, std::set<unsigned int> &out) const;
            public:
                void addWhere(const std::string& a_prop, const std::string& a_val);
                void setSelect(const std::set<std::string>& a_keys);
                void setOrder(const std::string& a_key_name);
                pbnjson::JValue execute(void) const;
                bool executeEx(const DefaultBson *bdata, pbnjson::JValue outJsonArr) const;
                size_t countEx(const DefaultBson *bdata) const;
        };
    private:
        std::string m_bsonPath;
        const bson_t *m_bsonDoc;
        bson_reader_t *m_bsonDocReader;
        IndexerMap m_indexes;
        bool    m_loadCompleted;
        std::set<unsigned int> m_entireIDs;

        /**
         * A Cache Map contains ID:JSON-String.
         */
        mutable std::map<unsigned int, std::string> m_cacheIdJsonString;

        /**
         * A mutex for m_cacheIdJsonString
         */
        mutable std::mutex m_lockCacheIdJsonString;

        void loadDefaultSettingsFilesForEachApp();
        bool loadAndDoIndexing(std::string &path, bool isAppend = false);
        void storeIntoCache(unsigned int id, const std::string& jsonStr) const;
        const std::string hitFromCache(unsigned int id) const;

    protected:
        /**
         * Constructor for DefaultSettings, but NOT used now.
         *
         * This constructor is only for SharedInstance.
         * Do not call this constructor. Use instance()
         * This uses predefined IndexerMap(category, app_id, country)
         */
        DefaultBson(const std::string& bsonPath);

        /**
         * Maintain instance for DefaultSettings.
         * NOT used NOW. To use this, move it public.
         * @return [description]
         */
        static DefaultBson *instance();

    public:
        /**
         * Default constructor for Description(also PerApp),
         * some predefined maps.
         *
         * This is for loadWithIndexerMap with user selected bson file
         * and custom IndexerMap.
         *
         * @seealso buildDescriptionCacheBson
         * @seealso buildCategoryKeysMapBson
         * @seealso PrefsKeyDescMap::setDimensionKeyFromDefault
         */
        DefaultBson();

        ~DefaultBson();

        /**
         * Load DefaultSettings.
         *
         * @seealso DefaultBson::DefaultBson
         * @return  true if success
         */
        bool load(void);

        /**
         * Load specifiec bson file and do indexing.
         * Do not use this for DefaultSettings
         *
         * @seealso           DefaultBson::DefaultBson
         * @param  bsonPath   path to the bson file
         * @param  indexerMap custom IndexerMap
         * @return            true if success
         */
        bool loadWithIndexerMap(const std::string &bsonPath, IndexerMap indexerMap);

        /**
         * Read additional bson files into index and cache.
         *
         * @param  dirPath     path to the directory files exists
         * @param  filePattern filter for file list
         * @return             true if success.
         */
        bool loadAppendDirectory(const std::string &dirPath, const std::string &filePattern);

        /**
         * Read additional json files into index and cache.
         *
         * @param  dirPath     path to the directory files exists
         * @param  filePattern filter for file list
         * @return             true if success.
         */
        bool loadAppendDirectoryJson(const std::string &dirPath, const std::string &filePattern);

        /**
         * Search data without any index.
         * @return set of found IDs
         */
        void searchIDs(const std::string &a_key_name, const std::set<std::string>& valueSet, std::set<unsigned int>& a_result) const;
};

#endif
