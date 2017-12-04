typedef enum /*_command_arg_type_t*/ {
	arg_string,
	arg_enum
} argument_type;

typedef struct /*_command_arg_t*/ {
	char* name;
	argument_type type;
	char** additional;
} argument_t;

typedef struct /*_rpcd_command_t*/ {
	char* name;
	char* command;
	size_t nargs;
	argument_t* args;
} command_t;

int command_ok(command_t* command);
void command_init(command_t* command);
void command_free(command_t* command);
