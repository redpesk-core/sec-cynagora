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

set(LIBCORE_SOURCES
	anydb.c
	cyn.c
	db.c
	dbinit.c
	expire.c
	fbuf.c
	filedb.c
	memdb.c
	pollitem.c
	queue.c
)

set(SERVER_SOURCES
	agent-at.c
	main-cynarad.c
	prot.c
	rcyn-protocol.c
	rcyn-server.c
	socket.c
)

set(LIBCLI_SOURCES
	cache.c
	lib-compat.c
	prot.c
	rcyn-client.c
	rcyn-protocol.c
	socket.c
)

set(LIBCLI_SOURCES
	cache.c
	prot.c
	rcyn-client.c
	rcyn-protocol.c
	socket.c
)

set(LIBCOMPAT_SOURCES
	lib-compat.c
)

add_compile_definitions(_GNU_SOURCE)

###########################################
# build and install libcynara-core
###########################################
add_library(cynara-core SHARED ${LIBCORE_SOURCES})
set_target_properties(cynara-core PROPERTIES
	VERSION ${CYNARA_VERSION}
	SOVERSION ${CYNARA_SOVERSION})
target_link_libraries(cynara-core
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-cynara-core.map
	-Wl,--as-needed
	-Wl,--gc-sections
)
install(TARGETS cynara-core LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

###########################################
# build and install libcynara-client
###########################################
add_library(cynara-client SHARED ${LIBCLI_SOURCES})
target_compile_definitions(cynara-client PRIVATE
	RCYN_DEFAULT_SOCKET_DIR="${DEFAULT_SOCKET_DIR}"
)
set_target_properties(cynara-client PROPERTIES
	VERSION ${CYNARA_VERSION}
	SOVERSION ${CYNARA_SOVERSION})
target_link_options(cynara-client
	PRIVATE
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-cynara-client.map
	-Wl,--as-needed
	-Wl,--gc-sections
)
install(TARGETS cynara-client LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
install(FILES rcyn-client.h DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/cynara)

###########################################
# build and install libcynara-compat
###########################################
add_library(cynara-compat SHARED ${LIBCOMPAT_SOURCES})
set_target_properties(cynara-compat PROPERTIES
	VERSION ${CYNARA_VERSION}
	SOVERSION ${CYNARA_SOVERSION})
target_link_libraries(cynara-compat
	PRIVATE cynara-client
)
target_link_options(cynara-compat
	PRIVATE
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-cynara-compat.map
	-Wl,--as-needed
	-Wl,--gc-sections
)
install(TARGETS cynara-compat LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

###########################################
# build and install cynarad
###########################################
add_executable(cynarad ${SERVER_SOURCES})
target_compile_definitions(cynarad PRIVATE
	DEFAULT_DB_DIR="${DEFAULT_DB_DIR}"
	DEFAULT_SOCKET_DIR="${DEFAULT_SOCKET_DIR}"
	DEFAULT_INIT_FILE="${DEFAULT_INIT_FILE}"
	RCYN_DEFAULT_SOCKET_DIR="${DEFAULT_SOCKET_DIR}"
)
if(SYSTEMD)
	target_compile_definitions(cynarad PRIVATE WITH_SYSTEMD_ACTIVATION)
	target_link_libraries(cynarad ${libsystemd_LDFLAGS} ${libsystemd_LINK_LIBRARIES})
	target_include_directories(cynarad PRIVATE ${libsystemd_INCLUDE_DIRS})
	target_compile_options(cynarad PRIVATE ${libsystemd_CFLAGS})
endif()
target_link_libraries(cynarad cynara-core cap)
install(TARGETS cynarad
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

###########################################
# build and install cynadm
###########################################
add_executable(cynadm main-cynadm.c expire.c)
target_link_libraries(cynadm cynara-client)
install(TARGETS cynadm
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

###########################################
# build and install test-old-cynara
###########################################
add_executable(test-old-cynara main-test-old-cynara.c)
target_link_libraries(test-old-cynara cynara-compat)
install(TARGETS test-old-cynara
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

