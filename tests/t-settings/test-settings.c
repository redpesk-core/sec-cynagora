

#include "../../src/settings.h"

#include <stdio.h>
#include <string.h>

char cyn_default_socket_dir[] = "<cyn_default_socket_dir>";


int main (int ac, char **av)
{
	settings_t s;
	int rc, i = 1;
	while (i < ac) {
		printf("\nreading %s\n-------\n", av[i]);
		initialize_default_settings(&s);
		rc = read_file_settings(&s, av[i]);
		printf("RC %d\n", rc);
		printf("makesockdir %s\n", s.makesockdir ? "yes" : "no");
		printf("makedbdir   %s\n", s.makedbdir ? "yes" : "no");
		printf("owndbdir    %s\n", s.owndbdir ? "yes" : "no");
		printf("ownsockdir  %s\n", s.ownsockdir ? "yes" : "no");
		printf("init        %s\n", s.init ?: "NULL");
		printf("dbdir       %s\n", s.dbdir ?: "NULL");
		printf("socketdir   %s\n", s.socketdir ?: "NULL");
		printf("user        %s\n", s.user ?: "NULL");
		printf("group       %s\n", s.group ?: "NULL");
		printf("\n");
		i++;
	}
	return 0;
}
