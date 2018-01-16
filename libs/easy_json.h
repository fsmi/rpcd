#pragma once

#include <stdbool.h>
#include <stdio.h>

#define MAX_JSON_DEPTH 1000

/**
 * Enum for eror codes.
 */
enum ejson_errors {
	/** Everything is ok */
	EJSON_OK = 0,
	/** The given json is not valid json. */
	EJSON_INVALID_JSON = 1,
	/** You try to access a value with the wrong type. */
	EJSON_WRONG_TYPE = 2,
	/** Cannot find key. */
	EJSON_KEY_NOT_FOUND = 3
};
/**
 * Enum for json types. Number is split in double and int.
 */
enum ejson_types {
	/** Int type. Part of the number json type */
	EJSON_INT = 10,
	/** Double type. Part of the number json type */
	EJSON_DOUBLE = 11,
	/** A json object */
	EJSON_OBJECT = 12,
	/** A json string. */
	EJSON_STRING = 13,
	/** A boolean value. */
	EJSON_BOOLEAN = 14,
	/** An json array. */
	EJSON_ARRAY = 15,
	/** Json null value */
	EJSON_NULL = 16
};

typedef struct {
	enum ejson_types type;
} ejson_base;

typedef struct {
	char* key;
	ejson_base* value;
} ejson_key;

typedef struct {
	ejson_base base;
	long length;
	ejson_key** keys;
} ejson_object;

typedef struct {
	ejson_base base;
	long length;
	ejson_base** values;
} ejson_array;

typedef struct {
	ejson_base base;
	char* value;
} ejson_string;

typedef struct {
	ejson_base base;
} ejson_null;

typedef struct {
	ejson_base base;
	long value;
} ejson_number;

typedef struct {
	ejson_base base;
	double value;
} ejson_real;

typedef struct {
	ejson_base base;
	int value;
} ejson_bool;


/**
 * Internal json parser structure.
 */
typedef struct {
	enum ejson_errors error;
	char* reason;
	char* data;
	size_t pos;
	size_t len;
	long counter;
	bool warnings;
	FILE* log;
} ejson_state;

ejson_base* ejson_find_by_key(ejson_object* root, char* key, int case_insensitive, int childs);

/**
 * Gets the value as int from given struct.
 * @param ejson_base* root element
 * @param int* i place for the returned int
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_int(ejson_base* root, int* i);

/**
 * Gets the value of the given key in an object as int.
 * @param ejson_object* root element
 * @param char* key key to search for.
 * @param int case_insensitive Search for key case insentitive.
 * @param int childs Search the childs, too.
 * @param char** i place for the returned int
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_int_from_key(ejson_object* root, char* key, int case_insensitive, int childs, int* i);

/**
 * Gets the value as int from given struct.
 * @param ejson_base* root element
 * @param int* i place for the returned int
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_double(ejson_base* root, double* d);

/**
 * Gets the value of the given key in an object as double.
 * @param ejson_object* root element
 * @param char* key key to search for.
 * @param int case_insensitive Search for key case insentitive.
 * @param int childs Search the childs, too.
 * @param char** i place for the returned double
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_double_from_key(ejson_object* root, char* key, int case_insensitive, int childs, double* d);

/**
 * Gets the value as double from the given struct. It also returns the number if the type is int.
 * @param ejson_base* root element
 * @param double* i place for the returned double
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_number(ejson_base* root, double* d);

/**
 * Gets the value of the given key in an object as double.
 * Returns also the number if type is int.
 * @param ejson_object* root element
 * @param char* key key to search for.
 * @param int case_insensitive Search for key case insentitive.
 * @param int childs Search the childs, too.
 * @param double* i place for the returned double
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_number_from_key(ejson_object* root, char* key, int case_insensitive, int childs, double* d);

/**
 * Gets the value of the given struct as string.
 * @param ejson_root* root element
 * @param char** s place for holding the string. Must not be freed.
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_string(ejson_base* root, char** s);

/**
 * Gets the value of the given key in an object as string.
 * @param ejson_object* root element
 * @param char* key key to search for.
 * @param int case_insensitive Search for key case insentitive.
 * @param int childs Search the childs, too.
 * @param char** i place for the returned string
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_string_from_key(ejson_object* root, char* key, int case_insensitive, int childs, char** i);

/**
 * Gets the value as boolean from the given struct.
 * @param ejson_struct* ejson json struct.
 * @param bool* b place for holding the boolean value.
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_boolean(ejson_base* root, bool* b);

/**
 * Gets the value of the given key in an object as bool.
 * @param ejson_object* root element
 * @param char* key key to search for.
 * @param int case_insensitive Search for key case insentitive.
 * @param int childs Search the childs, too.
 * @param char** i place for the returned bool
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_boolean_from_key(ejson_object* root, char* key, int case_insensitive, int childs, bool* i);

/**
 * Parses an json string into the given structure pointer.
 * IMPORTANT: It works direct on the given string. So it must be writable.
 * 	After the parsing you cannot parse this string again.
 * @param ejson_base** element to hold the parsed data.
 * @param char* string json string. MUST BE writable because this lib works directly
 * 	on the given memory.
 * @return enum ejson_errors enum with error flags for parsing. @see enum
 */
enum ejson_errors ejson_parse(char* string, size_t len, ejson_base** root);

/**
 * Same as ejson_parse but allows to write the warnings to a file or stderr.
 */
enum ejson_errors ejson_parse_warnings(char* string, size_t len, bool warnings, FILE* outputstream, ejson_base** root);

/**
 * Cleanup the json structure.
 * It doesn't cleanup any key or value strings.
 * @param ejson_struct* ejson structure for cleanup.
 */
void ejson_cleanup(ejson_base* root);
