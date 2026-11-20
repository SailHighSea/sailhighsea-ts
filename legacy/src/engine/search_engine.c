#include "search_engine.h"
#include "json_lite.h"
#include "../platform/http.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PER_SOURCE_MAX 100
#define BODY_MAX       (4 * 1024 * 1024)

typedef enum { KIND_TPB, KIND_YTS, KIND_RSS, KIND_HTML, KIND_CSV, KIND_BIT, KIND_KNABEN } SourceKind;

/* Every site the original app knew. Sites without an API are scraped from their search page; the first address that
   answers is used, so mirrors can be listed. Several of these sites are dead or blocked in places: they show as offline. */
static const struct { const char *name; SourceKind kind; bool on; const char *urls[3]; } SOURCES[SHS_SRC_COUNT] = {
    { "Pirate Bay",     KIND_TPB,  true,  { "https://apibay.org/q.php?q=%s&cat=0", NULL } },
    { "YTS",            KIND_YTS,  true,  { "https://movies-api.accel.li/api/v2/list_movies.json?query_term=%s&limit=30",
                                            "https://yts.mx/api/v2/list_movies.json?query_term=%s&limit=30", NULL } },
    { "Nyaa",           KIND_RSS,  true,  { "https://nyaa.si/?page=rss&q=%s&c=0_0&f=0", "https://nyaa.land/?page=rss&q=%s&c=0_0&f=0", NULL } },
    { "Sukebei",        KIND_RSS,  false, { "https://sukebei.nyaa.si/?page=rss&q=%s&c=0_0&f=0", NULL } },
    { "1337x",          KIND_HTML, true,  { "https://www.1337x.st/search/%s/1/", NULL } },
    { "TorrentGalaxy",  KIND_HTML, true,  { "https://torrentgalaxy.to/torrents.php?search=%s", NULL } },
    { "LimeTorrents",   KIND_HTML, true,  { "https://www.limetorrents.info/search/all/%s/", NULL } },
    { "Torlock",        KIND_HTML, true,  { "https://www.torlock.com/all/torrents/%s.html", NULL } },
    { "EZTV",           KIND_HTML, true,  { "https://eztv.io/search/%s", NULL } },
    { "ETTV",           KIND_HTML, true,  { "https://www.ettv.to/torrents-search.php?search=%s", NULL } },
    { "KickassTorrents", KIND_HTML, true, { "https://kickasstorrent.cr/usearch/%s/", NULL } },
    { "Zooqle",         KIND_HTML, true,  { "https://zooqle.com/search?q=%s", NULL } },
    { "TorrentKitty",   KIND_HTML, true,  { "https://www.torrentkitty.tv/search/%s/", NULL } },
    { "Torrentz2",      KIND_HTML, true,  { "https://torrentz2.eu/search?f=%s", NULL } },
    { "BTDB",           KIND_HTML, true,  { "https://btdb.eu/?search=%s", NULL } },
    { "BTSOW",          KIND_HTML, true,  { "https://btsow.pw/search/%s", NULL } },
    { "SkyTorrents",    KIND_HTML, true,  { "https://www.skytorrents.lol/?query=%s", NULL } },
    { "SeedPeer",       KIND_HTML, true,  { "https://www.seedpeer.me/search/%s", NULL } },
    { "ExtraTorrent",   KIND_HTML, true,  { "https://extratorrent.si/search/?search=%s&new=1&x=0&y=0", NULL } },
    { "Monova",         KIND_HTML, true,  { "https://monova.org/search?term=%s", NULL } },
    { "TheHiddenBay",   KIND_HTML, true,  { "https://thehiddenbay.com/search/%s/1/99/0", NULL } },
    { "BTDigg",         KIND_HTML, true,  { "http://btdiggba.pw/search/%s/1/0/0.html", NULL } },
    { "BTBaocai",       KIND_HTML, true,  { "http://www.btbaocai.in/search/%s/", NULL } },
    { "BTZhizhu",       KIND_HTML, true,  { "https://www.btzhizhu.me/search/%s/", NULL } },
    { "Diaosisou",      KIND_HTML, true,  { "http://www.diaosisou.in/list/%s/1", NULL } },
    { "NMS000",         KIND_HTML, true,  { "http://www.nms000.com/l/%s-hot-desc-1", NULL } },
    { "Aliciliba",      KIND_HTML, true,  { "http://aliciliba.ws/list/%s/1-0-0/", NULL } },
    { "Dabaicai",       KIND_HTML, true,  { "https://dabaicai.ml/infolist.php?q=%s", NULL } },
    { "Torrents.csv",   KIND_CSV,  true,  { "https://torrents-csv.com/service/search?q=%s&size=100", NULL } },
    { "BitSearch",      KIND_BIT,  true,  { "https://bitsearch.to/api/v1/search?q=%s", "https://solidtorrents.to/api/v1/search?q=%s", NULL } },
    { "Knaben",         KIND_KNABEN, true, { "https://api.knaben.org/v1", NULL } },
};

const char *ShsSourceName(int src)
{
    return (src >= 0 && src < SHS_SRC_COUNT) ? SOURCES[src].name : "?";
}

unsigned ShsSourceDefaultMask(void)
{
    unsigned m = 0;
    for (int i = 0; i < SHS_SRC_COUNT; i++) if (SOURCES[i].on) m |= 1u << i;
    return m;
}

/* ---- small helpers ----------------------------------------------------------------------------------------------- */

