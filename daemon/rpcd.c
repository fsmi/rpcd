#include <stdio.h>
#include <stdlib.h>

#include "config.h"

int usage(char* fn){
	return EXIT_FAILURE;
}

int main(int argc, char** argv){
	if(argc < 1){
		fprintf(stderr, "No config file\n");
		return usage(argv[0]);
	}

	if(parse_config(argv[1])){
		return usage(argv[0]);
	}



	return EXIT_SUCCESS;
}
