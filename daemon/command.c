#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"

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

int command_run(command_t* command, char* data, size_t data_len){
	command_instance_t instance = {
		.command = command
	};

	//TODO check if command already running
	//TODO parse arguments, for enums: check validity
	//TODO x11_select_frame(frame_id);
	//TODO if(fullscreen){
	//TODO 	x11_fullscreen();
	//TODO }
	//return command_execute(instance);
	fprintf(stderr, "Running %s with %zu bytes of options %s\n", command->name, data_len, data);
	return 0;
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
