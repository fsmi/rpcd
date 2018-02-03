#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "x11.h"
#include "control.h"
#include "child.h"

static int init_done = 0;

static size_t ndisplays = 0;
static display_t* displays = NULL;
static size_t nwindows = 0;
static tracked_window_t* windows = NULL;

size_t x11_count(){
	return ndisplays;
}

display_t* x11_get(size_t index){
	if(index < ndisplays){
		return displays + index;
	}
	return NULL;
}

size_t x11_find_id(char* name){
	size_t u;

	for(u = 0; u < ndisplays; u++){
		if(!strcasecmp(displays[u].name, name)){
			return u;
		}
	}

	fprintf(stderr, "Failed to find display %s, replacing with default display 0\n", name);
	return 0;
}

static int x11_handle_window(display_t* display, Window w){
	char* window_title = NULL;
	int pid_format = 0, rv = 0;
	unsigned long pid_items = 0, bytes_left = 0;
	Atom pid_type;
	XClassHint class_hints = {
		0
	};
	pid_t* pid = NULL;

	if(XGetWindowProperty(display->display_handle, w, display->net_wm_pid,
			0, sizeof(pid) / 4, False, XA_CARDINAL,
			&pid_type, &pid_format, &pid_items, &bytes_left,
			(unsigned char**) &pid) != Success || pid_type != XA_CARDINAL){
		//this should not have changed, but set it anyway
		pid = NULL;
		fprintf(stderr, "Failed to fetch PID for window %zu\n", w);
	}
	else if(bytes_left || pid_items != 1 || pid_format != 32){
		fprintf(stderr, "PID not in expected format, %zu bytes and %zu items left, format %d\n", bytes_left, pid_items, pid_format);
		if(pid){
			XFree(pid);
			pid = NULL;
		}
	}

	if(!XFetchName(display->display_handle, w, &window_title)){
		fprintf(stderr, "Failed to fetch window title for window %zu\n", w);
		window_title = NULL;
	}

	if(!XGetClassHint(display->display_handle, w, &class_hints)){
		fprintf(stderr, "Failed to fetch window class hints for window %zu\n", w);
	}

	rv = child_match_window(display - displays, w, pid ? *pid : 0, window_title, class_hints.res_name, class_hints.res_class);

	if(pid){
		XFree(pid);
	}
	if(window_title){
		XFree(window_title);
	}
	if(class_hints.res_name){
		XFree(class_hints.res_name);
	}
	if(class_hints.res_class){
		XFree(class_hints.res_class);
	}
	return rv;
}

static int x11_handle_event(display_t* display, XEvent* ev){
	size_t u;
	switch(ev->type){
		case ConfigureNotify:
			//on first configure: gather matching info and pass to command module
			//using configure instead of map as there is a race condition between telling
			//ratpoison to map a window to a frame and that window actually becoming exposed
			for(u = 0; u < nwindows; u++){
				if(windows[u].window == ev->xmap.window && windows[u].state == unconfigured){
					windows[u].state = active;
					return x11_handle_window(display, ev->xmap.window);
				}
			}
			break;
		case CreateNotify:
			//add to set of tracked windows
			for(u = 0; u < nwindows; u++){
				if(windows[u].state == inactive){
					break;
				}
			}

			if(u == nwindows){
				//no free entry, extend the array
				windows = realloc(windows, (nwindows + 1) * sizeof(tracked_window_t));
				if(!windows){
					fprintf(stderr, "Failed to allocate memory\n");
					nwindows = 0;
					return 1;
				}
				nwindows++;
			}

			windows[u].state = unconfigured;
			windows[u].window = ev->xcreatewindow.window;
			return 0;
		case DestroyNotify:
			//remove from tracking set, notify command if configured
			for(u = 0; u < nwindows; u++){
				if(windows[u].window == ev->xdestroywindow.window){
					if(windows[u].state == active){
						child_discard_window(display - displays, ev->xdestroywindow.window);
					}
					windows[u].state = inactive;
					return 0;
				}
			}
			fprintf(stderr, "Untracked window %zu destroyed on display %s\n", ev->xdestroywindow.window, display->identifier);
	}
	return 0;
}

