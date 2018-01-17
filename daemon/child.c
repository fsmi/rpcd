#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#include "x11.h"
#include "child.h"
#include "../libs/easy_json.h"

static size_t nchildren = 0;
static rpcd_child_t* children = NULL;
extern char** environ;

int child_active(rpcd_child_t* child){
	return child->state == running;
}

int child_discard_restores(size_t display_id){
	size_t u;
	for(u = 0; u < nchildren; u++){
		if(children[u].display_id == display_id){
			children[u].restore_layout = 0;
		}
	}
	return 0;
}

int child_stop(rpcd_child_t* child){
	//FIXME this should reset the stack maximum index
	//this happens when trying to stop a repatriated child
	if(!child->instance){
		child->state = stopped;
		return 0;
	}

	switch(child->state){
		case running:
			//send SIGTERM to process group
			if(kill(-child->instance, SIGTERM)){
				fprintf(stderr, "Failed to terminate child %s: %s\n", child->name, strerror(errno));
			}
			child->state = terminated;
			break;
		case terminated:
			//if that didnt help, send SIGKILL
			if(kill(-child->instance, SIGKILL)){
				fprintf(stderr, "Failed to terminate child %s: %s\n", child->name, strerror(errno));
			}
			break;
		case stopped:
			fprintf(stderr, "Child %s not running, not stopping\n", child->name);
			break;
	}
	return 0;
}

int child_reap(){
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
			for(u = 0; u < nchildren; u++){
				if(children[u].state != stopped && children[u].instance == status){
					children[u].state = stopped;
					//if restore requested, undo layout change
					if(children[u].restore_layout){
						x11_rollback(children[u].display_id);
						children[u].restore_layout = 0;
					}
					fprintf(stderr, "Instance of %s stopped\n", children[u].name);
				}
			}
		}
	}
	while(status);
	return 0;
}

