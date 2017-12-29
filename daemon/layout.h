#ifndef RPCD_LAYOUT_H
#define RPCD_LAYOUT_H

typedef struct /*_ratpoison_layout_frame*/ {
	size_t id;
	size_t bbox[4]; //x y w h
	size_t screen[3]; //w h id
} frame_t;

typedef struct /*_ratpoison_layout_t*/ {
	char* name;
	size_t nframes;
	size_t max_screen;
	frame_t* frames;
	size_t display_id;
} layout_t;

size_t layout_count();
layout_t* layout_get(size_t index);
layout_t* layout_find(size_t display_id, char* name);

int layout_new(char* name);
int layout_config(char* option, char* value);
int layout_ok();
void layout_cleanup();

#endif
