#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"
#include "easy_json.h"

size_t ncommands = 0;
command_t* commands = NULL;

int command_active(command_t* command){
	//TODO check for active commands
	return 0;
}

size_t command_count(){
	return ncommands;
}

command_t* command_get(size_t index){
	if(index < ncommands){
		return commands + index;
	}
	return NULL;
}

command_t* command_find(char* name){
	size_t u;
	for(u = 0; u < ncommands; u++){
		if(!strcmp(name, commands[u].name)){
			return commands + u;
		}
	}
	return NULL;
}
int check_enum_value(argument_t* arg, char* value) {
	char* item = *arg->additional;
	while(item != NULL) {
		if (!strcmp(value, item)) {
			return 0;
		}
		item++;
	}

	return 1;
}
int command_parse_json(command_t* command, command_instance_t* instance, ejson_struct* ejson) {
		ejson_struct* found = NULL;

		found = ejson_find_key(ejson, "frame", false);

		if (!found) {
			fprintf(stderr, "No frame parameter not found.\n");
			return 1;
		}

		int frame = -1;
		if (ejson_get_int(found, &frame) != EJSON_OK) {
			fprintf(stderr, "Frame parameter is not a number.\n");
			return 1;
		}

		if (frame < 0) {
			fprintf(stderr, "Frame out of range.\n");
			return 1;
		}
		//TODO x11_select_frame(frame);

		// fullscreen
		found = ejson_find_key(ejson, "fullscreen", false);
		if (found) {
			int fullscreen = -1;
			if (ejson_get_int(found, &fullscreen) != EJSON_OK) {
				fprintf(stderr, "Fullscreen parameter is not a number.\n");
				return 1;
			}

			if (fullscreen < 0 || fullscreen > 1) {
				fprintf(stderr, "Fullscreen parameter is not 0 or 1.\n");
				return 1;
			}
			//TODO x11_fullscreen();
			instance->restore_layout = 1;
		} else {
			instance->restore_layout = 0;
		}

		if (command->nargs) {
			found = ejson_find_key(ejson, "arguments", false);
			ejson_struct* arg;
			if (!found) {
				fprintf(stderr, "Arguments parameter not found.\n");
				return 1;
			}
			size_t u;
			argument_t* cmd_arg;
			for (u = 0; u < command->nargs; u++) {
				cmd_arg = command->args + u;
				arg = ejson_find_key(found, cmd_arg->name, false);
				if (arg) {
					if (ejson_get_string(arg, &instance->arguments[u]) != EJSON_OK) {
						fprintf(stderr, "Cannot get value of %s\n", cmd_arg->name);
						return 1;
					}

					if (cmd_arg->type == arg_enum && check_enum_value(cmd_arg, instance->arguments[u])) {
						fprintf(stderr, "Value of %s is not a valid enum value.\n", cmd_arg->name);
						return 1;
					}
				}
			}
		}

		return 0;
}

int command_run(command_t* command, char* data, size_t data_len){
	command_instance_t instance = {
		.command = command
	};
	instance.arguments = calloc(command->nargs, sizeof(char*));
	if (!instance.arguments) {
		fprintf(stderr, "Cannot allocate memeory.\n");
		return 1;
	}

	//return command_execute(instance);
	fprintf(stderr, "Running %s with %zu bytes of options %s\n", command->name, data_len, data);


	if (data_len < 1) {
		fprintf(stderr, "Empty data string.\n");
		return 0;
	}
	int ret = 1;
	ejson_struct* ejson = NULL;
	enum ejson_errors error = ejson_parse_warnings(&ejson, data, data_len, true, stderr);

	if (error == EJSON_OK) {
		if (!command_parse_json(command, &instance, ejson)) {
			ret = command_execute(instance);
		}
	}

	ejson_cleanup(ejson);

	return ret;
}

static void command_init(command_t* command){
	command_t empty = {
		0
	};
	*command = empty;
}

