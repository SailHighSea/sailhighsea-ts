#ifndef SHS_JSON_LITE_H
#define SHS_JSON_LITE_H

#include <stdbool.h>

/* Small JSON reader for the search sources: parses a whole document into a tree (depth limited), no writing. */
typedef enum { SHS_JSON_NULL, SHS_JSON_BOOL, SHS_JSON_NUM, SHS_JSON_STR, SHS_JSON_ARR, SHS_JSON_OBJ } ShsJsonType;

typedef struct ShsJson {
    ShsJsonType     type;
    char           *key;       /* member name when this node sits in an object */
    char           *str;       /* STR value */
    double          num;       /* NUM value, or 0/1 for BOOL */
    struct ShsJson *child;     /* first element / member */
    struct ShsJson *next;      /* next sibling */
} ShsJson;

ShsJson *ShsJsonParse(const char *text);                            /* NULL on a syntax error */
void     ShsJsonFree(ShsJson *root);
const ShsJson *ShsJsonGet(const ShsJson *obj, const char *key);     /* member of an object, or NULL */
/* Value as text / number even when the source quoted the other way ("12" vs 12). Missing member gives the fallback. */
const char *ShsJsonStr(const ShsJson *obj, const char *key, const char *fallback);
double      ShsJsonNum(const ShsJson *obj, const char *key, double fallback);

#endif /* SHS_JSON_LITE_H */