static void CopyStr(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

/* Lower-cases a 40-digit hex hash into `out`. Returns false for anything else (and for the all-zero "no results" row). */
static bool TakeHash(const char *in, char out[41])
{
    if (!in || strlen(in) != 40) return false;
    bool nonZero = false;
    for (int i = 0; i < 40; i++) {
        if (!isxdigit((unsigned char)in[i])) return false;
        out[i] = (char)tolower((unsigned char)in[i]);
        if (in[i] != '0') nonZero = true;
    }
    out[40] = 0;
    return nonZero;
}

void ShsFormatSize(long long b, char *out, size_t cap)
{
    if (b <= 0)                        snprintf(out, cap, "-");
    else if (b >= 1073741824LL * 1024) snprintf(out, cap, "%.2f TB", (double)b / (1073741824.0 * 1024));
    else if (b >= 1073741824LL)        snprintf(out, cap, "%.2f GB", (double)b / 1073741824.0);
    else if (b >= 1048576LL)           snprintf(out, cap, "%.0f MB", (double)b / 1048576.0);
    else if (b >= 1024LL)              snprintf(out, cap, "%.0f KB", (double)b / 1024.0);
    else                               snprintf(out, cap, "%lld B", b);
}

void ShsFormatAge(long long then, long long now, char *out, size_t cap)
{
    if (then <= 0) { snprintf(out, cap, "-"); return; }
    long long d = now - then;
    if (d < 0) d = 0;
    if (d < 3600)                snprintf(out, cap, "%lldm ago", d / 60);
    else if (d < 86400)          snprintf(out, cap, "%lldh ago", d / 3600);
    else if (d < 86400LL * 60)   snprintf(out, cap, "%lldd ago", d / 86400);
    else if (d < 86400LL * 730)  snprintf(out, cap, "%lldmo ago", d / (86400LL * 30));
    else                         snprintf(out, cap, "%lldy ago", d / (86400LL * 365));
}

/* Days since 1970-01-01 for a civil date (proleptic Gregorian). */
static long long DaysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

/* RFC 822 date as used by RSS: "Sat, 01 Nov 2025 12:00:00 -0000". Returns 0 when it does not parse. */
static long long ParseRssDate(const char *s)
{
    static const char *mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const char *c = strchr(s, ',');
    c = c ? c + 1 : s;
    int d, y, hh = 0, mm = 0, ss = 0;
    char mn[4] = "";
    if (sscanf(c, " %d %3s %d %d:%d:%d", &d, mn, &y, &hh, &mm, &ss) < 3) return 0;
    for (int i = 0; i < 12; i++)
        if (!strncmp(mn, mon[i], 3)) return (DaysFromCivil(y, i + 1, d) * 24 + hh) * 3600 + mm * 60 + ss;
    return 0;
}

/* "1.4 GiB" / "700 MiB" -> bytes */
static long long ParseHumanSize(const char *s)
{
    char *end;
    double v = strtod(s, &end);
    while (*end == ' ') end++;
    double mul = 1;
    switch (toupper((unsigned char)*end)) {
        case 'K': mul = 1024.0; break;
        case 'M': mul = 1048576.0; break;
        case 'G': mul = 1073741824.0; break;
        case 'T': mul = 1099511627776.0; break;
        default: break;
    }
    return (long long)(v * mul);
}

/* Replaces the HTML entities that show up in titles (&amp; &#39; &eacute; ...), in place. Decoding never makes the text longer. */
static void DecodeEntities(char *s)
{
    static const struct { const char *ent; unsigned cp; } ents[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' },
        { "auml", 0xE4 }, { "ouml", 0xF6 }, { "uuml", 0xFC }, { "euml", 0xEB }, { "iuml", 0xEF },
        { "Auml", 0xC4 }, { "Ouml", 0xD6 }, { "Uuml", 0xDC }, { "szlig", 0xDF },
        { "aacute", 0xE1 }, { "eacute", 0xE9 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA },
        { "agrave", 0xE0 }, { "egrave", 0xE8 }, { "ograve", 0xF2 }, { "ntilde", 0xF1 }, { "ccedil", 0xE7 },
        { "acirc", 0xE2 }, { "ecirc", 0xEA }, { "ocirc", 0xF4 }, { "Eacute", 0xC9 }, { "ndash", 0x2013 }, { "mdash", 0x2014 },
    };
    char *w = s;
    while (*s) {
        if (*s == '&') {
            const char *semi = strchr(s, ';');
            if (semi && semi - s <= 10) {
                char name[12];
                size_t l = (size_t)(semi - s - 1);
                memcpy(name, s + 1, l);
                name[l] = 0;
                unsigned cp = 0;
                if (name[0] == '#') cp = (unsigned)strtoul(name + 1 + (name[1] == 'x' || name[1] == 'X'), NULL, (name[1] == 'x' || name[1] == 'X') ? 16 : 10);
                else for (size_t i = 0; i < sizeof ents / sizeof ents[0]; i++) if (!strcmp(name, ents[i].ent)) { cp = ents[i].cp; break; }
                if (cp >= 32 && cp < 0x110000 && !(cp >= 0xD800 && cp < 0xE000)) {
                    if (cp < 0x80)        *w++ = (char)cp;
                    else if (cp < 0x800)  { *w++ = (char)(0xC0 | (cp >> 6)); *w++ = (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { *w++ = (char)(0xE0 | (cp >> 12)); *w++ = (char)(0x80 | ((cp >> 6) & 63)); *w++ = (char)(0x80 | (cp & 63)); }
                    else                  { *w++ = (char)(0xF0 | (cp >> 18)); *w++ = (char)(0x80 | ((cp >> 12) & 63)); *w++ = (char)(0x80 | ((cp >> 6) & 63)); *w++ = (char)(0x80 | (cp & 63)); }
                    s = (char *)semi + 1;
                    continue;
                }
            }
        }
        *w++ = *s++;
    }
    *w = 0;
}

/* ---- magnet link ------------------------------------------------------------------------------------------------- */

static const char *const TRACKERS[] = {
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://open.demonii.com:1337/announce",
};

static void AppendEnc(char *out, size_t cap, const char *s)
{
    char enc[600];
    ShsUrlEncode(s, enc, sizeof enc);
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "%s", enc);
}

void ShsMagnet(const ShsResult *r, char *out, size_t cap)
{
    snprintf(out, cap, "magnet:?xt=urn:btih:%s&dn=", r->hash);
    AppendEnc(out, cap, r->name);
    for (size_t i = 0; i < sizeof TRACKERS / sizeof TRACKERS[0]; i++) {
        size_t n = strlen(out);
        snprintf(out + n, cap - n, "&tr=");
        AppendEnc(out, cap, TRACKERS[i]);
    }
}

/* ---- parsers ----------------------------------------------------------------------------------------------------- */

const char *ShsTpbCategory(int c)
{
    static const struct { int code; const char *name; } t[] = {
        { 101, "Music" }, { 102, "Audio books" }, { 103, "Sound clips" }, { 104, "FLAC" }, { 199, "Other audio" },
        { 201, "Movies" }, { 202, "Movies DVDR" }, { 203, "Music videos" }, { 204, "Movie clips" }, { 205, "TV shows" },
        { 206, "Handheld video" }, { 207, "HD Movies" }, { 208, "HD TV shows" }, { 209, "3D" }, { 211, "4K Movies" }, { 212, "4K TV shows" }, { 299, "Other video" },
        { 301, "Windows" }, { 302, "Mac" }, { 303, "UNIX" }, { 304, "Handheld apps" }, { 305, "iOS" }, { 306, "Android" }, { 399, "Other software" },
        { 401, "PC games" }, { 402, "Mac games" }, { 403, "PSx games" }, { 404, "XBOX360 games" }, { 405, "Wii games" }, { 406, "Handheld games" },
        { 407, "iOS games" }, { 408, "Android games" }, { 499, "Other games" },
        { 601, "E-books" }, { 602, "Comics" }, { 603, "Pictures" }, { 604, "Covers" }, { 605, "Physibles" }, { 699, "Other" },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (t[i].code == c) return t[i].name;
    switch (c / 100) { case 1: return "Audio"; case 2: return "Video"; case 3: return "Applications"; case 4: return "Games"; case 5: return "Adult"; case 6: return "Other"; default: return ""; }
}

/* apibay.org (The Pirate Bay): a JSON array of flat objects whose numbers are quoted strings. Every parser returns -1 for a reply
   that is not in the expected format (a block page, an error), so the caller can try a mirror. */
int ShsParseTpb(const char *body, ShsResult *out, int max)
{
    ShsJson *root = ShsJsonParse(body);
    int n = (root && root->type == SHS_JSON_ARR) ? 0 : -1;
    if (n == 0) {
        for (const ShsJson *it = root->child; it && n < max; it = it->next) {
            ShsResult r = { 0 };
            if (!TakeHash(ShsJsonStr(it, "info_hash", NULL), r.hash)) continue;
            r.source = SHS_SRC_TPB;
            CopyStr(r.name, sizeof r.name, ShsJsonStr(it, "name", ""));
            DecodeEntities(r.name);                      /* apibay sends HTML entities in names */
            r.seeders = (int)ShsJsonNum(it, "seeders", 0);
            r.leechers = (int)ShsJsonNum(it, "leechers", 0);
            r.size = (long long)ShsJsonNum(it, "size", 0);
            r.added = (long long)ShsJsonNum(it, "added", 0);
            r.tid = (long long)ShsJsonNum(it, "id", 0);
            r.files = (int)ShsJsonNum(it, "num_files", 0);
            snprintf(r.detail, sizeof r.detail, "%s%s%s", ShsTpbCategory((int)ShsJsonNum(it, "category", 0)), ShsJsonStr(it, "username", "")[0] ? " - " : "", ShsJsonStr(it, "username", ""));
            out[n++] = r;
        }
    }
    ShsJsonFree(root);
    return n;
}

/* yts.mx: data.movies[] each with torrents[] (one result per quality). */
int ShsParseYts(const char *body, ShsResult *out, int max)
{
    ShsJson *root = ShsJsonParse(body);
    int n = ShsJsonGet(ShsJsonGet(root, "data"), "movie_count") ? 0 : -1;       /* a search without hits has no "movies" list */
    const ShsJson *movies = ShsJsonGet(ShsJsonGet(root, "data"), "movies");
    if (movies && movies->type == SHS_JSON_ARR) {
        if (n < 0) n = 0;
        for (const ShsJson *m = movies->child; m; m = m->next) {
            const ShsJson *tors = ShsJsonGet(m, "torrents");
            if (!tors || tors->type != SHS_JSON_ARR) continue;
            for (const ShsJson *t = tors->child; t && n < max; t = t->next) {
                ShsResult r = { 0 };
                if (!TakeHash(ShsJsonStr(t, "hash", NULL), r.hash)) continue;
                r.source = SHS_SRC_YTS;
                const char *q = ShsJsonStr(t, "quality", ""), *type = ShsJsonStr(t, "type", "");
                snprintf(r.name, sizeof r.name, "%s [%s %s]", ShsJsonStr(m, "title_long", ShsJsonStr(m, "title", "")), q, type);
                snprintf(r.detail, sizeof r.detail, "%s %s", q, type);
                r.seeders = (int)ShsJsonNum(t, "seeds", 0);
                r.leechers = (int)ShsJsonNum(t, "peers", 0);
                r.size = (long long)ShsJsonNum(t, "size_bytes", 0);
                r.added = (long long)ShsJsonNum(t, "date_uploaded_unix", 0);
                out[n++] = r;
            }
        }
    }
    ShsJsonFree(root);
    return n;
}

/* Text between <tag> and </tag> inside [from, to). Copies at most cap-1 bytes; decodes CDATA and the XML entities. */
static bool XmlTag(const char *from, const char *to, const char *tag, char *out, size_t cap)
{
    char open[40], close[40];
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    const char *s = strstr(from, open);
    if (!s || s >= to) return false;
    s += strlen(open);
    const char *e = strstr(s, close);
    if (!e || e > to) return false;
    if (!strncmp(s, "<![CDATA[", 9)) { s += 9; if (e - s >= 3 && !strncmp(e - 3, "]]>", 3)) e -= 3; }
    size_t n = (size_t)(e - s) < cap - 1 ? (size_t)(e - s) : cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
    DecodeEntities(out);
    return true;
}

/* nyaa.si RSS: <item> with <title>, <pubDate> and the nyaa: namespace fields. */
int ShsParseNyaa(const char *body, ShsResult *out, int max)
{
    if (!strstr(body, "<rss")) return -1;
    int n = 0;
    const char *p = body;
    while (n < max && (p = strstr(p, "<item>")) != NULL) {
        const char *end = strstr(p, "</item>");
        if (!end) break;
        char tmp[300];
        ShsResult r = { 0 };
        if (XmlTag(p, end, "nyaa:infoHash", tmp, sizeof tmp) && TakeHash(tmp, r.hash)) {
            r.source = SHS_SRC_NYAA;
            if (XmlTag(p, end, "title", tmp, sizeof tmp)) CopyStr(r.name, sizeof r.name, tmp);
            if (XmlTag(p, end, "nyaa:seeders", tmp, sizeof tmp)) r.seeders = atoi(tmp);
            if (XmlTag(p, end, "nyaa:leechers", tmp, sizeof tmp)) r.leechers = atoi(tmp);
            if (XmlTag(p, end, "nyaa:size", tmp, sizeof tmp)) r.size = ParseHumanSize(tmp);
            if (XmlTag(p, end, "pubDate", tmp, sizeof tmp)) r.added = ParseRssDate(tmp);
            if (XmlTag(p, end, "nyaa:category", tmp, sizeof tmp)) CopyStr(r.detail, sizeof r.detail, tmp);
            out[n++] = r;
        }
        p = end + 7;
    }
    return n;
}

/* "2026-11-03T15:00:14.260Z" / "2026-04-25T15:35:00+00:00" -> unix time (UTC; a zone offset is ignored). 0 when it does not parse. */
static long long ParseIsoDate(const char *s)
{
    int y, mo, d, hh = 0, mm = 0, ss = 0;
    if (!s || sscanf(s, "%d-%d-%d%*[T ]%d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) < 3) return 0;
    return (DaysFromCivil(y, mo, d) * 24 + hh) * 3600 + mm * 60 + ss;
}

/* torrents-csv.com: {"torrents":[{"infohash","name","size_bytes","created_unix","seeders","leechers"}, ...]} */
int ShsParseTorrentsCsv(const char *body, ShsResult *out, int max)
{
    ShsJson *root = ShsJsonParse(body);
    const ShsJson *list = ShsJsonGet(root, "torrents");
    int n = (list && list->type == SHS_JSON_ARR) ? 0 : -1;
    if (n == 0) {
        for (const ShsJson *it = list->child; it && n < max; it = it->next) {
            ShsResult r = { 0 };
            if (!TakeHash(ShsJsonStr(it, "infohash", NULL), r.hash)) continue;
            CopyStr(r.name, sizeof r.name, ShsJsonStr(it, "name", ""));
            r.seeders = (int)ShsJsonNum(it, "seeders", 0);
            r.leechers = (int)ShsJsonNum(it, "leechers", 0);
            r.size = (long long)ShsJsonNum(it, "size_bytes", 0);
            r.added = (long long)ShsJsonNum(it, "created_unix", 0);
            out[n++] = r;
        }
    }
    ShsJsonFree(root);
    return n;
}

/* bitsearch.to: {"success":true,"results":[{"infohash","title","size","seeders","leechers","updatedAt"}, ...]} */
int ShsParseBitSearch(const char *body, ShsResult *out, int max)
{
    ShsJson *root = ShsJsonParse(body);
    const ShsJson *list = ShsJsonGet(root, "results");
    int n = (list && list->type == SHS_JSON_ARR) ? 0 : -1;
    if (n == 0) {
        for (const ShsJson *it = list->child; it && n < max; it = it->next) {
            ShsResult r = { 0 };
            if (!TakeHash(ShsJsonStr(it, "infohash", NULL), r.hash)) continue;
            CopyStr(r.name, sizeof r.name, ShsJsonStr(it, "title", ""));
            r.seeders = (int)ShsJsonNum(it, "seeders", 0);
            r.leechers = (int)ShsJsonNum(it, "leechers", 0);
            r.size = (long long)ShsJsonNum(it, "size", 0);
            r.added = ParseIsoDate(ShsJsonStr(it, "updatedAt", NULL));
            out[n++] = r;
        }
    }
    ShsJsonFree(root);
    return n;
}

/* api.knaben.org: {"hits":[{"hash","title","bytes","date","seeders","peers","category"}, ...]} */
int ShsParseKnaben(const char *body, ShsResult *out, int max)
{
    ShsJson *root = ShsJsonParse(body);
    const ShsJson *list = ShsJsonGet(root, "hits");
    int n = (list && list->type == SHS_JSON_ARR) ? 0 : -1;
    if (n == 0) {
        for (const ShsJson *it = list->child; it && n < max; it = it->next) {
            ShsResult r = { 0 };
            if (!TakeHash(ShsJsonStr(it, "hash", NULL), r.hash)) continue;
            CopyStr(r.name, sizeof r.name, ShsJsonStr(it, "title", ""));
            r.seeders = (int)ShsJsonNum(it, "seeders", 0);
            r.leechers = (int)ShsJsonNum(it, "peers", 0);
            r.size = (long long)ShsJsonNum(it, "bytes", 0);
            r.added = ParseIsoDate(ShsJsonStr(it, "date", NULL));
            CopyStr(r.detail, sizeof r.detail, ShsJsonStr(it, "category", ""));
            out[n++] = r;
        }
    }
    ShsJsonFree(root);
    return n;
}

/* Quotes `s` for a JSON string value (the quotes themselves are not added). */
static void JsonEscape(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    for (; *s && n + 7 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[n++] = '\\'; out[n++] = (char)c; }
        else if (c < 32) n += (size_t)snprintf(out + n, cap - n, "\\u%04x", c);
        else out[n++] = (char)c;
    }
    out[n] = 0;
}

/* Generic scraper for sites without an API. Takes (1) every magnet link on the page (hash + "dn" name) and (2) every link
   that carries a 40-digit hash, named after the link text. Seeders and sizes are not read from arbitrary HTML, so they
   stay 0 / unknown for these sources. Returns -1 when the reply is not a web page. */
static bool IsB32(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'); }

/* 32 base32 characters -> 40 hex digits */
static bool Base32ToHex(const char *in, char out[41])
{
    unsigned long long acc = 0;
    int bits = 0, n = 0;
    for (int i = 0; i < 32; i++) {
        char c = in[i];
        int v = (c >= 'A' && c <= 'Z') ? c - 'A' : (c >= 'a' && c <= 'z') ? c - 'a' : c - '2' + 26;
        acc = (acc << 5) | (unsigned)v;
        bits += 5;
        while (bits >= 4 && n < 40) { bits -= 4; out[n++] = "0123456789abcdef"[(acc >> bits) & 15]; }
        acc &= (1ULL << bits) - 1;
    }
    out[40] = 0;
    return n == 40;
}

static void UrlDecode(char *s)
{
    char *w = s;
    while (*s) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = { s[1], s[2], 0 };
            *w++ = (char)strtol(h, NULL, 16);
            s += 3;
        } else if (*s == '+') { *w++ = ' '; s++; }
        else *w++ = *s++;
    }
    *w = 0;
}

static bool HaveHash(const ShsResult *out, int n, const char *hash)
{
    for (int i = 0; i < n; i++) if (!strcmp(out[i].hash, hash)) return true;
    return false;
}

int ShsParseHtml(const char *body, int src, ShsResult *out, int max)
{
    if (!strchr(body, '<')) return -1;
    int n = 0;

    /* 1. magnet links */
    for (const char *p = body; n < max && (p = strstr(p, "btih:")) != NULL; ) {
        p += 5;
        size_t run = 0;
        while (run < 41 && isalnum((unsigned char)p[run])) run++;
        char hash[41];
        bool ok = false;
        if (run == 40) { char cand[41]; memcpy(cand, p, 40); cand[40] = 0; ok = TakeHash(cand, hash); }
        else if (run == 32 && IsB32(p[0])) { ok = Base32ToHex(p, hash); if (ok) ok = TakeHash(hash, hash); }
        if (!ok || HaveHash(out, n, hash)) continue;
        ShsResult r = { 0 };
        r.source = src;
        snprintf(r.hash, sizeof r.hash, "%s", hash);
        const char *end = p;
        while (*end && end - p < 1500 && *end != '"' && *end != '\'' && *end != '<' && *end != ' ' && *end != '\n') end++;
        const char *dn = p + run;
        while (dn < end && strncmp(dn, "dn=", 3)) dn++;
        if (dn < end) {
            dn += 3;
            size_t l = 0;
            while (dn + l < end && dn[l] != '&' && l < sizeof r.name - 1) l++;
            memcpy(r.name, dn, l);
            r.name[l] = 0;
            UrlDecode(r.name);
        }
        if (!r.name[0]) snprintf(r.name, sizeof r.name, "%s", hash);
        out[n++] = r;
    }

    /* 2. links containing a hash */
    for (const char *p = body; n < max && (p = strstr(p, "href=")) != NULL; ) {
        p += 5;
        char q = *p;
        if (q != '"' && q != '\'') continue;
        const char *v = p + 1, *e = strchr(v, q);
        if (!e || e - v > 400) continue;
        char hash[41] = "";
        for (const char *h = v; h + 40 <= e; h++) {
            if (h > v && isalnum((unsigned char)h[-1])) continue;
            if (h + 40 < e && isalnum((unsigned char)h[40])) continue;
            char cand[41];
            memcpy(cand, h, 40);
            cand[40] = 0;
            if (TakeHash(cand, hash)) break;
            hash[0] = 0;
        }
        if (!hash[0] || HaveHash(out, n, hash)) continue;
        ShsResult r = { 0 };
        r.source = src;
        snprintf(r.hash, sizeof r.hash, "%s", hash);
        const char *t = strchr(e, '>');                    /* link text up to </a>, tags skipped */
        size_t l = 0;
        if (t && t - e < 300) {
            bool tag = false;
            for (t++; *t && l < sizeof r.name - 1 && !(t[0] == '<' && t[1] == '/' && (t[2] == 'a' || t[2] == 'A')); t++) {
                if (*t == '<') tag = true;
                else if (*t == '>') tag = false;
                else if (!tag && *t != '\n' && *t != '\r' && *t != '\t') r.name[l++] = *t;
            }
        }
        r.name[l] = 0;
        DecodeEntities(r.name);
        while (l && r.name[l - 1] == ' ') r.name[--l] = 0;
        char *lead = r.name;
        while (*lead == ' ') lead++;
        memmove(r.name, lead, strlen(lead) + 1);
        if (strlen(r.name) < 3) snprintf(r.name, sizeof r.name, "%s", hash);
        out[n++] = r;
    }
    return n;
}

/* ---- merged results + worker threads ------------------------------------------------------------------------------ */

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  typedef CRITICAL_SECTION Lock;
  #define LOCK_INIT(l)   InitializeCriticalSection(l)
  #define LOCK(l)        EnterCriticalSection(l)
  #define UNLOCK(l)      LeaveCriticalSection(l)
  #define THREAD_RET     DWORD WINAPI
  #define THREAD_END     return 0
  typedef LPVOID ThreadArg;
  static bool Spawn(LPTHREAD_START_ROUTINE fn, void *arg)
  {
      HANDLE h = CreateThread(NULL, 0, fn, arg, 0, NULL);
      if (!h) return false;
      CloseHandle(h);
      return true;
  }
#else
  #include <pthread.h>
  typedef pthread_mutex_t Lock;
  #define LOCK_INIT(l)   pthread_mutex_init(l, NULL)
  #define LOCK(l)        pthread_mutex_lock(l)
  #define UNLOCK(l)      pthread_mutex_unlock(l)
  #define THREAD_RET     void *
  #define THREAD_END     return NULL
  typedef void *ThreadArg;
  static bool Spawn(void *(*fn)(void *), void *arg)
  {
      pthread_t t;
      if (pthread_create(&t, NULL, fn, arg) != 0) return false;
      pthread_detach(t);
      return true;
  }
#endif

static Lock           g_lock;
static atomic_flag    g_lockStarted = ATOMIC_FLAG_INIT;
static atomic_int     g_lockReady = 0;
static ShsResult      g_results[SHS_MAX_RESULTS];
static int            g_count = 0;
static unsigned       g_gen = 0;                       /* identifies the current search; late answers of older ones are dropped */
static unsigned       g_version = 0;
static ShsSourceState g_state[SHS_SRC_COUNT];
static int            g_srcCount[SHS_SRC_COUNT];

static void EnsureLock(void)
{
    if (!atomic_flag_test_and_set(&g_lockStarted)) { LOCK_INIT(&g_lock); atomic_store(&g_lockReady, 1); }
    while (!atomic_load(&g_lockReady)) { }
}

typedef struct { int src; unsigned gen; char query[160]; } Job;

static bool BuildUrl(int src, int attempt, const char *query, char *url, size_t cap)
{
    if (src < 0 || src >= SHS_SRC_COUNT || attempt >= 3 || !SOURCES[src].urls[attempt]) return false;
    char q[480];
    ShsUrlEncode(query, q, sizeof q);
    snprintf(url, cap, SOURCES[src].urls[attempt], q);
    return true;
}

/* Adds one result to the merged list; a hash seen before keeps whichever copy has more seeders. Caller holds the lock. */
static void Merge(const ShsResult *r)
{
    for (int i = 0; i < g_count; i++) {
        if (!strcmp(g_results[i].hash, r->hash)) {
            if (r->seeders > g_results[i].seeders) g_results[i] = *r;
            return;
        }
    }
    if (g_count < SHS_MAX_RESULTS) g_results[g_count++] = *r;
}

static THREAD_RET Worker(ThreadArg arg)
{
    Job *job = (Job *)arg;
    char url[700];
    ShsResult *got = NULL;
    int n = 0;
    bool ok = false;
    got = (ShsResult *)calloc(PER_SOURCE_MAX, sizeof *got);
    for (int attempt = 0; got && !ok && BuildUrl(job->src, attempt, job->query, url, sizeof url); attempt++) {
        char *body;
        if (SOURCES[job->src].kind == KIND_KNABEN) {
            char esc[400], json[700];
            JsonEscape(job->query, esc, sizeof esc);
            snprintf(json, sizeof json, "{\"search_type\":\"100%%\",\"search_field\":\"title\",\"query\":\"%s\",\"order_by\":\"seeders\",\"order_direction\":\"desc\",\"from\":0,\"size\":100,\"hide_unsafe\":true}", esc);
            body = ShsHttpPostJson(url, json, BODY_MAX, NULL);
        } else body = ShsHttpGet(url, BODY_MAX, NULL);
        if (body) {
            int rows = -1;
            switch (SOURCES[job->src].kind) {
                case KIND_TPB:  rows = ShsParseTpb(body, got, PER_SOURCE_MAX); break;
                case KIND_YTS:  rows = ShsParseYts(body, got, PER_SOURCE_MAX); break;
                case KIND_RSS:  rows = ShsParseNyaa(body, got, PER_SOURCE_MAX); for (int i = 0; i < rows; i++) got[i].source = job->src; break;
                case KIND_HTML: rows = ShsParseHtml(body, job->src, got, PER_SOURCE_MAX); break;
                case KIND_CSV:  rows = ShsParseTorrentsCsv(body, got, PER_SOURCE_MAX); for (int i = 0; i < rows; i++) got[i].source = job->src; break;
                case KIND_BIT:  rows = ShsParseBitSearch(body, got, PER_SOURCE_MAX); for (int i = 0; i < rows; i++) got[i].source = job->src; break;
                case KIND_KNABEN: rows = ShsParseKnaben(body, got, PER_SOURCE_MAX); for (int i = 0; i < rows; i++) got[i].source = job->src; break;
                default: break;
            }
            if (rows >= 0) { n = rows; ok = true; }      /* zero rows is a valid empty answer; -1 means not the expected reply, try the next mirror */
        }
        free(body);
    }
    LOCK(&g_lock);
    if (job->gen == g_gen) {
        for (int i = 0; i < n; i++) Merge(&got[i]);
        g_state[job->src] = ok ? SHS_STATE_OK : SHS_STATE_FAILED;
        g_srcCount[job->src] = n;
        g_version++;
    }
    UNLOCK(&g_lock);
    free(got);
    free(job);
    THREAD_END;
}

void ShsSearchStart(const char *query, unsigned mask)
{
    EnsureLock();
    LOCK(&g_lock);
    g_gen++;
    g_count = 0;
    for (int s = 0; s < SHS_SRC_COUNT; s++) { g_state[s] = SHS_STATE_IDLE; g_srcCount[s] = 0; }
    g_version++;
    const unsigned gen = g_gen;
    UNLOCK(&g_lock);

    for (int s = 0; s < SHS_SRC_COUNT; s++) {
        if (!(mask & (1u << s))) continue;
        Job *job = (Job *)calloc(1, sizeof *job);
        if (!job) continue;
        job->src = s;
        job->gen = gen;
        CopyStr(job->query, sizeof job->query, query);
        LOCK(&g_lock);
        g_state[s] = SHS_STATE_RUNNING;
        UNLOCK(&g_lock);
        if (!Spawn(Worker, job)) {
            free(job);
            LOCK(&g_lock); g_state[s] = SHS_STATE_FAILED; g_version++; UNLOCK(&g_lock);
        }
    }
}

unsigned ShsSearchVersion(void)
{
    EnsureLock();
    LOCK(&g_lock);
    unsigned v = g_version;
    UNLOCK(&g_lock);
    return v;
}

int ShsSearchCopy(ShsResult *out, int max)
{
    EnsureLock();
    LOCK(&g_lock);
    int n = g_count < max ? g_count : max;
    memcpy(out, g_results, (size_t)n * sizeof *out);
    UNLOCK(&g_lock);
    return n;
}

ShsSourceState ShsSearchState(int src, int *count)
{
    EnsureLock();
    if (src < 0 || src >= SHS_SRC_COUNT) return SHS_STATE_IDLE;
    LOCK(&g_lock);
    ShsSourceState s = g_state[src];
    if (count) *count = g_srcCount[src];
    UNLOCK(&g_lock);
    return s;
}

bool ShsSearchBusy(void)
{
    EnsureLock();
    LOCK(&g_lock);
    bool busy = false;
    for (int s = 0; s < SHS_SRC_COUNT; s++) if (g_state[s] == SHS_STATE_RUNNING) busy = true;
    UNLOCK(&g_lock);
    return busy;
}

/* ---- suggestions -------------------------------------------------------------------------------------------------- */

int ShsParseSuggest(const char *body, char (*out)[SHS_SUGGEST_LEN], int max)
{
    ShsJson *root = ShsJsonParse(body);
    int n = -1;
    if (root && root->type == SHS_JSON_ARR && root->child && root->child->next && root->child->next->type == SHS_JSON_ARR) {
        n = 0;
        for (const ShsJson *s = root->child->next->child; s && n < max; s = s->next)
            if (s->type == SHS_JSON_STR && s->str && s->str[0]) CopyStr(out[n++], SHS_SUGGEST_LEN, s->str);
    }
    ShsJsonFree(root);
    return n;
}

static char     g_sugQuery[SHS_SUGGEST_LEN];
static char     g_sugList[SHS_SUGGEST_MAX][SHS_SUGGEST_LEN];
static int      g_sugCount = 0;
static unsigned g_sugGen = 0, g_sugVer = 0;

typedef struct { unsigned gen; char query[SHS_SUGGEST_LEN]; } SugJob;

static THREAD_RET SuggestWorker(ThreadArg arg)
{
    SugJob *job = (SugJob *)arg;
    char enc[480], url[600];
    ShsUrlEncode(job->query, enc, sizeof enc);
    snprintf(url, sizeof url, "https://suggestqueries.google.com/complete/search?client=firefox&q=%s", enc);
    char (*list)[SHS_SUGGEST_LEN] = (char (*)[SHS_SUGGEST_LEN])calloc(SHS_SUGGEST_MAX, SHS_SUGGEST_LEN);
    int n = -1;
    char *body = list ? ShsHttpGet(url, 64 * 1024, NULL) : NULL;
    if (body) n = ShsParseSuggest(body, list, SHS_SUGGEST_MAX);
    free(body);
    if (n >= 0) {
        LOCK(&g_lock);
        if (job->gen == g_sugGen) {
            memcpy(g_sugList, list, sizeof g_sugList);
            g_sugCount = n;
            CopyStr(g_sugQuery, sizeof g_sugQuery, job->query);
            g_sugVer++;
        }
        UNLOCK(&g_lock);
    }
    free(list);
    free(job);
    THREAD_END;
}

void ShsSuggestStart(const char *query)
{
    EnsureLock();
    SugJob *job = (SugJob *)calloc(1, sizeof *job);
    if (!job) return;
    CopyStr(job->query, sizeof job->query, query);
    LOCK(&g_lock);
    job->gen = ++g_sugGen;
    UNLOCK(&g_lock);
    if (!Spawn(SuggestWorker, job)) free(job);
}

unsigned ShsSuggestVersion(void)
{
    EnsureLock();
    LOCK(&g_lock);
    unsigned v = g_sugVer;
    UNLOCK(&g_lock);
    return v;
}

int ShsSuggestGet(char *forQuery, size_t cap, char (*out)[SHS_SUGGEST_LEN], int max)
{
    EnsureLock();
    LOCK(&g_lock);
    CopyStr(forQuery, cap, g_sugQuery);
    int n = g_sugCount < max ? g_sugCount : max;
    memcpy(out, g_sugList, (size_t)n * SHS_SUGGEST_LEN);
    UNLOCK(&g_lock);
    return n;
}

/* ---- torrent details: file list ------------------------------------------------------------------------------------ */

/* apibay f.php: [{"name":["dir/file"],"size":[123]}, ...]; the values come as one-element arrays or as {"0": ...}. */
static const ShsJson *FirstOf(const ShsJson *v)
{
    return (v && (v->type == SHS_JSON_ARR || v->type == SHS_JSON_OBJ)) ? v->child : v;
}

static void AddFile(ShsDetail *d, const char *path, long long size)
{
    if (d->shown < SHS_DETAIL_FILES) {
        ShsFile *f = &d->files[d->shown++];
        CopyStr(f->path, sizeof f->path, path);
        f->size = size;
    }
    d->fileCount++;
    d->total += size > 0 ? size : 0;
}

int ShsParseTpbFiles(const char *body, ShsDetail *d)
{
    ShsJson *root = ShsJsonParse(body);
    int n = -1;
    if (root && root->type == SHS_JSON_ARR) {
        n = 0;
        for (const ShsJson *it = root->child; it; it = it->next) {
            const ShsJson *nm = FirstOf(ShsJsonGet(it, "name")), *sz = FirstOf(ShsJsonGet(it, "size"));
            if (!nm || nm->type != SHS_JSON_STR || !nm->str) continue;
            if (!strcmp(nm->str, "Filelist not found")) continue;
            AddFile(d, nm->str, sz ? (sz->type == SHS_JSON_NUM ? (long long)sz->num : (sz->str ? atoll(sz->str) : 0)) : 0);
            n++;
        }
    }
    ShsJsonFree(root);
    return n;
}

/* Bencode reader for .torrent files (only what the file list needs). */
typedef struct { const unsigned char *p, *end; } Bc;

static bool BcInt(Bc *b, long long *v)
{
    if (b->p >= b->end || *b->p != 'i') return false;
    b->p++;
    bool neg = b->p < b->end && *b->p == '-';
    if (neg) b->p++;
    long long x = 0;
    int digits = 0;
    while (b->p < b->end && *b->p >= '0' && *b->p <= '9' && digits < 18) { x = x * 10 + (*b->p++ - '0'); digits++; }
    if (!digits || b->p >= b->end || *b->p != 'e') return false;
    b->p++;
    *v = neg ? -x : x;
    return true;
}

static bool BcStr(Bc *b, const unsigned char **s, size_t *n)
{
    size_t len = 0;
    int digits = 0;
    while (b->p < b->end && *b->p >= '0' && *b->p <= '9' && digits < 10) { len = len * 10 + (size_t)(*b->p++ - '0'); digits++; }
    if (!digits || b->p >= b->end || *b->p != ':') return false;
    b->p++;
    if (len > (size_t)(b->end - b->p)) return false;
    *s = b->p;
    *n = len;
    b->p += len;
    return true;
}

static bool BcSkip(Bc *b, int depth)
{
    if (depth > 20 || b->p >= b->end) return false;
    if (*b->p == 'i') { long long v; return BcInt(b, &v); }
    if (*b->p == 'l' || *b->p == 'd') {
        b->p++;
        while (b->p < b->end && *b->p != 'e') if (!BcSkip(b, depth + 1)) return false;
        if (b->p >= b->end) return false;
        b->p++;
        return true;
    }
    const unsigned char *s; size_t n;
    return BcStr(b, &s, &n);
}

static bool BcKey(Bc *b, const char *name)       /* reads a dict key and says whether it is `name` */
{
    const unsigned char *s; size_t n;
    return BcStr(b, &s, &n) && n == strlen(name) && !memcmp(s, name, n);
}

static bool BcPeekKey(Bc *b, char *key, size_t cap)
{
    const unsigned char *s; size_t n;
    if (!BcStr(b, &s, &n)) return false;
    size_t c = n < cap - 1 ? n : cap - 1;
    memcpy(key, s, c);
    key[c] = 0;
    return true;
}

static void CleanPart(char *s)
{
    for (; *s; s++) if (*s == '/' || *s == '\\' || (unsigned char)*s < 32) *s = '_';
}

/* `b` is at the info dictionary's 'd'. Keys are sorted, so "files" comes before "name": remember where it is and read it last. */
static int BcInfo(Bc *b, ShsDetail *d)
{
    if (b->p >= b->end || *b->p != 'd') return -1;
    b->p++;
    char name[200] = "";
    long long length = -1;
    Bc files = { 0 };
    bool haveFiles = false;
    while (b->p < b->end && *b->p != 'e') {
        char key[32];
        if (!BcPeekKey(b, key, sizeof key)) return -1;
        if (!strcmp(key, "name")) {
            const unsigned char *s; size_t n;
            if (!BcStr(b, &s, &n)) return -1;
            size_t c = n < sizeof name - 1 ? n : sizeof name - 1;
            memcpy(name, s, c);
            name[c] = 0;
            CleanPart(name);
        } else if (!strcmp(key, "length")) {
            if (!BcInt(b, &length)) return -1;
        } else if (!strcmp(key, "files")) {
            files = *b;
            haveFiles = true;
            if (!BcSkip(b, 1)) return -1;
        } else if (!BcSkip(b, 1)) return -1;
    }
    if (haveFiles) {
        if (files.p >= files.end || *files.p != 'l') return -1;
        files.p++;
        while (files.p < files.end && *files.p != 'e') {
            if (*files.p != 'd') return -1;
            files.p++;
            long long len = 0;
            char path[240] = "";
            bool havePath = false;
            while (files.p < files.end && *files.p != 'e') {
                char key[32];
                if (!BcPeekKey(&files, key, sizeof key)) return -1;
                if (!strcmp(key, "length")) { if (!BcInt(&files, &len)) return -1; }
                else if (!strcmp(key, "path") && files.p < files.end && *files.p == 'l') {
                    files.p++;
                    snprintf(path, sizeof path, "%s", name);
                    while (files.p < files.end && *files.p != 'e') {
                        const unsigned char *s; size_t n;
                        if (!BcStr(&files, &s, &n)) return -1;
                        char part[160];
                        size_t c = n < sizeof part - 1 ? n : sizeof part - 1;
                        memcpy(part, s, c);
                        part[c] = 0;
                        CleanPart(part);
                        size_t l = strlen(path);
                        snprintf(path + l, sizeof path - l, "%s%s", l ? "/" : "", part);
                    }
                    if (files.p >= files.end) return -1;
                    files.p++;
                    havePath = true;
                } else if (!BcSkip(&files, 2)) return -1;
            }
            if (files.p >= files.end) return -1;
            files.p++;
            if (havePath) AddFile(d, path, len);
        }
    } else if (length >= 0 && name[0]) AddFile(d, name, length);
    return d->fileCount > 0 ? d->fileCount : -1;
}

int ShsParseTorrent(const unsigned char *data, size_t len, ShsDetail *d)
{
    Bc b = { data, data + len };
    if (len < 2 || *b.p != 'd') return -1;
    b.p++;
    while (b.p < b.end && *b.p != 'e') {
        if (BcKey(&b, "info")) return BcInfo(&b, d);
        if (!BcSkip(&b, 1)) return -1;
    }
    return -1;
}

static ShsDetail g_det;                      /* the latest answer, under g_lock */
static unsigned  g_detGen = 0, g_detVer = 0;

typedef struct { unsigned gen; long long tid; char hash[41]; } DetJob;

static THREAD_RET DetailWorker(ThreadArg arg)
{
    DetJob *job = (DetJob *)arg;
    ShsDetail *d = (ShsDetail *)calloc(1, sizeof *d);
    bool ok = false;
    if (d) {
        CopyStr(d->hash, sizeof d->hash, job->hash);
        char url[200];
        if (job->tid > 0) {                              /* the Pirate Bay keeps file lists of its own */
            snprintf(url, sizeof url, "https://apibay.org/f.php?id=%lld", job->tid);
            char *body = ShsHttpGet(url, 2 * 1024 * 1024, NULL);
            if (body && ShsParseTpbFiles(body, d) > 0) ok = true;
            free(body);
        }
        if (!ok) {                                       /* any torrent: a .torrent cache keyed by the info-hash */
            char up[41];
            for (int i = 0; i < 41; i++) up[i] = (char)toupper((unsigned char)job->hash[i]);
            static const char *const caches[] = { "https://itorrents.net/torrent/%s.torrent", "https://itorrents.org/torrent/%s.torrent" };
            for (size_t c = 0; c < sizeof caches / sizeof caches[0] && !ok; c++) {
                snprintf(url, sizeof url, caches[c], up);
                size_t len = 0;
                char *body = ShsHttpGet(url, 8 * 1024 * 1024, &len);
                d->fileCount = d->shown = 0;
                d->total = 0;
                if (body && ShsParseTorrent((const unsigned char *)body, len, d) > 0) ok = true;
                free(body);
            }
        }
        d->state = ok ? SHS_STATE_OK : SHS_STATE_FAILED;
    }
    LOCK(&g_lock);
    if (job->gen == g_detGen) {
        if (d) g_det = *d;
        else g_det.state = SHS_STATE_FAILED;
        g_detVer++;
    }
    UNLOCK(&g_lock);
    free(d);
    free(job);
    THREAD_END;
}

void ShsDetailStart(const ShsResult *r)
{
    EnsureLock();
    DetJob *job = (DetJob *)calloc(1, sizeof *job);
    if (!job) return;
    CopyStr(job->hash, sizeof job->hash, r->hash);
    job->tid = r->tid;
    LOCK(&g_lock);
    job->gen = ++g_detGen;
    memset(&g_det, 0, sizeof g_det);
    CopyStr(g_det.hash, sizeof g_det.hash, r->hash);
    g_det.state = SHS_STATE_RUNNING;
    g_detVer++;
    UNLOCK(&g_lock);
    if (!Spawn(DetailWorker, job)) {
        LOCK(&g_lock); g_det.state = SHS_STATE_FAILED; g_detVer++; UNLOCK(&g_lock);
        free(job);
    }
}

unsigned ShsDetailVersion(void)
{
    EnsureLock();
    LOCK(&g_lock);
    unsigned v = g_detVer;
    UNLOCK(&g_lock);
    return v;
}

void ShsDetailGet(ShsDetail *out)
{
    EnsureLock();
    LOCK(&g_lock);
    *out = g_det;
    UNLOCK(&g_lock);
}

/* ---- saving the .torrent file ------------------------------------------------------------------------------------- */

static atomic_int g_saveState = 0;               /* 0 idle, 1 running, 2 done, 3 failed (2 and 3 are reported once by the poll) */
static char       g_savePath[600];
static char       g_saveHash[41];

static THREAD_RET SaveWorker(ThreadArg arg)
{
    (void)arg;
    char up[41], url[200];
    for (int i = 0; i < 41; i++) up[i] = (char)toupper((unsigned char)g_saveHash[i]);
    static const char *const caches[] = { "https://itorrents.net/torrent/%s.torrent", "https://itorrents.org/torrent/%s.torrent" };
    bool ok = false;
    for (size_t c = 0; c < sizeof caches / sizeof caches[0] && !ok; c++) {
        snprintf(url, sizeof url, caches[c], up);
        size_t len = 0;
        char *body = ShsHttpGet(url, 8 * 1024 * 1024, &len);
        ShsDetail *probe = body ? (ShsDetail *)calloc(1, sizeof *probe) : NULL;
        if (body && probe && ShsParseTorrent((const unsigned char *)body, len, probe) > 0) {     /* only a real .torrent is written */
            FILE *f = fopen(g_savePath, "wb");
            if (f) { ok = fwrite(body, 1, len, f) == len; if (fclose(f) != 0) ok = false; }
        }
        free(probe);
        free(body);
    }
    atomic_store(&g_saveState, ok ? 2 : 3);
    THREAD_END;
}

bool ShsTorrentSaveStart(const ShsResult *r, const char *path)
{
    int idle = 0, done2 = 2, done3 = 3;
    if (!atomic_compare_exchange_strong(&g_saveState, &idle, 1) &&
        !atomic_compare_exchange_strong(&g_saveState, &done2, 1) &&
        !atomic_compare_exchange_strong(&g_saveState, &done3, 1)) return false;           /* one save at a time */
    CopyStr(g_saveHash, sizeof g_saveHash, r->hash);
    CopyStr(g_savePath, sizeof g_savePath, path);
    if (!Spawn(SaveWorker, NULL)) { atomic_store(&g_saveState, 0); return false; }
    return true;
}

int ShsTorrentSavePoll(char *path, size_t cap)
{
    int s = atomic_load(&g_saveState);
    if (s != 2 && s != 3) return 0;
    if (path) CopyStr(path, cap, g_savePath);
    atomic_store(&g_saveState, 0);
    return s == 2 ? 1 : -1;
}
