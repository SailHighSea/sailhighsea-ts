#ifndef SHS_HTTP_H
#define SHS_HTTP_H

#include <stdbool.h>
#include <stddef.h>

/* Blocking HTTPS/HTTP GET (run it on a worker thread). Returns a malloc'd, NUL-terminated body (caller frees) or NULL on
   a network error, a non-200 status or a body larger than `maxBytes`. Only plain URL characters are accepted
   (macOS / Linux hand the address to curl), so build URLs with ShsUrlEncode(). */
char *ShsHttpGet(const char *url, size_t maxBytes, size_t *outLen);
/* Same for a POST with a JSON body (Linux / macOS keep the body in a temp file for curl). */
char *ShsHttpPostJson(const char *url, const char *json, size_t maxBytes, size_t *outLen);

/* Percent-encodes `s` for a query string (spaces become %20). */
void ShsUrlEncode(const char *s, char *out, size_t cap);

#endif /* SHS_HTTP_H */
