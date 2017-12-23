#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "x11.h"

static layout_t* default_layout = NULL;
static layout_t* current_layout = NULL;
static char* default_layout_name = NULL;
static Display* display_handle = NULL;
static Atom rp_command, rp_command_request, rp_command_result;

//See ratpoison:src/communications.c for the original implementation of the ratpoison
//command protocol
static int x11_fetch_response(Window w, char** response){
	int format, rv = -1;
	unsigned long items, bytes;
	unsigned char* result = NULL;
	Atom type;

	if(XGetWindowProperty(display_handle, w, rp_command_result,
				0, 0, False, XA_STRING,
				&type, &format, &items, &bytes, &result) != Success
			|| !result){
		fprintf(stderr, "Failed to fetch ratpoison command result status\n");
		goto bail;
	}

	XFree(result);

	if(XGetWindowProperty(display_handle, w, rp_command_result,
				0, (bytes / 4) + ((bytes % 4) ? 1 : 0), True, XA_STRING,
				&type, &format, &items, &bytes, &result) != Success
			|| !result){
		fprintf(stderr, "Failed to fetch ratpoison command result\n");
		goto bail;
	}

	if(*result){
		//command failed, look for a reason
		if(*result == '0'){
			fprintf(stderr, "Ratpoison command failed: %s\n", result + 1);
			goto bail;
		}

		//command ok
		if(*result == '1'){
			*response = strdup((char*) (result + 1));
			if(!*response){
				fprintf(stderr, "Failed to allocate memory\n");
			}
		}
	}

	rv = 0;
bail:
	if(result){
		XFree(result);
	}
	return rv;
}

static int x11_run_command(char* command, char** response){
	int rv = 1;
	XEvent ev;
	Window root = DefaultRootWindow(display_handle);
	Window w = XCreateSimpleWindow(display_handle, root, 0, 0, 1, 1, 0, 0, 0);
	char* command_string = calloc(strlen(command) + 2, sizeof(char));

	if(!command_string){
		fprintf(stderr, "Failed to allocate memory\n");
		goto bail;
	}

	if(!rp_command || !rp_command_request || !rp_command_result){
		fprintf(stderr, "Window manager interaction disabled, would have run: %s\n", command);
		rv = 0;
		goto bail;
	}

	memcpy(command_string + 1, command, strlen(command));

	XSelectInput(display_handle, w, PropertyChangeMask);
	XChangeProperty(display_handle, w, rp_command, XA_STRING, 8, PropModeReplace, (unsigned char*) command_string, strlen(command) + 2);
	XChangeProperty(display_handle, root, rp_command_request, XA_WINDOW, 8, PropModeAppend, (unsigned char*) &w, sizeof(Window));

	for(;;){
		XMaskEvent(display_handle, PropertyChangeMask, &ev);
		if(ev.xproperty.atom == rp_command_result && ev.xproperty.state == PropertyNewValue){
			rv = 0;
			if(response){
				rv = x11_fetch_response(w, response);
			}
			break;
		}
	}

bail:
	free(command_string);
	XDestroyWindow(display_handle, w);
  	return rv;
}

int x11_fetch_layout(char** layout){
	return x11_run_command("sfdump", layout);
}

int x11_activate_layout(layout_t* layout){
	size_t left = 0, frame = 0, off = 10;
	char* layout_string = strdup("sfrestore ");
	ssize_t required = 0;
	int rv;

	if(!layout_string){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	for(frame = 0; frame < layout->nframes; frame++){
		required = snprintf(layout_string + off, left, "%s(frame :number %zu :x %zu :y %zu :width %zu :height %zu :screenw %zu :screenh %zu) %zu",
				frame ? "," : "", layout->frames[frame].id,
				layout->frames[frame].bbox[0], layout->frames[frame].bbox[1],
				layout->frames[frame].bbox[2], layout->frames[frame].bbox[3],
				layout->frames[frame].screen[0], layout->frames[frame].screen[1],
				layout->frames[frame].screen[2]);

		if(required < 0){
			fprintf(stderr, "Failed to design layout string\n");
			rv = 1;
			goto bail;
		}

		if(required > left){
			layout_string = realloc(layout_string, (strlen(layout_string) + 1 + DATA_CHUNK) * sizeof(char));
			if(!layout_string){
				fprintf(stderr, "Failed to allocate memory\n");
				rv = 1;
				goto bail;
			}

			left = DATA_CHUNK;
			frame--;
			continue;
		}

		off += required;
		left -= required;
	}

	fprintf(stderr, "Generated layout: %s\n", layout_string);
	rv = x11_run_command(layout_string, NULL);
	current_layout = layout;
bail:
	free(layout_string);
	return rv;
}

int x11_fullscreen(){
	return x11_run_command("only", NULL);
}

int x11_rollback(){
	return x11_run_command("undo", NULL);
}

int x11_select_frame(size_t frame_id){
	char command_buffer[DATA_CHUNK];
	snprintf(command_buffer, sizeof(command_buffer), "fselect %zu", frame_id);
	return x11_run_command(command_buffer, NULL);
}

layout_t* x11_current_layout(){
	return current_layout ? current_layout : default_layout;
}

int x11_loop(fd_set* in, fd_set* out, int* max_fd){
	if(!default_layout){
		default_layout = layout_find(default_layout_name);
		if(!default_layout){
			fprintf(stderr, "Failed to find default layout %s\n", default_layout_name);
			return 1;
		}
	}
	return 0;
}

int x11_config(char* option, char* value){
	if(!strcmp(option, "display")){
		display_handle = XOpenDisplay(value);
		if(!display_handle){
			fprintf(stderr, "Failed to open display %s\n", value);
			return 1;
		}
		rp_command = XInternAtom(display_handle, "RP_COMMAND", True);
		rp_command_request = XInternAtom(display_handle, "RP_COMMAND_REQUEST", True);
		rp_command_result = XInternAtom(display_handle, "RP_COMMAND_RESULT", True);
		return 0;
	}
	else if(!strcmp(option, "deflayout")){
		default_layout_name = strdup(value);
		if(!default_layout_name){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "Invalid option %s in x11 section\n", option);
	return 1;
}

int x11_ok(){
	if(!display_handle){
		fprintf(stderr, "No connection to an X11 display\n");
		return 1;
	}

	if(!rp_command || !rp_command_request || !rp_command_result){
		fprintf(stderr, "Failed to query ratpoison-specific Atoms, window manager interaction disabled\n");
	}
	return 0;
}

void x11_cleanup(){
	free(default_layout_name);
	if(display_handle){
		XCloseDisplay(display_handle);
	}
}
