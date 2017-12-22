#include <sys/select.h>
#include "layout.h"

#define DATA_CHUNK 1024

int x11_activate_layout(layout_t* layout);
int x11_fullscreen();
int x11_rollback();
int x11_select_frame(size_t frame_id);
int x11_run_command(char* command, char** response);
layout_t* x11_current_layout();

int x11_loop(fd_set* in, fd_set* out, int* max_fd);
int x11_config(char* option, char* value);
int x11_ok();
void x11_cleanup();
