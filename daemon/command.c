#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#include "x11.h"
#include "command.h"
#include "easy_json.h"

static size_t ncommands = 0;
static command_t* commands = NULL;
extern char** environ;

int command_active(command_t* command){
	return command->state == running;
}

int command_stop(command_t* command){
	switch(command->state){
		case running:
			//send SIGTERM to process group
			if(kill(-command->instance, SIGTERM)){
				fprintf(stderr, "Failed to terminate command %s: %s\n", command->name, strerror(errno));
			}
			command->state = terminated;
			break;
		case terminated:
			//if that didnt help, send SIGKILL
			if(kill(-command->instance, SIGKILL)){
				fprintf(stderr, "Failed to terminate command %s: %s\n", command->name, strerror(errno));
			}
			break;
		case stopped:
			fprintf(stderr, "Command %s not running, not stopping\n", command->name);
			break;
	}
	return 0;
}

int command_reap(){
	int wait_status;
	pid_t status;
	size_t u;
	do{
		status = waitpid(-1, &wait_status, WNOHANG);
		if(status < 0){
			if(errno == ECHILD){
				break;
			}
			fprintf(stderr, "Failed to reap children: %s\n", strerror(errno));
			return 1;
		}
		else if(status != 0){
			for(u = 0; u < ncommands; u++){
				if(commands[u].state != stopped && commands[u].instance == status){
					commands[u].state = stopped;
					//if restore requested, undo layout change
					if(commands[u].restore_layout){
						x11_rollback(commands[u].display_id);
						commands[u].restore_layout = 0;
					}
					fprintf(stderr, "Instance of %s stopped\n", commands[u].name);
				}
			}
		}
	}
	while(status);
	return 0;
}

