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

set(SERVER_SOURCES
	agent-at.c
	anydb.c
	cyn.c
	db.c
	dbinit.c
	expire.c
	fbuf.c
	filedb.c
	main-cynarad.c
	memdb.c
	pollitem.c
	prot.c
	queue.c
	rcyn-protocol.c
	rcyn-server.c
	socket.c
)

set(LIB_SOURCES
	cache.c
	lib-compat.c
	prot.c
	rcyn-client.c
	rcyn-protocol.c
	socket.c
)

add_compile_definitions(_GNU_SOURCE)

###########################################
# build and install libcynara
###########################################
ADD_LIBRARY(cynara SHARED ${LIB_SOURCES})
target_compile_definitions(cynara PRIVATE
	RCYN_DEFAULT_CHECK_SOCKET_SPEC="unix:${DEFAULT_SOCKET_DIR}/cynara.check"
	RCYN_DEFAULT_ADMIN_SOCKET_SPEC="unix:${DEFAULT_SOCKET_DIR}/cynara.admin"
)
SET_TARGET_PROPERTIES(cynara PROPERTIES
	VERSION ${CYNARA_VERSION}
	SOVERSION ${CYNARA_SOVERSION})
TARGET_LINK_LIBRARIES(cynara
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map
	-Wl,--as-needed
	-Wl,--gc-sections
)
INSTALL(TARGETS cynara LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
INSTALL(FILES rcyn-client.h DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/cynara)

###########################################
# build and install cynarad
###########################################
add_executable(cynarad ${SERVER_SOURCES})
target_compile_definitions(cynarad PRIVATE
	DEFAULT_DB_DIR="${DEFAULT_DB_DIR}"
	DEFAULT_SOCKET_DIR="${DEFAULT_SOCKET_DIR}"
	DEFAULT_INIT_FILE="${DEFAULT_INIT_FILE}"
	RCYN_DEFAULT_CHECK_SOCKET_SPEC="unix:${DEFAULT_SOCKET_DIR}/cynara.check"
	RCYN_DEFAULT_ADMIN_SOCKET_SPEC="unix:${DEFAULT_SOCKET_DIR}/cynara.admin"
)
target_link_libraries(cynarad cap)
install(TARGETS cynarad
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

###########################################
# build and install cynadm
###########################################
ADD_EXECUTABLE(cynadm main-cynadm.c expire.c)
TARGET_LINK_LIBRARIES(cynadm cynara)
INSTALL(TARGETS cynadm
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

###########################################
# build and install test-old-cynara
###########################################
ADD_EXECUTABLE(test-old-cynara main-test-old-cynara.c)
TARGET_LINK_LIBRARIES(test-old-cynara cynara)
INSTALL(TARGETS test-old-cynara
        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

###########################################
# alterations for SYSTEMD and socket specs
###########################################

if(SYSTEMD)
   target_compile_definitions(cynarad PRIVATE WITH_SYSTEMD_ACTIVATION)
   target_link_libraries(cynarad ${libsystemd_LDFLAGS} ${libsystemd_LINK_LIBRARIES})
   target_include_directories(cynarad PRIVATE ${libsystemd_INCLUDE_DIRS})
   target_compile_options(cynarad PRIVATE ${libsystemd_CFLAGS})
endif()

