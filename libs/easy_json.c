#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "easy_json.h"

ejson_base* ejson_identify(ejson_state* state);

void ejson_cleanup_array(ejson_array* root) {

	long i;

	for (i = 0; i < root->length; i++) {
		ejson_cleanup(root->values[i]);
	}
	free(root->values);
	free(root);
}

void ejson_cleanup_object(ejson_object* root) {
	long i;

	for (i = 0; i < root->length; i++) {
		ejson_cleanup(root->keys[i]->value);
		free(root->keys[i]);
	}

	free(root->keys);
	free(root);
}

void ejson_cleanup(ejson_base* ejson) {

	if (!ejson) {
		return;
	}

	switch (ejson->type) {

		case EJSON_ARRAY:
			ejson_cleanup_array((ejson_array*) ejson);
			break;
		case EJSON_OBJECT:
			ejson_cleanup_object((ejson_object*) ejson);
			break;
		default:
			free(ejson);
	}
}

ejson_base* ejson_find_by_key(ejson_object* object, char* key, int case_insensitiv, int childs);
ejson_base* ejson_find_by_key_in_array(ejson_array* arr, char* key, int case_insensitiv, int childs) {
	int i;
	ejson_base* elem = NULL;
	for (i = 0; i < arr->length; i++) {
		if (arr->values[i]->type == EJSON_OBJECT) {
			elem = ejson_find_by_key((ejson_object*) arr->values[i], key, case_insensitiv, childs);
			if (elem) {
				return elem;
			}
		}
	}

	return NULL;
}
ejson_base* ejson_find_by_key(ejson_object* object, char* key, int case_insensitiv, int childs) {
	int i;
	for (i = 0; i < object->length; i++) {
		if (case_insensitiv) {
			if (!strcasecmp(object->keys[i]->key, key)) {
				return object->keys[i]->value;
			}

		} else {
			if (!strcmp(object->keys[i]->key, key)) {
				return object->keys[i]->value;
			}
		}

		if (childs) {
			ejson_base* elem = NULL;
			switch (object->keys[i]->value->type) {
				case EJSON_OBJECT:
					elem = ejson_find_by_key((ejson_object*) object->keys[i]->value, key, case_insensitiv, childs);
					break;
				case EJSON_ARRAY:
					elem = ejson_find_by_key_in_array((ejson_array*) object->keys[i]->value, key, case_insensitiv, childs);
					break;
				default:
					break;
			}

			if (elem) {
				return elem;
			}
		}
	}

	return NULL;
}



enum ejson_errors ejson_get_int(ejson_base* root, int* i) {

	if (root->type != EJSON_INT) {
		return EJSON_WRONG_TYPE;
	}

	ejson_number* elem = (ejson_number*) root;

	(*i) = elem->value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_int_from_key(ejson_object* root, char* key, int case_insensitive, int childs, int* i) {
	ejson_base* elem = ejson_find_by_key(root, key, case_insensitive, childs);

	if (!elem) {
		return EJSON_KEY_NOT_FOUND;
	}
	return ejson_get_int(elem, i);
}


enum ejson_errors ejson_get_double(ejson_base* root, double* d) {

	if (root->type != EJSON_DOUBLE) {
		return EJSON_WRONG_TYPE;
	}

	(*d) = ((ejson_real*) root)->value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_double_from_key(ejson_object* root, char* key, int case_insensitive, int childs, double* d) {
	ejson_base* elem = ejson_find_by_key(root, key, case_insensitive, childs);

	if (!elem) {
		return EJSON_KEY_NOT_FOUND;
	}

	return ejson_get_double(elem, d);
}

enum ejson_errors ejson_get_number(ejson_base* root, double* d) {
	switch (root->type) {
		case EJSON_DOUBLE:
			(*d) = ((ejson_real*) root)->value;
			break;
		case EJSON_INT:
			(*d) = ((ejson_number*) root)->value;
			break;
		default:
			return EJSON_WRONG_TYPE;
	}

