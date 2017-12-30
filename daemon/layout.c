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
#include "x11.h"

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

layout_t* layout_find(size_t display_id, char* name){
	size_t u;

	for(u = 0; u < nlayouts; u++){
		if(layouts[u].display_id == display_id && !strcmp(layouts[u].name, name)){
			return layouts + u;
		}
	}
	return NULL;
}

static int layout_parse(char* layout_string, size_t len, layout_t* layout){
	size_t u, p;
	int rv = 1;
	char *layout_data = NULL, *frame_info = NULL, *frame_tokenize = NULL, *token = NULL, *parameter_tokenize = NULL;

	layout_data = calloc(len + 1, sizeof(char));
	if(!layout_data){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	memcpy(layout_data, layout_string, len);
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

				if(new_frame.screen[2] > layout->max_screen){
					layout->max_screen = new_frame.screen[2];
				}
			}
		}

		//store the new frame
		layout->frames = realloc(layout->frames, (layout->nframes + 1) * sizeof(frame_t));
		if(!layout->frames){
			fprintf(stderr, "Failed to allocate memory\n");
			goto bail;
		}

		layout->frames[layout->nframes] = new_frame;
		layout->nframes++;
	}

	rv = 0;

bail:
	free(layout_data);
	return rv;
}

static int layout_parse_file(char* layout_file, layout_t* layout){
	struct stat source_info;
	int source, rv = 1;
	char* source_map = NULL;

	if(stat(layout_file, &source_info) < 0){
		fprintf(stderr, "Failed to access layout file %s: %s\n", layout_file, strerror(errno));
		return 1;
	}

	source = open(layout_file, O_RDONLY);
	if(source < 0){
		fprintf(stderr, "Failed to read layout file %s: %s\n", layout_file, strerror(errno));
		goto bail;
	}

	source_map = mmap(0, source_info.st_size, PROT_READ, MAP_PRIVATE, source, 0);
	if(source_map == MAP_FAILED){
		fprintf(stderr, "Failed to load layout file %s: %s\n", layout_file, strerror(errno));
		goto bail;
	}

	rv = layout_parse(source_map, source_info.st_size, layout);

bail:
	if(source_map){
		munmap(source_map, source_info.st_size);
	}
	if(source >= 0){
		close(source);
	}
	return rv;
}

static int layout_init(layout_t* layout, char* name, size_t display_id){
	if(name && strlen(name) < 1){
		fprintf(stderr, "Invalid layout name provided\n");
		return 1;
	}

	layout_t empty = {
		.name = name ? strdup(name) : NULL,
		.max_screen = 0,
		.nframes = 0,
		.frames = NULL,
		.display_id = display_id
	};
	*layout = empty;
	return 0;
}

static void layout_free(layout_t* layout){
	free(layout->name);
	free(layout->frames);
	layout_init(layout, NULL, 0);
}

int layout_new(char* name){
	int rv = 0;
	size_t u;
	char* display_name = "-none-";
	size_t display_id = 0;

	if(!strchr(name, ':')){
		fprintf(stderr, "Layout does not define display, using first display available\n");
	}
	else{
		display_name = name;
		name = strchr(name, ':');
		*name = 0;
		name++;
		display_id = x11_find_id(display_name);
	}

	for(u = 0; u < nlayouts; u++){
		if(!strcmp(layouts[u].name, name) && layouts[u].display_id == display_id){
			fprintf(stderr, "Layout %s already exists on display %s\n", name, display_name);
			return 1;
		}
	}

	layouts = realloc(layouts, (nlayouts + 1) * sizeof(layout_t));
	if(!layouts){
		nlayouts = 0;
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	rv = layout_init(layouts + nlayouts, name, display_id);
	nlayouts++;
	return rv;
}

int layout_config(char* option, char* value){
	char* read_layout = NULL;
	int rv = 1;
	layout_t* last = layouts + (nlayouts - 1);

	if(!layouts){
		fprintf(stderr, "No layouts defined yet\n");
		return 1;
	}

	if(!strcmp(option, "file")){
		return layout_parse_file(value, last);
	}
	if(!strcmp(option, "read-display")){
		if(!strcmp(value, "yes")){
			if(x11_fetch_layout(last->display_id, &read_layout)){
				return 1;
			}

			rv = layout_parse(read_layout, strlen(read_layout), last);

			free(read_layout);
			return rv;
		}
		return 0;
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
