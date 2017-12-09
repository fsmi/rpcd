#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "easy_json.h"

ejson_struct* ejson_init_struct() {
	ejson_struct* ejson = malloc(sizeof(ejson_struct));
	ejson->key = NULL;
	ejson->value = NULL;
	ejson->type = -1;
	ejson->child = NULL;
	ejson->next = NULL;

	return ejson;
}

void ejson_cleanup(ejson_struct* ejson) {

	if (!ejson) {
		return;
	}

	ejson_cleanup(ejson->child);
	ejson_cleanup(ejson->next);
	free(ejson);
}


ejson_struct* ejson_find_key(ejson_struct* ejson, char* key, bool childs) {

	ejson_struct* ejson_child;
	while (ejson) {

		if (ejson->key && !strcmp(ejson->key, key)) {
			return ejson;
		}

		if (childs && ejson->child) {
			ejson_child = ejson_find_key(ejson->child, key, childs);

			if (ejson_child) {
				return ejson_child;
			}
		}

		ejson = ejson->next;
	}

	return NULL;
}
enum ejson_errors ejson_get_long(ejson_struct* ejson, long* l) {


	if (ejson->type != EJSON_INT) {
		return EJSON_WRONG_TYPE;
	}

	int value = strtol(ejson->value, NULL, 10);

