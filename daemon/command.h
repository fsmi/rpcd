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

typedef struct /*_command_instance*/ {
	command_t* command;
	size_t restore_layout;
	char** arguments;
	pid_t command_pid;
} command_instance_t;

int command_active(command_t* command);
int command_run(command_t* command, char* posted_json, size_t data_length);

size_t command_count();
command_t* command_get(size_t index);
command_t* command_find(char* name);

int command_new(char* name);
int command_config(char* option, char* value);
int command_ok();
void command_cleanup();
