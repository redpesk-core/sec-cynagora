Agent of cynagora
=================

Cynagora provide a mechanism called agent that allows to add logic of
autorization to cynagora. It can be used for example to query a user
to autorize or not a permission ponctually.

Cynagora server implements a predefined agent named the `at` agent that
implements a simple redirection of a query.

General principle
-----------------

Rules of the database have a RESULT. That result is either `yes`, `no` or
an agent query. An agent query is of the form:

	NAME:VALUE

where NAME is the name of the agent, VALUE is a value attached to the rule
and passed to the agent when querying it.

The colon between the NAME and the VALUE is mandatory.

The agent is queried to give a result with the following values:

	VALUE CLIENT SESSION USER PERMISSION

Example of the agent AT
-----------------------

The file `cynagora.initial` that provides a default initialisation file
has the following lines:

	*  *  @ADMIN  *  yes                forever
	*  *  0       *  @:%c;%s;@ADMIN;%p  forever

The first line defines a special user `@ADMIN` that always has the permission.
The special user can be seen as a group: the admin group. Remember that strings
of the database are conventionnal, that is that the meaning of the USER part
is conventionnal. A common convention is to use the decimal representation of
the UID of the unix account to check. That convention is used on the second
line. That second line defines that the user root (UID 0) is in the group
admin. To achieve that it uses the agent-AT mecanism.

So if no other rule was selected for the user `0` then cynagora find at least
the rule that requires to query the predefined agent `@` (AT) with the value
`%c;%s;@ADMIN;%p`.

The agent is asked with the following values:

  - `%c;%s;@ADMIN;%p`  the value
  - `CLIENT`, `SESSION`, `USER` and `PERMISSION`, the values of original request

The AT-agent use the value `%c;%s;@ADMIN;%p` to compose a check query.
it interpret the value as a semi-colon separated rule query of cynagora, in the
order: client, session, user, permission. Then it replaces any occurency of:

  - `%c` with value of `CLIENT` of original request
  - `%s` with value of `SESSION` of original request
  - `%u` with value of `USER` of original request
  - `%p` with value of `PERMISSION` of original request
  - `%%` with `%`
  - `%;` with `;`

So for the given value, the result at the end is the result of querying
cynagora for the result of:

  - client: %c that is substituted by CLIENT
  - session: %s that is substituted by SESSION
  - user: @ADMIN
  - permission: %p that is substituted by PERMISSION

The query to cynagora with CLIENT SESSION @ADMIN PERMMISSION must be done using
sub-query of agents.

