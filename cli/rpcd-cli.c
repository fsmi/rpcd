#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include <stdarg.h>

#include "../libs/strdup.h"
#include "../libs/easy_args.h"
#include "../libs/easy_json.h"

#include "curl_conn.h"
#include "rpcd-cli.h"

/**
 * Returns a allocated formatted string.
 * Like sprintf but with alloction.
 */
char* c_sprintf(const char* format, ...) {

	va_list ap;

	va_start(ap, format);

	// get length
	int len = vsnprintf(NULL, 0, format, ap);

	va_end(ap);

	if (len <= 0) {
		fprintf(stderr, "snprintf returns a negative or zero length.\n");
		return NULL;
	}

	// allocate
	char* out = calloc(len + 1, sizeof(char));

	if (!out) {
		fprintf(stderr, "Failed to allocate memory.\n");
		return NULL;
	}

	va_start(ap, format);

	// copy
	vsprintf(out, format, ap);
	va_end(ap);

	return out;
}
int usage(int argc, char** argv, Config* config) {

	printf("%s - rpcd client\n"
			"%s [<options>] cmd\n"
			"\nCommands:\n"
			"    commands            List available commands\n"
			"    layouts             List available layouts\n"
			"    apply <layout>      Load a layout\n"
			"    run <cmd> <args>    Run a command with the specified arguments (formatted as key=value)\n"
			"    stop <command>      Stop a command\n"
			"    reset               Reset the server\n"
			"    status              Query server status\n"
			"    help                Show this help text\n"
			"\nOptions:\n"
			"    -?, --help          Show this help text\n"
			"    -F, --fullscreen    Run a command fullscreen\n"
			"    -f, --frame <frame> Select frame to execute command in\n"
			"    -j, --json          Machine readable (JSON) output\n"
			"    -h, --host <host>   Target host\n"
			"    -p, --port <port>   Target port\n"
			, config->progName, config->progName);

	return -1;
}

int set_fullscreen(int argc, char** argv, Config* config) {
	config->fullscreen = 1;
	return 0;
}

int set_json(int argc, char** argv, Config* config) {
	config->json = 1;
	return 0;
}

int set_host(int argc, char** argv, Config* config) {
	if (argc < 2) {
		return -1;
	}

	config->host = argv[1];
	return 1;
}

int set_frame(int argc, char** argv, Config* config) {
	if (argc < 2) {
		return -1;
	}
	char* display = strtok(argv[1], "/");
	char* frame = strtok(NULL, "/");

	if (frame) {
		config->display = display;
	} else {
		frame = display;
	}

	char* endptr;

	config->frame = strtoul(frame, &endptr, 10);
	if (frame == endptr) {
		fprintf(stderr, "Frame ID should be an unsigned number\n");
		return -1;
	}

	return 1;
}

int set_port(int argc, char** argv, Config* config) {
	if (argc < 2) {
		return -1;
	}

	char* endptr;

	config->port = strtoul(argv[1], &endptr, 10);
	if (argv[1] == endptr) {
		fprintf(stderr, "Invalid port provided\n");
		return -1;
	}

	return 1;
}


char* get_url(Config* config, char* format, ...) {
	va_list ap;
	const char* url_format = "http://%s:%d/";

	va_start(ap, format);

	int url_len = snprintf(NULL, 0, url_format, config->host, config->port);
	int arg_len = vsnprintf(NULL, 0, format, ap);
	va_end(ap);

	if (url_len <= 0 || arg_len <= 0) {
		fprintf(stderr, "snprintf return negative or zero length.\n");
		return NULL;
	}

	char* url = calloc(url_len + arg_len + 1, sizeof(char));

	if (!url) {
		fprintf(stderr, "Failed to allocate memory.\n");
		return NULL;
	}

	sprintf(url, "http://%s:%d/", config->host, config->port);
	va_start(ap, format);
	vsprintf(url + url_len, format, ap);
	va_end(ap);
	return url;
}

int parse_state_layout(Config* config, ejson_struct* root) {

	ejson_struct* next = root;

	char* name = NULL;
	char* display = NULL;
	printf("Currently active layouts:\n\t");
	while (next) {
		name = NULL;
		display = NULL;

		ejson_get_string_from_key(next->child, "layout", false, &name);

		if(!name){
			fprintf(stderr, "Server response format invalid: missing key\n");
			return 1;
		}

		ejson_get_string_from_key(next->child, "display", false, &display);

		if (display) {
			printf("%s/%s ", display, name);
		} else {
			printf("%s ", name);
		}

		next = next->next;
	}

	return 0;
}

