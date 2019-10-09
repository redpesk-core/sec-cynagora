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
	main-cynagorad.c
	prot.c
	cyn-protocol.c
	cyn-server.c
	socket.c
)

set(LIBCLI_SOURCES
	cache.c
	prot.c
	cynagora.c
	cyn-protocol.c
	socket.c
)

add_compile_definitions(_GNU_SOURCE)

###########################################
# build and install libcynagora-core
###########################################
add_library(cynagora-core SHARED ${LIBCORE_SOURCES})
target_include_directories(cynagora-core PUBLIC .)
set_target_properties(cynagora-core PROPERTIES
	VERSION ${CYNAGORA_VERSION}
	SOVERSION ${CYNAGORA_SOVERSION}
	LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-cynagora-core.map
)
install(TARGETS cynagora-core LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

###########################################
# build client-objects
###########################################
add_library(client-objects OBJECT ${LIBCLI_SOURCES})
target_compile_definitions(client-objects PRIVATE
	DEFAULT_SOCKET_DIR="${DEFAULT_SOCKET_DIR}"
)
target_compile_options(client-objects PRIVATE -fPIC)

###########################################
# build and install libcynagora-client
###########################################
add_library(cynagora SHARED $<TARGET_OBJECTS:client-objects>)
set_target_properties(cynagora PROPERTIES
	VERSION ${CYNAGORA_VERSION}
	SOVERSION ${CYNAGORA_SOVERSION}
	LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-cynagora.map
)
install(TARGETS cynagora LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
install(FILES cynagora.h DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR})

###########################################
# build and install cynagorad
###########################################
add_executable(cynagorad ${SERVER_SOURCES})
target_compile_definitions(cynagorad PRIVATE
	DEFAULT_DB_DIR="${DEFAULT_DB_DIR}"
	DEFAULT_SOCKET_DIR="${DEFAULT_SOCKET_DIR}"
	DEFAULT_INIT_FILE="${DEFAULT_INIT_FILE}"
)
if(WITH_SYSTEMD)
	target_compile_definitions(cynagorad PRIVATE WITH_SYSTEMD_ACTIVATION)
	target_link_libraries(cynagorad ${libsystemd_LDFLAGS} ${libsystemd_LINK_LIBRARIES})
	target_include_directories(cynagorad PRIVATE ${libsystemd_INCLUDE_DIRS})
	target_compile_options(cynagorad PRIVATE ${libsystemd_CFLAGS})
endif()
target_link_libraries(cynagorad cynagora-core cap)
install(TARGETS cynagorad
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

###########################################
# build and install cynagoracli
###########################################
add_executable(cynagoracli main-cynagoracli.c expire.c)
target_link_libraries(cynagoracli cynagora)
install(TARGETS cynagoracli
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

