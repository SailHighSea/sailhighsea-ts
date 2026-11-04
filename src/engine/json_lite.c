#include "json_lite.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 24

typedef struct { const char *p; } Cur;

static void Skip(Cur *c)
{
    while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r') c->p++;
}

static ShsJson *NewNode(ShsJsonType t)
{
    ShsJson *n = (ShsJson *)calloc(1, sizeof *n);
    if (n) n->type = t;
    return n;
}

static void PutUtf8(char *out, size_t *n, unsigned cp)
{
    if (cp < 0x80) out[(*n)++] = (char)cp;
    else if (cp < 0x800) { out[(*n)++] = (char)(0xC0 | (cp >> 6)); out[(*n)++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { out[(*n)++] = (char)(0xE0 | (cp >> 12)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 63)); out[(*n)++] = (char)(0x80 | (cp & 63)); }
    else { out[(*n)++] = (char)(0xF0 | (cp >> 18)); out[(*n)++] = (char)(0x80 | ((cp >> 12) & 63)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 63)); out[(*n)++] = (char)(0x80 | (cp & 63)); }
}

static int Hex4(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

/* Parses a quoted string at c->p (which points at the opening quote). Returns a malloc'd, unescaped copy. */
static char *ParseString(Cur *c)
{
    if (*c->p != '"') return NULL;
    c->p++;
    const char *s = c->p;
    while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; }
    if (*s != '"') return NULL;
    char *out = (char *)malloc((size_t)(s - c->p) + 1);       /* unescaping never grows the text */
    if (!out) return NULL;
    size_t n = 0;
    while (c->p < s) {
        char ch = *c->p++;
        if (ch != '\\') { out[n++] = ch; continue; }
        char e = *c->p++;
        switch (e) {
            case 'n': out[n++] = '\n'; break;
            case 't': out[n++] = '\t'; break;
            case 'r': out[n++] = '\r'; break;
            case 'b': out[n++] = '\b'; break;
            case 'f': out[n++] = '\f'; break;
            case 'u': {
                int cp = Hex4(c->p);
                if (cp < 0) { free(out); return NULL; }
                c->p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && c->p[0] == '\\' && c->p[1] == 'u') {      /* surrogate pair */
                    int lo = Hex4(c->p + 2);
                    if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); c->p += 6; }
                }
                if (cp >= 0xD800 && cp < 0xE000) cp = '?';
                PutUtf8(out, &n, (unsigned)cp);
            } break;
            default: out[n++] = e; break;
        }
    }
    out[n] = 0;
    c->p = s + 1;
    return out;
}

static ShsJson *ParseValue(Cur *c, int depth)
{
    Skip(c);
    if (depth > MAX_DEPTH) return NULL;
    ShsJson *n = NULL;
    if (*c->p == '"') {
        n = NewNode(SHS_JSON_STR);
        if (n && !(n->str = ParseString(c))) { free(n); return NULL; }
    } else if (*c->p == '{' || *c->p == '[') {
        const bool obj = *c->p == '{';
        const char close = obj ? '}' : ']';
        n = NewNode(obj ? SHS_JSON_OBJ : SHS_JSON_ARR);
        if (!n) return NULL;
        c->p++;
        ShsJson **tail = &n->child;
        Skip(c);
        if (*c->p == close) { c->p++; return n; }
        for (;;) {
            char *key = NULL;
            Skip(c);
            if (obj) {
                key = ParseString(c);
                Skip(c);
                if (!key || *c->p != ':') { free(key); ShsJsonFree(n); return NULL; }
                c->p++;
            }
            ShsJson *v = ParseValue(c, depth + 1);
            if (!v) { free(key); ShsJsonFree(n); return NULL; }
            v->key = key;
            *tail = v;
            tail = &v->next;
            Skip(c);
            if (*c->p == ',') { c->p++; continue; }
            if (*c->p == close) { c->p++; break; }
            ShsJsonFree(n);
            return NULL;
        }
    } else if (!strncmp(c->p, "true", 4))  { n = NewNode(SHS_JSON_BOOL); if (n) n->num = 1; c->p += 4; }
    else if (!strncmp(c->p, "false", 5))   { n = NewNode(SHS_JSON_BOOL); c->p += 5; }
    else if (!strncmp(c->p, "null", 4))    { n = NewNode(SHS_JSON_NULL); c->p += 4; }
    else if ((*c->p >= '0' && *c->p <= '9') || *c->p == '-') {
        char *end;
        double v = strtod(c->p, &end);
        if (end == c->p) return NULL;
        n = NewNode(SHS_JSON_NUM);
        if (n) n->num = v;
        c->p = end;
    }
    return n;
}

ShsJson *ShsJsonParse(const char *text)
{
    Cur c = { text };
    ShsJson *root = ParseValue(&c, 0);
    if (!root) return NULL;
    Skip(&c);
    if (*c.p) { ShsJsonFree(root); return NULL; }           /* trailing junk: probably an HTML error page */
    return root;
}

void ShsJsonFree(ShsJson *n)
{
    while (n) {
        ShsJson *next = n->next;
        ShsJsonFree(n->child);
        free(n->key);
        free(n->str);
        free(n);
        n = next;
    }
}

const ShsJson *ShsJsonGet(const ShsJson *obj, const char *key)
{
    if (!obj || obj->type != SHS_JSON_OBJ) return NULL;
    for (const ShsJson *m = obj->child; m; m = m->next)
        if (m->key && !strcmp(m->key, key)) return m;
    return NULL;
}

const char *ShsJsonStr(const ShsJson *obj, const char *key, const char *fallback)
{
    const ShsJson *v = ShsJsonGet(obj, key);
    return (v && v->type == SHS_JSON_STR && v->str) ? v->str : fallback;
}

double ShsJsonNum(const ShsJson *obj, const char *key, double fallback)
{
    const ShsJson *v = ShsJsonGet(obj, key);
    if (!v) return fallback;
    if (v->type == SHS_JSON_NUM || v->type == SHS_JSON_BOOL) return v->num;
    if (v->type == SHS_JSON_STR && v->str) return atof(v->str);
    return fallback;
}
