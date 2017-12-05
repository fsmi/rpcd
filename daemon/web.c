#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "web.h"

static int listen_fd = -1;

int web_config(char* option, char* value){
	if(!strcmp(option, "bind")){
		//TODO open listener
		return 0;
	}

	fprintf(stderr, "Unknown option %s for web section\n", option);
	return 1;
}

int web_ok(){
	return (listen_fd < 0) ? 1 : 0;
}

void web_cleanup(){
	close(listen_fd);
	listen_fd = -1;
	//TODO disconnect clients, free data
}
