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

pkgconf = configuration_data()
pkgconf.set('CYNARA_VERSION', cynara_version)
pkgconf.set('CMAKE_INSTALL_FULL_LIBDIR', get_option('libdir'))
pkgconf.set('CMAKE_INSTALL_FULL_INCLUDEDIR', get_option('includedir'))

configure_file(
  configuration: pkgconf,
  input: 'cynara-compat.pc.in', output: 'cynara-compat.pc',
  install: true, install_dir: get_option('libdir') + '/pkgconfig', install_mode: 'rw-r--r--'
)

foreach item : [ 'cynara-admin.pc',
             'cynara-agent.pc',
             'cynara-client-async.pc',
             'cynara-client.pc',
             'cynara-commons.pc',
             'cynara-creds-commons.pc',
             'cynara-creds-dbus.pc',
             'cynara-creds-gdbus.pc',
             'cynara-creds-self.pc',
             'cynara-creds-socket.pc',
             'cynara-monitor.pc',
             'cynara-plugin.pc',
             'cynara-session.pc' ]
  custom_target(item, install: true, output: item, install_dir: get_option('libdir') + '/pkgconfig',
	command: [ 'ln', '-sf', 'cynara-compat.pc', '@OUTPUT@' ])
endforeach