	(*l) = value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_int(ejson_struct* ejson, int* i) {


	if (ejson->type != EJSON_INT) {
		return EJSON_WRONG_TYPE;
	}

	int value = strtol(ejson->value, NULL, 10);

	(*i) = value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_double(ejson_struct* ejson, double* i) {


	if (ejson->type != EJSON_DOUBLE) {
		return EJSON_WRONG_TYPE;
	}

	double value = strtod(ejson->value, NULL);

	(*i) = value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_string(ejson_struct* ejson, char** s) {

	if (ejson->type != EJSON_STRING) {
		return EJSON_WRONG_TYPE;
	}

	*s = ejson->value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_boolean(ejson_struct* ejson, bool* b) {
	if (ejson->type != EJSON_BOOLEAN) {
		return EJSON_WRONG_TYPE;
	}

	switch (*ejson->value) {
		case 't':
			(*b) = true;
			break;
		case 'f':
			(*b) = false;
			break;
		default:
			return EJSON_INVALID_JSON;
	}

	return EJSON_OK;
}

int ejson_check_float(char* s) {

	if (*s == '-' || *s == '+') {
		s++;
	}

	while (*s != 0) {
		if (isdigit(*s)) {
			s++;
		} else {


			switch(*s) {
				case '.':
					return EJSON_DOUBLE;
				case ',':
				case '}':
				case ']':
				case ' ':
					return EJSON_INT;
				default:
					return EJSON_WRONG_TYPE;
			}
		}
	}

	return EJSON_OK;
}

size_t ejson_trim(ejson_state* state) {
	while (state->pos < state->len && isspace(state->data[state->pos])) {
		state->pos++;
	}

	return state->len - state->pos;
}

char* ejson_parse_get_string(ejson_state* state) {

	if (ejson_trim(state) == 0 || state->data[state->pos] != '"') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find leading \".";
		return NULL;
	}

	state->pos++;
	char* s = state->data + state->pos;
	unsigned offset = state->pos;
	char u[3];
	memset(u, 0, 3);
	unsigned u_1;
	unsigned u_2;

	while (state->pos < state->len) {

		switch (state->data[state->pos]) {
			case '\\':
				state->pos++;
				if (state->pos < state->len) {
					break;
				}
				switch(state->data[state->pos]) {
					case '\\':
						state->data[offset] = '\\';
						break;
					case '"':
						state->data[offset] = '"';
						break;
					case 'b':
						state->data[offset] = '\b';
						break;
					case 'f':
						state->data[offset] = '\f';
						break;
					case 'n':
						state->data[offset] = '\n';
						break;
					case 'r':
						state->data[offset] = '\r';
						break;
					case 't':
						state->data[offset] = '\t';
						break;
					case 'u':
						state->pos++;
						if (state->pos + 3 >= state->len) {
							state->error = EJSON_INVALID_JSON;
							state->reason = "Broken unicode.";
							return NULL;
						}
						//printf("Unicode: %.4s\n", string + curr);

						strncpy(u, state->data + state->pos, 2);
						u_1 = strtoul(u, NULL, 16);
						state->pos += 2;
						strncpy(u, state->data + state->pos, 2);
						u_2 = strtoul(u, NULL, 16);
						if (u_1 == 0x00 && u_2 <= 0x7F) {
							state->data[offset] = u_2;
						} else if (u_1 <= 0x07 && u_2 >= 0x80) {
							state->data[offset] = 0xC0;
							state->data[offset] |= (u_1 & 0x07) << 2 | (0xC0 & u_2) >> 6;
							offset++;
							state->data[offset] = 0x80;
							state->data[offset] |= u_2 & 0x3F;
						} else if (u_1 >= 0x80) {
							state->data[offset] = 0xE0;
							state->data[offset] |= (u_1 & 0xF0) >> 4;
							offset++;
							state->data[offset] = 0x80;
							state->data[offset] |= (u_1 & 0x0F) << 2 | (u_2 & 0xC0) >> 6;
							offset++;
							state->data[offset] = 0x80;
							state->data[offset] |= u_2 & 0x3F;
						}
						state->pos++;
						break;
					default:
						state->error = EJSON_INVALID_JSON;
						state->reason = "Unkown escape character.";
						return NULL;
				}

				break;
			case '"':
				state->data[state->pos] = 0;
				state->data[offset] = 0;
				state->pos++;
				return s;
			default:
				state->data[offset] = state->data[state->pos];
				break;
		}
		offset++;
		state->pos++;
	}

	state->error = EJSON_INVALID_JSON;
	state->reason = "Cannot find trailing \".";

	return NULL;
}

ejson_struct* ejson_parse_string(ejson_state* state, ejson_struct* origin) {
	char* s = ejson_parse_get_string(state);
	if (state->error != EJSON_OK) {
		ejson_cleanup(origin);
		return NULL;
	}

	if (ejson_trim(state) == 0) {
		ejson_cleanup(origin);
		return NULL;
	}
	char* key = NULL;

	if (state->data[state->pos] == ':') {
		state->data[state->pos] = 0;
		state->pos++;
		if (origin) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Cannot define more than one key.";
			ejson_cleanup(origin);
			return NULL;
		}

		key = s;
	}

	if (!origin) {
		origin = ejson_init_struct();
		origin->key = key;
	}

	if (key) {
		origin = ejson_identify(state, origin);
	} else {
		origin->type = EJSON_STRING;
		origin->value = s;
	}

	if (state->error != EJSON_OK) {
		ejson_cleanup(origin);
		return NULL;
	}

	return origin;
}

ejson_struct* ejson_parse_array(ejson_state* state, ejson_struct* origin) {

	// check if is a array
	if (ejson_trim(state) == 0 || state->data[state->pos] != '[') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find leading [.";
		ejson_cleanup(origin);
		return NULL;
	}

	// skip [
	state->pos++;

	// create struct if not exists
	if (!origin) {
		origin = ejson_init_struct();
	}

	origin->type = EJSON_ARRAY;

	ejson_struct* ejson_in_array = NULL;
	ejson_struct* lastChild = NULL;

	// build values
	while (state->pos < state->len && state->data[state->pos] != ']') {
		ejson_in_array = ejson_identify(state, NULL);

		if (state->error != EJSON_OK) {
			ejson_cleanup(origin);
			return NULL;
		}

		// save in structure
		if (!lastChild) {
			lastChild = ejson_in_array;
			origin->child = lastChild;
		} else {
			lastChild->next = ejson_in_array;
			lastChild = lastChild->next;
		}

		if (ejson_in_array->key) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "No key allowed in json array.";
			ejson_cleanup(origin);
			return NULL;
		}

		ejson_in_array = NULL;

		if(ejson_trim(state) == 0) {
			ejson_cleanup(origin);
			return NULL;
		}

		switch (state->data[state->pos]) {
			case ',':
				state->data[state->pos] = 0;
				state->pos++;
				if (state->pos >= state->len || state->data[state->pos] == ']') {
					state->error = EJSON_INVALID_JSON;
					state->reason = "Trailing comma is not allowed in array.";
					ejson_cleanup(origin);
					return NULL;
				}
				break;
			case ']':
				break;
			default:
				state->error = EJSON_INVALID_JSON;
				state->reason = "Cannot parse this char in array parsing";
				ejson_cleanup(origin);
				return NULL;
		}
	}

	if (state->data[state->pos] != ']') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find trailing [.";
		ejson_cleanup(origin);
		return NULL;
	}

	state->data[state->pos] = 0;
	state->pos++;
	return origin;
}

ejson_struct* ejson_parse_bool(ejson_state* state, ejson_struct* origin) {
	if (!origin) {
		origin = ejson_init_struct();
	}
	origin->type = EJSON_BOOLEAN;

	ejson_trim(state);

	if (state->pos + 4 < state->len && !strncmp(state->data + state->pos, "true", 4)) {
		origin->value = "true";
		state->pos += 4;
	} else if (state->pos + 5 < state->len && !strncmp(state->data + state->pos, "false", 5)) {
		origin->value = "false";
		state->pos += 5;
	} else {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot parse boolean.";
		ejson_cleanup(origin);
		return NULL;
	}
	return origin;
}

ejson_struct* ejson_parse_null(ejson_state* state, ejson_struct* origin) {
	if (!origin) {
		origin = ejson_init_struct();
	}

	if (state->pos + 4 < state->len && !strncmp(state->data + state->pos, "null", 4)) {
		origin->type = EJSON_NULL;
		origin->value = NULL;
		state->pos += 4;
	} else {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot parse null.";
		ejson_cleanup(origin);
		return NULL;
	}
	return origin;
}

ejson_struct* ejson_parse_number(ejson_state* state, ejson_struct* origin) {
	size_t offset = 0;
	if (state->data[state->pos] == '-') {
		offset++;
	}

	if (state->pos + offset >= state->len) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Invalid end of stream.";
		ejson_cleanup(origin);
		return NULL;
	}

