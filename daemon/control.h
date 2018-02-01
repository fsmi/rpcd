#include <X11/Xlib.h>
#define LISTEN_QUEUE_LENGTH 128
#define CLIENT_RECV_CHUNK 2048
#define CLIENT_RECV_LIMIT 8192

#include "x11.h"
#include "layout.h"

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

typedef enum /*_automation_opcode_t*/ {
	op_noop = 0,
	//unary operations
	op_layout_default, //requires display_id
	op_layout, //requires display_id, operand_a = layout_name
	op_skip, //requires operand_numeric = skip
	op_condition_empty, //requires negate, operand_a, resolve_a
	op_stop,
	//binary operations
	op_assign, //requires operand_a, display_id, operand_numeric = frame
	op_condition_greater, //requires negate, operand_a, operand_b, resolve_a, resolve_b
	op_condition_less, //see above
	op_condition_equals //see above
} automation_opcode_t;

typedef enum /*_automation_display_status_t*/ {
	display_ready,
	display_busy,
	display_waiting
} display_status_t;

typedef struct /*_automation_display_configuration_t*/ {
	display_status_t status;
	display_t* display;
	layout_t* layout;
} display_config_t;

typedef struct /*_automation_assignment_t*/ {
	size_t display_id;
	size_t frame_id;
	char* requested;
} automation_assign_t;

typedef struct /*_automation_op_t*/ {
	automation_opcode_t op;
	size_t display_id;
	size_t negate;
	size_t operand_numeric;
	size_t resolve_a;
	size_t resolve_b;
	char* operand_a;
	char* operand_b;
} automation_operation_t;

int control_config(char* option, char* value);
int control_config_variable(char* name, char* value);
int control_config_automation(char* line);
int control_loop(fd_set* in, fd_set* out, int* max_fd);
int control_run_automation();
int control_ok();
void control_cleanup();
