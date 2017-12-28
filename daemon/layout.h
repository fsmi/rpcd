#ifndef RPCD_LAYOUT_H
#define RPCD_LAYOUT_H
struct _x11_display_t;

#define display_t struct _x11_display_t

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
	display_t* display;
} layout_t;

size_t layout_count();
layout_t* layout_get(size_t index);
layout_t* layout_find(display_t* display, char* name);

int layout_new(char* name);
int layout_config(char* option, char* value);
int layout_ok();
void layout_cleanup();

#undef display_t

#endif
