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
#include "control.h"

static size_t ncommands = 0;
static rpcd_child_t* commands = NULL;
static size_t nwindows = 0;
static rpcd_child_t* windows = NULL;
static size_t last_command = 0;

int child_active(rpcd_child_t* child){
	return child->state != stopped;
}

int child_discard_restores(size_t display_id){
	size_t u;
	for(u = 0; u < ncommands; u++){
		if(commands[u].display_id == display_id){
			commands[u].restore_layout = 0;
		}
	}
	return 0;
}

int child_stop(rpcd_child_t* child){
	//this happens when trying to stop a repatriated child
	if(!child->instance){
		child->state = stopped;
		return 0;
	}

	//mark all windows as "do not use"
	child->order = -1;
	//reset the startup iteration counter so we can track startup tries again
	child->start_iteration = 0;

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

int child_stop_commands(size_t display_id){
	size_t u;
	int rv = 0;
	for(u = 0; u < ncommands; u++){
		if(commands[u].mode == user
				&& commands[u].state != stopped
				&& commands[u].display_id == display_id){
			rv |= child_stop(commands + u);
		}
	}
	return rv;
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
			for(u = 0; u < ncommands; u++){
				if(commands[u].state != stopped && commands[u].instance == status){
					commands[u].state = stopped;
					//if restore requested, undo layout change
					if(commands[u].restore_layout){
						x11_rollback(commands[u].display_id);
						commands[u].restore_layout = 0;
					}
					//commands without windows don't lock the display
					if(commands[u].mode == user){
						x11_unlock(commands[u].display_id);
						commands[u].frame_id = -1;
					}
					fprintf(stderr, "Instance of %s stopped\n", commands[u].name);
					break;
				}
			}

			for(u = 0; u < nwindows; u++){
				if(windows[u].state != stopped && windows[u].instance == status){
					windows[u].state = stopped;
					fprintf(stderr, "Automated window %s terminated\n", windows[u].name);
					break;
				}
			}

			//run automation as either a display may have become unlocked or a window may have died
			if(control_run_automation()){
				return 1;
			}
		}
	}
	while(status);
	return 0;
}

