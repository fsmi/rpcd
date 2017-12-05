#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"

size_t ncommands = 0;
command_t* commands = NULL;

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

	//TODO parse arguments
	return 1;
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