	// check for leading zeros
	if (state->data[state->pos + offset] == '0') {
		if (state->pos + offset + 1 >= state->len) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Invalid end of stream.";
			return NULL;
		} else if (isdigit(state->data[state->pos + offset + 1])) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "invalid number.";
			ejson_cleanup(origin);
			return NULL;
		}
	}

	if (!origin) {
		origin = ejson_init_struct();
	}

	origin->value = state->data + state->pos;

	char* number = strndup(state->data + state->pos, state->len - state->pos);
	char* end = "";
	strtol(number, &end, 10);
	if (*end == '.') {
		origin->type = EJSON_DOUBLE;
		strtod(number, &end);
	} else {
		origin->type = EJSON_INT;
	}
	state->pos = state->pos + (end - number);

	if (number == end) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot parse number.";
		ejson_cleanup(origin);
		origin = NULL;
	}
	free(number);
	return origin;
}

ejson_struct* ejson_parse_object(ejson_state* state, ejson_struct* origin) {

	ejson_struct* ejson = origin;
	ejson_trim(state);

	if (state->pos >= state->len || state->data[state->pos] != '{') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find leading {.";
		ejson_cleanup(ejson);
		return NULL;
	}
	state->data[state->pos] = 0;
	state->pos++;

	if (!ejson) {
		ejson = ejson_init_struct();
	}

	ejson->type = EJSON_OBJECT;

	ejson_struct* ejson_in_object = NULL;
	ejson_struct* lastChild = NULL;

	// while there is something
	while (state->pos < state->len && state->data[state->pos] != '}') {
		ejson_in_object = ejson_identify(state, NULL);

		// check for error
		if (state->error != EJSON_OK) {
			ejson_cleanup(ejson);
			return NULL;
		}

		// validate key
		if (!ejson_in_object->key) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Element has no key in object.";
			ejson_cleanup(ejson_in_object);
			ejson_cleanup(ejson);
			return NULL;
		}

		if (!lastChild) {
			lastChild = ejson_in_object;
			ejson->child = lastChild;
		} else {
			lastChild->next = ejson_in_object;
			lastChild = lastChild->next;
		}
		ejson_in_object = NULL;

		if(ejson_trim(state) == 0) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Invalid end of stream.";
			ejson_cleanup(ejson);
			return NULL;
		}

		// validate elements
		switch(state->data[state->pos]) {
			case ',':
				state->data[state->pos] = 0;
				state->pos++;
				break;
			case '}':
				break;
			default:
				state->error = EJSON_INVALID_JSON;
				state->reason = "Invalid char at this position.";
				ejson_cleanup(ejson);
				return NULL;
		}
	}

	if (state->pos >= state->len || state->data[state->pos] != '}') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find trailing }.";
		ejson_cleanup(ejson);
		return NULL;
	}

	state->data[state->pos] = 0;
	state->pos++;
	return ejson;
}

