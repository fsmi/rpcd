#include "control.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static size_t nvars = 0;
static variable_t* vars = NULL;

static size_t nfds = 0;
static int* fds = NULL;

int control_config(char* option, char* value){
	if(!strcmp(option, "socket")){
		//TODO
		return 0;
	}
	else if(!strcmp(option, "fifo")){
		//TODO
		return 0;
	}

	fprintf(stderr, "Unknown option %s for control section\n", option);
	return 1;
}

int control_config_variable(char* name, char* value){
	//TODO
	return 0;
}

int control_config_automation(char* line){
	//TODO
	return 0;
}

int control_loop(fd_set* in, fd_set* out, int* max_fd){
	//TODO
	return 0;
}

int control_ok(){
	if(!nfds){
		fprintf(stderr, "No control inputs defined, continuing\n");
	}
	return 0;
}

void control_cleanup(){
	size_t u;

	for(u = 0; u < nvars; u++){
		free(vars[u].name);
		free(vars[u].value);
	}
	free(vars);
	vars = NULL;
	nvars = 0;

	for(u = 0; u < nfds; u++){
		if(fds[u] >= 0){
			close(fds[u]);
		}
	}
	free(fds);
	fds = NULL;
	nfds = 0;
}
