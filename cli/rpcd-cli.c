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
			"    commands                      List available commands\n"
			"    layouts                       List available layouts\n"
			"    apply <layout>                Load a layout\n"
			"    run <cmd> <args>              Run a command with the specified arguments (formatted as key=value)\n"
			"    stop <command>                Stop a command\n"
			"    reset                         Reset the server\n"
			"    status                        Query server status\n"
			"    help                          Show this help text\n"
			"\nOptions:\n"
			"    -?, --help                    Show this help text\n"
			"    -F, --fullscreen              Run a command fullscreen\n"
			"    -f, --frame <frame>           Select frame to execute command in\n"
			"                <display>/<frame> Select display and frame to execute command in\n"
			"    -j, --json                    Machine readable (JSON) output\n"
			"    -h, --host <host>             Target host\n"
			"    -p, --port <port>             Target port\n"
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

int parse_state_layout(Config* config, ejson_array* root) {

	int i;
	int err;
	char* name = NULL;
	char* display = NULL;
	printf("Currently active layouts:\n\t");
	for (i = 0; i < root->length; i++) {

		if (root->values[i]->type != EJSON_OBJECT) {
			fprintf(stderr, "Value is not an object");
			continue;
		}

		name = NULL;
		display = NULL;

		err = ejson_get_string_from_key((ejson_object*)root->values[i], "layout", false, false, &name);

		if (err != EJSON_KEY_NOT_FOUND) {
			continue;
		} else if (err != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: missing key\n");
			return 1;
		}

		err = ejson_get_string_from_key((ejson_object*) root->values[i], "display", false, false, &display);
		if (err != EJSON_KEY_NOT_FOUND) {
			printf("%s ", name);
		} else if (err != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: missing key\n");
			return 1;
		} else {
			printf("%s/%s ", display, name);
		}
	}

	return 0;
}

int parse_state(Config* config, struct netdata* data) {

	ejson_base* root = NULL;

	if (ejson_parse_warnings(data->data, data->len, true, stderr, &root) != EJSON_OK) {
		return 1;
	}

	if (root->type != EJSON_OBJECT) {
		fprintf(stderr, "Server response format invalid: invalid root type\n");
		ejson_cleanup(root);
		return 1;
	}

	ejson_array* elem = (ejson_array*) ejson_find_by_key((ejson_object*) root, "layout", false, false);
	if (!elem) {
		fprintf(stderr, "Server response format invalid: missing key\n");
		ejson_cleanup(root);
		return 1;
	}

	if (elem->base.type != EJSON_ARRAY) {
		fprintf(stderr, "Server response format invalid: missing key\n");
		return 1;
	}

	if (parse_state_layout(config, (ejson_array*) elem)) {
		ejson_cleanup(root);
		return 1;
	}

	elem = (ejson_array*) ejson_find_by_key((ejson_object*) root, "running", false, false);
	if (!elem) {
		fprintf(stderr, "Server response format invalid: missing key\n");
		ejson_cleanup(root);
		return 1;
	}
	char* cmd;
	int i;
	printf("\nCurrently running commands:\n\t");
	for (i = 0; i < elem->length; i++) {
		if (ejson_get_string(elem->values[i], &cmd) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			ejson_cleanup(root);
			return 1;
		}

		printf("%s ", cmd);
	}
	printf("\n");

	ejson_cleanup(root);

	return 0;
}

