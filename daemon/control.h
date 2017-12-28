#include <X11/Xlib.h>
#include "x11.h"

typedef enum /*_window_mode*/ {
	ondemand = 0, /*start and stop at any time*/
	keepalive, /*start earliest, stop only when required*/
	lazy, /*start when required, stop when required*/
	irreplaceable /*repatriated windows can not be restarted when stopped*/
} window_mode_t;

typedef struct /*_managed_window_t*/ {
	ssize_t frame_id;

	Window window;
	pid_t pid;
	window_mode_t mode;

	char* name;
	char** command;
	char* filters[3]; //cname, class, name
} window_t;

typedef struct /*_automation_variable_t*/ {
	char* name;
	char* value;
} variable_t;

//TODO conditional operators

Window control_get_window(display_t* display, size_t frame_id);
int control_repatriate(display_t* display, size_t frame_id, Window w);
//int control_handle_event(XEvent ev);

int control_window_new(char* name);
int control_config_variable(char* name, char* value);
int control_config_window(char* option, char* value);
int control_window_ok();
void control_cleanup();
