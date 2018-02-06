#include "control.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <errno.h>
#include <ctype.h>

#include "child.h"

static size_t init_done = 0;

static size_t nvars = 0;
static variable_t* vars = NULL;

static size_t nfds = 0;
static control_input_t* fds = NULL;

static size_t noperations = 0;
static automation_operation_t* operations = NULL;
static display_config_t* display_status = NULL;

static size_t nassign = 0;
static automation_assign_t* assign = NULL;

static ssize_t control_variable_find(char* name){
	ssize_t u;

	for(u = 0; u < nvars; u++){
		if(!strcasecmp(name, vars[u].name)){
			return u;
		}
	}

	return -1;
}

static int control_input(input_type_t type, int fd){
	size_t u;
	control_input_t new_input = {
		.type = type,
		.fd = fd
	};

	for(u = 0; u < nfds; u++){
		if(fds[u].fd < 0){
			break;
		}
	}

	if(u == nfds){
		fds = realloc(fds, (nfds + 1) * sizeof(control_input_t));
		if(!fds){
			nfds = 0;
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		nfds++;
	}
	else{
		new_input.recv_buffer = fds[u].recv_buffer;
		new_input.recv_alloc = fds[u].recv_alloc;
	}

	fds[u] = new_input;
	return 0;
}

static int control_close(control_input_t* client){
	close(client->fd);
	client->fd = -1;
	client->recv_offset = 0;
	return 0;
}

static int control_new_socket(char* path){
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un info = {
		.sun_family = AF_UNIX
	};

	if(fd < 0){
		fprintf(stderr, "Failed to create socket: %ss\n", strerror(errno));
		return 1;
	}

	//unsafe unlink, should probably use a lockfile - however, we assume to be the only running instance
	//and actually want clients to come to us instead of old instances, so this works
	unlink(path);

	strncpy(info.sun_path, path, sizeof(info.sun_path) - 1);
	if(bind(fd, (struct sockaddr*) &info, sizeof(info))){
		fprintf(stderr, "Failed to bind socket path %s: %s\n", path, strerror(errno));
		close(fd);
		return 1;
	}

	if(listen(fd, LISTEN_QUEUE_LENGTH)){
		fprintf(stderr, "Failed to listen for socket %s: %s\n", path, strerror(errno));
		close(fd);
		return 1;
	}
	return control_input(control_socket, fd);
}

static int control_new_fifo(char* path){
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if(fd < 0){
		if(errno == ENOENT){
			if(mkfifo(path, S_IRUSR | S_IWUSR)){
				fprintf(stderr, "Failed to create FIFO at %s: %s\n", path, strerror(errno));
				return 1;
			}
			fd = open(path, O_RDWR | O_NONBLOCK);
		}
		else{
			fprintf(stderr, "Failed to open FIFO at %s: %s\n", path, strerror(errno));
			return 1;
		}
	}

	return control_input(control_fifo, fd);
}

static int control_accept(control_input_t* sock){
	int fd = accept(sock->fd, NULL, NULL);

	if(fd < 0){
		fprintf(stderr, "Failed to accept control socket client: %s\n", strerror(errno));
		return 1;
	}

	return control_input(control_client, fd);
}

static int control_command(char* command){
	ssize_t var;
	char* sep = NULL;
	if(!strchr(command, '=')){
		fprintf(stderr, "Invalid control command received: %s\n", command);
		return 1;
	}

	sep = strchr(command, '=');
	*sep++ = 0;

	var = control_variable_find(command);
	if(var < 0){
		fprintf(stderr, "Variable %s not found for control command\n", command);
		return 0;
	}

	if(!strcmp(vars[var].value, sep)){
		fprintf(stderr, "Variable %s unchanged\n", command);
		return 0;
	}

	free(vars[var].value);
	vars[var].value = strdup(sep);
	if(!vars[var].value){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	return control_run_automation();
}

static int control_data(control_input_t* client){
	size_t u, c, bytes_left = client->recv_alloc - client->recv_offset;
	ssize_t bytes_read = 0;

	//extend the buffer if necessary
	if(bytes_left < CLIENT_RECV_CHUNK){
		client->recv_buffer = realloc(client->recv_buffer, (client->recv_alloc + CLIENT_RECV_CHUNK) * sizeof(char));
		if(!client->recv_buffer){
			fprintf(stderr, "Failed to allocate memory\n");
			client->recv_alloc = client->recv_offset = 0;
			return 1;
		}

		client->recv_alloc += CLIENT_RECV_CHUNK;
		bytes_left += CLIENT_RECV_CHUNK;
	}

	bytes_read = read(client->fd, client->recv_buffer + client->recv_offset, bytes_left);
	if(bytes_read < 0){
		fprintf(stderr, "Failed to read from client: %s\n", strerror(errno));
		return control_close(client);
	}
	else if(bytes_read == 0){
		//client closed
		if(client->type == control_client){
			return control_close(client);
		}
		return 0;
	}

	//scan for \n in new bytes while eliminating control characters, handle line if done
	c = 0;
	for(u = 0; u < bytes_read; u++){
		if(client->recv_buffer[u + client->recv_offset] == '\n'){
			//line terminated
			client->recv_buffer[c + client->recv_offset] = 0;
			control_command(client->recv_buffer);

			//reposition scan head, reset read offset;
			client->recv_offset = 0;
			c = 0;
			continue;
		}

		//strip control characters
		if(!isprint(client->recv_buffer[u + client->recv_offset])){
			continue;
		}

		client->recv_buffer[c + client->recv_offset] = client->recv_buffer[u + client->recv_offset];
		c++;
	}

	client->recv_offset += c;
	if(client->recv_offset > CLIENT_RECV_LIMIT){
		if(client->type == control_client){
			control_close(client);
		}
		client->recv_offset = 0;
	}
	return 0;
}

int control_config(char* option, char* value){
	if(!strcmp(option, "socket")){
		return control_new_socket(value);
	}
	else if(!strcmp(option, "fifo")){
		return control_new_fifo(value);
	}

	fprintf(stderr, "Unknown option %s for control section\n", option);
	return 1;
}

int control_config_variable(char* name, char* value){
	size_t u;
	if(strlen(name) < 1){
		fprintf(stderr, "Control variable name unset\n");
		return 1;
	}

	if(isdigit(name[0])){
		fprintf(stderr, "Invalid control variable name: %s\n", name);
		return 1;
	}

	for(u = 0; u < strlen(name); u++){
		if(!isascii(name[u]) || !(isalnum(name[u]) || name[u] == '_')){
			fprintf(stderr, "Invalid control variable name: %s\n", name);
			return 1;
		}
	}

	if(control_variable_find(name) >= 0){
		fprintf(stderr, "Control variable %s already defined\n", name);
		return 1;
	}

	vars = realloc(vars, (nvars + 1) * sizeof(variable_t));
	if(!vars){
		fprintf(stderr, "Failed to allocate memory\n");
		nvars = 0;
		return 1;
	}

	vars[nvars].name = strdup(name);
	if(value){
		vars[nvars].value = strdup(value);
	}
	else{
		vars[nvars].value = strdup("");
	}

	if(!vars[nvars].name || !vars[nvars].value){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}
	nvars++;
	return 0;
}

static size_t control_evaluate_condition(automation_operation_t* op){
	size_t rv = 0;
	char* operand_a = op->operand_a, *operand_b = op->operand_b;

	if(op->resolve_a){
		//since the automation parser assures that all referenced variables exist, this works
		operand_a = vars[control_variable_find(operand_a)].value;
	}

	if(op->resolve_b){
		operand_b = vars[control_variable_find(operand_b)].value;
	}

	switch(op->op){
		case op_condition_greater:
			rv = (strtol(operand_a, NULL, 10) > strtol(operand_b, NULL, 10)) ? 0 : 1;
			//fprintf(stderr, "cond_greater: %s %s -> skip %zu\n", operand_a, operand_b, rv);
			break;
		case op_condition_less:
			rv = (strtol(operand_a, NULL, 10) < strtol(operand_b, NULL, 10)) ? 0 : 1;
			//fprintf(stderr, "cond_less: %s %s -> skip %zu\n", operand_a, operand_b, rv);
			break;
		case op_condition_equals:
			rv = strcmp(operand_a, operand_b) ? 1 : 0;
			//fprintf(stderr, "cond_equals: %s %s -> skip %zu\n", operand_a, operand_b, rv);
			break;
		case op_condition_empty:
			rv = strlen(operand_a) ? 1 : 0;
			//fprintf(stderr, "cond_empty: %s -> skip %zu\n", operand_a, rv);
			break;
		default:
			fprintf(stderr, "Unhandled conditional operation, result undefined\n");
			break;
	}

	return op->negate ? (rv ? 0 : 1) : rv;
}

static automation_operation_t* control_new_operation(){
	automation_operation_t empty = {
		0
	};

	operations = realloc(operations, (noperations + 1) * sizeof(automation_operation_t));
	if(!operations){
		fprintf(stderr, "Failed to allocate memory\n");
		noperations = 0;
		return NULL;
	}

	operations[noperations] = empty;
	noperations++;
	return operations + (noperations - 1);
}

static int control_build_environment(command_instance_t* inst){
	size_t u;
	inst->nargs = nvars * 2;
	inst->arguments = calloc(nvars * 2, sizeof(char*));
	if(!inst->arguments){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}
	for(u = 0; u < nvars; u++){
		inst->arguments[u * 2] = strdup(vars[u].name);
		inst->arguments[(u * 2) + 1] = strdup(vars[u].value);
		if(!inst->arguments[u * 2] || !inst->arguments[(u * 2) + 1]){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
	}
	return 0;
}

int control_run_automation(){
	size_t u, p, active_assigns = 0, done;
	int rv = 0;
	automation_operation_t* op = NULL;
	rpcd_child_t* window = NULL;
	command_instance_t instance_env = {
		0
	};

	//early exit
	if(!noperations || !init_done){
		return 0;
	}

	if(!display_status){
		//initialize display status list
		display_status = calloc(x11_count(), sizeof(display_config_t));
		if(!display_status){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
	}

	//set initial display states
	for(u = 0; u < x11_count(); u++){
		display_status[u].display = x11_get(u);
		display_status[u].status = x11_get(u)->busy ? display_busy : display_ready;
		display_status[u].layout = NULL;
	}

	//run the script to obtain the requested configuration
	for(u = 0; u < noperations; u++){
		op = operations + u;
		switch(op->op){
			case op_noop:
				break;
			case op_layout_default:
				display_status[op->display_id].layout = display_status[op->display_id].display->default_layout;
				break;
			case op_layout:
				display_status[op->display_id].layout = layout_find(op->display_id, op->operand_a);
				break;
			case op_assign:
				done = 0;
				//if an assign for the frame was already registered, replace it
				for(p = 0; p < active_assigns; p++){
					//assigning a window twice invalidates the first assign
					if(assign[p].requested && !strcmp(assign[p].requested, op->operand_a)){
						assign[p].requested = NULL;
					}
					if(assign[p].display_id == op->display_id
							&& assign[p].frame_id == op->operand_numeric){
						assign[p].requested = op->operand_a;
						done = 1;
					}
				}
				if(done){
					break;
				}

				//reallocate if necessary
				if(active_assigns <= nassign){
					assign = realloc(assign, (nassign + 1) * sizeof(automation_assign_t));
					if(!assign){
						fprintf(stderr, "Failed to allocate memory\n");
						nassign = 0;
						return 1;
					}
					nassign++;
				}

				assign[active_assigns].display_id = op->display_id;
				assign[active_assigns].frame_id = op->operand_numeric;
				assign[active_assigns].requested = op->operand_a;
				active_assigns++;
				break;
			case op_skip:
				u += operations[u].operand_numeric;
				break;
			case op_condition_greater:
			case op_condition_less:
			case op_condition_equals:
			case op_condition_empty:
				u += control_evaluate_condition(operations + u);
				break;
			case op_stop:
				fprintf(stderr, "Automation script execution stopped by operation\n");
				goto apply_results;
		}
	}

	fprintf(stderr, "Automation script execution stopped by end of instruction list\n");

apply_results:
	//start requested windows
	for(u = 0; u < active_assigns; u++){
		if(!assign[u].requested){
			continue;
		}

		//do not start on busy display
		if(display_status[assign[u].display_id].status == display_busy){
			continue;
		}

		window = child_window_find(assign[u].requested);

		//if stopped, start
		if(window->state == stopped){
			if(window->start_iteration >= WINDOW_START_RETRIES){
				fprintf(stderr, "Automation failed to start window %s in %zu tries, ignoring\n", window->name, window->start_iteration);
				continue;
			}
			//build argument list if not done yet
			if(!instance_env.arguments && control_build_environment(&instance_env)){
				return 1;
			}

			fprintf(stderr, "Automation starting window %s, iteration %zu\n", window->name, window->start_iteration);
			child_start(window, assign[u].display_id, assign[u].frame_id, &instance_env);
			//wait for window
			display_status[assign[u].display_id].status = display_waiting;
			continue;
		}

		//if running on different display, terminate
		if(window->display_id != assign[u].display_id){
			child_stop(window);
			//wait for termination
			display_status[assign[u].display_id].status = display_waiting;
			continue;
		}

		if(!window->nwindows){
			//still waiting for windows to be mapped
			display_status[assign[u].display_id].status = display_waiting;
			continue;
		}

		//TODO if replacing ondemand window, kill old occupant
		child_raise(window, assign[u].display_id, assign[u].frame_id);
	}

	//apply requested layouts
	for(u = 0; u < x11_count(); u++){
		if(display_status[u].status == display_ready
				&& display_status[u].layout){
			//TODO use damage tracking to apply the layout only if it changes
			if(x11_activate_layout(display_status[u].layout)){
				fprintf(stderr, "Automation failed to activate layout %s on display %zu, exiting\n", display_status[u].layout->name, u);
				rv = 1;
				goto cleanup;
			}
			fprintf(stderr, "Automation complete on display %zu\n", u);
		}
	}

cleanup:
	for(u = 0; u < instance_env.nargs; u++){
		free(instance_env.arguments[u]);
	}
	free(instance_env.arguments);
	return rv;
}

static int control_parse_layout(automation_operation_t* op, char* spec){
	//default display
	op->display_id = 0;
	if(strchr(spec, '/')){
		*strchr(spec, '/') = 0;
		op->display_id = x11_find_id(spec);
		spec += strlen(spec) + 1;
	}

	if(layout_find(op->display_id, spec)){
		op->operand_a = strdup(spec);
		if(!op->operand_a){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		op->op = op_layout;
		return 0;
	}

	fprintf(stderr, "No layout %s defined on display %zu\n", spec, op->display_id);
	return 1;
}

static int control_parse_assign(automation_operation_t* op, char* spec){
	char* dest = strchr(spec, ' ');
	op->display_id = 0;

	if(!dest){
		fprintf(stderr, "No frame destination for assign call\n");
		return 1;
	}

	*dest++ = 0;

	if(!child_window_find(spec)){
		fprintf(stderr, "Window %s not defined as assignable\n", spec);
		return 1;
	}

	if(strchr(dest, '/')){
		*strchr(dest, '/') = 0;
		op->display_id = x11_find_id(dest);
		dest += strlen(dest) + 1;
	}
	op->operand_numeric = strtoul(dest, NULL, 10);

	op->operand_a = strdup(spec);
	if(!op->operand_a){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	op->op = op_assign;
	return 0;
}

static char* control_parse_operand(char* spec, size_t* resolve, char** operand){
	*resolve = 1;
	size_t end = 0;

	if(!spec[0] || !isascii(spec[0]) || isblank(spec[0])){
		fprintf(stderr, "Invalid conditional operand expression\n");
		return NULL;
	}

	if(spec[0] == '"'){
		*resolve = 0;
		spec++;
		//scan until quote end
		for(end = 0; spec[end] && spec[end] != '"'; end++){
		}

		if(!spec[end]){
			fprintf(stderr, "Unterminated quoted string in conditional expression\n");
			return NULL;
		}

		spec[end] = ' ';
	}
	else{
		if(isdigit(spec[0])){
			*resolve = 0;
		}
		for(end = 0; spec[end] && !isblank(spec[end]); end++){
			if(!isascii(spec[end]) || !(isalnum(spec[end]) || spec[end] == '_')){
				*resolve = 0;
			}
		}
	}

	*operand = calloc(end + 1, sizeof(char));
	if(!*operand){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}
	strncpy(*operand, spec, end);

	//assert that variables exist
	if(*resolve && control_variable_find(*operand) < 0){
		fprintf(stderr, "Conditional operand variable %s does not exist\n", *operand);
		return NULL;
	}
	//advance until next graph
	for(; spec[end] && isblank(spec[end]); end++){
	}
	return spec + end;
}

static int control_parse_conditional(automation_operation_t* op, char* spec){
	char* body = strchr(spec, ',');
	if(!body){
		fprintf(stderr, "Empty conditional body\n");
		return 1;
	}
	*body++ = 0;
	for(; isspace(body[0]); body++){
	}

	if(!strncmp(spec, "not ", 4)){
		op->negate = 1;
		spec += 4;
	}

	if(!strncmp(spec, "empty ", 6)){
		op->op = op_condition_empty;
		spec += 6;
	}

	//parse operand a
	spec = control_parse_operand(spec, &op->resolve_a, &op->operand_a);

	if(!spec){
		fprintf(stderr, "Failed to parse first operand\n");
		op->op = op_noop;
		return 1;
	}

	if(op->op == op_condition_empty){
		goto parse_done;
	}

	switch(spec[0]){
		case '>':
			op->op = op_condition_greater;
			break;
		case '<':
			op->op = op_condition_less;
			break;
		case '=':
			op->op = op_condition_equals;
			break;
		default:
			fprintf(stderr, "Unknown conditional expression: %c, next tokens %s\n", spec[0], spec);
			op->op = op_noop;
			return 1;
	}

	for(spec++; spec[0] && isspace(spec[0]); spec++){
	}

	spec = control_parse_operand(spec, &op->resolve_b, &op->operand_b);
	if(!spec){
		fprintf(stderr, "Failed to parse second operand\n");
		op->op = op_noop;
		return 1;
	}

parse_done:
	//this is kind of yucky, but it works (unless the body contains another conditional...)
	return control_config_automation(body);
}

int control_config_automation(char* line){
	automation_operation_t* op = control_new_operation();
	if(!op){
		return 1;
	}

	//empty lines and comments are handled by the config parser, but nevertheless
	if(!strlen(line)){
		fprintf(stderr, "Synthesized noop for empty line\n");
		return 0;
	}

	if(!strncmp(line, "default ", 8)){
		op->display_id = x11_find_id(line + 8);
		op->op = op_layout_default;
		return 0;
	}
	else if(!strncmp(line, "layout ", 7)){
		if(control_parse_layout(op, line + 7)){
			fprintf(stderr, "Failed to parse automation layout call\n");
			return 1;
		}
		return 0;
	}
	else if(!strncmp(line, "assign ", 7)){
		if(control_parse_assign(op, line + 7)){
			fprintf(stderr, "Failed to parse automation assign call\n");
			return 1;
		}
		return 0;
	}
	else if(!strncmp(line, "skip ", 5)){
		op->operand_numeric = strtoul(line + 5, NULL, 10);
		if(op->operand_numeric){
			op->op = op_skip;
			return 0;
		}
		fprintf(stderr, "Failed to parse automation skip operand\n");
		return 1;
	}
	else if(!strcmp(line, "done")){
		op->op = op_stop;
		return 0;
	}
	else if(!strncmp(line, "if ", 3)){
		if(control_parse_conditional(op, line + 3)){
			fprintf(stderr, "Failed to parse automation conditional\n");
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "Failed to parse automation line: %s\n", line);
	return 1;
}

int control_loop(fd_set* in, fd_set* out, int* max_fd){
	size_t u;
	command_instance_t env = {
		0
	};

	if(!init_done){
		if(control_build_environment(&env)){
			return 1;
		}
		
		//this starts keepalive windows, but only allows one try - which is ok for most,
		//as when they are really needed, automation will retry to start
		for(u = 0; u < child_window_count(); u++){
			if(child_window_get(u)->mode == keepalive){
				//TODO need environment to start here
				fprintf(stderr, "Control starting keepalive window %s\n", child_window_get(u)->name);
				child_start(child_window_get(u), 0, 0, &env);
			}
		}
		//checked in _run_automation
		init_done = 1;
		if(control_run_automation()){
			return 1;
		}

		//free the environment instance
		for(u = 0; u < env.nargs; u++){
			free(env.arguments[u]);
		}
		free(env.arguments);
	}

	for(u = 0; u < nfds; u++){
		if(fds[u].fd >= 0 && FD_ISSET(fds[u].fd, in)){
			switch(fds[u].type){
				case control_socket:
					if(control_accept(fds + u)){
						return 1;
					}
					break;
				case control_fifo:
				case control_client:
					if(control_data(fds + u)){
						return 1;
					}
					break;
			}
		}

		//push all our fds to the core loop
		if(fds[u].fd >= 0){
			*max_fd = (*max_fd > fds[u].fd) ? *max_fd : fds[u].fd;
			FD_SET(fds[u].fd, out);
		}
	}
	return 0;
}

int control_ok(){
	if(!nfds){
		fprintf(stderr, "No control inputs defined, continuing\n");
	}
	return 0;
}

void control_cleanup(){
	size_t u;
	init_done = 0;

	for(u = 0; u < nvars; u++){
		free(vars[u].name);
		free(vars[u].value);
	}
	free(vars);
	vars = NULL;
	nvars = 0;

	for(u = 0; u < nfds; u++){
		if(fds[u].fd >= 0){
			close(fds[u].fd);
		}
		free(fds[u].recv_buffer);
	}
	free(fds);
	fds = NULL;
	nfds = 0;

	for(u = 0; u < noperations; u++){
		free(operations[u].operand_a);
		free(operations[u].operand_b);
	}
	free(operations);
	operations = NULL;
	noperations = 0;
	free(display_status);
	display_status = NULL;

	free(assign);
	nassign = 0;
	assign = NULL;
}
