#ifndef SHS_SEARCH_ENGINE_H
#define SHS_SEARCH_ENGINE_H

#include <stdbool.h>
#include <stddef.h>

/* The torrent sites that are queried. Each one is a public search endpoint that returns the info-hash; the magnet link is built here. */
enum { SHS_SRC_TPB = 0, SHS_SRC_YTS = 1, SHS_SRC_NYAA = 2 };   /* sources with their own parser; the rest are scraped (see ShsParseHtml) */
#define SHS_SRC_COUNT 31
#define SHS_SRC_ALL ((1u << SHS_SRC_COUNT) - 1u)

typedef struct {
    char      name[200];
    char      hash[41];            /* 40 hex digits, lower case */
    char      detail[40];          /* quality / uploader / category, may be empty */
    int       source;              /* ShsSource */
    int       seeders, leechers;
    long long size;                /* bytes, 0 = unknown */
    long long added;               /* unix time, 0 = unknown */
    long long tid;                 /* the site's own torrent id (Pirate Bay), 0 = none */
    int       files;               /* file count when the site says so, 0 = unknown */
} ShsResult;

typedef enum { SHS_STATE_IDLE, SHS_STATE_RUNNING, SHS_STATE_OK, SHS_STATE_FAILED } ShsSourceState;

#define SHS_MAX_RESULTS 400

const char *ShsSourceName(int src);
unsigned    ShsSourceDefaultMask(void);                             /* sources that are on until the user changes it */

/* ---- search (background threads, one per source) ---- */
/* Starts a new search over the sources in `mask` (bit per ShsSource); an unfinished earlier search is abandoned. */
void ShsSearchStart(const char *query, unsigned mask);
unsigned       ShsSearchVersion(void);                              /* changes whenever results or states change */
int            ShsSearchCopy(ShsResult *out, int max);              /* snapshot of the merged results; returns the count */
ShsSourceState ShsSearchState(int src, int *count);                 /* per source: state and how many results it gave */
bool           ShsSearchBusy(void);

/* ---- helpers ---- */
void ShsMagnet(const ShsResult *r, char *out, size_t cap);          /* magnet:?xt=urn:btih:...&dn=...&tr=... */
void ShsFormatSize(long long bytes, char *out, size_t cap);         /* "1.4 GB", "-" when unknown */
void ShsFormatAge(long long then, long long now, char *out, size_t cap);   /* "3d ago", "-" when unknown */

/* ---- parsers (exposed for the tests; each returns how many results it wrote, at most `max`) ---- */
int ShsParseTpb(const char *body, ShsResult *out, int max);
int ShsParseYts(const char *body, ShsResult *out, int max);
int ShsParseNyaa(const char *body, ShsResult *out, int max);
int ShsParseHtml(const char *body, int src, ShsResult *out, int max);   /* any other site: magnet links and 40-digit hashes in links */
int ShsParseTorrentsCsv(const char *body, ShsResult *out, int max);   /* torrents-csv.com */
int ShsParseBitSearch(const char *body, ShsResult *out, int max);     /* bitsearch.to / solidtorrents.to */
int ShsParseKnaben(const char *body, ShsResult *out, int max);        /* api.knaben.org */


/* ---- search suggestions (online, like a search box drop-down) ---- */
#define SHS_SUGGEST_MAX 8
#define SHS_SUGGEST_LEN 160
void     ShsSuggestStart(const char *query);       /* asks in the background; a newer call abandons an older one */
unsigned ShsSuggestVersion(void);                  /* changes when an answer arrives */
/* The latest answer: the query it belongs to and its suggestions. Returns the count. */
int      ShsSuggestGet(char *forQuery, size_t cap, char (*out)[SHS_SUGGEST_LEN], int max);
int      ShsParseSuggest(const char *body, char (*out)[SHS_SUGGEST_LEN], int max);   /* ["query", ["a", "b", ...]]; -1 when not that */


/* ---- torrent details: the file list of one torrent ---- */
#define SHS_DETAIL_FILES 300
typedef struct { char path[240]; long long size; } ShsFile;
typedef struct {
    char           hash[41];
    ShsSourceState state;          /* RUNNING while loading, FAILED when no list could be found */
    int            fileCount;      /* all files in the torrent */
    int            shown;          /* how many of them are in `files` (the first SHS_DETAIL_FILES) */
    long long      total;          /* bytes of all files */
    ShsFile        files[SHS_DETAIL_FILES];
} ShsDetail;

void     ShsDetailStart(const ShsResult *r);       /* loads the file list in the background (replaces an earlier request) */
unsigned ShsDetailVersion(void);
void     ShsDetailGet(ShsDetail *out);             /* latest answer; check ->hash against the card that is open */
int      ShsParseTpbFiles(const char *body, ShsDetail *d);                           /* apibay f.php; -1 when not that */
int      ShsParseTorrent(const unsigned char *data, size_t len, ShsDetail *d);       /* .torrent file contents; -1 when not valid */
const char *ShsTpbCategory(int code);                                                 /* "HD Movies" etc. */


/* ---- save the .torrent file (fetched from a .torrent cache by info-hash, in the background; one at a time) ---- */
bool ShsTorrentSaveStart(const ShsResult *r, const char *path);   /* false when a save is still running */
int  ShsTorrentSavePoll(char *path, size_t cap);                  /* 0 while running / idle, 1 saved, -1 not found (reported once) */

#endif /* SHS_SEARCH_ENGINE_H */
