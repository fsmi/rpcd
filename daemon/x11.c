#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <X11/Xlib.h>

#include "x11.h"

static layout_t* default_layout = NULL;
Display* display_handle = NULL;

int x11_activate_layout(layout_t* layout){
	return 0;
}

int x11_loop(fd_set* in, fd_set* out, int* max_fd){
	return 0;
}

int x11_config(char* option, char* value){
	if(!strcmp(option, "display")){
		display_handle = XOpenDisplay(value);
		if(!display_handle){
			fprintf(stderr, "Failed to open display %s\n", value);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "deflayout")){
		default_layout = layout_find(value);
		if(!default_layout){
			fprintf(stderr, "Failed to find default layout %s\n", value);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "fullscreen")){
		//TODO
		return 0;
	}

	fprintf(stderr, "Invalid option %s in x11 section\n", option);
	return 1;
}

int x11_ok(){
	//TODO check x11 status
	return 0;
}

void x11_cleanup(){
	XCloseDisplay(display_handle);
	//TODO clean up x11 resources
}
