#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layout.h"

static size_t nlayouts = 0;
static layout_t* layouts = NULL;

static int layout_parse(char* file, layout_t* layout){
	//TODO read and parse an sfdump file
	return 1;
}

static int layout_init(layout_t* layout, char* name){
	if(name && strlen(name) < 1){
		fprintf(stderr, "Invalid layout name provided\n");
		return 1;
	}

	layout_t empty = {
		.name = name ? strdup(name) : NULL,
		.width = 0,
		.height = 0,
		.nframes = 0,
		.frames = NULL
	};
	*layout = empty;
	return 0;
}

static void layout_free(layout_t* layout){
	free(layout->name);
	free(layout->frames);
	layout_init(layout, NULL);
}

int layout_new(char* name){
	int rv = 0;
	layouts = realloc(layouts, (nlayouts + 1) * sizeof(layout_t));
	if(!layouts){
		nlayouts = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	rv = layout_init(layouts + nlayouts, name);
	nlayouts++;
	return rv;
}

int layout_config(char* option, char* value){
	if(!layouts){
		fprintf(stderr, "No layouts defined yet\n");
		return 1;
	}

	if(!strcmp(option, "file")){
		return layout_parse(value, layouts + (nlayouts - 1));
	}

	fprintf(stderr, "Unknown layout option %s\n", option);
	return 1;
}

int layout_ok(){
	if(!layouts){
		fprintf(stderr, "No layouts defined\n");
		return 1;
	}

	layout_t* layout = layouts + (nlayouts - 1);
	if(!layout->name || !layout->frames){
		fprintf(stderr, "Layout defines no frames or has no name\n");
		return 1;
	}

	if(layout->width == 0 || layout->height == 0){
		fprintf(stderr, "Invalid layout dimensions\n");
		return 1;
	}

	return 0;
}

void layout_cleanup(){
	size_t u;

	for(u = 0; u < nlayouts; u++){
		layout_free(layouts + u);
	}
	free(layouts);
	nlayouts = 0;
}
