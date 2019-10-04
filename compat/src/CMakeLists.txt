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

option(DIRECT_CYNARA_COMPAT "cynara compat access server directly without using libcynagora" OFF)

add_compile_definitions(_GNU_SOURCE)

###########################################
# build and install libcynara-compat
###########################################
if(DIRECT_CYNARA_COMPAT)
	add_library(cynara-compat SHARED lib-compat.c $<TARGET_OBJECTS:client-objects>)
	target_include_directories(cynara-compat PRIVATE ../../src)
else()
	add_library(cynara-compat SHARED lib-compat.c)
	target_link_libraries(cynara-compat PRIVATE cynagora)
endif()
target_include_directories(cynara-compat PUBLIC ../include)
set_target_properties(cynara-compat
	PROPERTIES
		VERSION ${CYNARA_VERSION}
		SOVERSION ${CYNARA_SOVERSION}
		LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-cynara-compat.map
)
install(TARGETS cynara-compat LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

###########################################
# build and install test-old-cynara
###########################################
add_executable(test-old-cynara main-test-old-cynara.c)
target_link_libraries(test-old-cynara cynara-compat)
install(TARGETS test-old-cynara
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
