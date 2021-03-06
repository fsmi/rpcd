#include <sys/select.h>

#define RECV_CHUNK 4096
#define LISTEN_QUEUE_LENGTH 128
#define DEFAULT_PORT "8080"
#define HARD_SIZE_LIMIT 10240

typedef enum /*_http_method*/ {
	method_unknown = 0,
	http_get,
	http_post
} http_method_t;

typedef enum /*_http_client_state*/ {
	http_new = 0,
	http_headers,
	http_data
} http_state_t;

typedef struct /*_http_client*/ {
	int fd;

	size_t data_allocated;
	size_t recv_offset;
	char* recv_buf;

	size_t payload_size;
	http_method_t method;
	http_state_t state;

	char* endpoint;
} http_client_t;

int api_loop(fd_set* in, fd_set* out, int* max_fd);
int api_config(char* option, char* value);
int api_ok();
void api_cleanup();
