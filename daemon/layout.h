typedef struct /*_ratpoison_layout_frame*/ {
	size_t id;
	size_t bbox[4];
} frame_t;

typedef struct /*_ratpoison_layout_t*/ {
	char* name;
	size_t width;
	size_t height;
	size_t nframes;
	frame_t* frames;
} layout_t;

int layout_new(char* name);
int layout_config(char* option, char* value);
int layout_ok();
void layout_cleanup();
