#include <stdio.h>
#include <stdlib.h>

#include "rpcd.h"
#include "config.h"

rpcd_config_t rpcd_config = {
	0
};

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


	return EXIT_SUCCESS;
}
