#include "control.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static size_t nwindows = 0;
static window_t* windows = NULL;

static size_t nvars = 0;
static variable_t* vars = NULL;

static int control_window_init(window_t* window, char* name){
	window_t empty = {
		.frame_id = -1,
		0
	};

	if(name){
		empty.name = strdup(name);
	}

	*window = empty;
	return 0;
}

static void control_window_free(window_t* window){
	char** off;
	size_t u;

	free(window->name);
	window->name = NULL;

	for(off = window->command; off && *off; off++){
		free(*off);
		*off = NULL;
	}
	free(window->command);
	window->command = NULL;

	for(u = 0; u < sizeof(window->filters) / sizeof(char*); u++){
		free(window->filters[u]);
		window->filters[u] = NULL;
	}
}

static int control_window_create(char* name){
	int rv = 0;

	windows = realloc(windows, (nwindows + 1) * sizeof(window_t));
	if(!windows){
		nwindows = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	rv = control_window_init(windows + nwindows, name);
	nwindows++;
	return rv;
}

//FIXME this currently forces the automation to remove all previous frame maps
Window control_get_window(display_t* display, size_t frame_id){
	size_t u;
	for(u = 0; u < nwindows; u++){
		if(windows[u].frame_id == frame_id){
			return windows[u].window;
		}
	}

	//frame not mapped to a window, blank
	return 0;
}

int control_repatriate(display_t* display, size_t frame_id, Window w){
	if(!control_window_create(NULL)){
		windows[nwindows - 1].window = w;
		windows[nwindows - 1].frame_id = frame_id;
		windows[nwindows - 1].mode = irreplaceable;

		return 0;
	}
	return 1;
}

int control_config_variable(char* name, char* value){
	//TODO
	return 1;
}

int control_config_window(char* option, char* value){
	//TODO
	return 1;
}

int control_window_new(char* name){
	size_t u;

	if(!name || strlen(name) < 1){
		fprintf(stderr, "Invalid window name provided\n");
		return 1;
	}

	for(u = 0; u < nwindows; u++){
		if(!strcmp(windows[u].name, name)){
			fprintf(stderr, "Window %s already defined\n", name);
			return 1;
		}
	}

	return control_window_create(name);
}

int control_window_ok(){
	if(!windows){
		return 0;
	}

	window_t* last = windows + (nwindows - 1);

	if(last->mode != irreplaceable
			&& (!last->name || !last->command)){
		fprintf(stderr, "Window has no name or no invocation command\n");
		return 1;
	}
	return 0;
}

void control_cleanup(){
	size_t u;
	for(u = 0; u < nwindows; u++){
		control_window_free(windows + u);
	}
	free(windows);
	nwindows = 0;

	for(u = 0; u < nvars; u++){
		free(vars[u].name);
		free(vars[u].value);
	}
	free(vars);
	nvars = 0;
}