static void child_command_proc(rpcd_child_t* child, command_instance_t* args){
	char* token = NULL, **argv = NULL, *replacement = NULL;
	size_t nargs = 1, u, p;
	display_t* display = NULL;

	argv = calloc(2, sizeof(char*));
	if(!argv){
		fprintf(stderr, "Failed to allocate memory\n");
		exit(EXIT_FAILURE);
	}

	//ensure that no user supplied argument is NULL
	for(u = 0; u < child->nargs; u++){
		if(!args->arguments[u]){
			args->arguments[u] = "";
		}
	}

	//prepare command line for execution
	argv[0] = strtok(child->command, " ");
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
					for(p = 0; p < child->nargs; p++){
						if(!strncasecmp(argv[nargs] + u + 1, child->args[p].name, strlen(child->args[p].name))){
							//wasteful allocs
							replacement = calloc(strlen(argv[nargs]) + strlen(args->arguments[p]) + 1, sizeof(char));
							if(!replacement){
								fprintf(stderr, "Failed to allocate memory\n");
								exit(EXIT_FAILURE);
							}
							memcpy(replacement, argv[nargs], u);
							memcpy(replacement + u, args->arguments[p], strlen(args->arguments[p]));
							memcpy(replacement + u + strlen(args->arguments[p]), argv[nargs] + u + strlen(child->args[p].name) + 1, strlen(argv[nargs] + u + strlen(child->args[p].name)));

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
	if(child->mode == user){
		display = x11_get(child->display_id);
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
	if(execve(argv[0], argv, environ)){
		fprintf(stderr, "Failed to execute child process (%s): %s\n", argv[0], strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static int child_execute(rpcd_child_t* child, command_instance_t* args){
	child->instance = fork();
	switch(child->instance){
		case 0:
			//FIXME might want to differentiate between child types here
			child_command_proc(child, args);
			exit(EXIT_FAILURE);
		case -1:
			fprintf(stderr, "Failed to spawn off new process for command %s: %s\n", child->name, strerror(errno));
			return 1;
		default:
			child->state = running;
			child->restore_layout = args->restore_layout;
	}
	return 0;
}

size_t child_command_count(){
	size_t u, rv = 0;
	for(u = 0; u < nchildren; u++){
		if(children[u].mode == user || children[u].mode == user_no_windows){
			rv++;
		}
	}
	return rv;
}

rpcd_child_t* child_command_get(size_t index){
	size_t u, c = 0;
	for(u = 0; u < nchildren; u++){
		if(children[u].mode == user || children[u].mode == user_no_windows){
			if(c == index){
				return children + u;
			}
			c++;
		}
	}
	return NULL;
}

rpcd_child_t* child_command_find(char* name){
	size_t u;
	for(u = 0; u < nchildren; u++){
		if((children[u].mode == user || children[u].mode == user_no_windows)
				&& !strcasecmp(name, children[u].name)){
			return children + u;
		}
	}
	return NULL;
}

rpcd_child_t* child_window_find(char* name){
	size_t u;
	for(u = 0; u < nchildren; u++){
		if((children[u].mode != user && children[u].mode != user_no_windows)
				&& children[u].name
				&& !strcasecmp(name, children[u].name)){
			return children + u;
		}
	}
	return NULL;
}

static int child_verify_enum(argument_t* arg, char* value){
	char** item = NULL;
	for(item = arg->additional; *item; item++){
		if(!strcasecmp(*item, value)){
			//fix up case of submitted value
			memcpy(value, *item, strlen(*item));
			return 0;
		}
	}
	return 1;
}

static int child_parse_json(rpcd_child_t* command, command_instance_t* instance, ejson_object* ejson) {
	ejson_array* args = (ejson_array*) ejson_find_by_key(ejson, "arguments", false, false);
	size_t u;
	argument_t* cmd_arg;
	int err;

	if(command->mode == user){
		char* display_name = NULL;
		err = ejson_get_string_from_key(ejson, "display", false, false, &display_name);
		if (err == EJSON_KEY_NOT_FOUND) {
			fprintf(stderr, "No display provided for command, using default display\n");
			command->display_id = 0;
		} else if (err == EJSON_OK) {
			command->display_id = x11_find_id(display_name);
		} else {
			fprintf(stderr, "Failed to parse display parameter\n");
			return 1;
		}

		int frame_id = -1;
		err = ejson_get_int_from_key(ejson, "frame", false, false, &frame_id);
		if (err == EJSON_OK){
			x11_select_frame(command->display_id, frame_id);
		} else if (EJSON_KEY_NOT_FOUND) {
			fprintf(stderr, "No frame provided for command, using active one\n");
		} else {
			fprintf(stderr, "Failed to parse frame parameter\n");
		}

		int fullscreen = 0;
		err = ejson_get_int_from_key(ejson, "fullscreen", false, false, &fullscreen);
		if (err == EJSON_KEY_NOT_FOUND) {
			fprintf(stderr, "No fullscreen parameter provided, using off\n");
		} else if (err != EJSON_OK) {
			fprintf(stderr, "Failed to parse fullscreen parameter\n");
		}
		else if(fullscreen){
			x11_fullscreen(command->display_id);
			instance->restore_layout = 1;
		}
	}

	if(command->nargs){
		if(!args){
			fprintf(stderr, "No arguments supplied\n");
			return 1;
		}

		if (args->base.type != EJSON_ARRAY) {
			fprintf(stderr, "Arguments is not an array.\n");
			return 1;
		}
		int j;
		for (u = 0; u < command->nargs; u++) {
			cmd_arg = command->args + u;
			for (j = 0; j < args->length; j++) {

				if (args->values[j]->type != EJSON_OBJECT) {
					continue;
				}

				err = ejson_get_string_from_key((ejson_object*) args->values[j], cmd_arg->name, true, false, &instance->arguments[u]);
				if (err == EJSON_KEY_NOT_FOUND) {
					continue;
				} else if (err != EJSON_OK) {
					fprintf(stderr, "Failed to fetch assigned value for argument %s\n", cmd_arg->name);
					return 1;
				}

				if(cmd_arg->type == arg_enum && child_verify_enum(cmd_arg, instance->arguments[u])) {
					fprintf(stderr, "Value of %s is not a valid for enum type\n", cmd_arg->name);
					return 1;
				}
			}
		}
	}

	return 0;
}

int child_run_command(rpcd_child_t* command, char* data, size_t data_len){
	int rv = 1;
	ejson_base* ejson = NULL;
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

	enum ejson_errors error = ejson_parse_warnings(data, data_len, true, stderr, &ejson);
	if(error == EJSON_OK && ejson->type == EJSON_OBJECT){
		if(!child_parse_json(command, &instance, (ejson_object*) ejson)){
			//debug variable set
			for(u = 0; u < command->nargs; u++){
				fprintf(stderr, "%s.%s -> %s\n", command->name, command->args[u].name, instance.arguments[u] ? instance.arguments[u] : "-null-");
			}
			rv = child_execute(command, &instance);
		}
	}

	free(instance.arguments);
	ejson_cleanup(ejson);
	return rv;
}

static void child_init(rpcd_child_t* child){
	rpcd_child_t empty = {
		0
	};
	*child = empty;
}

static void child_free(rpcd_child_t* child){
	size_t u, p;
	for(u = 0; u < child->nargs; u++){
		for(p = 0; child->args[u].additional && child->args[u].additional[p]; p++){
			free(child->args[u].additional[p]);
		}
		free(child->args[u].additional);
		free(child->args[u].name);
	}
	free(child->windows);
	free(child->args);
	free(child->command);
	free(child->description);
	free(child->name);
	child_init(child);
}

static pid_t child_parent(pid_t pid){
	//weirdly, there is no proper API for this kind of thing (getting the parent pid of an arbitrary process).
	//man proc tells us to read it from /proc/<pid>/stat, which only works under linux
	//in addition, the format within that file is massively broken - the manpage tells us to scan the
	//executable name (which precedes the ppid, or else i wouldnt actually care) using the %s escape code,
	//which simply does not work with executable names containing spaces. tokenizing along parentheses is
	//a bad idea for the same reason. this problem actually breaks multiple tools doing the same thing.
	//for now, just scan the whole file and search for the _last_ closing parenthesis, which should hopefully
	//be the one terminating the executable name
	pid_t rv = 0;
	char stat_path[PATH_MAX];
	size_t chunks = 0;
	FILE* stat_file = NULL;
	char* pid_info = NULL;
	char* ppid = NULL;

	snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
	stat_file = fopen(stat_path, "r");
	if(!stat_file){
		fprintf(stderr, "Failed to open %s for reading: %s\n", stat_path, strerror(errno));
		goto done;
	}

	while(!feof(stat_file)){
		pid_info = realloc(pid_info, (chunks + 1) * PATH_MAX);
		if(!pid_info){
			fprintf(stderr, "Failed to allocate memory\n");
			goto done;
		}

		if(fread(pid_info + (chunks * PATH_MAX), PATH_MAX, 1, stat_file) != 1){
			break;
		}

		chunks++;
	}

	ppid = strrchr(pid_info, ')');
	if(!ppid){
		fprintf(stderr, "File %s malformed\n", stat_path);
		goto done;
	}

	//separator after image name
	for(; *ppid && ppid[0] != ' '; ppid++){
	}

	if(!*ppid){
		fprintf(stderr, "File %s malformed: short read\n", stat_path);
		goto done;
	}
	ppid++;

	//separator after process status
	for(; *ppid && ppid[0] != ' '; ppid++){
	}

	rv = strtol(ppid, NULL, 10);
done:
	if(stat_file){
		fclose(stat_file);
	}
	free(pid_info);
	return rv;
}

int child_match_window(size_t display_id, Window window, pid_t pid, char* title, char* name, char* class){
	pid_t current_pid = pid;
	size_t u, matched = 0;
	enum {
		match_pid = 0,
		match_title,
		match_name,
		match_class,
		done
	} strategy = pid ? match_pid : match_title; //skip pid match if not available

	for(; strategy < done; strategy++){
		for(u = 0; u < nchildren && !matched; u++){
			if(children[u].state == running
					&& children[u].display_id == display_id){
				switch(strategy){
					case match_pid:
						if(children[u].instance == current_pid){
							matched = 1;
							continue;
						}
						break;
					case match_title:
						if(children[u].filters[0] &&
								!strcmp(children[u].filters[0], title)){
							matched = 1;
							continue;
						}
						break;
					case match_name:
						if(children[u].filters[1] &&
								!strcmp(children[u].filters[0], name)){
							matched = 1;
							continue;
						}
						break;
					case match_class:
						if(children[u].filters[2] &&
								!strcmp(children[u].filters[2], class)){
							matched = 1;
							continue;
						}
						break;
					case done:
						fprintf(stderr, "Window matching reached invalid strategy\n");
						break;
				}
			}

		}

		if(matched){
			u--;
			break;
		}

		if(strategy == match_pid && current_pid > 1){
			current_pid = child_parent(current_pid);
			strategy--;
			continue;
		}
	}

	if(matched){
		children[u].windows = realloc(children[u].windows, (children[u].nwindows + 1) * sizeof(Window));
		if(!children[u].windows){
			children[u].nwindows = 0;
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		children[u].windows[children[u].nwindows] = window;
		children[u].nwindows++;

		fprintf(stderr, "Matched window %zu (%d, %s, %s, %s) on display %zu to child %zu (%s) using strategy %u, now at %zu windows\n",
				window, pid, title ? title : "-none-", name ? name : "-none-",
				class ? class : "-none-", display_id, u, children[u].name, strategy, children[u].nwindows);
		return 0;
	}

	fprintf(stderr, "Failed to match window %zu (%d, %s, %s, %s) on display %zu to executing child\n", window, pid, title, name, class, display_id);
	return 0;
}

int child_discard_window(size_t display_id, Window window){
	size_t u, c;

	for(u = 0; u < nchildren; u++){
		if(children[u].display_id == display_id
				&& children[u].nwindows > 0){
			for(c = 0; c < children[u].nwindows; c++){
				if(children[u].windows[c] == window){
					children[u].windows[c] = 0;

					//re-sort window array
					for(c++; c < children[u].nwindows; c++){
						children[u].windows[c - 1] = children[u].windows[c];
					}

					children[u].nwindows--;
					fprintf(stderr, "Dismissed window %zu for command %s, %zu left\n", window, children[u].name ? children[u].name : "-repatriated-", children[u].nwindows);
					return 0;
				}
			}
		}
	}

	fprintf(stderr, "Unmatched window %zu destroyed on display %zu\n", window, display_id);
	return 0;
}

Window child_window(size_t display_id, size_t frame_id){
	size_t u, stack_index = 0;
	Window matched_window = 0;

	for(u = 0; u < nchildren; u++){
		if(children[u].display_id == display_id
				&& children[u].frame_id == frame_id
				&& children[u].state == running
				&& children[u].nwindows > 0
				&& children[u].order >= stack_index){
			matched_window = children[u].windows[children[u].nwindows - 1];
		}
	}

	return matched_window;
}

int child_repatriate(size_t display_id, size_t frame_id, Window window){
	children = realloc(children, (nchildren + 1) * sizeof(rpcd_child_t));
	if(!children){
		fprintf(stderr, "Failed to allocate memory\n");
		nchildren = 0;
		return 1;
	}
	child_init(children + nchildren);

	//order implicitly set to 0
	children[nchildren].windows = malloc(sizeof(Window));
	if(!children[nchildren].windows){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	children[nchildren].nwindows = 1;
	children[nchildren].windows[0] = window;
	children[nchildren].mode = repatriated;
	children[nchildren].display_id = display_id;
	children[nchildren].frame_id = frame_id;
	children[nchildren].state = running;

	nchildren++;
	return 0;
}

int child_new_command(char* name){
	size_t u;

	for(u = 0; u < nchildren; u++){
		if((children[u].mode == user || children[u].mode == user_no_windows) && 
				!strcasecmp(children[u].name, name)){
			fprintf(stderr, "Command %s already defined\n", name);
			return 1;
		}
	}

	children = realloc(children, (nchildren + 1) * sizeof(rpcd_child_t));
	if(!children){
		nchildren = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	child_init(children + nchildren);
	children[nchildren].name = strdup(name);
	if(!children[nchildren].name){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	nchildren++;
	return 0;
}

int child_config_command(char* option, char* value){
	size_t u;
	argument_type new_type = arg_string;
	char* token = NULL;
	rpcd_child_t* last = children + (nchildren - 1);

	if(!children){
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
			last->mode = user_no_windows;
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
		if(!strcasecmp(last->args[u].name, option)){
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

int child_ok(){
	size_t u;
	if(!children){
		fprintf(stderr, "No children defined, continuing\n");
		return 0;
	}

	rpcd_child_t* last = children + (nchildren - 1);

	//repatriated windows have neither name or command set
	if(last->mode != repatriated
				&& (!last->name || !last->command)){
		fprintf(stderr, "Child definition has no name or command specified\n");
		return 1;
	}

	for(u = 0; u < last->nargs; u++){
		if(!last->args[u].name){
			fprintf(stderr, "User argument to command %s has no name\n", last->name);
			return 1;
		}

		if(last->args[u].type == arg_enum 
				&& !(last->args[u].additional && last->args[u].additional[0])){
			fprintf(stderr, "Enum arguments to command %s require at least one option\n", last->name);
			return 1;
		}
	}
	return 0;
}

void child_cleanup(){
	size_t u, done = 0;

	//stop all executing instances
	while(!done){
		done = 1;
		for(u = 0; u < nchildren; u++){
			if(children[u].state != stopped){
				child_stop(children + u);
				done = 0;
			}
		}
		child_reap();
	}

	for(u = 0; u < nchildren; u++){
		child_free(children + u);
	}
	free(children);
	nchildren = 0;
}
