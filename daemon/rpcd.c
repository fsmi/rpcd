#include <stdio.h>
#include <stdlib.h>

#include "rpcd.h"
#include "config.h"

#include "command.h"
#include "layout.h"
#include "x11.h"
#include "web.h"

volatile sig_atomic_t shutdown_requested = 0;

static int usage(char* fn){
	fprintf(stderr, "%s - Provide a minimal controller API for ratpoison via HTTP\n", VERSION);
	fprintf(stderr, "Usage:\n\t%s configfile\n", fn);
	return EXIT_FAILURE;
}

int main(int argc, char** argv){
	if(argc < 2){
		fprintf(stderr, "No configuration provided\n\n");
		return usage(argv[0]);
	}

	if(config_parse(argv[1])){
		return usage(argv[0]);
	}

	//x11_open
	//api_open
	
	while(!shutdown_requested){
		//api_handle
	}

	web_cleanup();
	x11_cleanup();
	layout_cleanup();
	command_cleanup();
	return EXIT_SUCCESS;
}
