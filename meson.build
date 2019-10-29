###########################################################################
# Copyright (C) 2019 "IoT.bzh"
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

project('cynagora', 'c', version: '2.0', license: 'Apache-2.0')

###########################################################################

cynagora_version = meson.project_version()
cynagora_soversion = '2'

runstatedir = get_option('localstatedir') + '/run'
dbdir = get_option('localstatedir') + '/lib/cynagora'
socketdir = runstatedir + '/cynagora'
init_file_dir = get_option('sysconfdir') + '/security'
init_file = init_file_dir + '/cynagora.initial'

###########################################################################

add_project_arguments(
   '-Wall',
   '-Wextra',
   '-Wconversion',
   '-Wno-unused-parameter',
   '-Werror=maybe-uninitialized',
   '-Werror=implicit-function-declaration',
   '-ffunction-sections',
   '-fdata-sections',
   '-D_GNU_SOURCE',
   language: 'c'
)

add_project_link_arguments(
   '-Wl,--as-needed',
   '-Wl,--gc-sections',
   '-Wl,--no-undefined',
   language: 'c'
)

###########################################################################

if get_option('with-systemd')
   sysd = dependency('libsystemd', required: true, version: '>=222')
else
   sysd = dependency('', required: false )
endif
cap = dependency('libcap', required: true)

###########################################################################

subdir('src')
subdir('pkgconfig')

if get_option('with-systemd')
   subdir('systemd')
endif

if get_option('with-cynara-compat')
   subdir('compat')
endif

install_data('cynagora.initial', install_dir: init_file_dir, install_mode: 'rw-r--r--')

