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

configure_file(cynara-compat.pc.in cynara-compat.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cynara-compat.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

foreach(item cynara-admin.pc 
             cynara-agent.pc 
             cynara-client-async.pc 
             cynara-client.pc 
             cynara-commons.pc 
             cynara-creds-commons.pc 
             cynara-creds-dbus.pc 
             cynara-creds-gdbus.pc 
             cynara-creds-self.pc 
             cynara-creds-socket.pc 
             cynara-monitor.pc
             cynara-plugin.pc
             cynara-session.pc) 
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/link-to-cynara-compat.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
         RENAME ${item})
endforeach(item)

