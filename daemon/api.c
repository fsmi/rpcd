#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include "x11.h"
#include "child.h"
#include "api.h"

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

		error = fcntl(fd, F_GETFD, 0) | FD_CLOEXEC;
		if(fcntl(fd, F_SETFD, error) < 0){
			fprintf(stderr, "Failed to set FD_CLOEXEC on listener: %s\n", strerror(errno));
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

static void api_disconnect(http_client_t* client){
	if(client->fd >= 0){
		close(client->fd);
	}

	client->fd = -1;
	client->recv_offset = 0;
	client->payload_size = 0;
	client->method = method_unknown;
	client->state = http_new;
	free(client->endpoint);
	client->endpoint = NULL;
}

static void api_client_init(http_client_t* client){
	http_client_t empty_client = {
		0
	};

	empty_client.fd = -1;

	*client = empty_client;
}

static int api_accept(){
	size_t u;
	int fd = accept(listen_fd, NULL, NULL);
	int flags;

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
			close(fd);
			return 1;
		}
		api_client_init(clients + nclients);
		nclients++;
	}

	flags = fcntl(fd, F_GETFD, 0) | FD_CLOEXEC;
	if(fcntl(fd, F_SETFD, flags) < 0){
		fprintf(stderr, "Failed to set FD_CLOEXEC on client fd: %s\n", strerror(errno));
	}

	clients[u].fd = fd;
	return 0;
}

static int api_send_header(http_client_t* client, char* code, bool json){
	return network_send(client->fd, "HTTP/1.1 ")
		|| network_send(client->fd, code)
		|| network_send(client->fd, "\r\nAccess-Control-Allow-Origin: *\r\n")
		|| (json ? network_send(client->fd, "Content-type: application/json\r\n") : 0)
		|| network_send(client->fd, "Connection: close\r\n")
		|| network_send(client->fd, "Server: rpcd\r\n\r\n");
}

static int api_handle_header(http_client_t* client){
	char* line = client->recv_buf;
	
	//reject header folding
	if(isspace(*line)){
		api_send_header(client, "400 Bad Request", false);
		api_disconnect(client);
		return 0;
	}

	//read method & endpoint
	if(client->state == http_new){
		if(strlen(line) < 5){
			fprintf(stderr, "Received short HTTP initiation, rejecting\n");
			api_send_header(client, "400 Bad Request", false);
			api_disconnect(client);
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
				api_disconnect(client);
				return 0;
			}

			if(!client->endpoint){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}

			//strip protocol info
			if(strchr(client->endpoint, ' ')){
				*(strchr(client->endpoint, ' ')) = 0;
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
				api_send_header(client, "400 Bad Request", false);
				api_disconnect(client);
			}
		}

		//try to find content length header
		if(!strncasecmp(line, "Content-length:", 15)){
			client->payload_size = strtoul(line + 16, NULL, 10);
		}
	}

	return 0;
}

static int api_send_commands(http_client_t* client){
	char send_buf[RECV_CHUNK];
	size_t commands = child_command_count(), u, p;
	char** option = NULL;
	rpcd_child_t* command = NULL;

	network_send(client->fd, "[");
	for(u = 0; u < commands; u++){
		command = child_command_get(u);

		//dump a command
		//FIXME escaping
		snprintf(send_buf, sizeof(send_buf), "%s{\"name\":\"%s\",\"description\":\"%s\",\"windows\":%d,\"args\":[",
				u ? "," : "", command->name,
				command->description ? command->description : "",
				(command->mode == user) ? 1 : 0);
		network_send(client->fd, send_buf);

		for(p = 0; p < command->nargs; p++){
			if(command->args[p].type == arg_enum){
				snprintf(send_buf, sizeof(send_buf), "%s{\"name\":\"%s\", \"type\":\"enum\", \"options\":[",
						p ? "," : "", command->args[p].name);
				network_send(client->fd, send_buf);
				for(option = command->args[p].additional; *option; option++){
					snprintf(send_buf, sizeof(send_buf), "%s\"%s\"",
							(option == command->args[p].additional) ? "" : ",",
							*option);
					network_send(client->fd, send_buf);
				}
				snprintf(send_buf, sizeof(send_buf), "]}");
			}
			else{
				snprintf(send_buf, sizeof(send_buf), "%s{\"name\":\"%s\", \"type\":\"string\", \"hint\":\"%s\"}",
						p ? "," : "", command->args[p].name,
						command->args[p].additional ? command->args[p].additional[0] : "");
			}
			network_send(client->fd, send_buf);
		}

		network_send(client->fd, "]}");
	}
	network_send(client->fd, "]");

	return 0;
}

