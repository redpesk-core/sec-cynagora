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

coresrcs = files(
   'anydb.c',
   'cyn.c',
   'db.c',
   'dbinit.c',
   'expire.c',
   'fbuf.c',
   'filedb.c',
   'memdb.c',
   'names.c',
   'pollitem.c',
   'queue.c'
)

srvsrcs = files(
   'agent-at.c',
   'cyn-protocol.c',
   'cyn-server.c',
   'idgen.c',
   'main-cynagorad.c',
   'prot.c',
   'socket.c'
)

clisrcs = files(
   'cache.c',
   'cyn-protocol.c',
   'cynagora.c',
   'expire.c',
   'idgen.c',
   'names.c',
   'prot.c',
   'socket.c'
)

###########################################
# build and install libcynagora-core
###########################################
corelib = shared_library('cynagora-core', coresrcs,
   version: cynagora_version, soversion: cynagora_soversion,
   link_args: '-Wl,--version-script='+meson.current_source_dir()+'/export-cynagora-core.map',
   install: true
)

###########################################
# build and install libcynagora-client
###########################################
clilib = shared_library('cynagora', clisrcs,
   version: cynagora_version, soversion: cynagora_soversion,
   c_args: '-DDEFAULT_SOCKET_DIR="' + socketdir + '"',
   link_args: '-Wl,--version-script='+meson.current_source_dir()+'/export-cynagora.map',
   install: true
)
install_headers('cynagora.h')

###########################################
# build and install cynagorad
###########################################
executable('cynagorad', srvsrcs,
   c_args: [
      '-DDEFAULT_DB_DIR="' + dbdir + '"',
      '-DDEFAULT_SOCKET_DIR="' + socketdir + '"',
      '-DDEFAULT_INIT_FILE="' + init_file + '"',
      get_option('with-cynara-compat') ? '-DWITH_SYSTEMD_ACTIVATION' : '-DWITHOUT_SYSTEMD_ACTIVATION'
   ],
   dependencies: [ sysd, cap ],
   link_with: corelib,
   install: true
)

###########################################
# build and install cynagora-admin
###########################################
executable('cynagora-admin', 'main-cynagora-admin.c', 'expire.c', link_with: clilib, install: true)

###########################################
# build and install cynagora-agent
###########################################
executable('cynagora-agent', 'main-cynagora-agent.c', 'expire.c', link_with: clilib, install: true)

