#include "command.h"
#include "layout.h"

typedef struct {
	size_t ncommands;
	command_t* commands;
	size_t nlayouts;
	layout_t* layouts;
} rpcd_config_t;

int config_parse(char* file);
void config_free();
