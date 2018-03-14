# Copyright (c) 2013-2018 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
# LICENSE@@@

cmake_minimum_required(VERSION 2.8.7)

project(SettingsService CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC3)
webos_component(1 0 22)

include(FindPkgConfig)

# -- check for glib 2.0
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

# -- check for openssl
pkg_check_modules(OPENSSL REQUIRED openssl)
webos_add_compiler_flags(ALL ${OPENSSL_CFLAGS})

# -- check for gthread 2.0
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
webos_add_compiler_flags(ALL ${GTHREAD2_CFLAGS})

# -- check for libbson
pkg_check_modules(LIBBSON REQUIRED libbson-1.0)
webos_add_compiler_flags(ALL ${LIBBSON_CFLAGS})

# -- check for pbnjson c and cpp libraries
pkg_check_modules(PBNJSON_C REQUIRED pbnjson_c)
webos_add_compiler_flags(ALL ${PBNJSON_C_CFLAGS})

pkg_check_modules(PBNJSON_CPP REQUIRED pbnjson_cpp)
webos_add_compiler_flags(ALL ${PBNJSON_CPP_CFLAGS})

# -- check for LS2 (internally depends on pmloglib)
pkg_check_modules(LS2 REQUIRED luna-service2)
webos_add_compiler_flags(ALL ${LS2_CFLAGS})

pkg_check_modules(PMLOG REQUIRED PmLogLib)
include_directories(${PMLOG_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMLOG_CFLAGS_OTHER})

webos_config_build_doxygen(doc Doxyfile)

webos_include_install_paths()

# -- no way to disable warn_unused_result right now.
webos_add_compiler_flags(ALL -g -O3 -Wall -fno-rtti -fPIC -std=c++11)
webos_add_linker_options(ALL --no-undefined)

#Uncomment the following line to enable trace log outputs
#add_definitions(-DPMLOG_TRACES_ENABLED -DPMLOG_TRACE_COMPONENT="settings_service")

configure_file(${CMAKE_SOURCE_DIR}/conf/SettingsService.h.in ${CMAKE_BINARY_DIR}/inc/SettingsService.h)
include_directories(${CMAKE_BINARY_DIR}/inc)
include_directories(inc)

file(GLOB HEADER_FILES inc/*.h)
file(GLOB SOURCE_FILES Src/*.cpp)
set(ALL_SOURCES ${SOURCE_FILES} ${HEADER_FILES})

# avoid double Main.cpp source rebuild, because it breaks coverage data consistency
# there is no need to build Main.cpp with library sources, so it can be safely removed from library sources list
list(REMOVE_ITEM SOURCE_FILES "${CMAKE_SOURCE_DIR}/Src/Main.cpp")

add_library(SettingsServiceCore ${SOURCE_FILES})

add_executable(SettingsService Src/Main.cpp)
target_link_libraries(SettingsService
                      SettingsServiceCore
                      ${PMLOG_LDFLAGS}
                      ${GLIB2_LDFLAGS}
                      ${OPENSSL_LDFLAGS}
                      ${GTHREAD2_LDFLAGS}
                      ${LIBBSON_LDFLAGS}
                      ${PBNJSON_C_LDFLAGS}
                      ${PBNJSON_CPP_LDFLAGS}
                      ${LS2_LDFLAGS}
                      )

webos_build_system_bus_files()
webos_build_daemon(LAUNCH files/launch/)

file(GLOB_RECURSE DB8_KIND_FILES files/db8/kinds/*)
file(GLOB_RECURSE DB8_PERM_FILES files/db8/permissions/*)
install(FILES ${DB8_KIND_FILES} DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/settings/kinds)
install(FILES ${DB8_PERM_FILES} DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/settings/permissions)
install(FILES ${DB8_KIND_FILES} DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/db/kinds)
install(FILES ${DB8_PERM_FILES} DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/db/permissions)

string(TOUPPER ${CMAKE_BUILD_TYPE} _build_type)
if (_build_type STREQUAL DEBUG)
  webos_add_compiler_flags(ALL -Werror)
endif()