ejson_struct* ejson_identify(ejson_state* state, ejson_struct* origin) {

	state->counter++;

	if (state->counter > 1000) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Too many objects (Max size is 1000).";
		ejson_cleanup(origin);
		return NULL;
	}

	if (ejson_trim(state) == 0) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Invalid end of stream.";
		ejson_cleanup(origin);
		return NULL;
	}
	switch(state->data[state->pos]) {
		case '"':
			origin = ejson_parse_string(state, origin);
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '-':
			//printf("parse number with starting: %.5s\n", state->pos);
			origin = ejson_parse_number(state, origin);
			break;
		case 't':
		case 'T':
		case 'F':
		case 'f':
			origin = ejson_parse_bool(state, origin);
			break;
		case '{':
			origin = ejson_parse_object(state, origin);
			break;
		case '[':
			origin = ejson_parse_array(state, origin);
			break;
		case 'n':
		case 'N':
			origin = ejson_parse_null(state, origin);
			break;
		default:
			state->error = EJSON_INVALID_JSON;
			state->reason = "Cannot identify next token. Unkown identifier";
			ejson_cleanup(origin);
			return NULL;
	}

	state->counter--;
	return origin;
}

enum ejson_errors ejson_parse(ejson_struct** ejson, char* string, size_t len) {

	return ejson_parse_warnings(ejson, string, len, false, stderr);
}

enum ejson_errors ejson_parse_warnings(ejson_struct** ejson, char* string, size_t len, bool warnings, FILE* log) {
	ejson_state state = {
		.error = EJSON_OK,
		.reason = "",
		.counter = 0l,
		.data = string,
		.pos = 0,
		.len = len,
		.warnings = warnings,
		.log = log
	};

	if (!state.log) {
		state.log = stderr;
	}

	*ejson = ejson_identify(&state, *ejson);

	if (state.error == EJSON_OK) {

		if (ejson_trim(&state) > 0) {
			state.error = EJSON_INVALID_JSON;
			state.reason = "There are characters after the structure.";
		}
	}

	if (state.error != EJSON_OK) {
		ejson_cleanup(*ejson);
		*ejson = NULL;
	}
	if (state.error != EJSON_OK && state.warnings) {

		fprintf(state.log, "Error: %s (%zd: %c).\n", state.reason, state.pos, state.data[state.pos]);
	}

	return state.error;
}
