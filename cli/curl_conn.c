#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>

#include "curl_conn.h"

void curl_init_global() {
	curl_global_init(CURL_GLOBAL_ALL);
}

CURL* c_init(const char* url) {
	CURL* curl = curl_easy_init();

	if (!curl) {
		fprintf(stderr, "Cannot init curl.\n");
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	//curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
	//curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	//curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/46.0.2490.86 Safari/537.36");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	//curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
	return curl;
}

size_t write_header_callback(char* buffer, size_t size, size_t nitems, char** data) {

	size_t len = size * nitems;
	if (len > 4 && !strncmp(buffer, "HTTP", 4)) {
		buffer[len - 1] = 0;
		*data = malloc(len * sizeof(char));
		strcpy(*data, buffer);
	}

	return len;
}

int curl_perform(CURL* curl) {
	if (!curl) {
		return 1;
	}

	char* header = NULL;

	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header);

	CURLcode res = curl_easy_perform(curl);
	/* Check for errors */
	if (res != CURLE_OK) {
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		free(header);
		return 1;
	}

	int ret = 0;
	long status;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	char* old_header = header;
	if (status >= 400) {
		header = strstr(header, " ");
		if (header) {
			header = strstr(header + 1, " ");
		}

		fprintf(stderr, "curl_easy_perform() failed: %ld -%s\n", status, header);
		ret = 1;
	}
	free(old_header);

	return ret;
}

size_t write_callback(char* ptr, size_t size, size_t nmemb, struct netdata* userdata) {

	userdata->data = realloc(userdata->data, (userdata->len + size * nmemb + 1));
	if (!userdata->data) {
		fprintf(stderr, "Realloc failed.\n");
		return -1;
	}

	strncpy(userdata->data + userdata->len, ptr, size * nmemb);
	userdata->len += size * nmemb;
	userdata->data[userdata->len] = 0;

	return size * nmemb;
}

int request(const char* url, char* post_data, struct netdata* data) {

	CURL* curl = c_init(url);

	if (!curl) {
		return 1;
	}

	if (!data->data) {
		data->data = strdup("");
	}
	data->len = 0;

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
	if (post_data) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
	}

	int status = curl_perform(curl);

	curl_easy_cleanup(curl);

	return status;
}
