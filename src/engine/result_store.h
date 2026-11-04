#ifndef SHS_RESULT_STORE_H
#define SHS_RESULT_STORE_H

#include "search_engine.h"

/* Favourites (saved results) and search history, each kept in a small text file next to the program. */
#define SHS_FAV_MAX      500
#define SHS_HISTORY_MAX  50
#define SHS_QUERY_MAX    160

typedef struct {
    ShsResult items[SHS_FAV_MAX];
    int       count;
} ShsFavorites;

void ShsFavLoad(ShsFavorites *f, const char *path);
bool ShsFavSave(const ShsFavorites *f, const char *path);
int  ShsFavFind(const ShsFavorites *f, const char *hash);      /* index or -1 */
bool ShsFavAdd(ShsFavorites *f, const ShsResult *r);           /* false when full or already there; newest goes first */
void ShsFavRemove(ShsFavorites *f, int idx);

typedef struct {
    char queries[SHS_HISTORY_MAX][SHS_QUERY_MAX];              /* newest first */
    int  count;
} ShsHistory;

void ShsHistoryLoad(ShsHistory *h, const char *path);
bool ShsHistorySave(const ShsHistory *h, const char *path);
void ShsHistoryAdd(ShsHistory *h, const char *query);          /* moves a repeated query to the top */
void ShsHistoryRemove(ShsHistory *h, int idx);

#endif /* SHS_RESULT_STORE_H */