static void x11_connection_watch(Display* dpy, XPointer data, int fd, Bool opening, XPointer* watch_data){
	size_t u;
	display_t* display = NULL;

	if(!data){
		fprintf(stderr, "x11_connection_watch called with invalid display data\n");
		return;
	}

	display = (display_t*) data;

	for(u = 0; u < display->nfds; u++){
		if(display->fds[u] == fd){
			if(!opening){
				display->fds[u] = -1;
			}
			return;
		}

		//this might cause an fd to appear multiple times, but we do not actually care
		if(opening && display->fds[u] == -1){
			display->fds[u] = fd;
			return;
		}
	}

	display->fds = realloc(display->fds, (display->nfds + 1) * sizeof(int));
	if(!display->fds){
		fprintf(stderr, "Failed to allocate memory\n");
		display->nfds = 0;
		return;
	}

	display->fds[display->nfds] = fd;
	display->nfds++;
}

//See ratpoison:src/communications.c for the original implementation of the ratpoison
//command protocol
static int x11_fetch_response(display_t* display, Window w, char** response){
	int format, rv = -1;
	unsigned long items, bytes;
	unsigned char* result = NULL;
	Atom type;

	if(XGetWindowProperty(display->display_handle, w, display->rp_command_result,
				0, 0, False, XA_STRING,
				&type, &format, &items, &bytes, &result) != Success
			|| !result){
		fprintf(stderr, "Failed to fetch ratpoison command result status\n");
		goto bail;
	}

	XFree(result);

	if(XGetWindowProperty(display->display_handle, w, display->rp_command_result,
				0, (bytes / 4) + ((bytes % 4) ? 1 : 0), True, XA_STRING,
				&type, &format, &items, &bytes, &result) != Success
			|| !result){
		fprintf(stderr, "Failed to fetch ratpoison command result\n");
		goto bail;
	}

	if(*result){
		//command failed, look for a reason
		if(*result == '0'){
			fprintf(stderr, "ratpoison command failed: %s\n", result + 1);
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

static int x11_run_command(display_t* display, char* command, char** response){
	int rv = 1;
	XEvent ev;

	if(!display){
		fprintf(stderr, "Invalid display passed to x11_run_command\n");
		return 1;
	}

	Window root = DefaultRootWindow(display->display_handle);
	Window w = XCreateSimpleWindow(display->display_handle, root, 0, 0, 1, 1, 0, 0, 0);
	char* command_string = calloc(strlen(command) + 2, sizeof(char));

	if(!command_string){
		fprintf(stderr, "Failed to allocate memory\n");
		goto bail;
	}

	if(!display->rp_command
			|| !display->rp_command_request
			|| !display->rp_command_result){
		fprintf(stderr, "Window manager interaction disabled on %s, would have run: %s\n", display->name, command);
		rv = 0;
		goto bail;
	}

	memcpy(command_string + 1, command, strlen(command));

	XSelectInput(display->display_handle, w, PropertyChangeMask);
	XChangeProperty(display->display_handle, w, display->rp_command, XA_STRING, 8, PropModeReplace, (unsigned char*) command_string, strlen(command) + 2);
	XChangeProperty(display->display_handle, root, display->rp_command_request, XA_WINDOW, 8, PropModeAppend, (unsigned char*) &w, sizeof(Window));

	for(;;){
		XMaskEvent(display->display_handle, PropertyChangeMask, &ev);
		if(ev.xproperty.atom == display->rp_command_result && ev.xproperty.state == PropertyNewValue){
			rv = 0;
			if(response){
				rv = x11_fetch_response(display, w, response);
			}
			break;
		}
	}

bail:
	free(command_string);
	XDestroyWindow(display->display_handle, w);
  	return rv;
}

static int x11_repatriate(size_t display_id){
	int rv = 1;
	size_t frame_id, window;
	char* layout = NULL, *frame = NULL;
	if(x11_fetch_layout(display_id, &layout) || !layout){
		fprintf(stderr, "Failed to repatriate windows, could not read layout\n");
		return 1;
	}

	for(frame = strtok(layout, ","); frame; frame = strtok(NULL, ",")){
		if(!strstr(frame, ":number") || !strstr(frame, ":window")){
			fprintf(stderr, "Skipping frame, missing either ID or window\n");
			continue;
		}

		frame_id = strtoul(strstr(frame, ":number") + 7, NULL, 10);
		window = strtoul(strstr(frame, ":window") + 7, NULL, 10);

		if(child_repatriate(display_id, frame_id, window)){
			fprintf(stderr, "Failed to repatriate frame %zu\n", frame_id);
			goto bail;
		}
	}

	rv = 0;
bail:
	free(layout);
	return rv;
}

static void x11_display_free(display_t* display){
	free(display->name);
	display->name = NULL;

	free(display->identifier);
	display->identifier = NULL;

	free(display->default_layout_name);
	display->default_layout_name = NULL;

	if(display->display_handle){
		XCloseDisplay(display->display_handle);
	}

	display->nfds = 0;
	free(display->fds);
}

static int x11_display_init(display_t* display, char* name){
	display_t empty = {
		0
	};

	*display = empty;
	display->name = strdup(name);
	return display->name ? 0 : 1;
}

int x11_fetch_layout(size_t display_id, char** layout){
	return x11_run_command(x11_get(display_id), "sfdump", layout);
}

void x11_lock(size_t display_id){
	x11_get(display_id)->busy++;
}

void x11_unlock(size_t display_id){
	display_t* display = x11_get(display_id);
	if(!display){
		fprintf(stderr, "Unlock requested on invalid display %zu\n", display_id);
		return;
	}
	if(display->busy){
		display->busy--;
		if(!display->busy){
			fprintf(stderr, "Display %s ready for automation\n", display->name);
		}
	}
	else{
		fprintf(stderr, "Unmatched display unlock on %zu\n", display_id);
	}
}

int x11_activate_layout(layout_t* layout){
	size_t left = 0, frame = 0, off = 10;
	display_t* display = x11_get(layout->display_id);
	char* layout_string = strdup("sfrestore ");
	ssize_t required = 0;
	int rv;

	if(!layout_string || !display){
		fprintf(stderr, "Failed to allocate memory\n");
		free(layout_string);
		return 1;
	}

	for(frame = 0; frame < layout->nframes; frame++){
		required = snprintf(layout_string + off, left, "%s(frame :number %zu :x %zu :y %zu :width %zu :height %zu :screenw %zu :screenh %zu :window %zu) %zu",
				frame ? "," : "", layout->frames[frame].id,
				layout->frames[frame].bbox[0], layout->frames[frame].bbox[1],
				layout->frames[frame].bbox[2], layout->frames[frame].bbox[3],
				layout->frames[frame].screen[0], layout->frames[frame].screen[1],
				child_window(layout->display_id, layout->frames[frame].id),
				layout->frames[frame].screen[2]);

		if(required < 0){
			fprintf(stderr, "Failed to design layout string for %s on %s\n", layout->name, display->name);
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

	rv = x11_run_command(display, layout_string, NULL);
	display->current_layout = layout;
	//stop commands from undoing the layout change
	child_discard_restores(layout->display_id);
bail:
	free(layout_string);
	return rv;
}

int x11_default_layout(size_t display_id){
	display_t* display = x11_get(display_id);
	if(display->default_layout){
		return x11_activate_layout(display->default_layout);
	}
	fprintf(stderr, "No default layout defined on display %s, none loaded\n", display->name);
	return 0;
}

int x11_fullscreen(size_t display_id){
	return x11_run_command(x11_get(display_id), "only", NULL);
}

int x11_rollback(size_t display_id){
	return x11_run_command(x11_get(display_id), "undo", NULL);
}

int x11_select_frame(size_t display_id, size_t frame_id){
	char command_buffer[DATA_CHUNK];
	snprintf(command_buffer, sizeof(command_buffer), "fselect %zu", frame_id);
	return x11_run_command(x11_get(display_id), command_buffer, NULL);
}

layout_t* x11_current_layout(size_t display_id){
	display_t* display = x11_get(display_id);

	if(!display){
		fprintf(stderr, "Invalid display ID passed to x11_current_layout\n");
		return NULL;
	}

	return display->current_layout ? display->current_layout : display->default_layout;
}

int x11_loop(fd_set* in, fd_set* out, int* max_fd){
	size_t u, p, mark;
	XEvent ev;

	if(!init_done){
		for(u = 0; u < ndisplays; u++){
			if(displays[u].default_layout_name){
				displays[u].default_layout = layout_find(u, displays[u].default_layout_name);
				if(!displays[u].default_layout){
					fprintf(stderr, "Failed to find default layout %s for %s\n", displays[u].default_layout_name, displays[u].name);
					return 1;
				}
				x11_default_layout(u);
			}
		}
		init_done = 1;
	}

	for(u = 0; u < ndisplays; u++){
		mark = 0;

		//check if display data ready
		for(p = 0; p < displays[u].nfds; p++){
			if(displays[u].fds[p] >= 0 && FD_ISSET(displays[u].fds[p], in)){
				XProcessInternalConnection(displays[u].display_handle, displays[u].fds[p]);
				mark = 1;
			}
		}

		//handle display events
		if(mark){
			while(XPending(displays[u].display_handle)){
				XNextEvent(displays[u].display_handle, &ev);
				if(x11_handle_event(displays + u, &ev)){
					return 1;
				}
			}
			XFlush(displays[u].display_handle);
		}

		//push all display fds to the core select loop
		for(p = 0; p < displays[u].nfds; p++){
			if(displays[u].fds[p] >= 0){
				*max_fd = (*max_fd > displays[u].fds[p]) ? *max_fd : displays[u].fds[p];
				FD_SET(displays[u].fds[p], out);
			}
		}
	}
	return 0;
}

int x11_new(char* name){
	size_t u;
	int rv = 0;

	if(!name || strlen(name) < 1){
		fprintf(stderr, "Invalid display name passed\n");
		return 1;
	}

	for(u = 0; u < ndisplays; u++){
		if(!strcasecmp(displays[u].name, name)){
			fprintf(stderr, "Display name %s already defined\n", name);
			return 1;
		}
	}

	displays = realloc(displays, (ndisplays + 1) * sizeof(display_t));
	if(!displays){
		ndisplays = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	rv = x11_display_init(displays + ndisplays, name);
	ndisplays++;
	return rv;
}

int x11_config(char* option, char* value){
	display_t* last = displays + (ndisplays - 1);
	size_t u;

	if(!strcmp(option, "display")){
		if(last->identifier){
			fprintf(stderr, "Multiple display connections specified for display %s\n", last->name);
			return 1;
		}

		last->display_handle = XOpenDisplay(value);
		if(!last->display_handle){
			fprintf(stderr, "Failed to open display %s\n", value);
			return 1;
		}

		//fetch ratpoison-specific atoms
		last->rp_command = XInternAtom(last->display_handle, "RP_COMMAND", True);
		last->rp_command_request = XInternAtom(last->display_handle, "RP_COMMAND_REQUEST", True);
		last->rp_command_result = XInternAtom(last->display_handle, "RP_COMMAND_RESULT", True);
		last->net_wm_pid = XInternAtom(last->display_handle, "_NET_WM_PID", True);

		//this might happen if rpcd is running in a non-ratpoison environment for some reason
		if(last->net_wm_pid == None){
			fprintf(stderr, "The current window manager does not seem to support the _NET_WM_PID protocol\n");
			return 1;
		}

		//add connection watch function for fd updates
		if(!XAddConnectionWatch(last->display_handle, x11_connection_watch, (XPointer) last)){
			fprintf(stderr, "Failed to add X11 connection watch function\n");
			return 1;
		}
		//add primary fd (according to the docs, the watch procedure is called immediately after registering,
		//with all open fds. in practice, this does not seem to happen)
		x11_connection_watch(last->display_handle, (XPointer) last, XConnectionNumber(last->display_handle), True, NULL);

		//copy identifier for updating children's DISPLAY environment
		last->identifier = strdup(value);
		if(!last->identifier){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}

		fprintf(stderr, "%d screens on display %s\n", XScreenCount(last->display_handle), value);
		for(u = 0; u < XScreenCount(last->display_handle); u++){
			XSelectInput(last->display_handle, XRootWindow(last->display_handle, u), StructureNotifyMask | SubstructureNotifyMask);
		}
		return 0;
	}
	else if(!strcmp(option, "deflayout")){
		last->default_layout_name = strdup(value);
		if(!last->default_layout_name){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "repatriate")){
		if(!strcmp(value, "yes")){
			last->repatriate = 1;
		}
		return 0;
	}

	fprintf(stderr, "Option %s not recognized for type display\n", option);
	return 1;
}

int x11_ok(){
	if(!displays){
		fprintf(stderr, "No display defined\n");
		return 1;
	}

	display_t* last = displays + (ndisplays - 1);

	if(!last->display_handle || !last->identifier){
		fprintf(stderr, "No display connected for %s\n", last->name);
		return 1;
	}

	if(!last->rp_command || !last->rp_command_request || !last->rp_command_result){
		fprintf(stderr, "Failed to query ratpoison-specific Atoms on %s, window manager interaction disabled\n", last->name);
	}

	if(last->repatriate){
		x11_repatriate(ndisplays - 1);
		last->repatriate = 0;
	}
	return 0;
}

void x11_cleanup(){
	size_t u;

	free(windows);
	windows = NULL;
	nwindows = 0;

	for(u = 0; u < ndisplays; u++){
		x11_display_free(displays + u);
	}
	free(displays);
	ndisplays = 0;
	displays = NULL;
	init_done = 0;
}