	return EJSON_OK;
}

enum ejson_errors ejson_get_number_from_key(ejson_object* root, char* key, int case_insensitive, int childs, double* d) {
	ejson_base* elem = ejson_find_by_key(root, key, case_insensitive, childs);

	if (!elem) {
		return EJSON_KEY_NOT_FOUND;
	}

	return ejson_get_number(elem, d);
}

enum ejson_errors ejson_get_string(ejson_base* root, char** s) {

	if (root->type != EJSON_STRING) {
		return EJSON_WRONG_TYPE;
	}
	ejson_string* elem = (ejson_string*) root;
	*s = elem->value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_string_from_key(ejson_object* root, char* key, int case_insensitive, int childs, char** s) {
	ejson_base* elem = ejson_find_by_key(root, key, case_insensitive, childs);

	if (!elem) {
		return EJSON_KEY_NOT_FOUND;
	}

	return ejson_get_string(elem, s);
}

enum ejson_errors ejson_get_boolean(ejson_base* root, bool* b) {
	if (root->type != EJSON_BOOLEAN) {
		return EJSON_WRONG_TYPE;
	}
	ejson_bool* elem = (ejson_bool*) root;
	(*b) = elem->value;

	return EJSON_OK;
}

enum ejson_errors ejson_get_boolean_from_key(ejson_object* root, char* key, int case_insensitive, int childs, bool* b) {
	ejson_base* elem = ejson_find_by_key(root, key, case_insensitive, childs);

	if (!elem) {
		return EJSON_KEY_NOT_FOUND;
	}

	return ejson_get_boolean(elem, b);
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
		if ((unsigned char ) state->data[state->pos] <= 0x001F) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Control characters must be escaped in strings";
			return NULL;
		}

		switch (state->data[state->pos]) {
			case '\\':
				state->pos++;
				if (state->pos >= state->len) {
					break;
				}
				switch(state->data[state->pos]) {
					case '\\':
						state->data[offset] = '\\';
						break;
					case '/':
						state->data[offset] = '/';
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
						char* end = NULL;
						u_1 = strtoul(u, &end, 16);
						if (u == end) {
							state->error = EJSON_INVALID_JSON;
							state->reason = "Invalid character in unicode escape sequence.";
							return NULL;
						}
						state->pos += 2;
						strncpy(u, state->data + state->pos, 2);
						u_2 = strtoul(u, &end, 16);
						if (u == end) {
							state->error = EJSON_INVALID_JSON;
							state->reason = "Invalid character in unicode escape sequence.";
							return NULL;
						}

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

ejson_key* ejson_parse_key(ejson_state* state) {
	char* key = ejson_parse_get_string(state);

	if (!key) {
		return NULL;
	}

	if (ejson_trim(state) == 0) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Unexpected end of input.\n";
		return NULL;
	}

	if (state->data[state->pos] != ':') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Missing key.";
		return NULL;
	}

	state->data[state->pos] = 0;
	state->pos++;

	ejson_key* elem = calloc(1, sizeof(ejson_key));
	elem->key = key;
	elem->value = ejson_identify(state);

	if (!elem->value) {
		free(elem);
		return NULL;
	}

	return elem;
}

ejson_string* ejson_parse_string(ejson_state* state) {

	char* s = ejson_parse_get_string(state);

	if (!s) {
		return NULL;
	}

	ejson_string* elem = calloc(1, sizeof(ejson_string));
	elem->base.type = EJSON_STRING;
	elem->value = s;

	return elem;
}

ejson_array* ejson_parse_array(ejson_state* state) {

	// skip [
	state->pos++;

	ejson_array* elem = calloc(1, sizeof(ejson_array));
	elem->base.type = EJSON_ARRAY;
	elem->length = 0;

	ejson_base* lastChild = NULL;

	bool last_comma = false;

	// build values
	while (state->pos < state->len && state->data[state->pos] != ']') {
		lastChild = ejson_identify(state);
		if (state->error != EJSON_OK || !lastChild) {
			ejson_cleanup_array(elem);
			return NULL;
		}


		elem->length++;
		elem->values = realloc(elem->values, elem->length * sizeof(ejson_base*));
		elem->values[elem->length - 1] = lastChild;

		if (ejson_trim(state) == 0) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Missing trailing array bracket(]).";
			ejson_cleanup_array(elem);
			return NULL;
		}
		last_comma = false;
		switch (state->data[state->pos]) {
			case ',':
				state->data[state->pos] = 0;
				state->pos++;
				last_comma = true;
				break;
			case ']':
				break;
			default:
				state->error = EJSON_INVALID_JSON;
				state->reason = "Cannot parse this char in array parsing";
				ejson_cleanup_array(elem);
				return NULL;
		}
	}