int parse_state(Config* config, struct netdata* data) {

	ejson_struct* root = NULL;

	if (ejson_parse_warnings(&root, data->data, data->len, true, stderr) != EJSON_OK) {
		return 1;
	}

	if (root->type != EJSON_OBJECT) {
		fprintf(stderr, "Server response format invalid: invalid root type\n");
		ejson_cleanup(root);
		return 1;
	}

	ejson_struct* elem = ejson_find_key(root->child, "layout", false);
	if (!elem) {
		fprintf(stderr, "Server response format invalid: missing key\n");
		ejson_cleanup(root);
		return 1;
	}
	if (parse_state_layout(config, elem->child)) {
		ejson_cleanup(root);
		return 1;
	}

	elem = ejson_find_key(root->child, "running", false);
	if (!elem) {
		fprintf(stderr, "Server response format invalid: missing key\n");
		ejson_cleanup(root);
		return 1;
	}
	ejson_struct* next = elem->child;
	char* cmd;

	printf("\nCurrently running commands:\n\t");
	while (next) {
		if (ejson_get_string(next, &cmd) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			ejson_cleanup(root);
			return 1;
		}

		printf("%s ", cmd);
		next = next->next;
	}
	printf("\n");

	ejson_cleanup(root);

	return 0;
}

int parse_frames(ejson_struct* root) {
	ejson_struct* elem;

	elem = ejson_find_key(root, "frames", false);
	if (!elem) {
		return 1;
	}

	elem = elem->child;

	printf("Available frames:\n");

	while (elem) {
		int id = -1;
		int x = -1;
		int y = -1;
		int w = -1;
		int h = -1;
		int screen = -1;

		ejson_get_int_from_key(elem->child, "id", false, &id);
		ejson_get_int_from_key(elem->child, "x", false, &x);
		ejson_get_int_from_key(elem->child, "y", false, &y);
		ejson_get_int_from_key(elem->child, "w", false, &w);
		ejson_get_int_from_key(elem->child, "h", false, &h);
		ejson_get_int_from_key(elem->child, "screen", false, &screen);

		printf("\t[ID %d]\t(%d,%d) %dx%d on Screen %d\n", id, x, y, w, h, screen);

		elem = elem->next;
	}

	return 0;
}

int parse_screens(ejson_struct* root) {
	ejson_struct* elem;

	elem = ejson_find_key(root, "screens", false);
	if (!elem) {
		return 1;
	}

	elem = elem->child;

	printf("\tMapped screens:\n");

	while (elem) {
		int id = -1;
		int w = -1;
		int h = -1;

		ejson_get_int_from_key(elem->child, "id", false, &id);
		ejson_get_int_from_key(elem->child, "width", false, &w);
		ejson_get_int_from_key(elem->child, "height", false, &h);

		printf("\t[%d]:\t%dx%d\n", id, w, h);

		elem = elem->next;
	}

	return 0;
}

int parse_layouts(Config* config, ejson_struct* root, char* display) {
	ejson_struct* next = root->child;

	char* name;
	while (next) {
		if (ejson_get_string_from_key(next->child, "name", false, &name) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			return 1;
		}

		printf("Layout %s/%s\n", display, name);

		parse_screens(next->child);
		parse_frames(next->child);
		printf("\n");

		next = next->next;
	}

	return 0;
}

int parse_displays(Config* config, struct netdata* data) {

	ejson_struct* root = NULL;

	if (ejson_parse_warnings(&root, data->data, data->len, true, stderr) != EJSON_OK) {
		return 1;
	}

	if (root->type != EJSON_ARRAY) {
		fprintf(stderr, "Server response format invalid: invalid root type\n");
		return 1;
	}

	ejson_struct* next = root->child;
	ejson_struct* layout;
	char* display;
	while (next) {
		if (ejson_get_string_from_key(next->child, "display", false, &display) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			return 1;
		}

		layout = ejson_find_key(next->child, "layouts", false);

		if (!layout) {
			fprintf(stderr, "Server response format invalid: missing key\n");
			return 1;
		}

		if (parse_layouts(config, layout, display)) {
			return 1;
		}

		next = next->next;
	}

	ejson_cleanup(root);

	return 0;
}

