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

static size_t nvars = 0;
static variable_t* vars = NULL;

static size_t nfds = 0;
static control_input_t* fds = NULL;

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

static int control_new_socket(char* path){
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un info = {
		.sun_family = AF_UNIX
	};

	if(fd < 0){
		fprintf(stderr, "Failed to create socket: %ss\n", strerror(errno));
		return 1;
	}

	//unsafe unlink, should probably use a lockfile
	unlink(path);

	strncpy(info.sun_path, path, sizeof(info.sun_path) - 1);
	if(bind(fd, (struct sockaddr*) &info, sizeof(info))){
		fprintf(stderr, "Failed to bind socket path %s: %s\n", path, strerror(errno));
		return 1;
	}

	if(listen(fd, LISTEN_QUEUE_LENGTH)){
		fprintf(stderr, "Failed to listen for socket %s: %s\n", path, strerror(errno));
		return 1;
	}
	return control_input(control_socket, fd);
}

static int control_new_fifo(char* path){
	int fd = open(path, O_NONBLOCK);
	if(fd < 0){
		if(errno == ENOENT){
			if(mkfifo(path, S_IRUSR | S_IWUSR)){
				fprintf(stderr, "Failed to create FIFO at %s: %s\n", path, strerror(errno));
				return 1;
			}
		}
		else{
			fprintf(stderr, "Failed to open FIFO at %s: %s\n", path, strerror(errno));
			return 1;
		}
	}

	return control_input(control_fifo, fd);
}

static int control_accept(control_input_t* sock){
	//TODO
	return 0;
}

static int control_data(control_input_t* client){
	//TODO
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
	//TODO
	return 0;
}

int control_config_automation(char* line){
	//TODO
	return 0;
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
}
