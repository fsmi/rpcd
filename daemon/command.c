#include <stdio.h>
#include <stdlib.h>

#include "command.h"

void command_init(command_t* command){
	command_t empty = {
		0
	};
	*command = empty;
}

void command_free(command_t* command){
	size_t u, p;
	for(u = 0; u < command->nargs; u++){
		for(p = 0; command->args[u].additional && command->args[u].additional[p]; p++){
			free(command->args[u].additional[p]);
		}
		free(command->args[u].additional);
		free(command->args[u].name);
	}
	free(command->args);
	free(command->command);
	free(command->name);
}
