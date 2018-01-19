#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "layout.h"
#include "child.h"
#include "api.h"
#include "x11.h"
#include "config.h"
#include "control.h"

static enum {
	conf_none,
	conf_api,
	conf_x11,
	conf_layout,
	conf_command,
	conf_control,
	conf_variables
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
	int rv = 1;
	size_t line_size = 0;
	ssize_t status;
	char* line_raw = NULL, *line = NULL, *argument = NULL;
	size_t line_no = 1;

	FILE* source = fopen(cfg_file, "r");
	if(!source){
		fprintf(stderr, "Failed to open configuration file %s: %s\n", cfg_file, strerror(errno));
		return 1;
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
					|| (config_state == conf_command && child_ok())
					|| (config_state == conf_x11 && x11_ok())){
				fprintf(stderr, "%s:%zu Cannot switch section before the previous configuration is done\n", cfg_file, line_no);
				goto bail;
			}

			if(!strcmp(line, "[api]")){
				config_state = conf_api;
			}
			else if(!strcmp(line, "[control]")){
				config_state = conf_control;
			}
			else if(!strcmp(line, "[variables]")){
				config_state = conf_variables;
			}
			else if(!strncmp(line, "[x11 ", 5)){
				line[strlen(line) - 1] = 0;
				if(x11_new(line + 5)){
					goto bail;
				}
				config_state = conf_x11;
			}
			else if(!strncmp(line, "[layout ", 8)){
				line[strlen(line) - 1] = 0;
				if(layout_new(line + 8)){
					goto bail;
				}
				config_state = conf_layout;
			}
			else if(!strncmp(line, "[command ", 9)){
				line[strlen(line) - 1] = 0;
				if(child_new_command(line + 9)){
					goto bail;
				}
				config_state = conf_command;
			}
			else{
				fprintf(stderr, "%s:%zu Unknown section keyword\n", cfg_file, line_no);
			}
		}
		else if(!strncmp(line, "include ", 8)){
			if(config_parse(line + 8)){
				goto bail;
			}
		}
		else{
			argument = strchr(line, '=');
			if(!argument){
				fprintf(stderr, "%s:%zu Not an assignment: %s\n", cfg_file, line_no, line);
				goto bail;
			}

			*argument = 0;
			argument++;
			line = config_trim_line(line);
			argument = config_trim_line(argument);
			switch(config_state){
				case conf_none:
					fprintf(stderr, "%s:%zu No section specified\n", cfg_file, line_no);
					goto bail;
				case conf_api:
					if(api_config(line, argument)){
						goto bail;
					}
					break;
				case conf_x11:
					if(x11_config(line, argument)){
						goto bail;
					}
					break;
				case conf_layout:
					if(layout_config(line, argument)){
						goto bail;
					}
					break;
				case conf_command:
					if(child_config_command(line, argument)){
						goto bail;
					}
					break;
				case conf_control:
					if(control_config(line, argument)){
						goto bail;
					}
					break;
				case conf_variables:
					if(control_config_variable(line, argument)){
						goto bail;
					}
					break;
			}

		}
		line_no++;
	}
	rv = 0;

bail:
	fclose(source);
	free(line_raw);

	return rv || child_ok() || control_ok() || layout_ok() || api_ok() || x11_ok();
}
