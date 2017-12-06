#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "layout.h"
#include "command.h"
#include "web.h"
#include "x11.h"
#include "config.h"

static enum {
	conf_none,
	conf_web,
	conf_x11,
	conf_layout,
	conf_command
} config_state = conf_none;

static char* config_trim_line(char* in){
	ssize_t u;
	//trim front
	for(; *in && !isgraph(*in); in++){
	}

	//trim back
	for(u = strlen(in); u >= 0 && !isgraph(in[u]); u--){
		in[u] = 0;
	}

	return in;
}

int config_parse(char* cfg_file){
	int rv = 0;
	size_t line_size = 0;
	ssize_t status;
	char* line_raw = NULL, *line = NULL, *argument = NULL;
	size_t line_no = 1;

	FILE* source = fopen(cfg_file, "r");
	if(!source){
		fprintf(stderr, "Failed to open configuration file %s: %s\n", cfg_file, strerror(errno));
		rv = 1;
		goto bail;
	}

	//read config file lines
	for(status = getline(&line_raw, &line_size, source); status >= 0; status = getline(&line_raw, &line_size, source)){
		line = config_trim_line(line_raw);

		//skip comments
		if(!*line || *line == ';'){
			continue;
		}

		if(*line == '[' && line[strlen(line) - 1] == ']'){
			//sanity check
			
			if((config_state == conf_layout && layout_ok())
					|| (config_state == conf_command && command_ok())){
				fprintf(stderr, "%s:%zu Cannot switch section before the previous configuration is done\n", cfg_file, line_no);
				rv = 1;
				goto bail;
			}

			if(!strcmp(line, "[web]")){
				config_state = conf_web;
			}
			else if(!strcmp(line, "[x11]")){
				config_state = conf_x11;
			}
			else if(!strncmp(line, "[layout ", 8)){
				line[strlen(line) - 1] = 0;
				if(layout_new(line + 8)){
					rv = 1;
					goto bail;
				}
				config_state = conf_layout;
			}
			else if(!strncmp(line, "[command ", 9)){
				line[strlen(line) - 1] = 0;
				if(command_new(line + 9)){
					rv = 1;
					goto bail;
				}
				config_state = conf_command;
			}
			else{
				fprintf(stderr, "%s:%zu Unknown section keyword\n", cfg_file, line_no);
			}
		}
		else{
			argument = strchr(line, '=');
			if(!argument){
				fprintf(stderr, "%s:%zu Not a assignment\n", cfg_file, line_no);
				rv = 1;
				goto bail;
			}

			*argument = 0;
			argument++;
			line = config_trim_line(line);
			argument = config_trim_line(argument);
			switch(config_state){
				case conf_none:
					fprintf(stderr, "%s:%zu No section specified\n", cfg_file, line_no);
					rv = 1;
					goto bail;
				case conf_web:
					if(web_config(line, argument)){
						rv = 1;
						goto bail;
					}
					break;
				case conf_x11:
					if(x11_config(line, argument)){
						rv = 1;
						goto bail;
					}
					break;
				case conf_layout:
					if(layout_config(line, argument)){
						rv = 1;
						goto bail;
					}
					break;
				case conf_command:
					if(command_config(line, argument)){
						rv = 1;
						goto bail;
					}
					break;
			}

		}
		line_no++;
	}

bail:
	fclose(source);
	free(line_raw);

	return rv || command_ok() || layout_ok() || web_ok() || x11_ok();
}
