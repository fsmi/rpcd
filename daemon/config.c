#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "config.h"

extern rpcd_config_t rpcd_config;

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

void config_free(){
	//TODO
}

static int config_web(char* option, char* value){
	//TODO
	return 0;
}

static int config_x11(char* option, char* value){
	//TODO
	return 0;
}

static int config_command_new(char* name){
	if(strlen(name) < 1){
		fprintf(stderr, "Invalid command name specified\n");
		return 1;
	}

	rpcd_config.commands = realloc(rpcd_config.commands, (rpcd_config.ncommands + 1) * sizeof(command_t));
	if(!rpcd_config.commands){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	command_init(&rpcd_config.commands[rpcd_config.ncommands]);

	rpcd_config.commands[rpcd_config.ncommands].name = strdup(name);
	if(!rpcd_config.commands[rpcd_config.ncommands].name){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	rpcd_config.ncommands++;
	return 0;
}

static int config_command(char* option, char* value){
	//TODO
	return 0;
}

static int config_layout_new(char* name){
	if(strlen(name) < 1){
		fprintf(stderr, "Invalid layout name specified\n");
		return 1;
	}

	rpcd_config.layouts = realloc(rpcd_config.layouts, (rpcd_config.nlayouts + 1) * sizeof(layout_t));
	if(!rpcd_config.layouts){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	layout_init(&rpcd_config.layouts[rpcd_config.nlayouts]);

	rpcd_config.layouts[rpcd_config.nlayouts].name = strdup(name);
	if(!rpcd_config.layouts[rpcd_config.nlayouts].name){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	rpcd_config.nlayouts++;
	return 0;
}

static int config_layout(char* option, char* value){
	if(!strcmp(option, "file")){
		if(layout_parse(value, &rpcd_config.layouts[rpcd_config.nlayouts - 1])){
			return 1;
		}
	}
	else{
		fprintf(stderr, "Unknown parameter %s for layout section\n", option);
		return 1;
	}
	return 0;
}

int config_sane(){
	return (rpcd_config.commands && command_ok(&rpcd_config.commands[rpcd_config.ncommands - 1]))
		|| (rpcd_config.layouts && layout_ok(&rpcd_config.layouts[rpcd_config.nlayouts - 1]));
//		|| web_ok() || x11_ok();
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
			
			if((config_state == conf_layout && !layout_ok(&rpcd_config.layouts[rpcd_config.nlayouts - 1]))
					|| (config_state == conf_command && !command_ok(&rpcd_config.commands[rpcd_config.ncommands - 1]))){
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
				if(config_layout_new(line + 8)){
					rv = 1;
					goto bail;
				}
				config_state = conf_layout;
			}
			else if(!strncmp(line, "[command ", 9)){
				line[strlen(line) - 1] = 0;
				if(config_command_new(line + 9)){
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
					if(config_web(line, argument)){
						rv = 1;
						goto bail;
					}
					break;
				case conf_x11:
					if(config_x11(line, argument)){
						rv = 1;
						goto bail;
					}
					break;
				case conf_layout:
					if(config_layout(line, argument)){
						rv = 1;
						goto bail;
					}
					break;
				case conf_command:
					if(config_command(line, argument)){
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

	return rv || config_sane();
}
