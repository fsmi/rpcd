#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include <stdarg.h>

#include "../libs/easy_args.h"
#include "../libs/curl_conn.h"
#include "../libs/easy_json.h"

#include "rpcd-cli.h"

int usage(int argc, char** argv, Config* config) {

	printf("%s - rpcd client\n"
			"%s [<options>] cmd\n"
			"\nCommands:\n"
			"    commands           List available commands\n"
			"    layouts            List available layouts\n"
			"    apply <layout>     Load a layout\n"
			"    run <cmd> <args>   Run a command with the specified arguments (formatted as key=value)\n"
			"    stop <command>     Stop a command\n"
			"    reset              Reset the server\n"
			"    status             Query server status\n"
			"    help               Show this help text\n"
			"\nOptions:\n"
			"    -?, --help         Show this help text\n"
			"    -F, --fullscreen   Run a command fullscreen\n"
			"    -f, --frame        Select frame to execute command in\n"
			"    -j, --json         Machine readable (JSON) output\n"
			"    -h, --host <host>  Target host\n"
			"    -p, --port <port>  Target port\n"
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

	char* endptr;

	config->frame = strtoul(argv[1], &endptr, 10);
	if (argv[1] == endptr) {
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
		return NULL;
	}

	char* url = calloc(url_len + arg_len + 1, sizeof(char));

	if (!url) {
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
			printf("%s%s%s ", display ? display : "", display ? "/" : "", name);
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

	printf("\tAvailable frames:\n");

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
			fprintf(stderr, "Arguments:\n");
			ejson_struct* args = elem->child;

			while (args) {
				elem = ejson_find_key(args->child, "name", false);
				if (!elem) {
					fprintf(stderr, "Server response format invalid: missing key\n");
					return 1;
				}

				char* arg_name;
				if (ejson_get_string(elem, &arg_name) != EJSON_OK) {
					fprintf(stderr, "Server response format invalid: invalid type\n");
					return 1;
				}

				elem = ejson_find_key(args->child, "type", false);
				if (!elem) {
					fprintf(stderr, "Server response format invalid: missing key\n");
					return 1;
				}

				char* type;
				if (ejson_get_string(elem, &type) != EJSON_OK) {
					fprintf(stderr, "Server response format invalid: invalid type\n");
					return 1;
				}

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

int list_commands(Config* config) {
	struct netdata data = {0};

	char* url = get_url(config, "commands");

	if (!url) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	int status = request(url, NULL, &data);
	free(url);
	if (!status) {

		if (config->json) {
			printf("%s\n", data.data);
		} else {
			status = parse_commands(config, &data);
		}
	}

	free(data.data);
	return status;
}

int state(Config* config) {
	struct netdata data = {0};

	char* url = get_url(config, "status");

	if (!url) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	int status = request(url, NULL, &data);
	free(url);
	if (!status) {
		if (config->json) {
			printf("%s\n", data.data);
		} else {
			status = parse_state(config, &data);
		}
	}

	free(data.data);
	return status;
}

int list_layouts(Config* config) {
	char* url = get_url(config, "layouts");

	if (!url) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	struct netdata data = {};
	int status = request(url, NULL, &data);


	if (!status) {
		status = parse_displays(config, &data);
	}

	free(data.data);
	free(url);
	return status;
}

int stop_command(Config* config, char* name) {

	char* url = get_url(config, "stop/%s", name);
	if (!url) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	struct netdata data = {};
	int status = request(url, NULL, &data);

	free(data.data);
	free(url);
	return status;
}

int run_command(Config* config, char* name, int argc, char** args) {
	const char* json_format = "{\"fullscreen\":%d,\"arguments\":{%s},\"frame\":%d%s}";
	char* keys[argc];
	char* values[argc];
	char* arg_json = strdup("");
	char* format;
	int length = 0;
	int oldlength = 0;
	int i;
	char* display_format = ",\"display\":\"%s\"";
	char* display;
	char* dis_out;
	int display_len = 0;
	char* cmd;
	display = strtok(name, "/");
	cmd = strtok(NULL, "/");
	if (cmd) {
		display_len = snprintf(NULL, 0, display_format, display);
		dis_out = calloc(display_len + 1, sizeof(char));
		sprintf(dis_out, display_format, display);
	} else {
		cmd = name;
		dis_out = strdup("");
	}

	for (i = 0; i < argc; i++) {
		keys[i] = strtok(args[i], "=");
		values[i] = strtok(NULL, "=");
		if (values[i] == NULL) {
			fprintf(stderr, "Invalid assignment for %s\n", keys[i]);
			free(dis_out);
			free(arg_json);
			return EXIT_CMD_ARGUMENT_MISSING_KEY;
		}
		fprintf(stderr, "%s.%s->%s\n", name, keys[i], values[i]);

		if (i == 0) {
			format = "\"%s\":\"%s\"";
		} else {
			format = ",\"%s\":\"%s\"";
		}
		length = snprintf(NULL, 0, format , keys[i], values[i]);
		arg_json = realloc(arg_json, oldlength + length + 1 * sizeof(char));
		if (!arg_json) {
			fprintf(stderr, "Failed to allocate memory\n");
			free(dis_out);
			free(arg_json);
			return 1;
		}
		sprintf(arg_json + oldlength, format, keys[i], values[i]);
		oldlength += length;
	}

	length = snprintf(NULL, 0, json_format, config->fullscreen, arg_json, config->frame, dis_out);
	char* post_data = calloc(length + 1, sizeof(char));
	if (!post_data) {
		fprintf(stderr, "Failed to allocate memory\n");
		free(dis_out);
		free(arg_json);
		return 1;
	}
	sprintf(post_data, json_format, config->fullscreen, arg_json, config->frame, dis_out);
	free(dis_out);
	free(arg_json);

	char* url = get_url(config, "command/%s", cmd);
	if (!url) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	struct netdata data = {};
	int status = request(url, post_data, &data);

	free(data.data);
	free(url);
	free(post_data);
	return status;
}

int apply_layout(Config* config, char* name) {
	printf("Loading layout %s\n", name);

	char* url = get_url(config, "layout/%s", name);

	if (!url) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	struct netdata data = {};
	int status = request(url, NULL, &data);

	free(data.data);
	free(url);
	return status;
}

int handle_command(Config* config, int cmdc, char** cmds) {
	if(!strcmp("help", cmds[0])){
		usage(0, NULL, config);
		return 0;
	}
	if (!strcmp("layouts", cmds[0])) {
		return list_layouts(config);
	}
	else if(!strcmp("commands", cmds[0])){
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
	} else if (!strcmp("status", cmds[0])) {
		return state(config);
	} else {
		usage(0, NULL, config);
		return EXIT_UNKNOWN_CMD;
	}

	return EXIT_SUCCESS;
}

int main(int argc, char** argv) {

	Config config = {
		.progName = "rpcd-cli",
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
		return EXIT_NO_CMD;
	}

	curl_init_global();

	int status = handle_command(&config, outputc, output);
	curl_global_cleanup();
	return status;
}
