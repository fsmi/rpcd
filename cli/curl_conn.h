#pragma once

#include <unistd.h>

struct netdata {
	char* data;
	size_t len;
};

void curl_init_global();
int request(const char* url, char* post_data, struct netdata* data);
