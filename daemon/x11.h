#ifndef RPCD_DISPLAY_H
#define RPCD_DISPLAY_H
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "layout.h"

#define DATA_CHUNK 1024

typedef struct _x11_display_t {
	layout_t* default_layout;
	layout_t* current_layout;

	char* name;
	char* identifier;
	char* default_layout_name;
	int repatriate;
	int busy;

	Display* display_handle;
	Atom rp_command;
	Atom rp_command_request;
	Atom rp_command_result;

	size_t nfds;
	int* fds;
} display_t;

size_t x11_count();
display_t* x11_get(size_t index);
size_t x11_find_id(char* name);

int x11_activate_layout(layout_t* layout);
int x11_fullscreen(size_t display_id);
int x11_rollback(size_t display_id);
int x11_select_frame(size_t display_id, size_t frame_id);
int x11_fetch_layout(size_t display_id, char** layout);
layout_t* x11_current_layout(size_t display_id);

int x11_new(char* name);
int x11_loop(fd_set* in, fd_set* out, int* max_fd);
int x11_config(char* option, char* value);
int x11_ok();
void x11_cleanup();
#endif
