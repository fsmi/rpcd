#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "config.h"

int config_parse(char* cfg_file){
	size_t line_size = 0;
	ssize_t status;
	char* line = NULL;

	FILE* source = fopen(cfg_file, "r");
	if(!source){
		fprintf(stderr, "Failed to open configuration file %s: %s\n", cfg_file, strerror(errno));
		return 1;
	}

	return 0;
}
