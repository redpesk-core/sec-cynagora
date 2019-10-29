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

sysdconf = configuration_data()
sysdconf.set('DEFAULT_SOCKET_DIR', socketdir )

admin = configure_file( input: 'cynagora-admin.socket.in', output: 'cynagora-admin.socket', configuration: sysdconf)
check = configure_file( input: 'cynagora-check.socket.in', output: 'cynagora-check.socket', configuration: sysdconf)
agent = configure_file( input: 'cynagora-agent.socket.in', output: 'cynagora-agent.socket', configuration: sysdconf)

unitdir = get_option('libdir') + '/systemd/system'

install_data(
   'cynagora.service',
   'cynagora.target',
   admin,
   check,
   agent,
   install_dir: unitdir,
   install_mode: 'rw-r--r--'
)

install_subdir(
   'sockets.target.wants',
   install_dir: unitdir,
   install_mode: 'rw-r--r--'
)