static void command_free(command_t* command){
	size_t u, p;
	for(u = 0; u < command->nargs; u++){
		for(p = 0; command->args[u].additional && command->args[u].additional[p]; p++){
			free(command->args[u].additional[p]);
		}
		free(command->args[u].additional);
		free(command->args[u].name);
	}
	free(command->args);
	free(command->command);
	free(command->name);
}

int command_new(char* name){
	commands = realloc(commands, (ncommands + 1) * sizeof(command_t));
	if(!commands){
		ncommands = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	command_init(commands + ncommands);
	commands[ncommands].name = strdup(name);
	if(!commands[ncommands].name){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	ncommands++;
	return 0;
}

int command_config(char* option, char* value){
	size_t u;
	argument_type new_type = arg_string;
	char* token = NULL;

	if(!commands){
		fprintf(stderr, "No commands defined yet\n");
		return 1;
	}

	if(!strcmp(option, "command")){
		commands[ncommands - 1].command = strdup(value);
		if(!commands[ncommands - 1].command){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}

	//add an argument to the last command
	command_t* cmd = commands + (ncommands - 1);

	if(strlen(option) < 1){
		fprintf(stderr, "Argument to command %s is missing name\n", cmd->name);
		return 1;
	}

	//check if the argument was already defined
	for(u = 0; u < cmd->nargs; u++){
		if(!strcmp(cmd->args[u].name, option)){
			fprintf(stderr, "Command %s has duplicate arguments %s\n", cmd->name, option);
		}
	}

	//check for argument type
	if(!strncmp(value, "string ", 7)){
		value += 7;
	}
	else if(!strncmp(value, "enum ", 5)){
		new_type = arg_enum;
		value += 5;
		//FIXME this check should probably include whitespaces
		if(strlen(value) < 1){
			fprintf(stderr, "ENUM argument %s to command %s requires at least one option\n", option, cmd->name);
			return 1;
		}
	}

	//allocate space for new argument
	cmd->args = realloc(cmd->args, (cmd->nargs + 1) * sizeof(argument_t));
	if(!cmd->args){
		cmd->nargs = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//add the new argument
	cmd->args[cmd->nargs].type = new_type;
	cmd->args[cmd->nargs].additional = NULL;
	cmd->args[cmd->nargs].name = strdup(option);
	if(!cmd->args[cmd->nargs].name){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//handle additional data
	if(new_type == arg_string){
		cmd->args[cmd->nargs].additional = calloc(2, sizeof(char*));
		if(!cmd->args[cmd->nargs].additional){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		cmd->args[cmd->nargs].additional[0] = strdup(value);
		if(!cmd->args[cmd->nargs].additional[0]){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
	}
	else{
		u = 0;
		for(token = strtok(value, " "); token; token = strtok(NULL, " ")){
			cmd->args[cmd->nargs].additional = realloc(cmd->args[cmd->nargs].additional, (u + 2) * sizeof(char*));
			if(!cmd->args[cmd->nargs].additional){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}

			cmd->args[cmd->nargs].additional[u + 1] = NULL;
			cmd->args[cmd->nargs].additional[u] = strdup(token);
			if(!cmd->args[cmd->nargs].additional[u]){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}
			u++;
		}
	}

	cmd->nargs++;

	return 0;
}

int command_ok(){
	size_t u;
	if(!commands){
		fprintf(stderr, "No commands defined, continuing\n");
		return 0;
	}

	command_t* command = commands + (ncommands - 1);

	if(!command->name || !command->command){
		fprintf(stderr, "Command has no name or command specified\n");
		return 1;
	}

	for(u = 0; u < command->nargs; u++){
		if(!command->args[u].name){
			fprintf(stderr, "Argument to command %s has no name\n", command->name);
			return 1;
		}

		if(command->args[u].type == arg_enum && (!command->args[u].additional || !command->args[u].additional[0])){
			fprintf(stderr, "Enum arguments to command %s require at least one option\n", command->name);
			return 1;
		}
	}
	return 0;
}

void command_cleanup(){
	size_t u;

	for(u = 0; u < ncommands; u++){
		command_free(commands + u);
	}
	free(commands);
	ncommands = 0;
}
