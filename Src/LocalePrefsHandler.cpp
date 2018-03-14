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

#include "LocalePrefsHandler.h"
#include "Logging.h"
#include "PrefsKeyDescMap.h"
#include "Utils.h"
#include "SettingsService.h"

static const char* s_defaultLocaleFile = "/etc/palm/locale.json";
static const char* s_custLocaleFile = "/usr/lib/luna/customization/locale.json";
static const char* s_defaultCountryFile = "/etc/palm/countryList.json";
static const char* s_custCountryFile = "/usr/lib/luna/customization/countryList.json";

LocalePrefsHandler::LocalePrefsHandler(LSHandle* service)
	: PrefsHandler(service)
{
	init();
}

std::list<std::string> LocalePrefsHandler::keys() const
{
	std::list<std::string> k;
	k.push_back("locale");
	k.push_back("country");
	return k;
}

bool LocalePrefsHandler::validateLocale(pbnjson::JValue value)
{
    // Do nothing currently.

	return true;
}

bool LocalePrefsHandler::validateCountry(pbnjson::JValue value)
{
    // Do nothing currently.

	return true;
}

bool LocalePrefsHandler::validate(const std::string& key, pbnjson::JValue value)
{
	if (key == "locale")
		return validateLocale(value);
	else if (key == "country")
		return validateCountry(value);

	return false;
}

void LocalePrefsHandler::valueChangedLocale(pbnjson::JValue value)
{
	// nothing to do
}

void LocalePrefsHandler::valueChangedCountry(pbnjson::JValue value)
{
	// nothing to do
}

void LocalePrefsHandler::valueChanged(const std::string& key, pbnjson::JValue value)
{
	// We will assume that the value has been validated
	if (key == "locale")
		valueChangedLocale(value);
	else if (key == "country")
		valueChangedCountry(value);
}

bool LocalePrefsHandler::isCountryContains(pbnjson::JValue a_countryObj, const std::string& a_country) const
{
    if (!a_countryObj.isArray()) {
        return false;
    }

    for (pbnjson::JValue it : a_countryObj.items())
    {
        if (it.isString())
        {
            const std::string& str = it.asString();
            if (a_country == str)
            {
                return true;
            }
        }
    }

    return false;
}

bool LocalePrefsHandler::isRegionsContains(pbnjson::JValue a_regionsObj, const std::string& a_region) const
{
    if (!a_regionsObj.isArray()) {
        return false;
    }

    for (pbnjson::JValue it : a_regionsObj.items())
    {
        if (it.isString())
        {
            const std::string& str = it.asString();
            if (a_region == (std::string("langSel")+str))
            {
                return true;
            }
        }
    }

    return false;
}

pbnjson::JValue LocalePrefsHandler::filterLocaleObject(const std::string& a_objectName, const std::string& a_region, const std::string& a_country) const
{
    bool includeAll = (a_region == "UNDEFINED");

    pbnjson::JValue array = m_localeObject[a_objectName];

	if (!array.isArray()) {
        return pbnjson::JValue();
    }

    pbnjson::JValue dstObj(pbnjson::Array());
	for (pbnjson::JValue it : array.items())
	{
		bool includeThis = false;

        if (a_objectName == "vkb") {
            pbnjson::JValue  includeAlways = it["includeAlways"];

            if (includeAlways.isBoolean())
            {
                includeThis = includeAlways.asBool();
            }

            if (isCountryContains(it["country"], a_country))
            {
                includeThis = true;
            }

            if (includeThis)
            {
                dstObj.append(it);
            }
        } else {
            pbnjson::JValue countryArray = it["countries"];
            if (!countryArray.isArray())
                continue;

            pbnjson::JValue newCountries(pbnjson::Array());
            for (pbnjson::JValue second_it : countryArray.items())
            {
                pbnjson::JValue regionsObj(second_it["regions"]);
                pbnjson::JValue countryObj(second_it["enableCountry"]);
                pbnjson::JValue includeAlways(second_it["includeAlways"]);
                bool includeThisCountry = false;

                if (includeAlways.isBoolean())
                {
                    includeThisCountry = includeAlways.asBool();
                }

                if ( isCountryContains(countryObj, a_country) )
                {
                    includeThisCountry = true;
                }

                if (includeThisCountry || isRegionsContains(regionsObj, a_region) || includeAll)
                {
                    includeThis = true;
                    newCountries.append(second_it);
                }
            }

            if (includeThis || includeAll)
            {
                pbnjson::JValue langObj = it.duplicate();
                langObj.put("countries", newCountries);
                dstObj.append(langObj);
            }
        }
    }

    return dstObj;
}

