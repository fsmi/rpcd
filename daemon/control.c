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

#include "x11.h"
#include "layout.h"

static size_t nvars = 0;
static variable_t* vars = NULL;

static size_t nfds = 0;
static control_input_t* fds = NULL;

static size_t noperations = 0;
static automation_operation_t* operations = NULL;
static display_status_t* display_status = NULL;

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
	fprintf(stderr, "Control command: %s\n", command);
	return 0;
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

static ssize_t control_variable_find(char* name){
	ssize_t u;

	for(u = 0; u < nvars; u++){
		if(!strcasecmp(name, vars[u].name)){
			return u;
		}
	}

	return -1;
}

int control_config_variable(char* name, char* value){
	if(strlen(name) < 1){
		fprintf(stderr, "Control variable name unset\n");
		return 1;
	}

	if(isdigit(name[0]) || name[0] == '-'){
		fprintf(stderr, "Invalid control variable name: %s\n", name);
		return 1;
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
			rv = (strtoul(operand_a, NULL, 10) > strtoul(operand_b, NULL, 10)) ? 1 : 0;
			break;
		case op_condition_less:
			rv = (strtoul(operand_a, NULL, 10) < strtoul(operand_b, NULL, 10)) ? 1 : 0;
			break;
		case op_condition_equals:
			rv = strcmp(op->operand_a, op->operand_b) ? 0 : 1;
			break;
		case op_condition_empty:
			rv = strlen(operand_a) ? 0 : 1;
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

int control_run_automation(){
	size_t u;
	layout_t* layout = NULL;

	//early exit
	if(!noperations){
		return 0;
	}

	if(!display_status){
		//initialize display status list
		display_status = calloc(x11_count(), sizeof(display_status_t));
	}

	//set initial display states
	for(u = 0; u < x11_count(); u++){
		display_status[u] = x11_get(u)->busy ? display_busy : display_ready;
	}

	for(u = 0; u < noperations; u++){
		switch(operations[u].op){
			case op_noop:
				break;
			case op_layout_default:
				if(display_status[operations[u].display_id] == display_ready){
					if(x11_default_layout(operations[u].display_id)){
						return 1;
					}
				}
				break;
			case op_layout:
				if(display_status[operations[u].display_id] == display_ready){
					//this works because the automation parser resolves layouts at parse time
					layout = layout_get(operations[u].operand_numeric);
					if(x11_activate_layout(layout)){
						fprintf(stderr, "Automation failed to activate layout %s, exiting\n", layout->name);
						return 1;
					}
				}
				break;
			case op_assign:
				if(display_status[operations[u].display_id] != display_busy){
					//TODO stop command if on different display
					//TODO start command if not started
					//if window not ready, set display not ready
				}
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
				fprintf(stderr, "Automation stopped by operation\n");
				return 0;
		}
	}

	fprintf(stderr, "Automation stopped by end of instruction list\n");
	return 0;
}

int control_config_automation(char* line){
	//TODO assert that all referenced variables exist
	char* token = NULL;
	automation_operation_t* op = control_new_operation();
	if(!op){
		return 1;
	}

	token = strtok(line, NULL);
	if(!token){
		fprintf(stderr, "Synthesized noop\n");
		return 0;
	}

	if(!strcmp(token, "default")){
		token = strtok(NULL, " ");
		if(token){
			op->display_id = x11_find_id(token);
			op->op = op_layout_default;
			return 0;
		}
	}
	else if(!strcmp(token, "layout")){
		token = strtok(NULL, " ");
		if(token){
			op->display_id = x11_find_id(token);
			//op->operand_numeric = 
			//op->op = op_layout;
		}
	}
	else if(!strcmp(token, "assign")){
		//TODO parse window assignment
	}
	else if(!strcmp(token, "skip")){
		token = strtok(NULL, " ");
		if(token){
			op->operand_numeric = strtoul(token, NULL, 10);
			if(op->operand_numeric){
				op->op = op_skip;
				return 0;
			}
			else{
				fprintf(stderr, "Failed to parse skip operand\n");
			}
		}
	}
	else if(!strcmp(token, "done")){
		op->op = op_stop;
		return 0;
	}
	else if(!strcmp(token, "if")){
		//TODO parse conditional statement
	}

	fprintf(stderr, "Failed to parse automation line, previous segment %s\n", line);
	return 1;
}

int control_loop(fd_set* in, fd_set* out, int* max_fd){
	size_t u;

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
}
