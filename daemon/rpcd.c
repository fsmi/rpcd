#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/select.h>
#include <string.h>

#include "rpcd.h"
#include "config.h"

#include "command.h"
#include "layout.h"
#include "x11.h"
#include "web.h"

volatile sig_atomic_t shutdown_requested = 0;

static void signal_handler(int signum){
	shutdown_requested = 1;
}

static int usage(char* fn){
	fprintf(stderr, "\n%s - Provide a minimal controller API for ratpoison via HTTP\n", VERSION);
	fprintf(stderr, "Usage:\n\t%s configfile\n", fn);
	return EXIT_FAILURE;
}

int main(int argc, char** argv){
	int rv = EXIT_FAILURE;

	fd_set primary, secondary;
	int max_fd, error;

	if(argc < 2){
		fprintf(stderr, "No configuration provided\n");
		rv = usage(argv[0]);
		goto bail;
	}

	if(config_parse(argv[1])){
		rv = usage(argv[0]);
		goto bail;
	}

	signal(SIGINT, signal_handler);
	FD_ZERO(&primary);

	while(!shutdown_requested){
		FD_ZERO(&secondary);
		max_fd = -1;

		//handle events on the primary set, gather new fds on secondary
		if(web_loop(&primary, &secondary, &max_fd)){
			goto bail;
		}

		if(x11_loop(&primary, &secondary, &max_fd)){
			goto bail;
		}

		//select on secondary
		error = select(max_fd + 1, &secondary, NULL, NULL, NULL);
		if(error < 0){
			fprintf(stderr, "select() returned: %s\n", strerror(errno));
			break;
		}

		//swap descriptor sets
		primary = secondary;
	}

	rv = EXIT_SUCCESS;

bail:
	web_cleanup();
	x11_cleanup();
	layout_cleanup();
	command_cleanup();
	return rv;
}