int parse_commands(Config* config, struct netdata* data) {
	ejson_struct* ejson = NULL;
	if (ejson_parse_warnings(&ejson, data->data, data->len, true, stderr) != EJSON_OK) {
		return 1;
	}

	if (ejson->type != EJSON_ARRAY) {
		fprintf(stderr, "Server response format invalid: invalid type\n");
		return 1;
	}

	if (!ejson->child) {
		fprintf(stderr, "Server response format invalid: empty root\n");
		return 1;
	}

	// objects
	ejson_struct* next = ejson->child;
	ejson_struct* elem =  NULL;
	while (next) {
		elem = ejson_find_key(next->child, "name", false);
		if (!elem) {
			fprintf(stderr, "Server response format invalid: missing key\n");
			return 1;
		}
		char* name;
		if (ejson_get_string(elem, &name) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			return 1;
		}
		printf("Command \"%s\"\n", name);

		elem = ejson_find_key(next->child, "description", false);

		if (elem) {
			char* description;

			if (ejson_get_string(elem, &description) != EJSON_OK) {
				fprintf(stderr, "Server response format invalid: invalid type\n");
				return 1;
			}

			if(description && strlen(description)){
				printf("-> %s\n\n", description);
			}
		}

		elem = ejson_find_key(next->child, "windows", false);
		if (elem) {
			int state;
			if (ejson_get_int(elem, &state) != EJSON_OK) {
				fprintf(stderr, "Server response format invalid: invalid type\n");
				return 1;
			}

			if (!state) {
				printf("This command will not create any windows\n");
			}
		}

		elem = ejson_find_key(next->child, "args", false);
		if (elem && elem->child && elem->child) {
			printf("Arguments:\n");
			ejson_struct* args = elem->child;

			while (args) {

				char* arg_name;
				if (ejson_get_string_from_key(elem, "name", false, &arg_name) != EJSON_OK) {
					fprintf(stderr, "Server response format invalid: missing key\n");
					return 1;
				}

				char* type;
				if (ejson_get_string_from_key(args->child, "type", false, &type) != EJSON_OK) {
					fprintf(stderr, "Server response format invalid: missing key\n");
					return 1;
				}

				printf("%s=", arg_name);
				if (!strcmp(type, "enum")) {
					elem = ejson_find_key(args->child, "options", false);
					if (!elem) {
						fprintf(stderr, "Server response format invalid: missing key\n");
						return 1;
					}

					printf("\t%s: ", arg_name);

					ejson_struct* keys = elem->child;
					char* option;
					while (keys) {
						if (ejson_get_string(keys, &option) != EJSON_OK) {
							fprintf(stderr, "Server response format invalid: invalid type\n");
							return 1;
						}
						printf("%s", option);
						keys = keys->next;

						if (keys) {
							printf(" | ");
						}
					}

					printf("\n");
				} else {
					elem = ejson_find_key(args->child, "hint", false);
					if (elem) {
						char* hint;
						if (ejson_get_string(elem, &hint) != EJSON_OK) {
							fprintf(stderr, "Server response format invalid: invalid type\n");
							return 1;
						}
						printf("\t%s: %s\n", arg_name, hint);
					}
				}

				args = args->next;
			}
			printf("\n");
		}

		next = next->next;
	}

	return 0;
}

int fetch(Config* config, char* url_param, int (*func)(Config* config, struct netdata* data)) {
	int status = 0;

	struct netdata data = {0};
	char* url = get_url(config, url_param);

	if (!url) {
		return 1;
	}

	status = request(url, NULL, &data);
	free(url);
	if (!status) {
		if (config->json) {
			printf("%s\n", data.data);
		} else {
			status = func(config, &data);
		}
	}

	free(data.data);

	return status;
}

int list_commands(Config* config) {
	return fetch(config, "commands", parse_commands);
}

int state(Config* config) {
	return fetch(config, "status", parse_state);
}

int list_layouts(Config* config) {
	return fetch(config, "layouts", parse_displays);
}

