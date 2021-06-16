###########################################################################
# Copyright (C) 2018-2021 IoT.bzh Company
#
# Author: José Bollo <jose.bollo@iot.bzh>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###########################################################################

cmake_minimum_required(VERSION 3.12)

project(cynagora
	DESCRIPTION "Secured permission database for applications"
	HOMEPAGE_URL "https://github.com/redpesk-core/sec-cynagora"
	VERSION 2.2.1
	LANGUAGES C
)

include(FindPkgConfig)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(GNUInstallDirs)
include(CTest)

if(NOT CMAKE_INSTALL_FULL_RUNSTATEDIR)
	set(CMAKE_INSTALL_FULL_RUNSTATEDIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/run")
endif()

###########################################################################
# possible settings

set(CYNAGORA_VERSION ${PROJECT_VERSION})
set(CYNAGORA_SOVERSION ${PROJECT_VERSION_MAJOR})

option(WITH_SYSTEMD       "should include systemd compatibility" ON)
option(WITH_CYNARA_COMPAT "produce artifacts for compatibility with cynara" OFF)

set(USER  cynagora CACHE STRING "user of the daemon")
set(GROUP cynagora CACHE STRING "group of the daemon")

set(DEFAULT_DB_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/cynagora"
	CACHE PATH "directory path of the database")
set(DEFAULT_SOCKET_DIR "${CMAKE_INSTALL_FULL_RUNSTATEDIR}/cynagora"
	CACHE PATH "directory path of the sockets")
set(DEFAULT_INIT_FILE "${CMAKE_INSTALL_FULL_SYSCONFDIR}/security/cynagora.initial")

###########################################################################

link_libraries(-Wl,--as-needed -Wl,--gc-sections -Wl,--no-undefined)

add_compile_options(-Wall -Wextra -Wconversion)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
add_compile_options(-Werror=maybe-uninitialized)
add_compile_options(-Werror=implicit-function-declaration)
add_compile_options(-ffunction-sections -fdata-sections)
#add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)

add_compile_definitions(VERSION="${PROJECT_VERSION}")

###########################################################################

if(WITH_SYSTEMD)
	PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)
	add_subdirectory(systemd)
endif()

PKG_CHECK_MODULES(libcap REQUIRED libcap)

add_subdirectory(src)
add_subdirectory(pkgconfig)

if(WITH_CYNARA_COMPAT)
	add_subdirectory(compat)
endif()

get_filename_component(
	DEFAULT_INIT_FILE_DIRECTORY
	${DEFAULT_INIT_FILE}
	DIRECTORY
)
install(FILES
	${CMAKE_CURRENT_SOURCE_DIR}/cynagora.initial
	DESTINATION
	${DEFAULT_INIT_FILE_DIRECTORY}
)
install(DIRECTORY DESTINATION ${DEFAULT_DB_DIR})