static int api_send_layouts(http_client_t* client){
	char send_buf[RECV_CHUNK];
	size_t layouts = layout_count(), displays = x11_count();
	size_t c, u, p, q, first;
	display_t* display = NULL;
	layout_t* layout = NULL;

	network_send(client->fd, "[");
	for(c = 0; c < displays; c++){
		first = 1;
		display = x11_get(c);

		snprintf(send_buf, sizeof(send_buf), "%s{\"display\":\"%s\",\"layouts\":[",
				c ? "," : "", display->name);
		network_send(client->fd, send_buf);


		for(u = 0; u < layouts; u++){
			layout = layout_get(u);
			if(layout->display_id != c){
				continue;
			}
	
			//dump a single layout
			//FIXME this is kinda ugly and disregards quoting
			snprintf(send_buf, sizeof(send_buf), "%s{\"name\":\"%s\",\"frames\":[",
					first ? "" : ",", layout->name);
			network_send(client->fd, send_buf);
	
			first = 0;
			for(p = 0; p < layout->nframes; p++){
				snprintf(send_buf, sizeof(send_buf), "%s{\"id\":%zu,\"x\":%zu,\"y\":%zu,\"w\":%zu,\"h\":%zu,\"screen\":%zu}",
						p ? "," : "", layout->frames[p].id,
						layout->frames[p].bbox[0], layout->frames[p].bbox[1],
						layout->frames[p].bbox[2], layout->frames[p].bbox[3],
						layout->frames[p].screen[2]);
				network_send(client->fd, send_buf);
			}
	
			network_send(client->fd, "],\"screens\":[");
	
			for(q = 0; q <= layout->max_screen; q++){
				for(p = 0; p < layout->nframes; p++){
					if(layout->frames[p].screen[2] == q){
						snprintf(send_buf, sizeof(send_buf), "%s{\"id\":%zu,\"width\":%zu,\"height\":%zu}",
								q ? "," : "", layout->frames[p].screen[2],
								layout->frames[p].screen[0], layout->frames[p].screen[1]);
						network_send(client->fd, send_buf);
						break;
					}
				}
			}
		
			network_send(client->fd, "]}");
		}
		network_send(client->fd, "]}");
	}
	network_send(client->fd, "]");

	return 0;
}

static int api_send_status(http_client_t* client){
	int rv = 0, first = 1;
	char send_buf[RECV_CHUNK];
	size_t u, n = 0;
	rpcd_child_t* cmd = NULL;
	display_t* display = NULL;
	layout_t* layout = NULL;

	snprintf(send_buf, sizeof(send_buf), "{\"layouts\":%zu,\"commands\":%zu,\"layout\":[",
			layout_count(), child_command_count());
	rv |= network_send(client->fd, send_buf);

	n = x11_count();
	for(u = 0; u < n; u++){
		display = x11_get(u);
		//FIXME this might return NULL
		layout = x11_current_layout(u);

		snprintf(send_buf, sizeof(send_buf), "%s{\"display\":\"%s\",\"layout\":\"%s\"}",
				u ? "," : "", display->name, layout->name);
		rv |= network_send(client->fd, send_buf);
	}

	rv |= network_send(client->fd, "],\"running\":[");
	n = child_command_count();
	for(u = 0; u < n; u++){
		cmd = child_command_get(u);
		if(cmd->state != stopped){
			snprintf(send_buf, sizeof(send_buf), "%s\"%s\"",
					first ? "" : ",", cmd->name);
			rv |= network_send(client->fd, send_buf);
			first = 0;
		}
	}

	rv |= network_send(client->fd, "]}");
	return rv;
}

static int api_handle_reset(){
	size_t u = 0;
	int rv = 0;
	for(u = 0; u < x11_count(); u++){
		//FIXME may want to reap exited children live
		rv |= child_discard_restores(u)
			| child_stop_commands(u)
			| x11_default_layout(u);
	}
	return rv;
}

