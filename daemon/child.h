#include <X11/Xlib.h>

typedef enum /*_user_command_arg_type_t*/ {
	arg_string,
	arg_enum
} argument_type;

typedef enum /*_instance_state*/ {
	stopped = 0,
	running,
	terminated
} instance_state;

typedef struct /*_user_command_arg_t*/ {
	char* name;
	argument_type type;
	char** additional;
} argument_t;

typedef enum /*_child_mode_t*/ {
	user = 0, /*user command, started on explicit request*/
	user_no_windows = 1, /*user command without windows*/
	ondemand, /*start when required, stop when not*/
	keepalive, /*start on initialization, stop only when required*/
	lazy, /*start when required, stop only when required, default*/
	repatriated /*can't be started, should not be stopped - used as last resort mapping*/
} child_mode_t;

typedef struct /*_rpcd_child_t*/ {
	/*generic child attributes*/
	char* name; /*command/window name*/
	char* command; /*executed command line*/
	char* working_directory; /*child working directory*/
	child_mode_t mode; /*command/window execution mode*/

	/*user command specific attributes*/
	char* description; /*command description*/
	size_t nargs; /*command user argument count*/
	argument_t* args; /*command user arguments*/
	size_t restore_layout; /*undo layout changes after termination*/

	/*automation specific attributes*/
	size_t start_iteration;

	/*x11 attributes*/
	size_t order; /*activation stack order*/
	size_t display_id; /*active display*/
	ssize_t frame_id; /*active frame*/
	size_t nwindows; /*number of displayed windows*/
	Window* windows; /*window handles*/
	char* filters[3]; /*title, app name, class name filters*/

	/*process control attributes*/
	instance_state state; /*process lifecycle state*/
	pid_t instance; /*process id*/
} rpcd_child_t;

typedef struct /*_user_command_instance_cfg*/ {
	size_t restore_layout;
	char** arguments;
} command_instance_t;

int child_active(rpcd_child_t* child);
int child_discard_restores(size_t display_id);
int child_start_command(rpcd_child_t* child, char* posted_json, size_t data_length);
int child_start_window(rpcd_child_t* child, size_t display_id, size_t frame_id);
int child_raise(rpcd_child_t* child, size_t display_id, size_t frame_id);
int child_stop(rpcd_child_t* child);
int child_stop_commands(size_t display_id);
int child_reap();

int child_match_window(size_t display_id, Window window, pid_t pid, char* title, char* res_name, char* res_class);
int child_discard_window(size_t display_id, Window window);
Window child_window(size_t display_id, size_t frame_id);
int child_repatriate(size_t display_id, size_t frame_id, Window window);

size_t child_command_count();
rpcd_child_t* child_command_get(size_t index);
rpcd_child_t* child_command_find(char* name);
size_t child_window_count();
rpcd_child_t* child_window_get(size_t index);
rpcd_child_t* child_window_find(char* name);

int child_new(char* name, size_t command);
int child_config(char* option, char* value);
int child_ok();
void child_cleanup();
