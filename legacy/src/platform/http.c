#include "http.h"

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#ifndef _WIN32
  #include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>

#define HTTP_AGENT "SailHighSea-TorrentSearch"

void ShsUrlEncode(const char *s, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *s && n + 4 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out[n++] = (char)c;
        else { out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15]; }
    }
    out[n] = 0;
}

static bool UrlPlain(const char *url)
{
    if (strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) return false;
    for (const char *p = url; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(":/?&=.%_-~+,", c))) return false;
    }
    return true;
}

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winhttp.h>

static char *HttpDo(const char *post, const char *url, size_t maxBytes, size_t *outLen)
{
    if (outLen) *outLen = 0;
    if (!UrlPlain(url)) return NULL;
    wchar_t wurl[1100];
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1100)) return NULL;

    wchar_t host[256], path[1024];
    URL_COMPONENTS uc = { 0 };
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;  uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 1024;
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return NULL;
    /* the query string ("?q=...") is returned as extra info; the path buffer holds only the path */
    wchar_t full[1100];
    swprintf(full, 1100, L"%.*ls%.*ls", (int)uc.dwUrlPathLength, path, (int)uc.dwExtraInfoLength, uc.lpszExtraInfo ? uc.lpszExtraInfo : L"");

    char *body = NULL;
    HINTERNET ses = WinHttpOpen(L"" HTTP_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!ses) return NULL;
    WinHttpSetTimeouts(ses, 8000, 8000, 10000, 12000);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    const wchar_t *wmethod = post ? L"POST" : L"GET";
    HINTERNET req = con ? WinHttpOpenRequest(con, wmethod, full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    const DWORD plen = post ? (DWORD)strlen(post) : 0;
    if (req && WinHttpSendRequest(req, post ? L"Content-Type: application/json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS, post ? (DWORD)-1L : 0,
                                   post ? (LPVOID)post : WINHTTP_NO_REQUEST_DATA, plen, plen, 0)
            && WinHttpReceiveResponse(req, NULL)) {
        DWORD status = 0, sz = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            size_t cap = 64 * 1024, len = 0;
            body = (char *)malloc(cap + 1);
            DWORD got = 0;
            bool ok = body != NULL;
            while (ok) {
                if (len == cap) {
                    if (cap >= maxBytes) { ok = false; break; }
                    cap *= 2;
                    char *nb = (char *)realloc(body, cap + 1);
                    if (!nb) { ok = false; break; }
                    body = nb;
                }
                if (!WinHttpReadData(req, body + len, (DWORD)(cap - len), &got)) { ok = false; break; }
                if (!got) break;
                len += got;
            }
            if (!ok) { free(body); body = NULL; }
            else { body[len] = 0; if (outLen) *outLen = len; }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return body;
}

char *ShsHttpGet(const char *url, size_t maxBytes, size_t *outLen) { return HttpDo(NULL, url, maxBytes, outLen); }
char *ShsHttpPostJson(const char *url, const char *json, size_t maxBytes, size_t *outLen) { return HttpDo(json, url, maxBytes, outLen); }

#else

/* No HTTP library is linked on macOS / Linux: curl ships with both. */
static char *HttpDo(const char *post, const char *url, size_t maxBytes, size_t *outLen)
{
    if (outLen) *outLen = 0;
    if (!UrlPlain(url) || strlen(url) > 1000) return NULL;
    char cmd[1300], tmp[40] = "";
    if (post) {                                          /* the body goes through a private temp file, never through the shell */
        snprintf(tmp, sizeof tmp, "/tmp/shs-post-XXXXXX");
        int fd = mkstemp(tmp);
        if (fd < 0) return NULL;
        FILE *tf = fdopen(fd, "wb");
        if (!tf) { close(fd); unlink(tmp); return NULL; }
        fputs(post, tf);
        fclose(tf);
        snprintf(cmd, sizeof cmd, "curl -fsSL --max-time 15 -A " HTTP_AGENT " -H \"Content-Type: application/json\" --data-binary @%s \"%s\" 2>/dev/null", tmp, url);
    } else snprintf(cmd, sizeof cmd, "curl -fsSL --max-time 15 -A " HTTP_AGENT " \"%s\" 2>/dev/null", url);
    FILE *p = popen(cmd, "r");
    if (!p) { if (tmp[0]) unlink(tmp); return NULL; }
    size_t cap = 64 * 1024, len = 0;
    char *body = (char *)malloc(cap + 1);
    while (body) {
        if (len == cap) {
            if (cap >= maxBytes) { free(body); body = NULL; break; }
            cap *= 2;
            char *nb = (char *)realloc(body, cap + 1);
            if (!nb) { free(body); body = NULL; break; }
            body = nb;
        }
        size_t n = fread(body + len, 1, cap - len, p);
        if (n == 0) break;
        len += n;
    }
    if (pclose(p) != 0 && body) { free(body); body = NULL; }
    if (tmp[0]) unlink(tmp);
    if (body) { body[len] = 0; if (outLen) *outLen = len; }
    return body;
}
#endif

#ifndef _WIN32
char *ShsHttpGet(const char *url, size_t maxBytes, size_t *outLen) { return HttpDo(NULL, url, maxBytes, outLen); }
char *ShsHttpPostJson(const char *url, const char *json, size_t maxBytes, size_t *outLen) { return HttpDo(json, url, maxBytes, outLen); }
#endif
