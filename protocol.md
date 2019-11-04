The cynagora protocol
=====================

Introduction
------------

### Notations:

 - c->s:    from client to cynagora server
 - s->c:    from cynagora server to client
 - CACHEID: a 32 bits positive integer
 - ID:      a string
 - EXPIRE:  if missing, means: can cache forever
            if `-`, means: don't cache
            if TIMESPEC (see below), means: valid until given RELATIVE time
 - SEXPIRE: Same as EXPIRE but also allows TIMESPEC prefixed with '-', meaning
            valid until given relative time and don't cache

For TIMESPEC see notes.

Messages
--------

### hello

synopsis:

	c->s cynagora 1
	s->c done 1 CACHEID

The client present itself with the version of the protocol it expects to
speak (today version 1 only). The server answer yes with the acknoledged
version it will use and the CACHEID that identify the cache (see note on
CACHEID)

If hello is used, it must be the first message. If it is not used, the
protocol implicitely switch to the default version.


### invalidate cache

synopsis:

	s->c clear CACHEID

The server ask the client to clear its cache and to start the cache whose
identifier is CACHEID.

This is the responsibility of the client to clear its cache. It is also a
decision of the client to implement or not a cache. If the client implements
a cache, it must clear that cache when it receives that message from the
server or otherwise its decisions would be wrong.


### test a permission

synopsis:

	c->s test ID CLIENT SESSION USER PERMISSION
	s->c (ack|yes|no) ID [EXPIRE]

Check whether the permission is granted (yes) or not granted (no)
or undecidable without querying an agent (ack).

This query ensure that the response is fast because agent are allowed to
delay requests before emitting the final status. But it doesn't ensure that
the answer is a final status. Receiving `ack` means that no final decision
can be decided. In that case the correct resolution is either to act as if
`no` were received or to ask for a check with not null probability that the
reply will take time.


### check a permission

synopsis:

	c->s check ID CLIENT SESSION USER PERMISSION
	s->c (yes|no) ID [EXPIRE]

Check whether the permission is granted (yes) or not granted (no) and invoke
agent if needed.

Agents are allowed to query user, remote server or any long time processing
that may delay a lot the reply. So don't forget when using check that the
reply might take time.


### enter critical (admin)

synopsis:

	c->s enter
	s->c done

Start modifications (prior to set or drop).


### leave critical (admin)

synopsis:

	c->s leave [commit|rollback]
	s->c done|error ...

Terminate modifications and commit it (commit) or cancel it (rollback).


### erase (admin)

synopsis:

	c->s drop CLIENT SESSION USER PERMISSION
	s->c done|error ...

Drop the rule matching the given filter (see FILTER).


### set (admin)

synopsis:

	c->s set CLIENT SESSION USER PERMISSION VALUE [SEXPIRE]
	s->c done|error ...

Create the rule as given.


### list permissions (admin):

synopsis:

	c->s get CLIENT SESSION USER PERMISSION
	s->c item CLIENT SESSION USER PERMISSION VALUE [SEXPIRE]
	s->c ...
	s->c done

List the rules matching the given filter (see FILTER).


### logging set/get (admin)

synopsis:

	c->s log [on|off]
	s->c done (on|off)

Tell to log or not the queries or query the current state.

With an argument, it activates or deactivates logging. Without argument,
it does nothing.

In all cases, returns the logging state afterward.

Logging is a global feature. The protocol commands that the server sends or 
receives are printed to the journal or not.


### register agent (agent)

synopsis:

	c->s agent NAME
	s->c done|error ...

Register the agent of NAME (see AGENT-NAME). The name must be valid. The
name must not be already registered, it must be unique.


### ask agent (agent):

synopsis:

	s->c ask ASKID NAME VALUE CLIENT SESSION USER PERMISSION
	c->s reply ASKID (yes|no) [SEXPIRE]

The server ask the agent of `NAME` to handle the request to the rule
CLIENT SESSION USER PERMISSION and the agent VALUE.

The agent implementation must return its result with the given associated
ASKID. If the agent implementation has to check cynagora for replying to
an agent request, it must use sub-check requests.


### sub check (agent):

synopsis:

	c->s sub ASKID ID CLIENT SESSION USER PERMISSION
	s->c (yes|no) ID [EXPIRE]

Make a check in the context of an agent resolution. Same as `check` but
in the context of the agent query ASKID.


Notes
-----

### TIMESPEC

The TIMESPEC describe a number of seconds in the futur RELATIVE TO NOW.

It can be a simple decimal integer. I can also use letters to designate
year (letter `y`), week (letter `w`), day (letter `d`), hour (letter `h`),
minute (letter `m`), second (letter `s`).

It can also be one of the predefined value: `*`, `forever` or `always`, all
meaning endless.

Examples:

  - 15d    15 days
  - 1h     1 hour
  - 5m30s  5 minutes 30 seconds


### CACHEID

The cacheid identify the current cache. It changes each time the database
changes. After a disconnection, clients can use HELLO to check whether their
version of cache is still valid. This is implemented by the default C library.


### FILTER

The commands `drop` and `get` are taking rule's filters. A rule filter
is a rule that accept the special character `#` as a catch all match.

For examples, the rule filter `# X # #` macthes the rules that have the
session `X` for any client, any user and any permission.


### AGENT-NAME

Agent's name are valid if it only contains the following characters:

  - latin letters upper or lower case (distinguished): `a`..`z` or `A`..`Z`
  - digits: `0`..`9`
  - one of the characters `@`, `$`, `-`, `_`

It can not be longer than 255 characters.

The case count so the agent 'a' is not the agent 'A'.

