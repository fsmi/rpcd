#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>

#include "web.h"

static int listen_fd = -1;
static size_t nclients = 0;
static http_client_t* clients = NULL;

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

static void web_disconnect(http_client_t* client){
	close(client->fd);

	client->fd = -1;
	client->recv_offset = 0;
	client->payload_size = 0;
	client->method = method_unknown;
	client->state = http_new;
	free(client->endpoint);
	client->endpoint = NULL;
}

static void web_client_init(http_client_t* client){
	http_client_t empty_client = {
		0
	};

	empty_client.fd = -1;

	*client = empty_client;
}

static int web_accept(){
	size_t u;
	int fd = accept(listen_fd, NULL, NULL);

	if(fd < 0){
		fprintf(stderr, "Invalid fd accepted\n");
		return 1;
	}

	for(u = 0; u < nclients; u++){
		if(clients[u].fd < 0){
			break;
		}
	}

	if(u == nclients){
		clients = realloc(clients, (nclients + 1) * sizeof(http_client_t));
		if(!clients){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		web_client_init(clients + nclients);
		nclients++;
	}

	clients[u].fd = fd;
	return 0;
}

static int web_send_header(http_client_t* client, char* code){
	return network_send(client->fd, "HTTP/1.1 ")
		|| network_send(client->fd, code)
		|| network_send(client->fd, "\r\nAccess-Control-Allow-Origin: *\r\n")
		|| network_send(client->fd, "Connection: close\r\n")
		|| network_send(client->fd, "Server: rpcd\r\n\r\n");
}

static int web_handle_header(http_client_t* client){
	char* line = client->recv_buf;
	
	//reject header folding
	if(isspace(*line)){
		web_send_header(client, "400 Bad Request");
		web_disconnect(client);
		return 0;
	}

	//read method & endpoint
	if(client->state == http_new){
		if(strlen(line) < 5){
			fprintf(stderr, "Received short HTTP initiation, rejecting\n");
			web_send_header(client, "400 Bad Request");
			web_disconnect(client);
		}
		else{
			if(!strncmp(line, "GET ", 4)){
				client->method = http_get;
				client->endpoint = strdup(line + 4);
			}
			else if(!strncmp(line, "POST ", 5)){
				client->method = http_post;
				client->endpoint = strdup(line + 5);
			}
			else{
				fprintf(stderr, "Unknown HTTP method: %s\n", line);
				web_disconnect(client);
				return 0;
			}

			if(!client->endpoint){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}

			client->state = http_headers;
		}
	}
	else{
		//detect end of header data
		if(strlen(line) == 0){
			client->state = http_data;

			if(client->method == http_post && !client->payload_size){
				fprintf(stderr, "Received POST request without Content-length header, rejecting\n");
				web_send_header(client, "400 Bad Request");
				web_disconnect(client);
			}
		}

		//try to find content length header
		if(!strncasecmp(line, "Content-length:", 15)){
			client->payload_size = strtoul(line + 16, NULL, 10);
		}
	}

	return 0;
}

static int web_handle_body(http_client_t* client){
	if(!strcmp(client->endpoint, "/commands")){
	}
	else if(!strcmp(client->endpoint, "/layouts")){
	}
	else if(!strcmp(client->endpoint, "/reset")){
	}
	else if(!strcmp(client->endpoint, "/stop")){
	}
	else if(!strcmp(client->endpoint, "/status")){
	}
	else if(!strncmp(client->endpoint, "/layout/", 8)){
	}
	else if(!strncmp(client->endpoint, "/command/", 9)){
	}
	else{
		web_send_header(client, "400 Unknown Endpoint");
		network_send(client->fd, "The requested endpoint is not supported");
	}
	web_disconnect(client);
	return 0;
}

static int web_data(http_client_t* client){
	ssize_t u, bytes_recv, bytes_left = client->data_allocated - client->recv_offset;
	if(bytes_left < RECV_CHUNK){
		client->recv_buf = realloc(client->recv_buf, (client->data_allocated + RECV_CHUNK) * sizeof(char));
		if(!client->recv_buf){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}

		client->data_allocated += RECV_CHUNK;
		bytes_left += RECV_CHUNK;
	}

	bytes_recv = recv(client->fd, client->recv_buf + client->recv_offset, bytes_left, 0);
	if(bytes_recv < 0){
		fprintf(stderr, "Failed to receive from HTTP client: %s\n", strerror(errno));
		return 1;
	}
	else if(bytes_recv == 0){
		web_disconnect(client);
		return 0;
	}

	//handle complete lines (except body data)
	for(u = 0; client->state != http_data && u < bytes_recv - 1; u++){
		if(!strncmp(client->recv_buf + client->recv_offset + u, "\r\n", 2)){
			//terminate complete line
			client->recv_buf[client->recv_offset + u] = 0;

			//handle header lines
			if(web_handle_header(client)){
				return 1;
			}

			//FIXME might want to check for inline disconnect here

			//remove line from buffer
			bytes_recv -= (u + 2);
			memmove(client->recv_buf, client->recv_buf + client->recv_offset + u + 2, bytes_recv);
			client->recv_offset = 0;
			//start at the beginning of the buffer (incremented by loop)
			u = -1;
		}
	}

	//handle http body
	if(client->state == http_data){
		if((client->payload_size && client->recv_offset + bytes_recv == client->payload_size) || !client->payload_size){
			//handle the request
			return web_handle_body(client);
		}
		else{
			fprintf(stderr, "Missing %zu bytes of payload data, waiting for input\n", client->payload_size - (client->recv_offset + bytes_recv));
		}
	}

	client->recv_offset += bytes_recv;
	return 0;
}

int web_loop(fd_set* in, fd_set* out, int* max_fd){
	size_t u;

	//re-select on the listen fd
	FD_SET(listen_fd, out);
	*max_fd = (listen_fd > *max_fd) ? listen_fd : *max_fd;

	if(FD_ISSET(listen_fd, in)){
		//handle new clients
		if(web_accept()){
			return 1;
		}
	}

	for(u = 0; u < nclients; u++){
		if(clients[u].fd >= 0 && FD_ISSET(clients[u].fd, in)){
			//handle client data
			if(web_data(clients + u)){
				return 1;
			}
		}

		//not collapsing conditions allows us to respond to disconnects in the previous handler
		if(clients[u].fd >= 0){
			//re-select on the client
			FD_SET(clients[u].fd, out);
			*max_fd = (clients[u].fd > *max_fd) ? clients[u].fd : *max_fd;
		}
	}

	return 0;
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
	size_t u;
	close(listen_fd);
	listen_fd = -1;

	for(u = 0; u < nclients; u++){
		web_disconnect(clients + u);
		free(clients[u].recv_buf);
		web_client_init(clients + u);
	}
	free(clients);
	nclients = 0;
}
