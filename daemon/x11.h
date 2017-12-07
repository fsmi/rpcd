#include <sys/select.h>
#include "layout.h"

int x11_activate_layout(layout_t* layout);

int x11_loop(fd_set* in, fd_set* out, int* max_fd);
int x11_config(char* option, char* value);
int x11_ok();
void x11_cleanup();
