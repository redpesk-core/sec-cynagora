###########################################################################
# Copyright (C) 2018 "IoT.bzh"
#
# author: José Bollo <jose.bollo@iot.bzh>
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

cmake_minimum_required(VERSION 3.0)

project(cynagora
	DESCRIPTION "Secured permission database for applications"
	HOMEPAGE_URL "https://gerrit.automotivelinux.org/gerrit/gitweb?p=src/cynagora.git;a=summary"
	VERSION 2.0
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

set(DEFAULT_DB_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/cynagora"
	CACHE PATH "directory path of the database")
set(DEFAULT_SOCKET_DIR "${CMAKE_INSTALL_FULL_RUNSTATEDIR}/cynagora"
	CACHE PATH "directory path of the sockets")
set(DEFAULT_CONFIG_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/security"
	CACHE PATH "directory of configuration")
set(DEFAULT_INIT_FILE "${DEFAULT_CONFIG_DIR}/cynagora.initial")

###########################################################################

link_libraries(-Wl,--as-needed -Wl,--gc-sections -Wl,--no-undefined)

add_compile_options(-Wall -Wextra -Wconversion)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
add_compile_options(-Wno-sign-compare -Wno-sign-conversion)
add_compile_options(-Werror=maybe-uninitialized)
add_compile_options(-Werror=implicit-function-declaration)
add_compile_options(-ffunction-sections -fdata-sections)
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)

###########################################################################

if(WITH_SYSTEMD)
	PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)
	add_subdirectory(systemd)
endif()

add_subdirectory(src)
add_subdirectory(pkgconfig)

if(WITH_CYNARA_COMPAT)
	add_subdirectory(compat)
endif()

install(FILES
	${CMAKE_CURRENT_SOURCE_DIR}/cynagora.initial
	DESTINATION
	${DEFAULT_CONFIG_DIR}
)
