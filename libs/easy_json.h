#pragma once

#include <stdbool.h>
#include <stdio.h>

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


/**
 * Definition of the basic ejson struct.
 * To understand the structure we use:
 * - To access all attributes of an json object
 *   you access first the child of the json object
 *   and then iterate over the list with the next pointer
 *   while next is not NULL.
 * - Same is to access values in an json array.
 * - Attributes then have a key (if come from object) and a value.
 */
typedef struct _ejson_struct {

	/**
	 * Type of the struct. @see ejson_types.
	 */
	enum ejson_types type;
	/** if the parent of this struct is an json object
	 * this holds the key. If not it is NULL.
	 */
	char* key;
	/**
	 * This holds the child struct. It is NULL if the struct has no child.
	 */
	struct _ejson_struct* child;
	/**
	 * This holds the next struct in the list.
	 * It is NULL if the struct is the last one in the list.
	 */
	struct _ejson_struct* next;
	char* value;
} ejson_struct;

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

/**
 * Returns the struct with the given key
 * @param ejson_struct* ejson root json struct
 * @param char* key key to find
 * @param bool childs check childrens of the root item
 * @return ejson struct with the given key or null.
 */
ejson_struct* ejson_find_key(ejson_struct* ejson, char* key, bool childs);

/**
 * Gets the value as int from given struct.
 * @param ejson_struct* ejson json struct
 * @param int* i place for the returned int
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_int(ejson_struct* ejson, int* i);

enum ejson_errors ejson_get_int_from_key(ejson_struct* ejson, char* key, bool childs, int* i);

/**
 * Gets the value as int from given struct.
 * @param ejson_struct* ejson json struct
 * @param long* l place for the returned int
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_long(ejson_struct* ejson, long* l);
enum ejson_errors ejson_get_long_from_key(ejson_struct* ejson, char* key, bool childs, long* l);

/**
 * Gets the value as int from given struct.
 * @param ejson_struct* ejson json struct
 * @param int* i place for the returned int
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_double(ejson_struct* ejson, double* i);
enum ejson_errors ejson_get_double_from_key(ejson_struct* ejson, char* key, bool childs, double* i);


/**
 * Gets the value of the given struct as string.
 * @param ejson_struct* ejson json struct
 * @param char** s place for holding the string. Must not be freed.
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_string(ejson_struct* ejson, char** s);
enum ejson_errors ejson_get_string_from_key(ejson_struct* ejson, char* key, bool childs, char** s);

/**
 * Gets the value as boolean from the given struct.
 * @param ejson_struct* ejson json struct.
 * @param bool* b place for holding the boolean value.
 * @return enum ejson_errors returns EJSON_WRONG_TYPE if there is an error.
 */
enum ejson_errors ejson_get_boolean(ejson_struct* ejson, bool* b);
enum ejson_errors ejson_get_boolean_from_key(ejson_struct* ejson, char* key, bool childs, bool* b);

/**
 * Parses an json string into the given structure pointer.
 * IMPORTANT: It works direct on the given string. So it must be writable.
 * 	After the parsing you cannot parse this string again.
 * @param ejson_struct* ejson pointer for parsing the structure into.
 * @param char* string json string. MUST BE writable because this lib works directly 
 * 	on the given memory.
 * @return enum ejson_errors enum with error flags for parsing. @see enum
 */
enum ejson_errors ejson_parse(ejson_struct** ejson, char* string, size_t len);

/**
 * Same as ejson_parse but allows to write the warnings to a file or stderr.
 */
enum ejson_errors ejson_parse_warnings(ejson_struct** ejson, char* string, size_t len, bool warnings, FILE* outputstream);

/**
 * Cleanup the json structure.
 * It doesn't cleanup any key or value strings.
 * @param ejson_struct* ejson structure for cleanup.
 */
void ejson_cleanup(ejson_struct* ejson);

/**
 * For internal use.
 */
ejson_struct* ejson_identify(ejson_state* state, ejson_struct* ejson);
