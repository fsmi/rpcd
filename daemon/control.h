#include <X11/Xlib.h>
#define LISTEN_QUEUE_LENGTH 128

typedef struct /*_automation_variable_t*/ {
	char* name;
	char* value;
} variable_t;

typedef enum /*_control_input_type_t*/ {
	control_fifo,
	control_socket,
	control_client
} input_type_t;

typedef struct /*_control_input_t*/ {
	input_type_t type;
	int fd;
	size_t recv_offset;
	size_t recv_alloc;
	char* recv_buffer;
} control_input_t;

int control_config(char* option, char* value);
int control_config_variable(char* name, char* value);
int control_config_automation(char* line);
int control_loop(fd_set* in, fd_set* out, int* max_fd);
int control_ok();
void control_cleanup();
