#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "x11.h"

int x11_loop(fd_set* in, fd_set* out, int* max_fd){
	return 0;
}

int x11_config(char* option, char* value){
	//TODO handle x11 config
	return 0;
}

int x11_ok(){
	//TODO check x11 status
	return 0;
}

void x11_cleanup(){
	//TODO clean up x11 resources
}
