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

CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

PROJECT(cynara C)

SET(PROJECT_NAME "Cynara")
SET(PROJECT_PRETTY_NAME "Permission database")
SET(PROJECT_DESCRIPTION "Secured permission database for applications")
SET(PROJECT_VERSION "1.99.RC1")
set(PROJECT_URL "https://gerrit.automotivelinux.org/gerrit/gitweb?p=src/cynara.git;a=summary")

INCLUDE(FindPkgConfig)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(GNUInstallDirs)
INCLUDE(CTest)

###########################################################################
# possible settings

set(CYNARA_VERSION ${PROJECT_VERSION})
set(CYNARA_SOVERSION 1.99)

add_definitions(-DCYNARA_VERSION="${CYNARA_VERSION}")

set(SYSTEMD ON CACHE BOOL "should use systemd")

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

set(CMAKE_C_FLAGS_PROFILING    "-g -O2 -pg -Wp,-U_FORTIFY_SOURCE")
set(CMAKE_C_FLAGS_DEBUG        "-g -ggdb -Wp,-U_FORTIFY_SOURCE")
set(CMAKE_C_FLAGS_RELEASE      "-g -O2")
set(CMAKE_C_FLAGS_CCOV         "-g -O2 --coverage")

###########################################################################

if(SYSTEMD)
   PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)
endif()

if(SYSTEMD)
   set(SOCKET_DIR "/run/platform"
                  CACHE PATH "path of the socket system directories")
   set(SYSTEMD_UNIT_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/systemd/system"
                  CACHE PATH "Path to systemd system unit files")
   set(CHECK_SOCKET_SPEC "unix:${SOCKET_DIR}/cynara.check")
   set(ADMIN_SOCKET_SPEC "unix:${SOCKET_DIR}/cynara.admin")
   add_subdirectory(systemd)
endif()
add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(pkgconfig)