static void command_child(command_t* command, command_instance_t* args){
	char* token = NULL, *child = NULL, **argv = NULL, *replacement = NULL;
	size_t nargs = 1, u, p;
	display_t* display = NULL;

	argv = calloc(2, sizeof(char*));
	if(!argv){
		fprintf(stderr, "Failed to allocate memory\n");
		exit(EXIT_FAILURE);
	}

	//ensure that no argument is NULL
	for(u = 0; u < command->nargs; u++){
		if(!args->arguments[u]){
			args->arguments[u] = "";
		}
	}

	//prepare command line for execution
	child = argv[0] = strtok(command->command, " ");
	for(token = strtok(NULL, " "); token; token = strtok(NULL, " ")){
		argv = realloc(argv, (nargs + 2) * sizeof(char*));
		if(!argv){
			fprintf(stderr, "Failed to allocate memory\n");
			exit(EXIT_FAILURE);
		}

		argv[nargs + 1] = NULL;
		argv[nargs] = token;

		//variable replacement
		if(strchr(token, '%')){
			for(u = 0; argv[nargs][u]; u++){
				if(argv[nargs][u] == '%'){
					for(p = 0; p < command->nargs; p++){
						if(!strncmp(argv[nargs] + u + 1, command->args[p].name, strlen(command->args[p].name))){
							//wasteful allocs
							replacement = calloc(strlen(argv[nargs]) + strlen(args->arguments[p]) + 1, sizeof(char));
							memcpy(replacement, argv[nargs], u);
							memcpy(replacement + u, args->arguments[p], strlen(args->arguments[p]));
							memcpy(replacement + u + strlen(args->arguments[p]), argv[nargs] + u + strlen(command->args[p].name) + 1, strlen(argv[nargs] + u + strlen(command->args[p].name)));

							argv[nargs] = replacement;
							u += strlen(args->arguments[p]);
						}
					}
				}
			}
		}
		nargs++;
	}

	//update the environment with proper DISPLAY
	if(command->windows){
		display = x11_get(command->display_id);
		if(setenv("DISPLAY", display->identifier, 1)){
			fprintf(stderr, "Failed to update the environment: %s\n", strerror(errno));
		}
	}
	else if(unsetenv("DISPLAY")){
		fprintf(stderr, "Failed to update the environment: %s\n", strerror(errno));
	}

	//make the child a session leader to be able to kill the entire group
	setpgrp();
	//exec into command
	if(execve(child, argv, environ)){
		fprintf(stderr, "Failed to execute child process (%s): %s\n", child, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static int command_execute(command_t* command, command_instance_t* args){
	command->instance = fork();
	switch(command->instance){
		case 0:
			command_child(command, args);
			exit(EXIT_FAILURE);
		case -1:
			fprintf(stderr, "Failed to spawn off new process for command %s: %s\n", command->name, strerror(errno));
			return 1;
		default:
			command->state = running;
			command->restore_layout = args->restore_layout;
	}
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

static int command_verify_enum(argument_t* arg, char* value){
	char** item = NULL;
	for(item = arg->additional; *item; item++){
		if(!strcmp(*item, value)){
			return 0;
		}
	}
	return 1;
}

static int command_parse_json(command_t* command, command_instance_t* instance, ejson_struct* ejson) {
	ejson_struct* display_info = ejson_find_key(ejson, "display", true);
	ejson_struct* frame_info = ejson_find_key(ejson, "frame", true);
	ejson_struct* fullscreen_info = ejson_find_key(ejson, "fullscreen", true);
	ejson_struct* args = ejson_find_key(ejson, "arguments", true);
	ejson_struct* arg;
	size_t u;
	argument_t* cmd_arg;

	if(command->windows){
		if(display_info){
			char* display_name = NULL;
			if(ejson_get_string(display_info, &display_name) != EJSON_OK){
				fprintf(stderr, "Failed to parse display parameter\n");
				return 1;
			}

			command->display_id = x11_find_id(display_name);
		}
		else{
			fprintf(stderr, "No display provided for command, using default display\n");
			command->display_id = 0;
		}

		if(frame_info){
			int frame_id = -1;
			if(ejson_get_int(frame_info, &frame_id) != EJSON_OK){
				fprintf(stderr, "Failed to parse frame parameter\n");
			}
			else{
				x11_select_frame(command->display_id, frame_id);
			}
		}

		if(fullscreen_info){
			int fullscreen = 0;
			if(ejson_get_int(fullscreen_info, &fullscreen) != EJSON_OK) {
				fprintf(stderr, "Failed to parse fullscreen parameter\n");
			}
			else if(fullscreen){
				x11_fullscreen(command->display_id);
				instance->restore_layout = 1;
			}
		}
	}

	if(command->nargs){
		if(!args){
			fprintf(stderr, "No arguments supplied\n");
			return 1;
		}
		for (u = 0; u < command->nargs; u++) {
			cmd_arg = command->args + u;
			arg = ejson_find_key(args, cmd_arg->name, true);
			if(arg){
				if (ejson_get_string(arg, &instance->arguments[u]) != EJSON_OK) {
					fprintf(stderr, "Failed to fetch assigned value for argument %s\n", cmd_arg->name);
					return 1;
				}

				if(cmd_arg->type == arg_enum && command_verify_enum(cmd_arg, instance->arguments[u])) {
					fprintf(stderr, "Value of %s is not a valid for enum type\n", cmd_arg->name);
					return 1;
				}
			}
		}
	}

	return 0;
}

int command_run(command_t* command, char* data, size_t data_len){
	int rv = 1;
	ejson_struct* ejson = NULL;
	size_t u;
	command_instance_t instance = {
		.arguments = calloc(command->nargs, sizeof(char*))
	};

	if(!instance.arguments){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	if(data_len < 1) {
		fprintf(stderr, "No execution information provided for command %s\n", command->name);
		free(instance.arguments);
		return 1;
	}

	enum ejson_errors error = ejson_parse_warnings(&ejson, data, data_len, true, stderr);
	if(error == EJSON_OK){
		if(!command_parse_json(command, &instance, ejson)){
			//debug variable set
			for(u = 0; u < command->nargs; u++){
				fprintf(stderr, "%s.%s -> %s\n", command->name, command->args[u].name, instance.arguments[u] ? instance.arguments[u] : "-null-");
			}
			rv = command_execute(command, &instance);
		}
	}

	free(instance.arguments);
	ejson_cleanup(ejson);
	return rv;
}

static void command_init(command_t* command){
	command_t empty = {
		0
	};
	empty.windows = 1;
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
	free(command->description);
	free(command->name);
}

int command_new(char* name){
	size_t u;

	for(u = 0; u < ncommands; u++){
		if(!strcmp(commands[u].name, name)){
			fprintf(stderr, "Command %s already defined\n", name);
			return 1;
		}
	}

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
	command_t* last = commands + (ncommands - 1);

	if(!commands){
		fprintf(stderr, "No commands defined yet\n");
		return 1;
	}

	if(!strcmp(option, "command")){
		last->command = strdup(value);
		if(!last->command){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "description")){
		last->description = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "windows")){
		if(!strcmp(value, "no")){
			last->windows = 0;
		}
		return 0;
	}

	//add an argument to the last command
	if(strlen(option) < 1){
		fprintf(stderr, "Argument to command %s is missing name\n", last->name);
		return 1;
	}

	//check if the argument was already defined
	for(u = 0; u < last->nargs; u++){
		if(!strcmp(last->args[u].name, option)){
			fprintf(stderr, "Command %s has duplicate arguments %s\n", last->name, option);
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
			fprintf(stderr, "ENUM argument %s to command %s requires at least one option\n", option, last->name);
			return 1;
		}
	}

	//allocate space for new argument
	last->args = realloc(last->args, (last->nargs + 1) * sizeof(argument_t));
	if(!last->args){
		last->nargs = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//add the new argument
	last->args[last->nargs].type = new_type;
	last->args[last->nargs].additional = NULL;
	last->args[last->nargs].name = strdup(option);
	if(!last->args[last->nargs].name){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//handle additional data
	if(new_type == arg_string){
		last->args[last->nargs].additional = calloc(2, sizeof(char*));
		if(!last->args[last->nargs].additional){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		last->args[last->nargs].additional[0] = strdup(value);
		if(!last->args[last->nargs].additional[0]){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
	}
	else{
		u = 0;
		for(token = strtok(value, " "); token; token = strtok(NULL, " ")){
			last->args[last->nargs].additional = realloc(last->args[last->nargs].additional, (u + 2) * sizeof(char*));
			if(!last->args[last->nargs].additional){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}

			last->args[last->nargs].additional[u + 1] = NULL;
			last->args[last->nargs].additional[u] = strdup(token);
			if(!last->args[last->nargs].additional[u]){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}
			u++;
		}
	}

	last->nargs++;

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
	size_t u, done = 0;

	//stop all executing instances
	while(!done){
		done = 1;
		for(u = 0; u < ncommands; u++){
			if(commands[u].state != stopped){
				command_stop(commands + u);
				done = 0;
			}
		}
		command_reap();
	}

	for(u = 0; u < ncommands; u++){
		command_free(commands + u);
	}
	free(commands);
	ncommands = 0;
}
