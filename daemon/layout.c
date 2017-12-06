#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "layout.h"

static size_t nlayouts = 0;
static layout_t* layouts = NULL;

size_t layout_count(){
	return nlayouts;
}

layout_t* layout_get(size_t index){
	if(index < nlayouts){
		return layouts + index;
	}
	return NULL;
}

static int layout_parse(char* layout_file, layout_t* layout){
	struct stat source_info;
	size_t u, p, max_screen = 0;
	int source, rv = 0;
	char* layout_data = NULL, *source_map = NULL, *frame_info = NULL, *frame_tokenize = NULL, *token = NULL, *parameter_tokenize = NULL;

	if(stat(layout_file, &source_info) < 0){
		fprintf(stderr, "Failed to access layout file %s: %s\n", layout_file, strerror(errno));
		return 1;
	}

	layout_data = calloc(source_info.st_size + 1, sizeof(char));
	if(!layout_data){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}
	
	source = open(layout_file, O_RDONLY);
	if(source < 0){
		fprintf(stderr, "Failed to read layout file %s: %s\n", layout_file, strerror(errno));
		rv = 1;
		goto bail;
	}

	source_map = mmap(0, source_info.st_size, PROT_READ, MAP_PRIVATE, source, 0);
	if(source_map == MAP_FAILED){
		fprintf(stderr, "Failed to load layout file %s: %s\n", layout_file, strerror(errno));
		rv = 1;
		goto bail;
	}

	memcpy(layout_data, source_map, source_info.st_size);
	u = 0;
	for(p = 0; layout_data[p]; p++){
		//strip non-graphs completely, saves on parsing overhead
		if(isgraph(layout_data[p])){
			layout_data[u++] = layout_data[p];
		}
	}
	layout_data[u] = 0;

	for(frame_info = strtok_r(layout_data, ",", &frame_tokenize); frame_info; frame_info = strtok_r(NULL, ",", &frame_tokenize)){
		frame_t new_frame = {
			0
		};
		for(token = strtok_r(frame_info, ":", &parameter_tokenize); token; token = strtok_r(NULL, ":", &parameter_tokenize)){
			if(!strncmp(token, "number", 6)){
				new_frame.id = strtoul(token + 6, NULL, 10);
			}
			else if(!strncmp(token, "x", 1)){
				new_frame.bbox[0] = strtoul(token + 1, NULL, 10);
			}
			else if(!strncmp(token, "y", 1)){
				new_frame.bbox[1] = strtoul(token + 1, NULL, 10);
			}
			else if(!strncmp(token, "width", 5)){
				new_frame.bbox[2] = strtoul(token + 5, NULL, 10);
			}
			else if(!strncmp(token, "height", 6)){
				new_frame.bbox[3] = strtoul(token + 6, NULL, 10);
			}
			else if(!strncmp(token, "screenw", 7)){
				new_frame.screen[0] = strtoul(token + 7, NULL, 10);
			}
			else if(!strncmp(token, "screenh", 7)){
				new_frame.screen[1] = strtoul(token + 7, NULL, 10);
			}

			if(strchr(token, ')')){
				//parse screen no
				token = strchr(token, ')');
				new_frame.screen[2] = strtoul(token + 1, NULL, 10);

				if(new_frame.screen[2] > max_screen){
					max_screen = new_frame.screen[2];
				}
			}
		}

		//store the new frame
		layout->frames = realloc(layout->frames, (layout->nframes + 1) * sizeof(frame_t));
		if(!layout->frames){
			fprintf(stderr, "Failed to allocate memory\n");
			rv = 1;
			goto bail;
		}

		layout->frames[layout->nframes] = new_frame;
		layout->nframes++;
	}

	//recalculate screen extents
	for(p = 0; p < max_screen; p++){
		for(u = 0; u < layout->nframes; u++){
			if(layout->frames[u].screen[2] == p){
				layout->width += layout->frames[u].screen[0];
				if(layout->height < layout->frames[u].screen[1]){
					layout->height = layout->frames[u].screen[1];
				}
				break;
			}
		}
	}

bail:
	free(layout_data);
	munmap(source_map, source_info.st_size);
	close(source);
	return rv;
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
