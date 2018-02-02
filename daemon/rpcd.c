#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/select.h>
#include <string.h>

#include "rpcd.h"
#include "config.h"

#include "child.h"
#include "x11.h"
#include "layout.h"
#include "api.h"
#include "control.h"

volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t pid_signaled = 0;
static volatile sig_atomic_t reload_requested = 3;

static void signal_handler(int signum){
	switch(signum){
		case SIGINT:
			shutdown_requested = 1;
			break;
		case SIGHUP:
			reload_requested = reload_requested ? 3 : 1; //1 -> requested, 2 -> acknowledged, 3 -> forced
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

static void cleanup_all(){
	api_cleanup();
	control_cleanup();
	child_cleanup();
	x11_cleanup();
	layout_cleanup();
	config_cleanup();
}

static int reload(char* file){
	size_t u = 0;

	switch(reload_requested){
		case 0:
			break;
		case 1:
		case 2:
			//check if any display is busy
			for(u = 0; u < x11_count(); u++){
				if(x11_get(u)->busy){
					if(reload_requested == 1){
						fprintf(stderr, "A display is currently busy, postponing configuration reload\n");
					}
					reload_requested = 2;
					return 0;
				}
			}
			//fall-thru
		case 3:
			fprintf(stderr, "%sReloading configuration file %s\n", (reload_requested == 3) ? "Force-":"", file);
			//clean up all modules
			cleanup_all();
			//run the configuration parse
			if(config_parse(file)){
				return 1;
			}
			reload_requested = 0;
			break;
	}
	return 0;
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

	if(reload(argv[1])){
		rv = usage(argv[0]);
		goto bail;
	}

	signal(SIGINT, signal_handler);
	signal(SIGCHLD, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGPIPE, SIG_IGN);
	FD_ZERO(&primary);

	fprintf(stderr, "%s now waiting for API clients\n", VERSION);
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

		//control loop after x11 loop due to initialization requirements within x11
		if(control_loop(&primary, &secondary, &max_fd)){
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
			if(child_reap()){
				goto bail;
			}
			pid_signaled = 0;
			//FIXME somehow, after this section, the listening fd is set in secondary
			//this causes the process to block on accept
			//resetting the active fd set works around this
			FD_ZERO(&secondary);
		}

		if(reload_requested){
			if(reload(argv[1])){
				cleanup_all();
			}
			//flush old module fds
			FD_ZERO(&secondary);
		}
		//swap descriptor sets
		primary = secondary;
	}

	rv = EXIT_SUCCESS;

bail:
	cleanup_all();
	return rv;
}