	if (last_comma) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Comma as last character in an array is not allowed.";
		ejson_cleanup_array(elem);
		return NULL;
	}

	if (state->pos >= state->len || state->data[state->pos] != ']') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find trailing ].";
		ejson_cleanup_array(elem);
		return NULL;
	}

	state->data[state->pos] = 0;
	state->pos++;

	return elem;
}

ejson_bool* ejson_parse_bool(ejson_state* state) {
	ejson_bool* elem = calloc(1, sizeof(ejson_bool));
	elem->base.type = EJSON_BOOLEAN;

	if (ejson_trim(state) == 0) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Unexpected end of input.";
		free(elem);
		return NULL;
	}
	printf("pos: %ld, len: %ld\n", state->pos, state->len);
	if (state->pos + 3 < state->len && !strncmp(state->data + state->pos, "true", 4)) {
		elem->value = 1;
		state->pos += 4;
	} else if (state->pos + 4 < state->len && !strncmp(state->data + state->pos, "false", 5)) {
		elem->value = 0;
		state->pos += 5;
	} else {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot parse boolean.";
		free(elem);
		return NULL;
	}

	return elem;
}

ejson_null* ejson_parse_null(ejson_state* state) {


	if (state->pos + 4 < state->len && strncmp(state->data + state->pos, "null", 4)) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot parse null.";
		return NULL;
	}

	ejson_null* elem = calloc(1, sizeof(ejson_null));
	elem->base.type = EJSON_NULL;
	state->pos += 4;
	return elem;
}

ejson_base* ejson_parse_number(ejson_state* state) {
	size_t offset = 0;
	ejson_base* root;
	// check sign
	if (state->data[state->pos] == '-') {
		offset++;
	}

	if (state->data[state->pos] == '+') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "The plus character in front of a number is not allowed.\n";
		return NULL;
	}

	if (state->pos + offset >= state->len) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Invalid end of stream.";
		return NULL;
	}

	if (state->data[state->pos + offset] == '0') {
		if (state->pos + offset + 1 >= state->len) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Invalid end of stream.";
			return NULL;
		} else if (isdigit(state->data[state->pos + offset + 1])) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Invalid number.";
			return NULL;
		}
	}

	size_t len;
	for (len = 0; state->pos + len < state->len; len++) {
		char c = state->data[state->pos + len];
		if (!isdigit(c)
				&& tolower(c) != 'e'
				&& c != '+'
				&& c != '-'
				&& c != '.') {
			break;
		}
	}

	if (!isdigit(state->data[state->pos + len - 1])){
		state->error = EJSON_INVALID_JSON;
		state->reason = "Illegal end of number.";
		return NULL;
	}

	char* number = strndup(state->data + state->pos, state->pos + len);

	char* end = "";
	long num = strtol(number, &end, 10);
	if (number == end) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Not a valid number.";
		free(number);
		return NULL;
	}
	if (*end == '.' || tolower(*end) == 'e') {
		ejson_real* elem = calloc(1, sizeof(ejson_real));
		elem->base.type = EJSON_DOUBLE;
		end = "";
		elem->value = strtod(number, &end);

		if (number == end) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Not a valid number.";
			free(elem);
			free(number);
			return NULL;
		}

		root = (ejson_base*) elem;
	} else {
		ejson_number* elem = calloc(1, sizeof(ejson_number));
		elem->base.type = EJSON_INT;
		elem->value = num;
		root = (ejson_base*) elem;
	}

	state->pos = state->pos + (end - number);
	free(number);
	printf("pos: %ld, len: %ld\n", state->pos, state->len);
	return root;
}