static void child_command_proc(rpcd_child_t* child, command_instance_t* args){
	char* token = NULL, **argv = NULL, *replacement = NULL;
	size_t nargs = 1, u, p;

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

	//exec into command
	if(execvp(argv[0], argv)){
		fprintf(stderr, "Failed to execute child command process (%s): %s\n", argv[0], strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void child_window_proc(rpcd_child_t* child, command_instance_t* instance_info){
	size_t u, nargs = 1;
	char** argv = calloc(2, sizeof(char*));
	char* token = NULL;
	if(!argv){
		fprintf(stderr, "Failed to allocate memory\n");
		exit(EXIT_FAILURE);
	}

	//update the environment with all variables
	for(u = 0; u < instance_info->nargs / 2; u++){
		if(setenv(instance_info->arguments[u * 2], instance_info->arguments[(u * 2) + 1], 1)){
			fprintf(stderr, "Failed to update the environment: %s\n", strerror(errno));
		}
	}

	argv[0] = strtok(child->command, " ");
	for(token = strtok(NULL, " "); token; token = strtok(NULL, " ")){
		argv = realloc(argv, (nargs + 2) * sizeof(char*));
		if(!argv){
			fprintf(stderr, "Failed to allocate memory\n");
			exit(EXIT_FAILURE);
		}

		argv[nargs + 1] = NULL;
		argv[nargs] = token;
	}

	//exec into window executable
	if(execvp(argv[0], argv)){
		fprintf(stderr, "Failed to execute child window process (%s): %s\n", argv[0], strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static size_t child_restack(){
	size_t u, stack_min = -1, stack_max = 0;
	rpcd_child_t* child = 0;

	for(u = 0; u < ncommands + nwindows; u++){
		child = (u < ncommands) ? commands + u : windows + (u - ncommands);
		if(child->order > 0){
			stack_min = (child->order < stack_min || stack_min < 0) ? child->order : stack_min;
			stack_max = (child->order > stack_max) ? child->order : stack_max;
		}
	}

	//shift all indices to the bottom
	if(stack_min > 1){
		stack_min--;
		stack_max -= stack_min;

		for(u = 0; u < ncommands + nwindows; u++){
			child = (u < ncommands) ? commands + u : windows + (u - ncommands);
			if(child->order > stack_min){
				child->order -= stack_min;
			}
		}
	}

	return stack_max + 1;
}

int child_start(rpcd_child_t* child, size_t display_id, size_t frame_id, command_instance_t* instance_args){
	display_t* display = NULL;

	child->order = child_restack();
	child->display_id = display_id;
	child->frame_id = frame_id;
	child->start_iteration++;

	if(child->mode != user_no_windows){
		display = x11_get(display_id);
		x11_select_frame(display_id, frame_id);
		//reset in _reap
		if(child->restore_layout){
			x11_fullscreen(display_id);
		}
	}

	child->instance = fork();
	switch(child->instance){
		case 0:
			//update the environment with proper DISPLAY
			if(child->mode != user_no_windows){
				if(setenv("DISPLAY", display->identifier, 1)){
					fprintf(stderr, "Failed to update the environment: %s\n", strerror(errno));
				}
			}
			else if(unsetenv("DISPLAY")){
				fprintf(stderr, "Failed to update the environment: %s\n", strerror(errno));
			}
			//make the child a session leader to be able to kill the entire group
			setpgrp();
			//update the working directory if requested
			if(child->working_directory){
				if(chdir(child->working_directory)){
					fprintf(stderr, "Failed to change working directory to %s for %s: %s\n",
							child->working_directory, child->name, strerror(errno));
				}
			}

			//handle with appropriate child procedure
			if(child->mode == user || child->mode == user_no_windows){
				child_command_proc(child, instance_args);
			}
			else{
				child_window_proc(child, instance_args);
			}
			//if for some reason it comes to this, terminate the child
			exit(EXIT_FAILURE);
		case -1:
			fprintf(stderr, "Failed to spawn child process for command %s: %s\n", child->name, strerror(errno));
			return 1;
		default:
			if(child->mode == user){
				x11_lock(child->display_id);
			}
			child->state = running;
	}
	return 0;
}

size_t child_command_count(){
	return ncommands;
}

rpcd_child_t* child_command_get(size_t index){
	if(index < ncommands){
		return commands + index;
	}
	return NULL;
}

rpcd_child_t* child_command_find(char* name){
	size_t u;
	for(u = 0; u < ncommands; u++){
		if(!strcasecmp(name, commands[u].name)){
			return commands + u;
		}
	}
	return NULL;
}

size_t child_window_count(){
	return nwindows;
}

rpcd_child_t* child_window_get(size_t index){
	if(index < nwindows){
		return windows + index;
	}
	return NULL;
}

rpcd_child_t* child_window_find(char* name){
	size_t u;
	for(u = 0; u < nwindows; u++){
		if(windows[u].name && !strcasecmp(name, windows[u].name)){
			return windows + u;
		}
	}
	return NULL;
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
	free(child->working_directory);
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
	size_t bytes_read = 0;

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

		bytes_read += fread(pid_info + (chunks * PATH_MAX), 1, PATH_MAX, stat_file);
		if(bytes_read % PATH_MAX){
			break;
		}

		chunks++;
	}
	//terminate string - actually, this eliminates the last read byte, too, but we dont really care
	pid_info[bytes_read] = 0;

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
	rpcd_child_t* match = NULL;
	pid_t current_pid = pid;
	size_t u, matched = 0;
	enum {
		match_pid = 0,
		match_title,
		match_name,
		match_class,
		match_heuristic,
		done
	} strategy = pid ? match_pid : match_title; //skip pid match if not available

	for(; strategy < done; strategy++){
		for(u = 0; u < ncommands + nwindows && !matched; u++){
			match = (u < ncommands) ? commands + u : windows + (u - ncommands);

			if(match->state == running
					&& match->display_id == display_id){
				switch(strategy){
					case match_pid:
						if(match->instance == current_pid){
							matched = 1;
							continue;
						}
						break;
					case match_title:
						if(title && match->filters[0] &&
								!strcmp(match->filters[0], title)){
							matched = 1;
							continue;
						}
						break;
					case match_name:
						if(name && match->filters[1] &&
								!strcmp(match->filters[0], name)){
							matched = 1;
							continue;
						}
						break;
					case match_class:
						if(class && match->filters[2] &&
								!strcmp(match->filters[2], class)){
							matched = 1;
							continue;
						}
						break;
					case match_heuristic:
						if(match->mode == user){
							fprintf(stderr, "Using heuristic to match this window, please notify the developers\n");
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
			break;
		}

		if(strategy == match_pid && current_pid > 1){
			current_pid = child_parent(current_pid);
			strategy--;
			continue;
		}

		//if we had a pid and could not match it, do not continue
		if(strategy == match_pid){
			break;
		}
	}

	if(matched){
		match->windows = realloc(match->windows, (match->nwindows + 1) * sizeof(Window));
		if(!match->windows){
			match->nwindows = 0;
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		match->windows[match->nwindows] = window;
		match->nwindows++;

		fprintf(stderr, "Matched window %zu (%d, %s, %s, %s) on display %zu to child %zu (%s) using strategy %u, now at %zu windows\n",
				window, pid, title ? title : "-none-", name ? name : "-none-",
				class ? class : "-none-", display_id, u, match->name, strategy, match->nwindows);

		//run automation if an automated window was mapped
		if(match->mode != user && match->mode != user_no_windows){
			return control_run_automation();
		}
		return 0;
	}

	fprintf(stderr, "Failed to match window %zu (%d, %s, %s, %s) on display %zu to executing child\n", window, pid, title ? title : "-none-",
			name ? name : "-none-", class ? class : "-none-", display_id);
	return 0;
}

int child_discard_window(size_t display_id, Window window){
	size_t u, c;
	rpcd_child_t* check = NULL;

	for(u = 0; u < ncommands + nwindows; u++){
		check = (u < ncommands) ? commands + u : windows + (u - ncommands);
		if(check->display_id == display_id
				&& check->nwindows > 0){
			for(c = 0; c < check->nwindows; c++){
				if(check->windows[c] == window){
					check->windows[c] = 0;

					//re-sort window array
					for(c++; c < check->nwindows; c++){
						check->windows[c - 1] = check->windows[c];
					}

					check->nwindows--;
					fprintf(stderr, "Dismissed window %zu for command %s, %zu left\n", window, check->name ? check->name : "-repatriated-", check->nwindows);
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

	for(u = 0; u < ncommands; u++){
		if(commands[u].display_id == display_id
				&& commands[u].frame_id == frame_id
				&& commands[u].state == running
				&& commands[u].nwindows > 0
				&& commands[u].order >= stack_index){
			matched_window = commands[u].windows[commands[u].nwindows - 1];
		}
	}

	for(u = 0; !matched_window && u < nwindows; u++){
		if(windows[u].display_id == display_id
				&& windows[u].frame_id == frame_id
				&& windows[u].state == running
				&& windows[u].nwindows > 0
				&& windows[u].order >= stack_index){
			matched_window = windows[u].windows[windows[u].nwindows - 1];
		}
	}

	return matched_window;
}

static rpcd_child_t* child_allocate_command(){
	commands = realloc(commands, (ncommands + 1) * sizeof(rpcd_child_t));
	if(!commands){
		fprintf(stderr, "Failed to allocate memory\n");
		ncommands = 0;
		return NULL;
	}

	child_init(commands + ncommands);
	return commands + ncommands++;
}

static rpcd_child_t* child_allocate_window(){
	windows = realloc(windows, (nwindows + 1) * sizeof(rpcd_child_t));
	if(!windows){
		fprintf(stderr, "Failed to allocate memory\n");
		nwindows = 0;
		return NULL;
	}

	child_init(windows + nwindows);
	windows[nwindows].mode = lazy;
	return windows + nwindows++;
}

int child_repatriate(size_t display_id, size_t frame_id, Window window){
	rpcd_child_t* rep = child_allocate_window();

	if(!rep){
		return 1;
	}

	//order implicitly set to 0
	rep->windows = malloc(sizeof(Window));
	if(!rep->windows){
		fprintf(stderr, "Failed to allocate memory\n");
		//implicitly forget the window
		nwindows--;
		return 1;
	}

	rep->nwindows = 1;
	rep->windows[0] = window;
	rep->mode = repatriated;
	rep->display_id = display_id;
	rep->frame_id = frame_id;
	rep->state = running;
	return 0;
}

int child_raise(rpcd_child_t* child, size_t display_id, size_t frame_id){
	if(child->display_id != display_id){
		fprintf(stderr, "Failed to raise window for child %s: mismatched display\n", child->name);
		return 1;
	}

	//update the frame and reorder the window to the top of the stack
	child->frame_id = frame_id;
	child->order = child_restack();
	return 0;
}

int child_new(char* name, size_t command){
	size_t u, n = command ? ncommands : nwindows;
	rpcd_child_t* pool = command ? commands : windows;
	rpcd_child_t* child = NULL;

	for(u = 0; u < n; u++){
		if(pool[u].name && !strcasecmp(pool[u].name, name)){
			fprintf(stderr, "%s %s already defined\n", command ? "Command" : "Window", name);
			return 1;
		}
	}

	child = command ? child_allocate_command() : child_allocate_window();
	if(!child){
		return 1;
	}

	child->name = strdup(name);
	if(!child->name){
		fprintf(stderr, "Failed to allocate memory\n");
		//forget the new child
		if(command){
			ncommands--;
		}
		else{
			nwindows--;
		}
		return 1;
	}

	last_command = command;
	return 0;
}

int child_config(char* option, char* value){
	size_t u;
	argument_type new_type = arg_string;
	char* token = NULL;
	rpcd_child_t* last = last_command ? (commands + (ncommands - 1)) : (windows + (nwindows - 1));

	if((last_command && !commands)
			|| (!last_command && !windows)){
		fprintf(stderr, "No children to configure yet\n");
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
	else if(!strcmp(option, "chdir")){
		if(last->working_directory){
			fprintf(stderr, "Option chdir specified multiple times for command %s\n", last->name);
			return 1;
		}
		last->working_directory = strdup(value);
		return 0;
	}

	//TODO filters

	//window-specific
	if(!last_command){
		if(!strcmp(option, "mode")){
			if(!strcmp(value, "ondemand")){
				last->mode = ondemand;
			}
			else if(!strcmp(value, "keepalive")){
				last->mode = keepalive;
			}
			else{
				fprintf(stderr, "Unknown window mode %s\n", value);
				return 1;
			}
			return 0;
		}
		fprintf(stderr, "Unknown option %s for type window\n", option);
		return 1;
	}

	//command-specific
	if(!strcmp(option, "description")){
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
	rpcd_child_t* last = NULL;

	if(commands){
		last = commands + (ncommands - 1);
		if(!last->name){
			fprintf(stderr, "Command defined without name\n");
			return 1;
		}
		if(!last->command){
			fprintf(stderr, "Command %s missing command definition\n", last->name);
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
	}

	if(windows){
		last = windows + (nwindows - 1);
		//repatriated windows have neither name or command set
		if(last->mode != repatriated){
			if(!last->name){
				fprintf(stderr, "Window defined without name\n");
				return 1;
			}
			if(!last->command){
				fprintf(stderr, "Window %s missing command definition\n", last->name);
				return 1;
			}
		}
	}

	return 0;
}

void child_cleanup(){
	size_t u, done = 0;

	//stop all executing instances
	while(!done){
		done = 1;
		for(u = 0; u < ncommands; u++){
			if(commands[u].state != stopped){
				child_stop(commands + u);
				done = 0;
			}
		}

		for(u = 0; u < nwindows; u++){
			if(windows[u].state != stopped){
				child_stop(windows + u);
				done = 0;
			}
		}
		child_reap();
	}

	for(u = 0; u < ncommands; u++){
		child_free(commands + u);
	}

	for(u = 0; u < nwindows; u++){
		child_free(windows + u);
	}

	free(commands);
	ncommands = 0;
	commands = NULL;
	free(windows);
	nwindows = 0;
	windows = NULL;
}