pbnjson::JValue LocalePrefsHandler::valuesForLocale()
{
    std::string region = PrefsKeyDescMap::instance()->getCountryGroupCode();
    std::string country = PrefsKeyDescMap::instance()->getCountryCode();

    //readCountryFile();
    std::list<std::string> keyList;
    {
        for(const pbnjson::JValue::KeyValue it : m_localeObject.children()) {
            keyList.push_back(it.first.asString());
        }
    }

    // Contains ONLY if it's current region.
    //
    pbnjson::JValue output(pbnjson::Object());

    for (const std::string& it : keyList)
    {
        pbnjson::JValue obj(filterLocaleObject(it, region, country));
        if (!obj.isNull())
        {
            output.put(it, obj);
        }
    }

    pbnjson::JValue clone(output);

    clone.remove("includeAlways");
    clone.remove("regions");
    clone.remove("country");

    clone.put("returnValue", true);

    return clone;
}

pbnjson::JValue LocalePrefsHandler::filterCountryObject(const std::string& a_region) const
{
    pbnjson::JValue array = m_countryObject["countryList"];

	if (!array.isArray()) {
        return pbnjson::JValue();
    }

    pbnjson::JValue dstObj(pbnjson::Array());
	for (pbnjson::JValue it : array.items())
	{
		pbnjson::JValue groupCodeObj = it["groupCode"];
        if (groupCodeObj.isString())
        {
            if (a_region == (std::string("langSel") + groupCodeObj.asString()) )
            {
                dstObj.append(it);
            }
        }
    }

    return dstObj;
}

pbnjson::JValue LocalePrefsHandler::valuesForCountry()
{
    std::string region = PrefsKeyDescMap::instance()->getCountryGroupCode();

    if (region == "UNDEFINED" || region == "others" || region == "all")
    {
        return m_countryObject;
    }

    pbnjson::JValue json(pbnjson::Object());
    pbnjson::JValue countryList = filterCountryObject(region);
    if (!countryList.isNull())
    {
	    json.put("countryList", countryList);
	}

    json.put("returnValue", true);

	return json;
}

pbnjson::JValue LocalePrefsHandler::valuesForKey(const std::string& key)
{
	if (key == "locale")
		return valuesForLocale();
	else if (key == "country")
		return valuesForCountry();
	else
		return pbnjson::Object();
}

void LocalePrefsHandler::init()
{
	readLocaleFile();
	readCountryFile();
}

void LocalePrefsHandler::readLocaleFile()
{
	// Read the locale file
    std::string jsonStr;
	if (!Utils::readFile(s_custLocaleFile, jsonStr))
		if (!Utils::readFile(s_defaultLocaleFile, jsonStr)) {
		    SSERVICELOG_ERROR(MSGID_LOCALE_FILES_LOAD_FAILED, 2, PMLOGKS("customization_locale_file_", s_custLocaleFile),
				  PMLOGKS("default_locale_file",s_defaultLocaleFile), "");
		    return;
	    }

	pbnjson::JValue root = pbnjson::JDomParser::fromString(jsonStr);
	if (root.isNull()) {
		SSERVICELOG_ERROR(MSGID_LOCALE_FILE_PARSE_ERR, 0, "Failed to parse locale file contents into json");
		return;
	}

	const pbnjson::JValue label(root["locale"]);
	if (label.isNull()) {
		SSERVICELOG_ERROR(MSGID_LOCALE_GET_ENTRY_FAILED, 0, "Failed to get locale entry from locale file");
		return;
	}

	m_localeObject = root;
}

void LocalePrefsHandler::readCountryFile()
{
	// Read the locale file
	std::string jsonStr;
	if (!Utils::readFile(s_custCountryFile, jsonStr))
		if (!Utils::readFile(s_defaultCountryFile, jsonStr)) {
		SSERVICELOG_ERROR(MSGID_COUNTRY_FILES_LOAD_FAILED, 2, PMLOGKS("customization_country_file",s_custCountryFile),
				  PMLOGKS("default_country_file",s_defaultCountryFile), "");
		return;
	}

	pbnjson::JValue root = pbnjson::JDomParser::fromString(jsonStr);
	if (root.isNull()) {
		SSERVICELOG_ERROR(MSGID_COUNTRY_FILE_PARSE_ERR, 0,"Failed to parse country file contents into json");
		return;
	}

    m_countryObject = root;
}