static int api_handle_body(http_client_t* client){
	int rv = 0;
	if(!strcmp(client->endpoint, "/commands")){
		rv = api_send_header(client, "200 OK", true)
			|| api_send_commands(client);
	}
	else if(!strcmp(client->endpoint, "/layouts")){
		rv = api_send_header(client, "200 OK", true)
			|| api_send_layouts(client);
	}
	else if(!strcmp(client->endpoint, "/reset")){
		rv = api_send_header(client, "200 OK", true);
		if(api_handle_reset()){
			rv = 1;
		}
		else{
			rv |= api_send_header(client, "200 OK", true)
				|| network_send(client->fd, "{}");
		}
	}
	else if(!strcmp(client->endpoint, "/status")){
		rv = api_send_header(client, "200 OK", true)
			|| api_send_status(client);
	}
	else if(!strncmp(client->endpoint, "/stop/", 6)){
		rpcd_child_t* command = child_command_find(client->endpoint + 6);
		if(!command){
			rv = api_send_header(client, "400 No such command", false);
		}
		else if(!child_active(command)){
			rv = api_send_header(client, "500 Not running", false);
		}
		else if(child_stop(command)){
			rv = api_send_header(client, "500 Failed to stop", false);
		}
		else{
			rv |= api_send_header(client, "200 OK", true) ||
				network_send(client->fd, "{}");
		}
	}
	else if(!strncmp(client->endpoint, "/layout/", 8)){
		if(!strchr(client->endpoint + 8, '/')){
			fprintf(stderr, "Missing display in layout request\n");
			rv = api_send_header(client, "500 Missing display", false);
		}
		else{
			*strchr(client->endpoint + 8, '/') = 0;
			layout_t* layout = layout_find(x11_find_id(client->endpoint + 8), client->endpoint + strlen(client->endpoint) + 1);

			if(!layout){
				rv = api_send_header(client, "400 No such layout", false);
			}
			else if(x11_activate_layout(layout)){
				rv = api_send_header(client, "500 Failed to activate", false);
			}
			else{
				rv = api_send_header(client, "200 OK", true)
					|| network_send(client->fd, "{}");
			}
		}
	}
	else if(!strncmp(client->endpoint, "/command/", 9)){
		rpcd_child_t* command = child_command_find(client->endpoint + 9);
		if(!command){
			rv = api_send_header(client, "400 No such command", false);
		}
		else if(child_active(command)){
			rv = api_send_header(client, "500 Already running", false);
		}
		else if(child_run_command(command, client->recv_buf, client->payload_size)){
			rv = api_send_header(client, "500 Failed to start", false);
		}
		else{
			rv = api_send_header(client, "200 OK", true)
				|| network_send(client->fd, "{}");
		}
	}
	else{
		rv = api_send_header(client, "400 Unknown Endpoint", false)
			|| network_send(client->fd, "The requested endpoint is not supported");
	}

	api_disconnect(client);
	return rv;
}

static int api_data(http_client_t* client){
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

	bytes_recv = recv(client->fd, client->recv_buf + client->recv_offset, bytes_left - 1, 0);
	if(bytes_recv < 0){
		fprintf(stderr, "Failed to receive from HTTP client: %s\n", strerror(errno));
		return 1;
	}
	else if(bytes_recv == 0){
		api_disconnect(client);
		return 0;
	}

	//handle complete lines (except body data)
	for(u = 0; client->state != http_data && u < bytes_recv - 1; u++){
		if(!strncmp(client->recv_buf + client->recv_offset + u, "\r\n", 2)){
			//terminate complete line
			client->recv_buf[client->recv_offset + u] = 0;

			//handle header lines
			if(api_handle_header(client)){
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
			//terminate data
			client->recv_buf[client->payload_size] = 0;
			//handle the request
			return api_handle_body(client);
		}
		else{
			fprintf(stderr, "Missing %zu bytes of payload data, waiting for input\n", client->payload_size - (client->recv_offset + bytes_recv));
		}
	}

	client->recv_offset += bytes_recv;
	return 0;
}

int api_loop(fd_set* in, fd_set* out, int* max_fd){
	size_t u;

	//re-select on the listen fd
	FD_SET(listen_fd, out);
	*max_fd = (listen_fd > *max_fd) ? listen_fd : *max_fd;

	if(FD_ISSET(listen_fd, in)){
		//handle new clients
		if(api_accept()){
			return 1;
		}
	}

	for(u = 0; u < nclients; u++){
		if(clients[u].fd >= 0 && FD_ISSET(clients[u].fd, in)){
			//handle client data
			if(api_data(clients + u)){
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

int api_config(char* option, char* value){
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

int api_ok(){
	if(listen_fd < 0){
		fprintf(stderr, "No listening socket for API\n");
		return 1;
	}
	return 0;
}

void api_cleanup(){
	size_t u;
	close(listen_fd);
	listen_fd = -1;

	for(u = 0; u < nclients; u++){
		api_disconnect(clients + u);
		free(clients[u].recv_buf);
		api_client_init(clients + u);
	}
	free(clients);
	nclients = 0;
}