int exec_request(Config* config, char* format, char* name, char* post_data) {

	char* url = get_url(config, format, name);
	if (!url) {
		return 1;
	}

	struct netdata data = {};
	int status = request(url, post_data, &data);
	free(data.data);
	free(url);

	return status;
}

int apply_layout(Config* config, char* name) {
	return exec_request(config, "layout/%s", name, NULL);
}
int stop_command(Config* config, char* name) {
	return exec_request(config, "stop/%s", name, NULL);
}

int assamble_arguments(int argc, char** argv, char** out) {
	char* key;
	char* value;
	char* format;
	*out = strdup("");
	unsigned int u;
	int length = 0;
	int old_length = 0;
	for (u = 0; u < argc; u++) {
		key = strtok(argv[u], "=");
		value = strtok(NULL, "=");

		if (!value) {
			fprintf(stderr, "Invalid assignment for %s\n", key);
			return EXIT_CMD_ARGUMENT_MISSING_KEY;
		}
		if (u == 0) {
			format = "\"%s\":\"%s\"";
		} else {
			format = ",\"%s\":\"%s\"";
		}
		length = snprintf(NULL, 0, format , key, value);
		*out = realloc(*out, old_length + length + 1 * sizeof(char));
		if (!*out) {
			return EXIT_ALLOCATION_FAILED;
		}
		sprintf(*out + old_length, format, key, value);
		old_length += length;
	}

	return EXIT_SUCCESS;
}

int run_command(Config* config, char* name, int argc, char** args) {
	const char* json_format = "{\"fullscreen\":%d,\"arguments\":{%s},\"frame\":%d%s}";
	char* display_format = ",\"display\":\"%s\"";
	char* json_display;

	if (config->display) {
		json_display = c_sprintf(display_format, config->display);
		if (!json_display) {
			return 1;
		}
	} else {
		json_display = strdup("");
	}
	char* json_arg = NULL;

	int status = assamble_arguments(argc, args, &json_arg);

	if (status) {
		free(json_arg);
		free(json_display);
		return status;
	}

	char* post_data = c_sprintf(json_format, config->fullscreen, json_arg, config->frame, json_display);
	free(json_display);
	free(json_arg);

	if (!post_data) {
		return 1;
	}

	status = exec_request(config, "command/%s", name, post_data);

	free(post_data);
	return status;
}

int handle_command(Config* config, int cmdc, char** cmds) {

	if (!strcmp("layouts", cmds[0])) {
		return list_layouts(config);
	} else if (!strcmp("commands", cmds[0])) {
		return list_commands(config);
	} else if (!strcmp("run", cmds[0])) {
		if (cmdc < 2) {
			fprintf(stderr, "What command should be run.\n");
			return EXIT_RUN_MISSING_CMD;
		}
		return run_command(config, cmds[1], cmdc - 2, cmds + 2);
	} else if (!strcmp("stop", cmds[0])) {
		if (cmdc < 2) {
			fprintf(stderr, "Missing command name.\n");
			return EXIT_MISSING_CMD_NAME;
		}
		if (stop_command(config, cmds[1])) {
			return EXIT_REQUEST_ERROR;
		}
	} else if (!strcmp("state", cmds[0]) || !strcmp("status", cmds[0])) {
		return state(config);
	} else {
		fprintf(stderr, "Command %s unkown.\n", cmds[0]);
		return EXIT_UNKNOWN_CMD;
	}

	return EXIT_SUCCESS;
}

int main(int argc, char** argv) {

	Config config = {
		.progName = argv[0],
		.host = "localhost",
		.port = 8080,
		.fullscreen = 0,
		.frame = 1
	};

	eargs_addArgument("-?", "--help", usage, 0);
	eargs_addArgument("-F", "--fullscreen", set_fullscreen, 0);
	eargs_addArgument("-f", "--frame", set_frame, 1);
	eargs_addArgument("-j", "--json", set_json, 0);
	eargs_addArgument("-h", "--host", set_host, 1);
	eargs_addArgument("-p", "--port", set_port, 1);

	char* output[argc];

	int outputc = eargs_parse(argc, argv, output, &config);

	if (outputc < 1) {
		fprintf(stderr, "No command given.\n");
		usage(argc, argv, &config);
		return EXIT_NO_CMD;
	}

	curl_init_global();

	int status = handle_command(&config, outputc, output);
	curl_global_cleanup();
	return status;
}
