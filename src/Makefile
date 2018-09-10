.PHONY: all clean

srcssrv = db.c fbuf.c queue.c cyn.c prot.c rcyn-protocol.c socket.c

srcscli = prot.c rcyn-client.c rcyn-protocol.c lib-compat.c cache.c socket.c

incssrv = db.h fbuf.h cyn.h prot.h rcyn-protocol.h socket.h

incscli = prot.h rcyn-client.h rcyn-protocol.h cache.h socket.h

defs = -DRCYN_DEFAULT_CHECK_SOCKET_SPEC=\"tcp:localhost:5555\" \
	-DRCYN_DEFAULT_ADMIN_SOCKET_SPEC=\"tcp:localhost:4444\"

bins = cynarad test-cynara

all: $(bins)

clean:
	rm cynara.names cynara.rules $(bins) 2>/dev/null || true

cynarad: rcyn-server.c $(srcssrv) $(incssrv)
	gcc -o cynarad -g -Wall rcyn-server.c $(srcssrv) $(defs)

test-cynara: test-lib-compat.c  $(srcscli) $(incscli)
	gcc -o test-cynara -I../include -g -Wall test-lib-compat.c $(srcscli) $(defs)

