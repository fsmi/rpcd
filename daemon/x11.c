#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "x11.h"

static layout_t* default_layout = NULL;
Display* display_handle = NULL;
Atom rp_command, rp_command_request, rp_command_result;

static int x11_run_command(char* command){
	int rv = 0;
	XEvent ev;
	Window root = DefaultRootWindow(display_handle);
	Window w = XCreateSimpleWindow(display_handle, root, 0, 0, 1, 1, 0, 0, 0);
	char* command_string = calloc(strlen(command) + 2, sizeof(char));

	if(!command_string){
		fprintf(stderr, "Failed to allocate memory\n");
		rv = 1;
		goto bail;
	}

	memcpy(command_string + 1, command, strlen(command));

	XSelectInput(display_handle, w, PropertyChangeMask);
	XChangeProperty(display_handle, w, rp_command, XA_STRING, 8, PropModeReplace, (unsigned char*) command_string, strlen(command) + 2);
	XChangeProperty(display_handle, root, rp_command_request, XA_WINDOW, 8, PropModeAppend, (unsigned char*) &w, sizeof(Window));

	for(;;){
		XMaskEvent(display_handle, PropertyChangeMask, &ev);
		if(ev.xproperty.atom == rp_command_result && ev.xproperty.state == PropertyNewValue){
			break;
		}
	}

bail:
	free(command_string);
	XDestroyWindow(display_handle, w);
  	return rv;
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

	//FIXME this generates an access violation somewhere
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
	rv = x11_run_command(layout_string);

bail:
	free(layout_string);
	return rv;
}

int x11_fullscreen(){
	return x11_run_command("only");
}

int x11_rollback(){
	return x11_run_command("undo");
}

int x11_select_frame(size_t frame_id){
	char command_buffer[DATA_CHUNK];
	snprintf(command_buffer, sizeof(command_buffer), "fselect %zu", frame_id);
	return x11_run_command(command_buffer);
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
		rp_command = XInternAtom(display_handle, "RP_COMMAND", False);
		rp_command_request = XInternAtom(display_handle, "RP_COMMAND_REQUEST", False);
		rp_command_result = XInternAtom(display_handle, "RP_COMMAND_RESULT", False);
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

	fprintf(stderr, "Invalid option %s in x11 section\n", option);
	return 1;
}

int x11_ok(){
	if(!display_handle){
		fprintf(stderr, "No connection to an X11 display\n");
		return 1;
	}

	if(!rp_command || !rp_command_request){
		fprintf(stderr, "Failed to query ratpoison-specific Atoms\n");
		return 1;
	}
	return 0;
}

void x11_cleanup(){
	XCloseDisplay(display_handle);
}
