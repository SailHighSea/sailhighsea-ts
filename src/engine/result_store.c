#include "result_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tabs and line breaks would break the one-record-per-line files. */
static void Clean(char *s)
{
    for (; *s; s++) if (*s == '\t' || *s == '\r' || *s == '\n') *s = ' ';
}

/* ---- favourites: hash <TAB> source <TAB> seeders <TAB> leechers <TAB> size <TAB> added <TAB> detail <TAB> name ---- */

void ShsFavLoad(ShsFavorites *f, const char *path)
{
    f->count = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    char line[600];
    while (f->count < SHS_FAV_MAX && fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char *fld[8];
        char *p = line;
        int n = 0;
        while (n < 8) {
            fld[n++] = p;
            if (n == 8) break;
            p = strchr(p, '\t');
            if (!p) break;
            *p++ = 0;
        }
        if (n < 8 || strlen(fld[0]) != 40) continue;
        ShsResult r = { 0 };
        snprintf(r.hash, sizeof r.hash, "%s", fld[0]);
        r.source = atoi(fld[1]);
        if (r.source < 0 || r.source >= SHS_SRC_COUNT) r.source = 0;
        r.seeders = atoi(fld[2]);
        r.leechers = atoi(fld[3]);
        r.size = atoll(fld[4]);
        r.added = atoll(fld[5]);
        snprintf(r.detail, sizeof r.detail, "%s", fld[6]);
        char *extra = strchr(fld[7], '\t');                 /* name, then tab, id, tab, file count (older files end at the name) */
        if (extra) { *extra++ = 0; r.tid = atoll(extra); char *t2 = strchr(extra, '\t'); if (t2) r.files = atoi(t2 + 1); }
        snprintf(r.name, sizeof r.name, "%s", fld[7]);
        f->items[f->count++] = r;
    }
    fclose(fp);
}

bool ShsFavSave(const ShsFavorites *f, const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    for (int i = 0; i < f->count; i++) {
        ShsResult r = f->items[i];
        Clean(r.name);
        Clean(r.detail);
        fprintf(fp, "%s\t%d\t%d\t%d\t%lld\t%lld\t%s\t%s\t%lld\t%d\n", r.hash, r.source, r.seeders, r.leechers, r.size, r.added, r.detail, r.name, r.tid, r.files);
    }
    return fclose(fp) == 0;
}

int ShsFavFind(const ShsFavorites *f, const char *hash)
{
    for (int i = 0; i < f->count; i++) if (!strcmp(f->items[i].hash, hash)) return i;
    return -1;
}

bool ShsFavAdd(ShsFavorites *f, const ShsResult *r)
{
    if (f->count >= SHS_FAV_MAX || ShsFavFind(f, r->hash) >= 0) return false;
    memmove(&f->items[1], &f->items[0], (size_t)f->count * sizeof f->items[0]);
    f->items[0] = *r;
    f->count++;
    return true;
}

void ShsFavRemove(ShsFavorites *f, int idx)
{
    if (idx < 0 || idx >= f->count) return;
    memmove(&f->items[idx], &f->items[idx + 1], (size_t)(f->count - idx - 1) * sizeof f->items[0]);
    f->count--;
}

/* ---- history: one query per line, newest first ---- */

void ShsHistoryLoad(ShsHistory *h, const char *path)
{
    h->count = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    char line[SHS_QUERY_MAX + 8];
    while (h->count < SHS_HISTORY_MAX && fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) snprintf(h->queries[h->count++], SHS_QUERY_MAX, "%s", line);
    }
    fclose(fp);
}

bool ShsHistorySave(const ShsHistory *h, const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    for (int i = 0; i < h->count; i++) fprintf(fp, "%s\n", h->queries[i]);
    return fclose(fp) == 0;
}

void ShsHistoryRemove(ShsHistory *h, int idx)
{
    if (idx < 0 || idx >= h->count) return;
    memmove(h->queries[idx], h->queries[idx + 1], (size_t)(h->count - idx - 1) * SHS_QUERY_MAX);
    h->count--;
}

void ShsHistoryAdd(ShsHistory *h, const char *query)
{
    char q[SHS_QUERY_MAX];
    while (*query == ' ') query++;
    snprintf(q, sizeof q, "%s", query);
    Clean(q);
    for (size_t n = strlen(q); n && q[n - 1] == ' '; ) q[--n] = 0;
    if (!q[0]) return;
    for (int i = 0; i < h->count; i++)
        if (!strcmp(h->queries[i], q)) { ShsHistoryRemove(h, i); break; }
    if (h->count == SHS_HISTORY_MAX) h->count--;
    memmove(h->queries[1], h->queries[0], (size_t)h->count * SHS_QUERY_MAX);
    snprintf(h->queries[0], SHS_QUERY_MAX, "%s", q);
    h->count++;
}