int parse_frames(ejson_object* root) {
	ejson_array* elem;

	elem = (ejson_array*) ejson_find_by_key(root, "frames", false, false);
	if (!elem) {
		return 1;
	}

	if (elem->base.type != EJSON_ARRAY) {
		return 1;
	}

	printf("Available frames:\n");
	int i;
	ejson_object* item;
	for (i = 0; i < elem->length; i++) {
		item = (ejson_object*) elem->values[i];

		if (item->base.type != EJSON_OBJECT) {
			continue;
		}

		int id = -1;
		int x = -1;
		int y = -1;
		int w = -1;
		int h = -1;
		int screen = -1;

		ejson_get_int_from_key(item, "id", false, false, &id);
		ejson_get_int_from_key(item, "x", false, false, &x);
		ejson_get_int_from_key(item, "y", false, false, &y);
		ejson_get_int_from_key(item, "w", false, false, &w);
		ejson_get_int_from_key(item, "h", false, false, &h);
		ejson_get_int_from_key(item, "screen", false, false, &screen);

		printf("\t[ID %d]\t(%d,%d) %dx%d on Screen %d\n", id, x, y, w, h, screen);
	}

	return 0;
}

int parse_screens(ejson_object* root) {
	ejson_array* elem;

	elem = (ejson_array*) ejson_find_by_key(root, "screens", false, false);
	if (!elem) {
		return 1;
	}

	if (elem->base.type != EJSON_ARRAY) {
		return 1;
	}

	printf("\tMapped screens:\n");

	int i;
	ejson_object* item;
	for (i = 0; i < elem->length; i++) {
		item = (ejson_object*) elem->values[i];

		if (item->base.type != EJSON_OBJECT) {
			continue;
		}

		int id = -1;
		int w = -1;
		int h = -1;

		ejson_get_int_from_key(item, "id", false, false, &id);
		ejson_get_int_from_key(item, "width", false, false, &w);
		ejson_get_int_from_key(item, "height", false, false, &h);

		printf("\t[%d]:\t%dx%d\n", id, w, h);
	}

	return 0;
}

int parse_layouts(Config* config, ejson_array* root, char* display) {

	int i;
	char* name;
	ejson_object* elem;
	for (i = 0; i < root->length; i++) {
		elem = (ejson_object*) root->values[i];
		if (elem->base.type != EJSON_OBJECT) {
			continue;
		}

		if (ejson_get_string_from_key(elem, "name", false, false, &name) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			return 1;
		}

		printf("Layout %s/%s\n", display, name);

		parse_screens(elem);
		parse_frames(elem);
		printf("\n");
	}

	return 0;
}

int parse_displays(Config* config, struct netdata* data) {

	ejson_array* root = NULL;

	if (ejson_parse_warnings(data->data, data->len, true, stderr, (ejson_base**) &root) != EJSON_OK) {
		return 1;
	}

	if (root->base.type != EJSON_ARRAY) {
		fprintf(stderr, "Server response format invalid: invalid root type\n");
		return 1;
	}

	ejson_object* elem;
	ejson_array* layout;
	char* display;
	int i;
	for (i = 0; i < root->length; i++) {
		elem = (ejson_object*) root->values[i];
		if (ejson_get_string_from_key(elem, "display", false, false, &display) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			return 1;
		}

		layout = (ejson_array*) ejson_find_by_key(elem, "layouts", false, false);

		if (!layout) {
			fprintf(stderr, "Server response format invalid: missing key\n");
			return 1;
		}

		if (parse_layouts(config, layout, display)) {
			return 1;
		}
	}

	ejson_cleanup((ejson_base*) root);

	return 0;
}

int print_options(Config* config, ejson_object* root, char* arg_name) {
	ejson_array* options = (ejson_array*) ejson_find_by_key(root, "options", false, false);
	if (!options) {
		fprintf(stderr, "Server response format invalid: missing key\n");
		return 1;
	}

	if (options->base.type != EJSON_ARRAY) {
		fprintf(stderr, "Server response format invalid: wrong type\n");
		return 1;
	}

	printf("\t%s: ", arg_name);

	char* option;
	int i;
	for (i = 0; i < options->length; i++) {
		if (ejson_get_string(options->values[i], &option) != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			return 1;
		}
		printf("%s", option);
		if (i + 1 < options->length) {
			printf(" | ");
		}
	}

	printf("\n");

	return 0;
}