ejson_object* ejson_parse_object(ejson_state* state) {

	ejson_object* ejson = NULL;

	if (ejson_trim(state) == 0) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find leading {.";
		return NULL;
	}

	if (state->data[state->pos] != '{') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find leading {.";
		return NULL;
	}
	state->data[state->pos] = 0;
	state->pos++;

	ejson = calloc(1, sizeof(ejson_object));

	ejson->base.type = EJSON_OBJECT;
	ejson->length = 0;

	ejson_key* key;
	int last_comma = false;
	// while there is something
	while (state->pos < state->len && state->data[state->pos] != '}') {
		key = ejson_parse_key(state);
		// check for error
		if (state->error != EJSON_OK || !key) {
			ejson_cleanup_object(ejson);
			return NULL;
		}

		ejson->length++;
		ejson->keys = realloc(ejson->keys, ejson->length * sizeof(ejson_key*));

		ejson->keys[ejson->length - 1] = key;

		if (ejson_trim(state) == 0) {
			state->error = EJSON_INVALID_JSON;
			state->reason = "Unexpected end of input.";
			ejson_cleanup_object(ejson);
			return NULL;
		}

		last_comma = false;
		// validate elements
		switch(state->data[state->pos]) {
			case ',':
				state->data[state->pos] = 0;
				state->pos++;
				last_comma = true;
				break;
			case '}':
				break;
			default:
				state->error = EJSON_INVALID_JSON;
				state->reason = "Invalid char at this position.";
				ejson_cleanup_object(ejson);
				return NULL;
		}
	}

	if (last_comma) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "A trailing comma in an object is not allowed.";
		ejson_cleanup_object(ejson);
		return NULL;
	}

	if (state->data[state->pos] != '}') {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Cannot find trailing }.";
		ejson_cleanup_object(ejson);
		return NULL;
	}

	state->data[state->pos] = 0;
	state->pos++;

	return ejson;
}

ejson_base* ejson_identify(ejson_state* state) {

	state->counter++;

	if (state->counter > MAX_JSON_DEPTH) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Too many levels of depth.";
		return NULL;
	}

	if (ejson_trim(state) == 0) {
		state->error = EJSON_INVALID_JSON;
		state->reason = "Unexcepted end of input.";
		return NULL;
	}

	switch(state->data[state->pos]) {
		case '"':
			return (ejson_base*) ejson_parse_string(state);
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
		case '+':
			//printf("parse number with starting: %.5s\n", state->pos);
			return ejson_parse_number(state);
			break;
		case 't':
		case 'T':
		case 'F':
		case 'f':
			return (ejson_base*) ejson_parse_bool(state);
			break;
		case '{':
			return (ejson_base*) ejson_parse_object(state);
		case '[':
			return (ejson_base*) ejson_parse_array(state);
			break;
		case 'n':
		case 'N':
			return (ejson_base*) ejson_parse_null(state);
			break;
		default:
			state->error = EJSON_INVALID_JSON;
			state->reason = "Cannot identify next token. Unkown identifier";
			return NULL;
	}
}

enum ejson_errors ejson_parse(char* string, size_t len, ejson_base** root) {
	return ejson_parse_warnings(string, len, false, stderr, root);
}

enum ejson_errors ejson_parse_warnings(char* string, size_t len, bool warnings, FILE* log, ejson_base** root) {
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

	*root = ejson_identify(&state);

	if (state.error == EJSON_OK) {
		if (ejson_trim(&state) > 0) {
			state.error = EJSON_INVALID_JSON;
			state.reason = "There are characters left.";
			ejson_cleanup(*root);
			*root = NULL;
		}
	}

	if (state.error != EJSON_OK && state.warnings) {
		if (state.data[state.pos] == 0) {
			fprintf(state.log, "Error: %s (<null>).\n", state.reason);
		} else {
			fprintf(state.log, "Error: %s (%c).\n", state.reason, state.data[state.pos]);
		}
	}

	return state.error;
}
