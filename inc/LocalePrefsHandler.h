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

#ifndef LOCALEPREFSHANDLER_H
#define LOCALEPREFSHANDLER_H

#include "PrefsHandler.h"

class LocalePrefsHandler : public PrefsHandler
{
public:

	LocalePrefsHandler(LSHandle* service);

	virtual std::list<std::string> keys() const;
	virtual bool validate(const std::string& key, pbnjson::JValue value);
	virtual void valueChanged(const std::string& key, pbnjson::JValue value);
	virtual pbnjson::JValue valuesForKey(const std::string& key);

private:

	void init();
	void readLocaleFile();
	void readCountryFile();

	bool validateLocale(pbnjson::JValue value);
	bool validateCountry(pbnjson::JValue value);
	void valueChangedLocale(pbnjson::JValue value);
	void valueChangedCountry(pbnjson::JValue value);
	pbnjson::JValue valuesForLocale();
	pbnjson::JValue valuesForCountry();

    bool isRegionsContains(pbnjson::JValue a_regionsObj, const std::string& a_region) const;
    bool isCountryContains(pbnjson::JValue a_countryObj, const std::string& a_country) const;
    pbnjson::JValue filterLocaleObject(const std::string& a_objectName, const std::string& a_region, const std::string& a_country) const;
    pbnjson::JValue filterCountryObject(const std::string& a_region) const;

private:
    pbnjson::JValue m_localeObject;
    pbnjson::JValue m_countryObject;
};

#endif /* LOCALEPREFSHANDLER_H */