int parse_commands(Config* config, struct netdata* data) {
	ejson_array* ejson = NULL;
	if (ejson_parse_warnings(data->data, data->len, true, stderr, (ejson_base**) &ejson) != EJSON_OK) {
		return 1;
	}

	if (!ejson || ejson->base.type != EJSON_ARRAY) {
		fprintf(stderr, "Server response format invalid: invalid type\n");
		ejson_cleanup((ejson_base*) ejson);
		return 1;
	}

	ejson_object* elem;
	ejson_array* args;
	int i;
	int err;
	int state;
	char* name;
	char* description;
	for (i = 0; i < ejson->length; i++) {
		elem = (ejson_object*) ejson->values[i];
		if (elem->base.type != EJSON_OBJECT) {
			continue;
		}
		err = ejson_get_string_from_key(elem, "name", false, false, &name);
		if (err == EJSON_KEY_NOT_FOUND) {
			continue;
		} else if (err != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			ejson_cleanup((ejson_base*) ejson);
			return 1;
		}
		printf("Command \"%s\"\n", name);

		err = ejson_get_string_from_key(elem, "description", false, false, &description);
		if (err != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			ejson_cleanup((ejson_base*) ejson);
			return 1;
		}

		if(description && strlen(description)){
			printf("-> %s\n\n", description);
		}

		err = ejson_get_int_from_key(elem, "windows", false, false, &state);
		if (err != EJSON_OK) {
			fprintf(stderr, "Server response format invalid: invalid type\n");
			ejson_cleanup((ejson_base*) ejson);
			return 1;
		}

		if (!state) {
			printf("This command will not create any windows\n");
		}

		args = (ejson_array*) ejson_find_by_key(elem, "args", false, false);
		if (args->base.type != EJSON_ARRAY) {
			fprintf(stderr, "Args is not an array.\n");
			ejson_cleanup((ejson_base*) ejson);
			return 1;
		}

		printf("Arguments:\n");
		int j;
		ejson_object* arg;
		char* arg_name;
		for (j = 0; j < args->length; j++) {
			arg = (ejson_object*) args->values[j];

			if (arg->base.type != EJSON_OBJECT) {
				continue;
			}

			err = ejson_get_string_from_key(arg, "name", false, false, &arg_name);
			if (err != EJSON_OK) {
				fprintf(stderr, "Server response format invalid: missing key\n");
				ejson_cleanup((ejson_base*) ejson);
				return 1;
			}

			char* type;
			err = ejson_get_string_from_key(arg, "type", false, false, &type);
			if (err != EJSON_OK) {
				fprintf(stderr, "Server response format invalid: missing key\n");
				ejson_cleanup((ejson_base*) ejson);
				return 1;
			}

			printf("%s=", arg_name);
			if (!strcmp(type, "enum")) {
				if (print_options(config, arg, arg_name)) {
					ejson_cleanup((ejson_base*) ejson);
					return 1;
				}
			} else {
				char* hint = NULL;
				err = ejson_get_string_from_key(arg, "hint", false, false, &hint);
				if (err == EJSON_OK) {
					printf("\t%s: %s\n", arg_name, hint);
				} else if (err != EJSON_KEY_NOT_FOUND) {
					fprintf(stderr, "Server response format invalid: invalid type\n");
					ejson_cleanup((ejson_base*) ejson);
					return 1;
				}
			}
			printf("\n");
		}
	}
	ejson_cleanup((ejson_base*) ejson);
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
	}
	else if(!strcmp("apply", cmds[0])){
		return apply_layout(config, cmds[1]);
	}
	else if (!strcmp("state", cmds[0]) || !strcmp("status", cmds[0])) {
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

	char* envhost = getenv("RPCD_HOST");
	char* envport = getenv("RPCD_PORT");

	if (envhost) {
		config.host = envhost;
	}

	if (envport) {
		config.port = strtoul(envport, NULL, 10);
	}

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
