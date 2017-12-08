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
#include "api.h"

volatile sig_atomic_t shutdown_requested = 0;
volatile sig_atomic_t pid_signaled = 0;

static void signal_handler(int signum){
	switch(signum){
		case SIGINT:
			shutdown_requested = 1;
			break;
		case SIGCHLD:
			pid_signaled = 1;
			break;
	}
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
	signal(SIGCHLD, signal_handler);
	FD_ZERO(&primary);

	while(!shutdown_requested){
		FD_ZERO(&secondary);
		max_fd = -1;

		//handle events on the primary set, gather new fds on secondary
		if(api_loop(&primary, &secondary, &max_fd)){
			goto bail;
		}

		if(x11_loop(&primary, &secondary, &max_fd)){
			goto bail;
		}

		//select on secondary
		error = select(max_fd + 1, &secondary, NULL, NULL, NULL);
		if(error < 0){
			if(errno == EINTR){
				if(shutdown_requested){
					fprintf(stderr, "Exiting cleanly\n");
					break;
				}
			}
			else{
				fprintf(stderr, "select() failed: %s\n", strerror(errno));
				goto bail;
			}
		}

		if(pid_signaled){
			if(command_reap()){
				goto bail;
			}
			pid_signaled = 0;
			//FIXME somehow, after this section, the listening fd is set in secondary
			//this causes the process to block on accept
			//resetting the active fd set works around this
			FD_ZERO(&secondary);
		}

		//swap descriptor sets
		primary = secondary;
	}

	rv = EXIT_SUCCESS;

bail:
	api_cleanup();
	x11_cleanup();
	layout_cleanup();
	command_cleanup();
	return rv;
}
