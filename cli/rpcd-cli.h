#pragma once

typedef enum {
	EXIT_NO_CMD = 1,
	EXIT_UNKNOWN_CMD = 4,
	EXIT_UNKNOWN_LIST_CMD = 5,
	EXIT_MISSING_LIST_CMD = 6,
	EXIT_MISSING_CMD_NAME = 7,
	EXIT_CMD_ARGUMENT_MISSING_KEY = 8,
	EXIT_RUN_MISSING_CMD = 9,
	EXIT_REQUEST_ERROR = 10
} ExitCodes;

typedef struct {
	char* progName;
	char* host;
	char* display;
	int json;
	int port;
	int fullscreen;
	unsigned int frame;
} Config;
