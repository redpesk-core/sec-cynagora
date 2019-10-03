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

project(cynara C)

set(PROJECT_NAME "Cynara")
set(PROJECT_PRETTY_NAME "Permission database")
set(PROJECT_DESCRIPTION "Secured permission database for applications")
set(PROJECT_VERSION "1.99.99")
set(PROJECT_URL "https://gerrit.automotivelinux.org/gerrit/gitweb?p=src/cynara.git;a=summary")

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

set(CYNARA_VERSION ${PROJECT_VERSION})
set(CYNARA_SOVERSION 1.99)

add_definitions(-DCYNARA_VERSION="${CYNARA_VERSION}")

set(SYSTEMD ON CACHE BOOL "should use systemd")
set(DEFAULT_DB_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/cynara"
	CACHE PATH "directory path of the database")
set(DEFAULT_SOCKET_DIR "${CMAKE_INSTALL_FULL_RUNSTATEDIR}/cynara"
	CACHE PATH "directory path of the sockets")
set(DEFAULT_CONFIG_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/security"
	CACHE PATH "directory of configuration")
set(DEFAULT_INIT_FILE "${DEFAULT_CONFIG_DIR}/cynara.initial")

###########################################################################

include_directories(include)
link_libraries(-Wl,--as-needed -Wl,--gc-sections -Wl,--no-undefined)

add_compile_options(-Wall -Wextra -Wconversion)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
add_compile_options(-Wno-sign-compare -Wno-sign-conversion)
add_compile_options(-Werror=maybe-uninitialized)
add_compile_options(-Werror=implicit-function-declaration)
add_compile_options(-ffunction-sections -fdata-sections)
add_compile_options(-fPIC)
add_compile_options(-g)
add_compile_options(-fstack-protector -D_FORTIFY_SOURCE=2 -O2)
add_compile_options(-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.)

set(CMAKE_C_FLAGS_PROFILING    "-g -O2 -pg")
set(CMAKE_C_FLAGS_DEBUG        "-g -ggdb --fstack-protector -D_FORTIFY_SOURCE=2")
set(CMAKE_C_FLAGS_RELEASE      "-g -O2")
set(CMAKE_C_FLAGS_CCOV         "-g -O2 --coverage")

###########################################################################

if(SYSTEMD)
	PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)
	add_subdirectory(systemd)
endif()

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(pkgconfig)

install(FILES
	${CMAKE_CURRENT_SOURCE_DIR}/cynara.initial
	DESTINATION
	${DEFAULT_CONFIG_DIR}
)
