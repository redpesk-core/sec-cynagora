<pre>
   ____  __   __  _   _      _       ____    ___    ____       _    
  / ___| \ \ / / | \ | |    / \     / ___|  / _ \  |  _ \     / \   
 | |      \ V /  |  \| |   / _ \   | |  _  | | | | | |_) |   / _ \  
 | |___    | |   | |\  |  / ___ \  | |_| | | |_| | |  _ <   / ___ \ 
  \____|   |_|   |_| \_| /_/   \_\  \____|  \___/  |_| \_\ /_/   \_\

</pre>

# Cynagora

[Cynagora][1] is fast, simple and safe permission database
service.
Functions of *cynagora* are:
 * checking access for certain permission
 * holding permission database
 * simple, single function API - for checking permissions
 * ability to use external agent (in case of policies that can't be full
   processed in *cynagora* and plugins)

## Basics

*Cynagora* delivers permissions based on 4 keys: CLIENT, SESSION, USER,
PERMISSION.

The original principle is that a server ask cynagora if a permission
(the key PERMISSION) is granted for a client identified by:

 - its Smack label (the key CLIENT)
 - its user identifier, uid (the key USER)
 - its process identifier, pid (the key SESSION)

In facts, the keys can be used with other values that the one primarily
designed. For example, using the pid for the session is not safe. So it
can be replaced with a string really identifying a session.

The database of *cynagora* is made of rules that set permissions 

Cynagora implements handles differently the rules targeting any sessions
and the rules targeting specific sessions. 

## API Overview

CYNAGORA comes with 2 APIs:

 - a protocol API that can be easily implemented in most languages
   (see src/cynagora-protocol.txt)

 - a client C library (see src/cynagora.h)

It also provide optionally for compatibility a subset of the C client libraries.

## History

Cynagora is a refit of [cynara][2] that allows inclusion of expirations.
It implements the same permission database by principle but the details
changes.

# Compiling

The compilation use the build system *cmake*. Cynagora has no dependencies.
However, it can be built for using it with systemd activation. In that
case it requires _lisystemd_.

Example for compiling and installing cynagora:

	mkdir build
	cd build
	cmake ..
	make install

Options to pass to cmake:

 - *WITH_SYSTEMD*: flag for generating systemd compatible units (default ON)

 - *WITH_CYNARA_COMPAT*: flag for producing cynara compatibility artifacts
   (default OFF)

 - *DEFAULT_DB_DIR*: path of the directory for the database (default
   ${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/cynagora)

 - *DEFAULT_SOCKET_DIR*: directory path of the sockets (default 
   ${CMAKE_INSTALL_FULL_RUNSTATEDIR}/cynagora)

 - *DEFAULT_INIT_FILE*: path to the initialization file to use (default 
   ${CMAKE_INSTALL_FULL_SYSCONFDIR}/security/cynagora.initial)

Example:

	cmake -DCMAKE_INSTALL_PREFIX=~/.local -DWITH_SYSTEMD=OFF ..

# License

Cynagora is licensed under a Apache License Version 2.0, January 2004.
Available on Apache [website][3] or in LICENSE file.

[1]: https://git.automotivelinux.org/src/cynagora/
[2]: https://wiki.tizen.org/wiki/Security:Cynara
[3]: https://www.apache.org/licenses/LICENSE-2.0
