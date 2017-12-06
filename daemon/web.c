#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>

#include "web.h"

static int listen_fd = -1;

static int network_listener(char* host, char* port, int socktype){
	int fd = -1, error;
	struct addrinfo* head, *iter;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = socktype,
		.ai_flags = AI_PASSIVE
	};

	error = getaddrinfo(host, port, &hints, &head);
	if(error){
		fprintf(stderr, "Failed to open a %s socket for %s port %s: %s\n", 
				(socktype == SOCK_STREAM) ? "TCP":"UDP", host, port, gai_strerror(error));
		return -1;
	}

	for(iter = head; iter; iter = iter->ai_next){
		fd = socket(iter->ai_family, iter->ai_socktype, iter->ai_protocol);
		if(fd < 0){
			continue;
		}

		error = 0;
		if(setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&error, sizeof(error))){
			fprintf(stderr, "Failed to enable dual-stack operation on port %s: %s\n", port, strerror(errno));
		}

		error = 1;
		if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&error, sizeof(error))){
			fprintf(stderr, "Failed to allow socket reuse on port %s: %s\n", port, strerror(errno));
		}

		if(bind(fd, iter->ai_addr, iter->ai_addrlen)){
			close(fd);
			continue;
		}

		break;
	}

	freeaddrinfo(head);

	if(!iter){
		fprintf(stderr, "Unable to create socket for %s port %s\n", host, port);
		return -1;
	}

	if(socktype == SOCK_DGRAM){
		return fd;
	}

	if(listen(fd, LISTEN_QUEUE_LENGTH)){
		fprintf(stderr, "Failed to listen: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

static int network_send(int fd, char* data){
	ssize_t total = 0, sent;

	while(total < strlen(data)){
		sent = send(fd, data + total, strlen(data) - total, 0);
		if(sent < 0){
			fprintf(stderr, "Failed to send: %s\n", strerror(errno));
			return 1;
		}
		total += sent;
	}
	return 0;
}

int web_loop(fd_set* in, fd_set* out, int* max_fd){
	return 1;
}

int web_config(char* option, char* value){
	char* separator = value;
	if(!strcmp(option, "bind")){
		separator = strchr(value, ' ');
		if(separator){
			*separator = 0;
			separator++;
		}
		else{
			separator = DEFAULT_PORT;
		}

		listen_fd = network_listener(value, separator, SOCK_STREAM);
		return listen_fd < 0;
	}

	fprintf(stderr, "Unknown option %s for web section\n", option);
	return 1;
}

int web_ok(){
	if(listen_fd < 0){
		fprintf(stderr, "No listening socket for API\n");
		return 1;
	}
	return 0;
}

void web_cleanup(){
	close(listen_fd);
	listen_fd = -1;
	//TODO disconnect clients, free data
}
