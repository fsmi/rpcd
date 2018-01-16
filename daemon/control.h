#include <X11/Xlib.h>

typedef struct /*_automation_variable_t*/ {
	char* name;
	char* value;
} variable_t;

int control_config(char* option, char* value);
int control_config_variable(char* name, char* value);
int control_config_automation(char* line);
int control_loop(fd_set* in, fd_set* out, int* max_fd);
int control_ok();
void control_cleanup();
