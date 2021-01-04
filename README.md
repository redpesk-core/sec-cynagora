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

The database of *cynagora* is made of rules. Each cynagora rule is
a tuple of five strings and one integer:

    (CLIENT, SESSION, USER, PERMISSION, RESULT, EXPIRE).

The strings `CLIENT`, `SESSION`, `USER`, `PERMISSION` are arbitrary.
They can also have the special value `*` (STAR) that means that the rule
matches any value. Otherwise, the rule matches a query only if the
value matches the string of the rule. That match is:

  - case sensitive for CLIENT, SESSION, USER
  - case insensitive for PERMISSION

The string RESULT has basically one of the two values `yes` or `no`. It can
also be an agent item that will imply a request to an existing agent (see
file agent.md for details on agents).

When more than one rule match the query, only one is selected to apply using
the following rules:

  1. the rules that matched with the less STAR as possible are selected (it
     means that selected rules must matche more precisely the request)
  2. then from the rules selected above the rule that matches more exactly
     the keys in the following order of priority: session, user, client,
     permission

Cynagora implements handles differently the rules targeting any sessions
and the rules targeting specific sessions. The rules that have SESSION equals
to `*` are stored persistentely in the filesystem. That rule whose SESSION
is not STAR are volatile and only reside in memory.

Expiration is a 64 bits signed integer that express the date of expiration
of the rule in epoch (number of seconds since 1 January 1970). The special
value 0 means no expiration, permanent rule. The negative values are used
to avoid caching, their expiration value is given by the formula `-(1 + x)`.

Cynagora allows tiers programs to add features through the agent mechanism.
The file `agent.md` explains it more in detail.

## API Overview

CYNAGORA comes with 2 APIs:

 - a protocol API that can be easily implemented in most languages
   (see `protocol.md`)

 - a client C library (see `src/cynagora.h`)

It also provide optionally for compatibility a subset of the C client libraries.

## History

Cynagora is a refit of [cynara][2] that allows inclusion of expirations.
It implements the same permission database by principle but the details
changes.

As a possible replacement, cynagora can supply a simple compatibility
library that offers light legacy API of cynara. This would allow to run
simple cynara clients (admin/check/async-check but not agents) without
changes.

# Compiling

Cynagora is written in language C.

Cynagora only depends of _libcap_ that is used by the cynagora server.

The server can be built for using systemd socket activation. In that
case it requires _lisystemd_.

## Compiling with cmake and make

The compilation use the build system *cmake*. 

Example for compiling and installing cynagora:

	mkdir build
	cd build
	cmake ..
	make install

Options to pass to cmake:

 - *WITH_SYSTEMD*: flag for generating systemd compatible units (default ON)

 - *DEFAULT_DB_DIR*: path of the directory for the database (default
   ${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/cynagora)

 - *DEFAULT_SOCKET_DIR*: directory path of the sockets (default 
   ${CMAKE_INSTALL_FULL_RUNSTATEDIR}/cynagora)

 - *DEFAULT_INIT_FILE*: path to the initialization file to use (default 
   ${CMAKE_INSTALL_FULL_SYSCONFDIR}/security/cynagora.initial)

 - *WITH_CYNARA_COMPAT*: flag for producing cynara compatibility artifacts
   (default OFF)

 - *DIRECT_CYNARA_COMPAT*: if true, dont use the shared client library to
   access cynara server but use the static library instead, avoid a dependency
   to the shared library.

Example:

	cmake -DCMAKE_INSTALL_PREFIX=~/.local -DWITH_SYSTEMD=OFF ..
	make install


# Licenses

Cynagora is licensed under a Apache License Version 2.0, January 2004,
available on [Apache website][3] or in Apache-2.0 file.

Logo is licensed under Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0),
avaliable on [creative commons website][4] or in CC-BY-SA-3.0 file.

[1]: https://git.automotivelinux.org/src/cynagora/
[2]: https://wiki.tizen.org/wiki/Security:Cynara
[3]: https://www.apache.org/licenses/LICENSE-2.0
[4]: https://creativecommons.org/licenses/by-sa/3.0/
