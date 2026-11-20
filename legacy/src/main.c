#include "raylib.h"
#include "ui/shs_theme.h"
#include "ui/icon_data.h"
#include "ui/shs_icons.h"
#include "engine/search_engine.h"
#include "engine/result_store.h"
#include "platform/platform.h"
#include "platform/updater.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define SHS_APP_TITLE   "SailHighSea TorrentSearch"
#ifndef SHS_VERSION
#define SHS_VERSION     "0.0.1"   /* normally injected by CMake */
#endif
#define SHS_REPO_URL    "https://github.com/SailHighSea/sailhighsea-ts"
#define TITLEBAR_H      48
#define STATUSBAR_H     30
#define SIDEBAR_W       64      /* icon rail */

#define ROW_H           62.0f
#define ROW_GAP         8.0f

#if defined(_WIN32)
  #define ABOUT_OS "Windows"
#elif defined(__APPLE__)
  #define ABOUT_OS "macOS"
#else
  #define ABOUT_OS "Linux"
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  #define ABOUT_PLATFORM ABOUT_OS " arm64"
#else
  #define ABOUT_PLATFORM ABOUT_OS " x64"
#endif

typedef enum { NAV_SEARCH, NAV_FAVORITES, NAV_HISTORY, NAV_SETTINGS, NAV_ABOUT, NAV_COUNT } Nav;
typedef enum { SORT_SEEDERS, SORT_SIZE, SORT_NEWEST, SORT_NAME, SORT_COUNT } SortMode;

static const char *NAV_LABEL[NAV_COUNT] = { "Search", "Favorites", "History", "Settings", "About" };
static const char *SORT_LABEL[SORT_COUNT] = { "Sort by Seeders", "Sort by Size", "Sort by Newest", "Sort by Name" };

/* ---- application state -------------------------------------------------- */

static char          g_appDir[512];
static char          g_favFile[600], g_histFile[600], g_settingsFile[600];

static ShsFavorites  g_fav;
static ShsHistory    g_hist;

static unsigned      g_sourceMask = 0;                 /* which sites a search asks (set to the defaults at start-up) */
static SortMode      g_sort = SORT_SEEDERS;
static bool          g_checkUpdates = false;          /* look for a newer release on GitHub at start-up and once a day */
static bool          g_updateDev = false;             /* ...and also tell about dev (pre-release) builds */
static double        g_nextUpdateCheck = 0;
static bool          g_updateFound = false;
static char          g_updateTag[64], g_updateUrl[256];
static char          g_updateMsg[96];
static float         g_setScroll = 0, g_setMax = 0;   /* Settings page scroll offset and the most it can scroll */
static float         g_aboutScroll = 0, g_aboutMax = 0;   /* About page, same idea */
static bool          g_restart = false;               /* theme switch: start a fresh copy after closing */
static bool          g_block = false;                 /* something owns the mouse (scrolled-off widgets must not take clicks) */
static double        g_lastInput = 0;                 /* for the idle frame rate; bumped whenever something changes on its own */
static Texture2D     g_logo;

static ShsResult     g_res[SHS_MAX_RESULTS];          /* sorted copy of the current search results */
static int           g_resN = 0;
static unsigned      g_resVersion = (unsigned)-1;     /* version of the engine results the copy was taken from */
static bool          g_searched = false;              /* a search was started in this session */
static char          g_lastQuery[SHS_QUERY_MAX];

static char          g_toast[96];
static double        g_toastUntil = 0;

typedef struct { float scroll, target; int open; } ListView;     /* scroll position and the expanded row (-1 = none) */
static ListView      g_searchView = { 0, 0, -1 }, g_favView = { 0, 0, -1 };

static Vector2 Mouse(void)   { return g_block ? (Vector2){ -10000.0f, -10000.0f } : GetMousePosition(); }
static bool    Pressed(void) { return !g_block && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

static void Toast(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_toast, sizeof g_toast, fmt, ap);
    va_end(ap);
    g_toastUntil = GetTime() + 4.0;
    g_lastInput = GetTime();
}

static void AppPath(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", g_appDir, name);
}

/* ---- drawing helpers (same look as SimpleSync) -------------------------- */

static float RoundNess(Rectangle r, float radius)
{
    float m = r.width < r.height ? r.width : r.height;
    return m > 0 ? fminf(1.0f, 2.0f * radius / m) : 0.0f;
}

static void FillRound(Rectangle r, float radius, Color c)
{
    if (radius < 0.5f) DrawRectangleRec(r, c);
    else               ShsRoundRec(r, RoundNess(r, radius), 16, c);
}

static void FillRoundTop(Rectangle r, float radius, Color c)
{
    FillRound(r, radius, c);
    if (radius >= 0.5f) DrawRectangleRec((Rectangle){ r.x, r.y + r.height - radius, r.width, radius }, c);
}

static void FillRoundBottom(Rectangle r, float radius, Color c)
{
    FillRound(r, radius, c);
    if (radius >= 0.5f) DrawRectangleRec((Rectangle){ r.x, r.y, r.width, radius }, c);
}

static bool ButtonI(Rectangle r, const char *label, ShsIcon icon, bool primary, bool active)
{
    bool hot = CheckCollisionPointRec(Mouse(), r);
    Color bg = primary ? (hot ? SHS_COLOR_PRIMARY_HOVER : SHS_COLOR_PRIMARY)
                       : (active ? SHS_COLOR_PRIMARY : (hot ? SHS_COLOR_CARD_HOVER : SHS_COLOR_CARD));
    ShsDrawButtonBox(r, 9.0f, bg, (primary || active) ? bg : SHS_COLOR_BORDER, active || (hot && !g_block && IsMouseButtonDown(MOUSE_BUTTON_LEFT)));
    Color ink = (primary || active) ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN;

    int tw = ShsMeasureText(label, 15);
    float iconW = icon != SHS_ICON_NONE ? 16.0f : 0.0f;
    float gap = (icon != SHS_ICON_NONE && tw > 0) ? 8.0f : 0.0f;
    float x = r.x + (r.width - (iconW + gap + (float)tw)) / 2;
    if (icon != SHS_ICON_NONE) ShsDrawIcon(icon, x + 8.0f, r.y + r.height / 2, 16.0f, ink);
    ShsDrawText(label, (int)(x + iconW + gap), (int)(r.y + (r.height - 15) / 2), 15, ink);
    return hot && Pressed();
}

static bool Button(Rectangle r, const char *label, bool primary, bool active)
{
    return ButtonI(r, label, SHS_ICON_NONE, primary, active);
}

static int PrevCp(const char *s, int i)          /* byte index of the character before i */
{
    do { i--; } while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80);
    return i < 0 ? 0 : i;
}

static int NextCp(const char *s, int i)          /* byte index of the character after i */
{
    if (!s[i]) return i;
    i++;
    while (s[i] && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

/* Text editing at a cursor (byte offset *cur) with an optional selection (from *anchor to *cur; equal = none).
   Typed characters, paste, copy / cut, Backspace, Delete, Left, Right, Home, End (Shift extends the selection), Ctrl+A. UTF-8 aware. */
static bool HasSel(int cur, int anchor) { return anchor != cur; }

static void DeleteSel(char *buf, int *cur, int *anchor)
{
    int a = *cur < *anchor ? *cur : *anchor, b = *cur < *anchor ? *anchor : *cur;
    memmove(buf + a, buf + b, strlen(buf + b) + 1);
    *cur = *anchor = a;
}

static void InsertText(char *buf, size_t cap, int *cur, int *anchor, const char *s, int n)
{
    if (HasSel(*cur, *anchor)) DeleteSel(buf, cur, anchor);
    int len = (int)strlen(buf);
    if (n <= 0 || (size_t)len + (size_t)n >= cap) return;
    memmove(buf + *cur + n, buf + *cur, (size_t)(len - *cur) + 1);
    memcpy(buf + *cur, s, (size_t)n);
    *cur += n;
    *anchor = *cur;
}

static void EditText(char *buf, size_t cap, int *cur, int *anchor)
{
    int len = (int)strlen(buf);
    if (*cur > len) *cur = len;
    if (*cur < 0) *cur = 0;
    if (*anchor > len || *anchor < 0) *anchor = *cur;
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (ctrl && IsKeyPressed(KEY_A)) { *anchor = 0; *cur = len; }
    if (ctrl && (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_X)) && HasSel(*cur, *anchor)) {
        int a = *cur < *anchor ? *cur : *anchor, b = *cur < *anchor ? *anchor : *cur;
        char tmp[SHS_QUERY_MAX];
        snprintf(tmp, sizeof tmp, "%.*s", b - a, buf + a);
        SetClipboardText(tmp);
        if (IsKeyPressed(KEY_X)) DeleteSel(buf, cur, anchor);
    }
    int c;
    while ((c = GetCharPressed()) != 0) {
        if (c < 32 || c == 127 || ctrl) continue;
        int n = 0;
        const char *u = CodepointToUTF8(c, &n);
        InsertText(buf, cap, cur, anchor, u, n);
    }
    len = (int)strlen(buf);
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (HasSel(*cur, *anchor)) DeleteSel(buf, cur, anchor);
        else if (*cur > 0) { *anchor = PrevCp(buf, *cur); DeleteSel(buf, cur, anchor); }
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
        if (HasSel(*cur, *anchor)) DeleteSel(buf, cur, anchor);
        else if (*cur < (int)strlen(buf)) { *anchor = NextCp(buf, *cur); DeleteSel(buf, cur, anchor); }
    }
    len = (int)strlen(buf);
    int moved = -1;                                  /* new cursor position when a movement key was used */
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        if (HasSel(*cur, *anchor) && !shift) moved = *cur < *anchor ? *cur : *anchor;
        else moved = PrevCp(buf, *cur);
    }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        if (HasSel(*cur, *anchor) && !shift) moved = *cur > *anchor ? *cur : *anchor;
        else moved = NextCp(buf, *cur);
    }
    if (IsKeyPressed(KEY_HOME)) moved = 0;
    if (IsKeyPressed(KEY_END))  moved = len;
    if (moved >= 0) { *cur = moved; if (!shift) *anchor = moved; }
    if (ctrl && IsKeyPressed(KEY_V)) {
        const char *cb = GetClipboardText();
        char clean[SHS_QUERY_MAX];
        size_t k = 0;
        for (; cb && *cb && k + 1 < sizeof clean; cb++) if (*cb != '\r' && *cb != '\n' && *cb != '\t') clean[k++] = *cb;
        InsertText(buf, cap, cur, anchor, clean, (int)k);
    }
}

/* Width in pixels of text[from, to). */
static int MeasureSpan(const char *text, int from, int to)
{
    char tmp[SHS_QUERY_MAX + 8];
    int n = to - from;
    if (n <= 0) return 0;
    if (n > (int)sizeof tmp - 1) n = (int)sizeof tmp - 1;
    memcpy(tmp, text + from, (size_t)n);
    tmp[n] = 0;
    return ShsMeasureText(tmp, 15);
}

static float g_fieldTextX = 0;                   /* where the search box's text starts (pixels) and which byte is the first one shown; set when it is drawn */
static int   g_fieldStart = 0;

/* Byte offset in `text` nearest to the pixel column x of the search box. */
static int FieldIndexAt(const char *text, float x)
{
    const int len = (int)strlen(text);
    int best = g_fieldStart;
    float bestD = 1e9f;
    for (int i = g_fieldStart; i <= len; i = (i < len ? NextCp(text, i) : len + 1)) {
        float d = fabsf(g_fieldTextX + (float)MeasureSpan(text, g_fieldStart, i) - x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

static bool IsWordByte(unsigned char c) { return isalnum(c) || c >= 0x80 || c == '_'; }

static void WordAt(const char *text, int idx, int *from, int *to)       /* the word around idx (or the run of spaces / symbols) */
{
    const int len = (int)strlen(text);
    if (!len) { *from = *to = 0; return; }
    const int pos = idx < len ? idx : len - 1;
    const bool word = IsWordByte((unsigned char)text[pos]);
    int a = pos, b = pos + 1;
    while (a > 0 && IsWordByte((unsigned char)text[a - 1]) == word) a--;
    while (b < len && IsWordByte((unsigned char)text[b]) == word) b++;
    *from = a;
    *to = b;
}

static void DrawFieldEx(Rectangle r, const char *text, const char *placeholder, bool focused, ShsIcon icon, bool onTitle, int cursor, int anchor)
{
    const bool tint = onTitle && !ShsThemeClassic();         /* retro themes keep a white field with dark text on the title bar */
    const Color ink    = tint ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN;
    const Color muted  = tint ? Fade(SHS_COLOR_ON_PRIMARY, 0.65f) : SHS_COLOR_TEXT_MUTED;
    if (tint) {
        ShsRoundRec(r, 0.5f, 16, Fade(SHS_COLOR_ON_PRIMARY, focused ? 0.20f : 0.12f));
        if (focused) ShsRoundLines(r, 0.5f, 16, 1.5f, Fade(SHS_COLOR_ON_PRIMARY, 0.75f));
    } else {
        ShsDrawRoundedBox(r, 9.0f, ShsThemeClassic() ? SHS_COLOR_CARD : SHS_COLOR_BG, focused ? SHS_COLOR_PRIMARY : SHS_COLOR_BORDER);
    }
    const int pad = icon != SHS_ICON_NONE ? 38 : 12;
    if (icon != SHS_ICON_NONE)
        ShsDrawIcon(icon, r.x + 20, r.y + r.height / 2, 16.0f, tint ? ink : (focused ? SHS_COLOR_PRIMARY : muted));
    const int avail = (int)r.width - pad - 12;
    const int len = (int)strlen(text);
    if (cursor < 0 || cursor > len) cursor = len;
    if (anchor < 0 || anchor > len) anchor = cursor;
    int start = 0;                                           /* scroll the text so that the cursor stays visible */
    while (start < cursor && MeasureSpan(text, start, cursor) > avail) start = NextCp(text, start);
    g_fieldTextX = r.x + pad;
    g_fieldStart = start;
    int ty = (int)(r.y + (r.height - 15) / 2);
    if (focused && anchor != cursor) {                       /* selection highlight */
        int a = cursor < anchor ? cursor : anchor, b = cursor < anchor ? anchor : cursor;
        if (a < start) a = start;
        if (b > a) {
            float sx = g_fieldTextX + (float)MeasureSpan(text, start, a), ex = g_fieldTextX + (float)MeasureSpan(text, start, b);
            if (ex > g_fieldTextX + avail) ex = g_fieldTextX + avail;
            DrawRectangle((int)sx, ty - 3, (int)(ex - sx), 21, tint ? Fade(SHS_COLOR_ON_PRIMARY, 0.38f) : Fade(SHS_COLOR_PRIMARY, 0.35f));
        }
    }
    if (*text) {
        BeginScissorMode((int)r.x + pad, (int)r.y, avail + 4, (int)r.height);
        ShsDrawText(text + start, (int)r.x + pad, ty, 15, ink);
        EndScissorMode();
    } else ShsDrawText(placeholder, (int)r.x + pad, ty, 15, muted);
    if (focused && anchor == cursor && ((int)(GetTime() * 2) & 1))
        DrawRectangle((int)r.x + pad + MeasureSpan(text, start, cursor) + (cursor > start ? 1 : 0), ty - 1, 1, 17, ink);
}

static void DrawSwitch(Rectangle sw, bool on)
{
    if (ShsThemeClassic() && !ShsThemeFlat() && !ShsThemeBevel()) {      /* Mac Platinum, GNOME 2: a rounded square, not a pill */
        ShsDrawRoundedBox(sw, 4.0f, on ? SHS_COLOR_PRIMARY : SHS_COLOR_BORDER, (Color){ 0, 0, 0, 0 });
        ShsDrawRoundedBox((Rectangle){ sw.x + (on ? 24.0f : 3.0f), sw.y + 3, 17, 18 }, 3.0f, SHS_COLOR_ON_PRIMARY, (Color){ 0, 0, 0, 0 });
        return;
    }
    ShsRoundRec(sw, 1.0f, 16, on ? SHS_COLOR_PRIMARY : SHS_COLOR_BORDER);
    if (ShsThemeFlat() || ShsThemeBevel()) {
        Rectangle k = { sw.x + (on ? 24.0f : 3.0f), sw.y + 3, 17, 18 };
        if (ShsThemeBevel()) ShsDrawBevel(k, false, SHS_COLOR_BG);
        else                 DrawRectangleRec(k, SHS_COLOR_ON_PRIMARY);
    } else DrawCircleV((Vector2){ sw.x + (on ? 33.0f : 11.0f), sw.y + 12 }, 8.5f, SHS_COLOR_ON_PRIMARY);
}

/* Settings-page switch: label + hint, returns true when clicked (and flips *value). */
static bool Toggle(float x, float y, float w, const char *label, const char *hint, bool *value)
{
    Rectangle row = { x, y, w, 46 };
    Rectangle sw = { x, y + 3, 44, 24 };
    DrawSwitch(sw, *value);
    ShsDrawText(label, (int)x + 58, (int)y + 1, 16, SHS_COLOR_TEXT_MAIN);
    ShsDrawText(hint, (int)x + 58, (int)y + 23, 13, SHS_COLOR_TEXT_MUTED);
    if (CheckCollisionPointRec(Mouse(), row) && Pressed()) { *value = !*value; return true; }
    return false;
}

/* Copies `text` into `out`, cutting it (with "...") so that it fits `maxW` pixels at `size`. */
static void FitText(char *out, size_t cap, const char *text, int size, float maxW)
{
    snprintf(out, cap, "%s", text);
    if (ShsMeasureText(out, size) <= (int)maxW) return;
    size_t len = strlen(out);
    while (len > 1) {
        do { len--; } while (len && ((unsigned char)out[len] & 0xC0) == 0x80);
        if (len + 3 >= cap) continue;
        out[len] = '.'; out[len + 1] = '.'; out[len + 2] = '.'; out[len + 3] = 0;
        if (ShsMeasureText(out, size) <= (int)maxW) return;
    }
}

/* Page tab with the divider line under it, as on SimpleSync's Tasks page. */
static void DrawPageHeader(float px, float py, float pw, const char *label)
{
    float tw = (float)ShsMeasureText(label, 17) + 28;
    Rectangle activeTabR = { 0 };
    Color tabInk = SHS_COLOR_PRIMARY;
    int tabTextY = (int)py + 9;
    if (ShsThemeFolderTabs() || ShsThemeBevel()) {
        Rectangle bt = { px, py + 1, tw, 40 };
        tabInk = ShsDrawTab(bt, true);
        activeTabR = bt;
    }
    ShsDrawText(label, (int)(px + 14), tabTextY, 17, tabInk);
    if (!ShsThemeFolderTabs() && !ShsThemeBevel()) DrawRectangle((int)px, (int)py + 36, (int)tw, 3, SHS_COLOR_PRIMARY);
    DrawRectangle((int)px, (int)py + 38, (int)pw, 1, SHS_COLOR_BORDER);
    if (ShsThemeBevel()) DrawRectangle((int)px, (int)py + 39, (int)pw, 1, WHITE);
    if (activeTabR.width > 0) DrawRectangle((int)activeTabR.x + 2, (int)py + 38, (int)activeTabR.width - 4, 3, ShsThemeTabFill());
}

/* Thin scroll bar at the right edge of a page that scrolls inside [top, bottom]. */
static void DrawPageScrollbar(float scroll, float maxScroll, float top, float bottom, int W)
{
    if (maxScroll <= 0) return;
    const float trackH = bottom - top - 8;
    const float thumbH = fmaxf(30.0f, trackH * trackH / (trackH + maxScroll));
    const float thumbY = top + 4 + (trackH - thumbH) * (scroll / maxScroll);
    DrawRectangleRounded((Rectangle){ (float)W - 10, thumbY, 5, thumbH }, 1.0f, 4, Fade(SHS_COLOR_TEXT_MUTED, 0.55f));
}

/* ---- persistence -------------------------------------------------------- */

/* Settings file, one "key=value" per line: theme=..., sources=<bit mask>, sort=N, checkUpdates=0|1, updateDev=0|1. */
static void SaveSettings(void)
{
    FILE *f = fopen(g_settingsFile, "wb");
    if (!f) return;
    fprintf(f, "theme=%s\nsources=%u\nsort=%d\ncheckUpdates=%d\nupdateDev=%d\n",
            ShsThemeName(ShsThemeGetMode()), g_sourceMask, SHS_SRC_COUNT, (int)g_sort, g_checkUpdates ? 1 : 0, g_updateDev ? 1 : 0);
    fclose(f);
}

static void LoadSettings(void)
{
    int savedCount = 0;                                    /* how many sources the settings file knew about */
    char line[96];
    ShsThemeMode mode = SHS_THEME_AUTO;
    FILE *f = fopen(g_settingsFile, "rb");
    while (f && fgets(line, sizeof line, f)) {
        if (!strncmp(line, "theme=", 6)) {
            char nm[32] = "";
            sscanf(line + 6, "%31[a-z0-9]", nm);
            mode = ShsThemeFromName(nm);
        }
        else if (!strncmp(line, "sources=", 8))  { g_sourceMask = (unsigned)strtoul(line + 8, NULL, 10) & SHS_SRC_ALL; if (!savedCount) savedCount = 28; }   /* files from before sourceCount had 28 sources */
        else if (!strncmp(line, "sourceCount=", 12)) savedCount = atoi(line + 12);
        else if (!strncmp(line, "sort=", 5))     { int s = atoi(line + 5); if (s >= 0 && s < SORT_COUNT) g_sort = (SortMode)s; }
        else if (strstr(line, "checkUpdates=1")) g_checkUpdates = true;
        else if (strstr(line, "updateDev=1"))    g_updateDev = true;
    }
    if (f) fclose(f);
    for (int s = savedCount; savedCount && s < SHS_SRC_COUNT; s++)                /* sources added since the file was written start in their default state */
        if ((ShsSourceDefaultMask() >> s) & 1u) g_sourceMask |= 1u << s;
    ShsThemeSetMode(mode);
}

/* ---- search ------------------------------------------------------------- */

static SortMode g_sortKey = SORT_SEEDERS;          /* for the qsort callback */

static int ResultCmp(const void *pa, const void *pb)
{
    const ShsResult *a = (const ShsResult *)pa, *b = (const ShsResult *)pb;
    switch (g_sortKey) {
        case SORT_SIZE:   if (a->size != b->size) return a->size < b->size ? 1 : -1; break;
        case SORT_NEWEST: if (a->added != b->added) return a->added < b->added ? 1 : -1; break;
        case SORT_NAME:   { int c = strcmp(a->name, b->name); if (c) return c; } break;
        default:          if (a->seeders != b->seeders) return a->seeders < b->seeders ? 1 : -1; break;
    }
    return a->leechers < b->leechers ? 1 : (a->leechers > b->leechers ? -1 : 0);
}

static void ResortResults(void)
{
    g_sortKey = g_sort;
    qsort(g_res, (size_t)g_resN, sizeof g_res[0], ResultCmp);
    g_searchView.open = -1;
}

static void StartSearch(const char *query)
{
    char q[SHS_QUERY_MAX];
    snprintf(q, sizeof q, "%s", query);
    for (size_t n = strlen(q); n && q[n - 1] == ' '; ) q[--n] = 0;
    const char *s = q;
    while (*s == ' ') s++;
    if (!*s) return;
    if (!(g_sourceMask & SHS_SRC_ALL)) { Toast("Turn on at least one source in Settings"); return; }
    ShsSearchStart(s, g_sourceMask);
    ShsHistoryAdd(&g_hist, s);
    ShsHistorySave(&g_hist, g_histFile);
    snprintf(g_lastQuery, sizeof g_lastQuery, "%s", s);
    g_searched = true;
    g_resN = 0;
    g_resVersion = (unsigned)-1;
    g_searchView = (ListView){ 0, 0, -1 };
    g_lastInput = GetTime();
}

static void SyncResults(void)
{
    unsigned v = ShsSearchVersion();
    if (v == g_resVersion) return;
    g_resVersion = v;
    g_resN = ShsSearchCopy(g_res, SHS_MAX_RESULTS);
    ResortResults();
    g_lastInput = GetTime();
}

/* ---- search suggestions: recent searches first, then what the web suggests ---- */

static char g_sug[SHS_SUGGEST_MAX][SHS_QUERY_MAX];
static bool g_sugLocal[SHS_SUGGEST_MAX];               /* from the history (clock icon) rather than online */
static int  g_sugN = 0, g_sugSel = -1;
static bool g_sugOpen = false;
static bool g_sugPending = false;                      /* an online lookup is waiting for its pause to end */
static char g_sugPrev[SHS_QUERY_MAX] = "\x01";         /* the text the list was last built for */
static int  g_searchCur = 0, g_searchAnchor = 0;        /* cursor and selection anchor in the search box (byte offsets; equal = no selection) */

static bool ContainsNoCase(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return true;
    }
    return nl == 0;
}

static bool SugHas(const char *s)
{
    for (int i = 0; i < g_sugN; i++) if (!strcasecmp(g_sug[i], s)) return true;
    return false;
}

static void SugAdd(const char *s, bool local)
{
    if (g_sugN >= SHS_SUGGEST_MAX || !s[0] || SugHas(s)) return;
    snprintf(g_sug[g_sugN], SHS_QUERY_MAX, "%s", s);
    g_sugLocal[g_sugN++] = local;
}

static void RebuildSuggestions(const char *text)
{
    g_sugN = 0;
    g_sugSel = -1;
    const int localMax = text[0] ? 4 : 6;
    int local = 0;
    for (int i = 0; i < g_hist.count && local < localMax; i++)
        if (ContainsNoCase(g_hist.queries[i], text) && strcasecmp(g_hist.queries[i], text)) { SugAdd(g_hist.queries[i], true); local++; }
    if (text[0]) {
        char forQ[SHS_SUGGEST_LEN], list[SHS_SUGGEST_MAX][SHS_SUGGEST_LEN];
        int n = ShsSuggestGet(forQ, sizeof forQ, list, SHS_SUGGEST_MAX);
        if (!strcmp(forQ, text))
            for (int i = 0; i < n; i++) if (strcasecmp(list[i], text)) SugAdd(list[i], false);
    }
}

static void OpenMagnet(const ShsResult *r)
{
    char m[2000];
    ShsMagnet(r, m, sizeof m);
    if (ShsOpenUrl(m)) Toast("Magnet link sent to your torrent app");
    else { SetClipboardText(m); Toast("No torrent app handles magnet links - the link was copied instead"); }
}

static void CopyMagnet(const ShsResult *r)
{
    char m[2000];
    ShsMagnet(r, m, sizeof m);
    SetClipboardText(m);
    Toast("Magnet link copied");
}

/* Starts saving the .torrent file into the Downloads folder (the program's folder when there is none). */
static void SaveTorrent(const ShsResult *r)
{
    char dir[560] = "";
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (home) {
        snprintf(dir, sizeof dir, "%s/Downloads", home);
        if (!DirectoryExists(dir)) dir[0] = 0;
    }
    if (!dir[0]) snprintf(dir, sizeof dir, "%s", g_appDir);
    char name[120];
    size_t n = 0;
    for (const char *p = r->name; *p && n < sizeof name - 1; p++)            /* plain ASCII file name */
        name[n++] = (isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == '(' || *p == ')') ? *p : '_';
    name[n] = 0;
    if (n < 3) snprintf(name, sizeof name, "%s", r->hash);
    char path[760];
    snprintf(path, sizeof path, "%s/%s.torrent", dir, name);
    if (ShsTorrentSaveStart(r, path)) Toast("Fetching the .torrent file...");
    else Toast("Another .torrent file is still being fetched");
}

static void ToggleFavorite(const ShsResult *r)
{
    int i = ShsFavFind(&g_fav, r->hash);
    if (i >= 0) { ShsFavRemove(&g_fav, i); Toast("Removed from favorites"); }
    else if (ShsFavAdd(&g_fav, r)) Toast("Added to favorites");
    else { Toast("Favorites are full"); return; }
    ShsFavSave(&g_fav, g_favFile);
}

/* ---- update check ------------------------------------------------------- */

#define UPDATE_FIRST_S   8.0
#define UPDATE_EVERY_S   86400.0
static void UpdateCheckNow(bool manual)
{
    if (ShsUpdateCheckStart(g_updateDev)) { if (manual) snprintf(g_updateMsg, sizeof g_updateMsg, "Checking..."); }
}

static void UpdateTick(void)
{
    double now = GetTime();
    if (g_checkUpdates && now >= g_nextUpdateCheck && now >= UPDATE_FIRST_S) {
        g_nextUpdateCheck = now + UPDATE_EVERY_S;
        UpdateCheckNow(false);
    }
    char tag[64], url[256];
    int r = ShsUpdateCheckPoll(tag, sizeof tag, url, sizeof url);
    if (r == 0) return;
    g_lastInput = now;
    if (r == 1) {
        if (g_updateDev ? ShsVersionNewer(tag, SHS_VERSION) : ShsVersionCoreNewer(tag, SHS_VERSION)) {
            g_updateFound = true;
            snprintf(g_updateTag, sizeof g_updateTag, "%s", tag);
            snprintf(g_updateUrl, sizeof g_updateUrl, "%s", url);
            snprintf(g_updateMsg, sizeof g_updateMsg, "Version %s is available", tag);
        } else {
            g_updateFound = false;
            snprintf(g_updateMsg, sizeof g_updateMsg, "You are up to date (latest release: %s)", tag);
        }
    } else {
        snprintf(g_updateMsg, sizeof g_updateMsg, "Could not check for updates (offline, or no release yet)");
    }
}

/* ---- result list -------------------------------------------------------- */

/* ---- torrent details (file list of the open card) ---- */

#define TREE_MAX      600
#define TREE_VISIBLE  10
#define TREE_LINE_H   18

typedef struct { char text[200]; int depth; long long size; bool dir; } TreeLine;

static ShsDetail g_detUi;                        /* copy of the engine's latest answer; ->hash says which torrent it is for */
static unsigned  g_detSeen = 0;
static TreeLine  g_tree[TREE_MAX];
static int       g_treeN = 0, g_treeTop = 0;     /* lines, and the first one shown in the box */
static Rectangle g_treeBoxPrev = { 0 };          /* where the file box was drawn last frame (the wheel scrolls it instead of the list) */

/* Folders and files of the detail list, as indented lines in the order of the torrent. */
static void BuildTree(void)
{
    g_treeN = 0;
    g_treeTop = 0;
    char prev[8][160];
    int prevN = 0;
    for (int i = 0; i < g_detUi.shown && g_treeN < TREE_MAX - 8; i++) {
        char path[240], parts[8][160];
        snprintf(path, sizeof path, "%s", g_detUi.files[i].path);
        int np = 0;
        for (char *p = path; p && np < 8; ) {
            char *slash = (np < 7) ? strchr(p, '/') : NULL;      /* whatever is left after 7 levels stays in the last part */
            if (slash) *slash = 0;
            snprintf(parts[np++], sizeof parts[0], "%s", p);
            p = slash ? slash + 1 : NULL;
        }
        int common = 0;
        while (common < np - 1 && common < prevN && !strcmp(parts[common], prev[common])) common++;
        for (int k = common; k < np - 1 && g_treeN < TREE_MAX; k++) {
            TreeLine *t = &g_tree[g_treeN++];
            snprintf(t->text, sizeof t->text, "%s", parts[k]);
            t->depth = k; t->size = 0; t->dir = true;
        }
        if (g_treeN < TREE_MAX) {
            TreeLine *t = &g_tree[g_treeN++];
            snprintf(t->text, sizeof t->text, "%s", parts[np - 1]);
            t->depth = np - 1; t->size = g_detUi.files[i].size; t->dir = false;
        }
        for (int k = 0; k < np - 1; k++) snprintf(prev[k], sizeof prev[0], "%s", parts[k]);
        prevN = np - 1;
    }
}

static void PollDetail(void)
{
    unsigned v = ShsDetailVersion();
    if (v == g_detSeen) return;
    g_detSeen = v;
    ShsDetailGet(&g_detUi);
    BuildTree();
    g_lastInput = GetTime();
}

static bool HaveDetail(const ShsResult *r) { return !strcmp(g_detUi.hash, r->hash); }

static void OpenCard(const ShsResult *r)             /* a card was expanded: ask for its file list */
{
    memset(g_detUi.hash, 0, sizeof g_detUi.hash);
    g_detUi.state = SHS_STATE_RUNNING;
    g_detUi.shown = g_detUi.fileCount = 0;
    g_treeN = g_treeTop = 0;
    ShsDetailStart(r);
    g_detSeen = (unsigned)-1;                        /* take the engine's "loading" state at the next frame */
}

#define CARD_OPEN_BASE  204.0f

static float RowHeight(const ShsResult *r, bool open)
{
    if (!open) return ROW_H;
    if (HaveDetail(r) && g_detUi.state == SHS_STATE_OK && g_treeN > 0)
        return CARD_OPEN_BASE + (float)(g_treeN < TREE_VISIBLE ? g_treeN : TREE_VISIBLE) * TREE_LINE_H + 30;      /* box bottom to card bottom = 16, like the sides */
    return CARD_OPEN_BASE;
}

/* Draws `items` as expandable cards inside `area` and handles scrolling and the card buttons. */
static void DrawResultList(Rectangle area, const ShsResult *items, int n, ListView *lv)
{
    const time_t now = time(NULL);

    float total = 0;
    for (int i = 0; i < n; i++) total += RowHeight(&items[i], i == lv->open) + ROW_GAP;
    const float maxScroll = fmaxf(0.0f, total - area.height);

    const bool inArea = CheckCollisionPointRec(Mouse(), area);
    const bool overTree = g_treeBoxPrev.width > 0 && CheckCollisionPointRec(Mouse(), g_treeBoxPrev) && g_treeN > TREE_VISIBLE;
    if (inArea && overTree) {                                     /* the wheel scrolls the file list while the mouse is over it */
        g_treeTop -= (int)(GetMouseWheelMove() * 3);
        if (g_treeTop > g_treeN - TREE_VISIBLE) g_treeTop = g_treeN - TREE_VISIBLE;
        if (g_treeTop < 0) g_treeTop = 0;
    } else if (inArea) lv->target -= GetMouseWheelMove() * 90.0f;
    if (lv->target > maxScroll) lv->target = maxScroll;
    if (lv->target < 0) lv->target = 0;
    lv->scroll += (lv->target - lv->scroll) * 0.35f;
    if (fabsf(lv->target - lv->scroll) < 0.5f) lv->scroll = lv->target;
    else g_lastInput = GetTime();
    Rectangle treeBoxNow = { 0 };

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    const bool prevBlock = g_block;
    if (!inArea) g_block = true;                                  /* cards scrolled out of view (or below the list) take no clicks */

    float y = area.y - lv->scroll;
    const float cw = area.width - 8;                               /* leave room for the scroll bar */
    int toggleRow = -2;
    for (int i = 0; i < n; i++) {
        const bool open = i == lv->open;
        const ShsResult *r = &items[i];
        const float h = RowHeight(r, open);
        if (y + h < area.y || y > area.y + area.height) { y += h + ROW_GAP; continue; }
        Rectangle card = { area.x, y, cw, h };
        Rectangle head = { card.x, card.y, card.width, ROW_H };
        const bool hot = CheckCollisionPointRec(Mouse(), card);
        ShsDrawRoundedBox(card, 12.0f, (hot && !open) ? SHS_COLOR_CARD_HOVER : SHS_COLOR_CARD, open ? SHS_COLOR_PRIMARY : SHS_COLOR_BORDER);

        /* buttons, right to left: favourite, copy magnet, open magnet */
        const float bs = 34, by = y + (ROW_H - bs) / 2;
        Rectangle openB = { card.x + card.width - 12 - bs, by, bs, bs };
        Rectangle copyB = { openB.x - 6 - bs, by, bs, bs };
        Rectangle favB  = { copyB.x - 6 - bs, by, bs, bs };
        Rectangle dlB   = { favB.x - 6 - bs, by, bs, bs };
        const bool isFav = ShsFavFind(&g_fav, r->hash) >= 0;
        bool onButton = CheckCollisionPointRec(Mouse(), openB) || CheckCollisionPointRec(Mouse(), copyB) || CheckCollisionPointRec(Mouse(), favB) || CheckCollisionPointRec(Mouse(), dlB);
        if (ButtonI(openB, "", SHS_ICON_MAGNET, true, false)) OpenMagnet(r);
        if (ButtonI(copyB, "", SHS_ICON_COPY, false, false)) CopyMagnet(r);
        if (ButtonI(dlB, "", SHS_ICON_DOWNLOAD, false, false)) SaveTorrent(r);
        if (ButtonI(favB, "", isFav ? SHS_ICON_STAR_FILLED : SHS_ICON_STAR, false, isFav)) { ToggleFavorite(r); if (items == g_fav.items) { lv->open = -1; break; } }

        /* seeders / leechers */
        const float statX = dlB.x - 12 - 108;
        char num[24];
        snprintf(num, sizeof num, "%d", r->seeders);
        ShsDrawText(num, (int)statX, (int)y + 12, 17, r->seeders > 0 ? SHS_COLOR_SUCCESS : SHS_COLOR_TEXT_MUTED);
        ShsDrawText("Seeders", (int)statX, (int)y + 36, 12, SHS_COLOR_TEXT_MUTED);
        snprintf(num, sizeof num, "%d", r->leechers);
        ShsDrawText(num, (int)statX + 58, (int)y + 12, 17, r->leechers > 0 ? SHS_COLOR_DANGER : SHS_COLOR_TEXT_MUTED);
        ShsDrawText("Leechers", (int)statX + 58, (int)y + 36, 12, SHS_COLOR_TEXT_MUTED);

        /* name and meta line */
        char nm[220], meta[200], size[24], age[24];
        FitText(nm, sizeof nm, r->name, 16, statX - card.x - 32);
        ShsDrawText(nm, (int)card.x + 16, (int)y + 11, 16, SHS_COLOR_TEXT_MAIN);
        ShsFormatSize(r->size, size, sizeof size);
        ShsFormatAge(r->added, (long long)now, age, sizeof age);
        snprintf(meta, sizeof meta, "%s   |   %s   |   %s%s%s", ShsSourceName(r->source), size, age, r->detail[0] ? "   |   " : "", r->detail);
        FitText(meta, sizeof meta, meta, 13, statX - card.x - 32);
        ShsDrawText(meta, (int)card.x + 16, (int)y + 36, 13, SHS_COLOR_TEXT_MUTED);

        if (open) {
            const float lx = card.x + 16, vx = card.x + 100, vw = card.width - 120;
            DrawRectangle((int)card.x + 14, (int)y + (int)ROW_H, (int)card.width - 28, 1, Fade(SHS_COLOR_BORDER, 0.7f));
            char fit[300], line[300];

            ShsDrawText("Info hash", (int)lx, (int)y + 76, 13, SHS_COLOR_TEXT_MUTED);
            ShsDrawText(r->hash, (int)vx, (int)y + 75, 14, SHS_COLOR_TEXT_MAIN);

            ShsDrawText("Name", (int)lx, (int)y + 98, 13, SHS_COLOR_TEXT_MUTED);
            FitText(fit, sizeof fit, r->name, 14, vw);
            ShsDrawText(fit, (int)vx, (int)y + 97, 14, SHS_COLOR_TEXT_MAIN);

            /* what is known about it: category and uploader, upload date, file count, size */
            char date[32] = "unknown";
            if (r->added > 0) { time_t t = (time_t)r->added; struct tm *tm = gmtime(&t); if (tm) strftime(date, sizeof date, "%Y-%m-%d", tm); }
            int nfiles = (HaveDetail(r) && g_detUi.state == SHS_STATE_OK) ? g_detUi.fileCount : r->files;
            char files[32] = "unknown";
            if (nfiles > 0) snprintf(files, sizeof files, "%d", nfiles);
            snprintf(line, sizeof line, "%s%sAdded %s   |   Files %s   |   Size %s   |   Source %s",
                     r->detail, r->detail[0] ? "   |   " : "", date, files, size, ShsSourceName(r->source));
            ShsDrawText("Details", (int)lx, (int)y + 122, 13, SHS_COLOR_TEXT_MUTED);
            FitText(fit, sizeof fit, line, 14, vw);
            ShsDrawText(fit, (int)vx, (int)y + 121, 14, SHS_COLOR_TEXT_MAIN);

            float bx = card.x + 16, byy = y + 146;
            if (ButtonI((Rectangle){ bx, byy, 140, 28 }, "Open magnet", SHS_ICON_MAGNET, true, false)) OpenMagnet(r);
            if (ButtonI((Rectangle){ bx + 148, byy, 140, 28 }, "Copy magnet", SHS_ICON_COPY, false, false)) CopyMagnet(r);
            if (ButtonI((Rectangle){ bx + 296, byy, 130, 28 }, "Copy hash", SHS_ICON_COPY, false, false)) { SetClipboardText(r->hash); Toast("Info hash copied"); }
            if (ButtonI((Rectangle){ bx + 434, byy, 160, 28 }, "Save .torrent", SHS_ICON_DOWNLOAD, false, false)) SaveTorrent(r);

            /* file list */
            const bool have = HaveDetail(r);
            if (have && g_detUi.state == SHS_STATE_OK && g_treeN > 0) {
                char hd[120], tot[24];
                ShsFormatSize(g_detUi.total, tot, sizeof tot);
                if (g_detUi.fileCount > g_detUi.shown) snprintf(hd, sizeof hd, "Files (%d, first %d shown)   |   %s in total", g_detUi.fileCount, g_detUi.shown, tot);
                else                                   snprintf(hd, sizeof hd, "Files (%d)   |   %s in total", g_detUi.fileCount, tot);
                ShsDrawText(hd, (int)lx, (int)y + 186, 14, SHS_COLOR_TEXT_MAIN);
                const int vis = g_treeN < TREE_VISIBLE ? g_treeN : TREE_VISIBLE;
                Rectangle box = { lx, y + 208, card.width - 32, (float)vis * TREE_LINE_H + 10 };
                treeBoxNow = box;
                ShsDrawRoundedBox(box, 8.0f, SHS_COLOR_BG, SHS_COLOR_BORDER);
                if (g_treeTop > g_treeN - vis) g_treeTop = g_treeN - vis;
                if (g_treeTop < 0) g_treeTop = 0;
                for (int k = 0; k < vis; k++) {
                    const TreeLine *t = &g_tree[g_treeTop + k];
                    const float ly = box.y + 5 + k * TREE_LINE_H, ix = box.x + 12 + t->depth * 18.0f;
                    char sz[24] = "";
                    if (!t->dir) ShsFormatSize(t->size, sz, sizeof sz);
                    const int szw = ShsMeasureText(sz, 13);
                    if (t->dir) ShsDrawIcon(SHS_ICON_FOLDER, ix + 7, ly + 9, 13.0f, SHS_COLOR_PRIMARY);
                    FitText(fit, sizeof fit, t->text, 13, box.width - (ix - box.x) - 30 - szw - 24);
                    ShsDrawText(fit, (int)ix + 18, (int)ly + 1, 13, t->dir ? SHS_COLOR_TEXT_MAIN : SHS_COLOR_TEXT_MUTED);
                    if (!t->dir) ShsDrawText(sz, (int)(box.x + box.width - 14 - szw - (g_treeN > TREE_VISIBLE ? 8 : 0)), (int)ly + 1, 13, SHS_COLOR_TEXT_MUTED);
                }
                if (g_treeN > TREE_VISIBLE) {                      /* scroll thumb for the file box */
                    float th = fmaxf(16.0f, (box.height - 8) * vis / (float)g_treeN);
                    float ty2 = box.y + 4 + (box.height - 8 - th) * ((float)g_treeTop / (float)(g_treeN - vis));
                    DrawRectangleRounded((Rectangle){ box.x + box.width - 9, ty2, 4, th }, 1.0f, 4, Fade(SHS_COLOR_TEXT_MUTED, 0.5f));
                }
            } else if (have && g_detUi.state == SHS_STATE_OK) {
                ShsDrawText("The file list is empty.", (int)lx, (int)y + 186, 14, SHS_COLOR_TEXT_MUTED);
            } else if (!have || g_detUi.state == SHS_STATE_RUNNING) {
                ShsDrawText("Loading the file list...", (int)lx, (int)y + 186, 14, SHS_COLOR_TEXT_MUTED);
            } else {
                snprintf(line, sizeof line, "The file list could not be found for this torrent.");
                ShsDrawText(line, (int)lx, (int)y + 186, 14, SHS_COLOR_TEXT_MUTED);
            }
        }

        if (Pressed() && CheckCollisionPointRec(Mouse(), head) && !onButton) toggleRow = i;
        y += h + ROW_GAP;
    }
    g_treeBoxPrev = treeBoxNow;
    if (toggleRow >= 0) {
        if (lv->open == toggleRow) lv->open = -1;
        else { lv->open = toggleRow; OpenCard(&items[toggleRow]); }
    }

    if (maxScroll > 0) {                                           /* scroll bar */
        float trackH = area.height, thumbH = fmaxf(30.0f, trackH * area.height / (total > 0 ? total : 1));
        float thumbY = area.y + (trackH - thumbH) * (lv->scroll / maxScroll);
        DrawRectangleRounded((Rectangle){ area.x + area.width - 5, thumbY, 4, thumbH }, 1.0f, 4, Fade(SHS_COLOR_TEXT_MUTED, 0.5f));
    }
    g_block = prevBlock;
    EndScissorMode();
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    snprintf(g_appDir, sizeof g_appDir, "%s", GetApplicationDirectory());
    for (size_t n = strlen(g_appDir); n > 1 && (g_appDir[n - 1] == '/' || g_appDir[n - 1] == '\\'); ) g_appDir[--n] = 0;
    AppPath(g_favFile, sizeof g_favFile, "torrentsearch-favorites.tsv");
    AppPath(g_histFile, sizeof g_histFile, "torrentsearch-history.txt");
    AppPath(g_settingsFile, sizeof g_settingsFile, "torrentsearch-settings.txt");

    {   /* square retro themes are chosen before the window exists: they turn anti-aliasing off and load smaller fonts */
        char ln[96];
        FILE *sf = fopen(g_settingsFile, "rb");
        while (sf && fgets(ln, sizeof ln, sf)) {
            if (!strncmp(ln, "theme=", 6)) {
                char nm[32] = "";
                sscanf(ln + 6, "%31[a-z0-9]", nm);
                if (ShsThemeFromName(nm) >= SHS_THEME_FLAT98) ShsThemeSetMode(ShsThemeFromName(nm));
            }
        }
        if (sf) fclose(sf);
    }

    /* Undecorated (custom title bar) + high-DPI aware; must be set before InitWindow. */
    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT |
                   (ShsThemeFlat() ? 0 : FLAG_MSAA_4X_HINT));
    InitWindow(1080, 650, SHS_APP_TITLE);
    Image appIcon = LoadImageFromMemory(".png", SHS_ICON_PNG, (int)sizeof SHS_ICON_PNG);
    if (appIcon.data) {
        SetWindowIcon(appIcon);
        if (ShsThemeFlat()) ImageResize(&appIcon, 128, 128);
        g_logo = LoadTextureFromImage(appIcon);
        SetTextureFilter(g_logo, TEXTURE_FILTER_BILINEAR);
        UnloadImage(appIcon);
    }
    if (!ShsThemeFlat()) ShsRoundWindowCorners(GetWindowHandle());
    SetWindowMinSize(860, 540);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    g_sourceMask = ShsSourceDefaultMask();
    ShsFontsInit();
    LoadSettings();
    ShsFavLoad(&g_fav, g_favFile);
    ShsHistoryLoad(&g_hist, g_histFile);

    Nav nav = NAV_SEARCH;
    bool searchFocus = false, shouldClose = false, dragging = false;   /* the box takes focus when it is clicked (so no suggestions pop up at start-up) */
    char search[SHS_QUERY_MAX] = "";
    Vector2 dragGrab = { 0 };
    bool typedOnly = false;                                /* --type= was used: fill the box, do not search */
    bool autoExpand = false;
    bool selDrag = false;                                   /* dragging a selection in the search box */
    double lastSearchClick = -1;                            /* for the double click */
    for (int i = 1; i < argc; i++)                         /* torrentsearch <words>: start with a search */
        if (!strcmp(argv[i], "--expand")) autoExpand = true;
        else if (!strncmp(argv[i], "--type=", 7)) { snprintf(search, sizeof search, "%s", argv[i] + 7); typedOnly = true; }   /* dev: fill the box without searching */
        else if (argv[i][0] != '-') {
            if (search[0]) strncat(search, " ", sizeof search - strlen(search) - 1);
            strncat(search, argv[i], sizeof search - strlen(search) - 1);
        }
    g_searchCur = g_searchAnchor = (int)strlen(search);
    if (search[0] && !typedOnly) StartSearch(search);

    while (!shouldClose && !WindowShouldClose()) {
        bool skipDraw = false;
        {   /* Frame rate follows activity: 60 while the user is interacting, 20 when nothing happens. */
            static int curFps = 60;
            static double lastDraw = 0;
            double nowT = GetTime();
            Vector2 md = GetMouseDelta();
            if (md.x != 0 || md.y != 0 || GetMouseWheelMove() != 0 || IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsKeyDown(KEY_BACKSPACE) || IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_TAB) ||
                IsKeyDown(KEY_ESCAPE) || IsKeyDown(KEY_LEFT_CONTROL) || GetKeyPressed() != 0 || IsWindowResized())
                g_lastInput = nowT;
            const bool busy = ShsSearchBusy();
            if (busy) g_lastInput = nowT;
            static unsigned sugSeen = 0;                       /* a suggestion answer arrived while idle: wake up to show it */
            unsigned sv = ShsSuggestVersion();
            if (sv != sugSeen || g_sugPending) { sugSeen = sv; g_lastInput = nowT; }
            int fps = nowT - g_lastInput < 1.0 ? 60 : 20;
            if (fps != curFps) { SetTargetFPS(fps); curFps = fps; }
            /* Nothing is moving and nobody is interacting: do not render at all (only once a minute so the "ago" texts stay fresh). */
            if (nowT - g_lastInput >= 1.0 && nowT - lastDraw < 60.0) skipDraw = true;
            else lastDraw = nowT;
        }

        const int W = GetScreenWidth(), H = GetScreenHeight();
        g_block = false;
        Vector2 mouse = Mouse();
        bool maximized = IsWindowMaximized();

        ShsThemeUpdate();
        ShsFontsCheckDpi();
        UpdateTick();
        SyncResults();
        PollDetail();
        {
            char sp[760];
            int sr = ShsTorrentSavePoll(sp, sizeof sp);
            if (sr == 1) Toast("Saved %s", sp);
            else if (sr == -1) Toast("No .torrent file found for this torrent (it is not in the torrent caches) - use the magnet link");
        }
        if (autoExpand && g_resN > 0 && !ShsSearchBusy()) { autoExpand = false; g_searchView.open = 0; OpenCard(&g_res[0]); }     /* dev: --expand opens the first result */

        if (skipDraw) {
            PollInputEvents();
            ShsSleepMs(10);
            continue;
        }

        BeginDrawing();
        ClearBackground(SHS_COLOR_BG);

        /* ================= title bar ================= */
        if (ShsThemeClassic()) {
            ShsDrawTitleBar(W, TITLEBAR_H);
            Color bk = ShsThemeTitleBack();
            if (bk.a) DrawRectangle(8, 8, ShsMeasureText(SHS_APP_TITLE, 19) + 54, TITLEBAR_H - 16, bk);
        } else FillRoundTop((Rectangle){ 0, 0, (float)W, TITLEBAR_H }, 0.0f, SHS_COLOR_TITLEBAR);
        if (g_logo.id)
            DrawTexturePro(g_logo, (Rectangle){ 0, 0, (float)g_logo.width, (float)g_logo.height },
                           (Rectangle){ 14, 10, 28, 28 }, (Vector2){ 0, 0 }, 0.0f, WHITE);
        const Color titleInk = ShsThemeClassic() ? ShsThemeTitleInk() : SHS_COLOR_ON_PRIMARY;
        ShsDrawText(SHS_APP_TITLE, 52, 14, 19, titleInk);
        if (ShsThemeFauxBold()) ShsDrawText(SHS_APP_TITLE, 53, 14, 19, titleInk);

        const float wbW = 36, wbH = 32, wbGap = 6, wbY = (TITLEBAR_H - wbH) / 2;
        Rectangle closeB = { (float)W - 12 - wbW, wbY, wbW, wbH };
        Rectangle maxB   = { closeB.x - wbGap - wbW, wbY, wbW, wbH };
        Rectangle minB   = { maxB.x - wbGap - wbW, wbY, wbW, wbH };

        float searchW = fminf(520.0f, fmaxf(200.0f, (float)W - 2 * 340.0f));
        Rectangle searchB = { ((float)W - searchW) / 2, wbY, searchW, wbH };
        DrawFieldEx(searchB, search, "Search torrents...", searchFocus, SHS_ICON_SEARCH, true, g_searchCur, g_searchAnchor);

        bool hMin = CheckCollisionPointRec(mouse, minB), hMax = CheckCollisionPointRec(mouse, maxB), hClose = CheckCollisionPointRec(mouse, closeB);
        const bool flat98 = ShsThemeClassic();
        if (flat98) {
            const Rectangle wbs[3] = { minB, maxB, closeB };
            const bool wbHot[3] = { hMin, hMax, hClose };
            for (int i = 0; i < 3; i++)
                ShsDrawButtonBox(wbs[i], ShsThemeFlat() ? 0.0f : 6.0f, (wbHot[i] && !ShsThemeBevel()) ? (i == 2 ? SHS_COLOR_DANGER : SHS_COLOR_CARD_HOVER) : SHS_COLOR_BG, SHS_COLOR_BORDER, wbHot[i] && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        } else {
            if (hMin)   ShsRoundRec(minB,   0.4f, 10, Fade(SHS_COLOR_ON_PRIMARY, 0.18f));
            if (hMax)   ShsRoundRec(maxB,   0.4f, 10, Fade(SHS_COLOR_ON_PRIMARY, 0.18f));
            if (hClose) ShsRoundRec(closeB, 0.4f, 10, SHS_COLOR_DANGER);
        }
        const Color wInk = flat98 ? ((hClose && !ShsThemeBevel()) ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN) : SHS_COLOR_ON_PRIMARY;
        const float mcx = minB.x + wbW / 2,   mcy = minB.y + wbH / 2;
        const float xcx = maxB.x + wbW / 2,   xcy = maxB.y + wbH / 2;
        const float ccx = closeB.x + wbW / 2, ccy = closeB.y + wbH / 2;
        DrawLineEx((Vector2){ mcx - 6, mcy + 3 }, (Vector2){ mcx + 6, mcy + 3 }, 2.0f, wInk);
        DrawRectangleLinesEx((Rectangle){ xcx - 6, xcy - 6, 12, 12 }, 2.0f, wInk);
        DrawLineEx((Vector2){ ccx - 6, ccy - 6 }, (Vector2){ ccx + 6, ccy + 6 }, 2.0f, wInk);
        DrawLineEx((Vector2){ ccx + 6, ccy - 6 }, (Vector2){ ccx - 6, ccy + 6 }, 2.0f, wInk);

        /* ---- search suggestions ---- */
        Rectangle sugR = { 0 };
        {
            static double fetchAt = 0;
            static unsigned ver = 0;
            const double nowS = GetTime();
            if (strcmp(g_sugPrev, search)) {                       /* the text changed: show what we know, ask the web after a short pause */
                snprintf(g_sugPrev, sizeof g_sugPrev, "%s", search);
                g_sugOpen = searchFocus;
                fetchAt = nowS + 0.25;
                g_sugPending = strlen(search) >= 2;
                RebuildSuggestions(search);
            }
            if (g_sugPending && nowS >= fetchAt) { ShsSuggestStart(search); g_sugPending = false; }
            unsigned v = ShsSuggestVersion();
            if (v != ver) { ver = v; RebuildSuggestions(search); g_lastInput = nowS; }
            const bool exact = g_sugN == 1 && !strcasecmp(g_sug[0], search);
            if (searchFocus && g_sugOpen && g_sugN > 0 && !exact)
                sugR = (Rectangle){ searchB.x, searchB.y + searchB.height + 6, searchB.width, (float)g_sugN * 34 + 8 };
            if (sugR.width > 0 && CheckCollisionPointRec(GetMousePosition(), sugR)) {
                g_block = true;                                    /* the drop-down owns the mouse: nothing underneath reacts */
                mouse = Mouse();
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    int row = (int)((GetMousePosition().y - sugR.y - 4) / 34);
                    if (row >= 0 && row < g_sugN) {
                        snprintf(search, sizeof search, "%s", g_sug[row]); g_searchCur = g_searchAnchor = (int)strlen(search);
                        snprintf(g_sugPrev, sizeof g_sugPrev, "%s", search);
                        g_sugOpen = false;
                        nav = NAV_SEARCH;
                        StartSearch(search);
                    }
                }
            }
        }

        bool overTitleBtns = hMin || hMax || hClose || CheckCollisionPointRec(mouse, searchB);
        if (Pressed()) {
            if (hClose) shouldClose = true;
            else if (hMin) MinimizeWindow();
            else if (hMax) { if (maximized) RestoreWindow(); else MaximizeWindow(); }
            else if (CheckCollisionPointRec(mouse, searchB)) {
                const bool wasFocused = searchFocus;
                searchFocus = true;
                if (!wasFocused) { g_sugOpen = true; RebuildSuggestions(search); }
                const int idx = FieldIndexAt(search, mouse.x);
                const double nowC = GetTime();
                if (nowC - lastSearchClick < 0.4 && search[0]) { WordAt(search, idx, &g_searchAnchor, &g_searchCur); selDrag = false; }     /* double click: select the word */
                else { g_searchCur = g_searchAnchor = idx; selDrag = true; }                                                           /* press: place the cursor, dragging selects */
                lastSearchClick = nowC;
            }
            else {
                searchFocus = false;
                if (mouse.y < TITLEBAR_H && !overTitleBtns && !maximized) { dragging = true; dragGrab = mouse; }
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { dragging = false; selDrag = false; }
        if (selDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_searchCur = FieldIndexAt(search, GetMousePosition().x);
        if (dragging) {
            Vector2 win = GetWindowPosition();
            Vector2 now = GetMousePosition();
            SetWindowPosition((int)(win.x + now.x - dragGrab.x), (int)(win.y + now.y - dragGrab.y));
        }
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_F)) { searchFocus = true; g_sugOpen = true; RebuildSuggestions(search); }
        if (searchFocus) {
            const bool listShown = sugR.width > 0;
            if (listShown && g_sugSel >= 0 && g_sugSel < g_sugN && IsKeyPressed(KEY_SPACE)) {
                /* Space on a highlighted suggestion takes it into the box (no search yet) so typing can go on */
                while (GetCharPressed() != 0) { }                  /* drop the space itself; one is added after the text */
                snprintf(search, sizeof search, "%s ", g_sug[g_sugSel]); g_searchCur = g_searchAnchor = (int)strlen(search);
            }
            EditText(search, sizeof search, &g_searchCur, &g_searchAnchor);
            if (!listShown && IsKeyPressed(KEY_DOWN)) { g_sugOpen = true; RebuildSuggestions(search); }     /* Down brings the list back */
            if (listShown && IsKeyPressed(KEY_DOWN)) g_sugSel = (g_sugSel + 1) % g_sugN;
            if (listShown && IsKeyPressed(KEY_UP))   g_sugSel = g_sugSel <= 0 ? g_sugN - 1 : g_sugSel - 1;
            if (IsKeyPressed(KEY_ESCAPE)) { if (listShown) g_sugOpen = false; else searchFocus = false; }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                if (listShown && g_sugSel >= 0 && g_sugSel < g_sugN) { snprintf(search, sizeof search, "%s", g_sug[g_sugSel]); g_searchCur = g_searchAnchor = (int)strlen(search); }
                snprintf(g_sugPrev, sizeof g_sugPrev, "%s", search);
                g_sugOpen = false;
                nav = NAV_SEARCH;
                StartSearch(search);
            }
        }

        /* ================= sidebar ================= */
        Rectangle side = { 0, TITLEBAR_H, SIDEBAR_W, (float)(H - TITLEBAR_H - STATUSBAR_H) };
        DrawRectangleRec(side, ShsThemeClassic() ? SHS_COLOR_BG : SHS_COLOR_CARD);
        DrawRectangle(SIDEBAR_W - 1, TITLEBAR_H, 1, (int)side.height, SHS_COLOR_BORDER);

        const char *railTip = NULL;
        float railTipY = 0;
        {
            const float bs = 40, bx = (SIDEBAR_W - bs) / 2;
            static const ShsIcon navIcon[NAV_COUNT] = { SHS_ICON_SEARCH, SHS_ICON_STAR, SHS_ICON_CLOCK, SHS_ICON_GEAR, SHS_ICON_INFO };
            for (int i = 0; i < NAV_COUNT; i++) {
                Rectangle r = { bx, (float)(TITLEBAR_H + 14 + i * 48), bs, bs };
                bool hot = CheckCollisionPointRec(mouse, r), on = (int)nav == i;
                if (ShsThemeBevel()) ShsDrawBevel(r, on || (hot && IsMouseButtonDown(MOUSE_BUTTON_LEFT)), SHS_COLOR_BG);
                else {
                    if (on)       ShsRoundRec(r, 0.25f, 8, Fade(SHS_COLOR_PRIMARY, 0.25f));
                    else if (hot) ShsRoundRec(r, 0.25f, 8, SHS_COLOR_CARD_HOVER);
                    if (on)       DrawRectangle(0, (int)r.y + 10, 3, 20, SHS_COLOR_PRIMARY);
                }
                ShsDrawIcon(navIcon[i], r.x + bs / 2, r.y + bs / 2, 20.0f, on ? SHS_COLOR_PRIMARY : SHS_COLOR_TEXT_MUTED);
                if (hot) {
                    static char tipBuf[48];
                    if (i == NAV_FAVORITES)    snprintf(tipBuf, sizeof tipBuf, "%s (%d)", NAV_LABEL[i], g_fav.count);
                    else if (i == NAV_HISTORY) snprintf(tipBuf, sizeof tipBuf, "%s (%d)", NAV_LABEL[i], g_hist.count);
                    else                       snprintf(tipBuf, sizeof tipBuf, "%s", NAV_LABEL[i]);
                    railTip = tipBuf; railTipY = r.y + bs / 2;
                    if (Pressed()) { nav = (Nav)i; if (nav == NAV_SEARCH) searchFocus = true; }
                }
            }
        }

        /* ================= main pane ================= */
        float px = SIDEBAR_W + 20, pw = (float)(W - SIDEBAR_W - 40);
        float py = TITLEBAR_H + 16;

        if (nav == NAV_SEARCH) {
            char tl[48];
            snprintf(tl, sizeof tl, g_searched ? "Results (%d)" : "Results", g_resN);
            DrawPageHeader(px, py, pw, tl);

            /* source summary + sort button */
            float fx = px, fy = py + 50;
            {
                int on = 0, done = 0, failed = 0;
                for (int s = 0; s < SHS_SRC_COUNT; s++) {
                    if (!((g_sourceMask >> s) & 1u)) continue;
                    on++;
                    ShsSourceState st = ShsSearchState(s, NULL);
                    if (g_searched && (st == SHS_STATE_OK || st == SHS_STATE_FAILED)) done++;
                    if (g_searched && st == SHS_STATE_FAILED) failed++;
                }
                char sb[96];
                if (g_searched) snprintf(sb, sizeof sb, "Sources (%d/%d answered, %d offline)", done, on, failed);
                else            snprintf(sb, sizeof sb, "Sources (%d on)", on);
                float bw = (float)ShsMeasureText(sb, 15) + 28 + 24;
                if (ButtonI((Rectangle){ fx, fy, bw, 30 }, sb, SHS_ICON_GRID, false, false)) nav = NAV_SETTINGS;
            }
            if (ButtonI((Rectangle){ px + pw - 170, fy, 170, 30 }, SORT_LABEL[g_sort], SHS_ICON_SORT, false, false)) {
                g_sort = (SortMode)((g_sort + 1) % SORT_COUNT);
                ResortResults();
                SaveSettings();
            }

            Rectangle listB = { px, fy + 42, pw, (float)(H - STATUSBAR_H) - 12 - (fy + 42) };
            if (g_resN > 0) DrawResultList(listB, g_res, g_resN, &g_searchView);
            else {
                const char *l1, *l2 = NULL;
                if (!g_searched)                 { l1 = "Type what you are looking for in the box above and press Enter."; l2 = "Results come from the sites switched on above; the magnet link goes to your torrent app."; }
                else if (ShsSearchBusy())        { l1 = "Searching..."; }
                else                             { l1 = "No results. Try other words, or check that the sources above are online."; }
                ShsDrawText(l1, (int)px + 8, (int)listB.y + 24, 16, SHS_COLOR_TEXT_MUTED);
                if (l2) ShsDrawText(l2, (int)px + 8, (int)listB.y + 50, 14, SHS_COLOR_TEXT_MUTED);
            }
        } else if (nav == NAV_FAVORITES) {
            char tl[48];
            snprintf(tl, sizeof tl, "Favorites (%d)", g_fav.count);
            DrawPageHeader(px, py, pw, tl);
            Rectangle listB = { px, py + 54, pw, (float)(H - STATUSBAR_H) - 12 - (py + 54) };
            if (g_fav.count > 0) {
                DrawResultList(listB, g_fav.items, g_fav.count, &g_favView);
            } else {
                ShsDrawText("No favorites yet. Click the star on a result to keep it here.", (int)px + 8, (int)listB.y + 24, 16, SHS_COLOR_TEXT_MUTED);
            }
        } else if (nav == NAV_HISTORY) {
            char tl[48];
            snprintf(tl, sizeof tl, "History (%d)", g_hist.count);
            DrawPageHeader(px, py, pw, tl);
            if (g_hist.count > 0 && ButtonI((Rectangle){ px + pw - 130, py, 130, 32 }, "Clear all", SHS_ICON_TRASH, false, false)) {
                g_hist.count = 0;
                ShsHistorySave(&g_hist, g_histFile);
            }
            float hy = py + 54;
            int jump = -1, del = -1;
            for (int i = 0; i < g_hist.count && hy + 44 < H - STATUSBAR_H; i++, hy += 50) {
                Rectangle row = { px, hy, pw, 42 };
                bool hot = CheckCollisionPointRec(mouse, row);
                ShsDrawRoundedBox(row, 10.0f, hot ? SHS_COLOR_CARD_HOVER : SHS_COLOR_CARD, SHS_COLOR_BORDER);
                ShsDrawIcon(SHS_ICON_CLOCK, row.x + 24, row.y + 21, 16.0f, SHS_COLOR_TEXT_MUTED);
                char fit[SHS_QUERY_MAX];
                FitText(fit, sizeof fit, g_hist.queries[i], 15, pw - 120);
                ShsDrawText(fit, (int)row.x + 46, (int)row.y + 12, 15, SHS_COLOR_TEXT_MAIN);
                Rectangle delB = { row.x + row.width - 40, row.y + 5, 32, 32 };
                if (ButtonI(delB, "", SHS_ICON_CLOSE, false, false)) del = i;
                else if (hot && Pressed()) jump = i;
            }
            if (g_hist.count == 0) ShsDrawText("Your searches show up here.", (int)px + 8, (int)py + 78, 16, SHS_COLOR_TEXT_MUTED);
            if (del >= 0) { ShsHistoryRemove(&g_hist, del); ShsHistorySave(&g_hist, g_histFile); }
            else if (jump >= 0) {
                snprintf(search, sizeof search, "%s", g_hist.queries[jump]); g_searchCur = g_searchAnchor = (int)strlen(search);
                nav = NAV_SEARCH;
                StartSearch(search);
            }
        } else if (nav == NAV_SETTINGS) {
            float y = py + 8;
            const float viewTop = y - 8, viewBot = (float)(H - STATUSBAR_H);
            if (!g_block) g_setScroll -= GetMouseWheelMove() * 48.0f;
            if (g_setScroll > g_setMax) g_setScroll = g_setMax;
            if (g_setScroll < 0) g_setScroll = 0;
            BeginScissorMode(0, (int)viewTop, W, (int)(viewBot - viewTop));
            const bool prevBlock = g_block;
            const Vector2 mp = GetMousePosition();
            if (mp.y < viewTop || mp.y > viewBot) g_block = true;
            const float contentTop = y;
            y -= g_setScroll;

            ShsDrawText("APPEARANCE", (int)px, (int)y, 12, SHS_COLOR_TEXT_MUTED);
            for (int m = 0; m < SHS_THEME_COUNT; m++) {                 /* two rows of theme buttons */
                const float bwid = 120;
                int col = m < 5 ? m : m - 5, row = m < 5 ? 0 : 1;
                if (Button((Rectangle){ px + col * (bwid + 8), y + 22 + row * 40, bwid, 34 }, ShsThemeLabel((ShsThemeMode)m), false, (int)ShsThemeGetMode() == m)) {
                    bool wasFlat = ShsThemeFlat();
                    ShsThemeSetMode((ShsThemeMode)m);
                    SaveSettings();
                    if (wasFlat != ShsThemeFlat()) { g_restart = true; shouldClose = true; }   /* anti-aliasing and fonts are fixed at start-up */
                }
            }
            char look[96];
            snprintf(look, sizeof look, "Using the %s theme", ShsThemeClassic() ? ShsThemeLabel(ShsThemeGetMode()) : (ShsThemeIsDark() ? "dark" : "light"));
            ShsDrawText(look, (int)px + 5 * 128 + 8, (int)y + 31, 14, SHS_COLOR_TEXT_MUTED);
            ShsDrawText("Themes with square corners (Flat 98, Win98, Mac System 7, Linux CDE) restart the app when you switch to or from them. They use less memory.", (int)px, (int)y + 102, 13, SHS_COLOR_TEXT_MUTED);
            y += 124;

            ShsDrawText("SOURCES", (int)px, (int)y, 12, SHS_COLOR_TEXT_MUTED);
            ShsDrawText("Sites that are searched. Several of the older sites are dead or blocked in some countries; they simply show as offline.", (int)px, (int)y + 20, 13, SHS_COLOR_TEXT_MUTED);
            {
                if (Button((Rectangle){ px, y + 42, 110, 28 }, "All on", false, false))  { g_sourceMask = SHS_SRC_ALL; SaveSettings(); }
                if (Button((Rectangle){ px + 118, y + 42, 110, 28 }, "Defaults", false, false)) { g_sourceMask = ShsSourceDefaultMask(); SaveSettings(); }
                const int perCol = (SHS_SRC_COUNT + 1) / 2;
                const float colW = 300;
                for (int s = 0; s < SHS_SRC_COUNT; s++) {
                    float sx = px + (s / perCol) * colW, sy2 = y + 82 + (s % perCol) * 32;
                    bool v = (g_sourceMask >> s) & 1u;
                    Rectangle row = { sx, sy2, colW - 12, 28 };
                    DrawSwitch((Rectangle){ sx, sy2 + 2, 44, 24 }, v);
                    ShsDrawText(ShsSourceName(s), (int)sx + 56, (int)sy2 + 5, 15, SHS_COLOR_TEXT_MAIN);
                    if (CheckCollisionPointRec(Mouse(), row) && Pressed()) { g_sourceMask ^= 1u << s; SaveSettings(); }
                }
                y += 82 + perCol * 32;
            }
            float ty = y;
            y = ty + 6;

            ShsDrawText("UPDATES", (int)px, (int)y, 12, SHS_COLOR_TEXT_MUTED);
            bool v = g_checkUpdates;
            if (Toggle(px, y + 24, 640, "Check for updates automatically", "Looks for a newer release on GitHub at start-up and once a day, and tells you in the status bar", &v)) {
                g_checkUpdates = v;
                g_nextUpdateCheck = 0;
                SaveSettings();
            }
            v = g_updateDev;
            if (Toggle(px, y + 78, 640, "Include dev builds", "Also tell me about pre-release dev builds (newer features, may be unstable)", &v)) {
                g_updateDev = v;
                g_updateFound = false;
                g_updateMsg[0] = 0;
                g_nextUpdateCheck = 0;
                SaveSettings();
            }
            if (Button((Rectangle){ px, y + 134, 140, 32 }, "Check now", false, false)) UpdateCheckNow(true);
            if (g_updateMsg[0]) ShsDrawText(g_updateMsg, (int)px + 156, (int)y + 142, 14, g_updateFound ? SHS_COLOR_PRIMARY : SHS_COLOR_TEXT_MUTED);
            if (g_updateFound && Button((Rectangle){ px + 156 + ShsMeasureText(g_updateMsg, 14) + 16, y + 134, 140, 32 }, "Download", true, false)) ShsOpenUrl(g_updateUrl);
            y += 182;

            ShsDrawText("DATA", (int)px, (int)y, 12, SHS_COLOR_TEXT_MUTED);
            char df[700];
            snprintf(df, sizeof df, "Favorites file:  %s", g_favFile);
            ShsDrawText(df, (int)px, (int)y + 24, 14, SHS_COLOR_TEXT_MUTED);
            snprintf(df, sizeof df, "History file:  %s", g_histFile);
            ShsDrawText(df, (int)px, (int)y + 46, 14, SHS_COLOR_TEXT_MUTED);
            if (ButtonI((Rectangle){ px, y + 76, 200, 34 }, "Clear search history", SHS_ICON_TRASH, false, false)) {
                g_hist.count = 0;
                ShsHistorySave(&g_hist, g_histFile);
                Toast("Search history cleared");
            }
            if (ButtonI((Rectangle){ px + 210, y + 76, 170, 34 }, "Open data folder", SHS_ICON_FOLDER, false, false))
                if (!ShsOpenFolder(g_appDir)) Toast("Could not open %s", g_appDir);
            y += 112;
            ShsDrawText("Only download material you have the right to. Search results come from third-party sites.", (int)px, (int)y, 13, SHS_COLOR_WARNING);
            EndScissorMode();
            g_block = prevBlock;
            g_setMax = fmaxf(0.0f, (y + 24 + g_setScroll) - contentTop - (viewBot - contentTop));
DrawPageScrollbar(g_setScroll, g_setMax, viewTop, viewBot, W);
        } else if (nav == NAV_ABOUT) {
            const float viewTop = py, viewBot = (float)(H - STATUSBAR_H);
            if (!g_block) g_aboutScroll -= GetMouseWheelMove() * 48.0f;
            if (g_aboutScroll > g_aboutMax) g_aboutScroll = g_aboutMax;
            if (g_aboutScroll < 0) g_aboutScroll = 0;
            BeginScissorMode(0, (int)viewTop, W, (int)(viewBot - viewTop));
            const bool prevBlockA = g_block;
            if (GetMousePosition().y < viewTop || GetMousePosition().y > viewBot) g_block = true;
            float y = py + 8 - g_aboutScroll;
            const float icon = 96.0f, tx = px + icon + 24.0f;
            if (g_logo.id)
                DrawTexturePro(g_logo, (Rectangle){ 0, 0, (float)g_logo.width, (float)g_logo.height },
                               (Rectangle){ px, y, icon, icon }, (Vector2){ 0, 0 }, 0.0f, WHITE);
            ShsDrawText(SHS_APP_TITLE, (int)tx, (int)y + 8, 26, SHS_COLOR_TEXT_MAIN);
            ShsDrawText("Version " SHS_VERSION, (int)tx, (int)y + 44, 15, SHS_COLOR_PRIMARY);
            ShsDrawText("A tiny, portable, open-source torrent search tool.", (int)tx, (int)y + 68, 15, SHS_COLOR_TEXT_MUTED);

            float cy = y + icon + 24.0f;
            ShsDrawText("Searches public torrent indexes and gives you the magnet link, ready for your own torrent app.", (int)px, (int)cy, 15, SHS_COLOR_TEXT_MUTED);
            ShsDrawText("It never downloads or shares files itself. Star a result to keep it in Favorites.", (int)px, (int)cy + 22, 15, SHS_COLOR_TEXT_MUTED);
            ShsDrawText("Click a result for its info hash, or use the buttons to open or copy the magnet link.", (int)px, (int)cy + 44, 15, SHS_COLOR_TEXT_MUTED);

            const float cw = fminf(pw, 760.0f), rowH = 32.0f;
            const char *rows[][2] = {
                { "Version",    SHS_VERSION },
                { "Build",      __DATE__ " " __TIME__ },
                { "Platform",   ABOUT_PLATFORM },
                { "Graphics",   "raylib " RAYLIB_VERSION },
                { "Font",       NULL },
                { "License",    "MIT License" },
                { "Copyright",  "(c) 2026 SailHighSea contributors" },
                { "Website",    "github.com/SailHighSea/sailhighsea-ts" },
                { "Data folder", NULL },
            };
            const int nRows = (int)(sizeof rows / sizeof rows[0]);
            Rectangle card = { px, cy + 76, cw, rowH * nRows + 20 };
            ShsDrawRoundedBox(card, 14.0f, SHS_COLOR_CARD, SHS_COLOR_BORDER);
            for (int i = 0; i < nRows; i++) {
                float ry = card.y + 10 + i * rowH;
                ShsDrawText(rows[i][0], (int)card.x + 20, (int)ry + 7, 14, SHS_COLOR_TEXT_MUTED);
                char val[600];
                const char *vv = rows[i][1];
                if (!vv) vv = (i == 4) ? ShsFontName() : g_appDir;
                snprintf(val, sizeof val, "%s", vv);
                while (val[0] && ShsMeasureText(val, 15) > (int)(card.width - 190)) {   /* keep the tail of long paths */
                    memmove(val, val + 1, strlen(val));
                    if (strlen(val) > 3) { val[0] = '.'; val[1] = '.'; val[2] = '.'; }
                }
                ShsDrawText(val, (int)card.x + 150, (int)ry + 6, 15, SHS_COLOR_TEXT_MAIN);
                if (i < nRows - 1) DrawRectangle((int)card.x + 16, (int)(ry + rowH), (int)card.width - 32, 1, Fade(SHS_COLOR_BORDER, 0.6f));
            }

            float by = card.y + card.height + 18;
            if (ButtonI((Rectangle){ px, by, 150, 36 }, "GitHub page", SHS_ICON_INFO, true, false))
                if (!ShsOpenUrl(SHS_REPO_URL)) Toast("Could not open the browser");
            if (ButtonI((Rectangle){ px + 160, by, 150, 36 }, "Data folder", SHS_ICON_FOLDER, false, false))
                if (!ShsOpenFolder(g_appDir)) Toast("Could not open %s", g_appDir);
            if (ButtonI((Rectangle){ px + 320, by, 150, 36 }, "Copy info", SHS_ICON_LOGS, false, false)) {
                char info[512];
                snprintf(info, sizeof info, SHS_APP_TITLE " v%s\nBuilt %s %s\n%s, raylib %s\nData folder: %s",
                         SHS_VERSION, __DATE__, __TIME__, ABOUT_PLATFORM, RAYLIB_VERSION, g_appDir);
                SetClipboardText(info);
                Toast("Version info copied to the clipboard");
            }
            EndScissorMode();
            g_block = prevBlockA;
            g_aboutMax = fmaxf(0.0f, by + 36 + 16 + g_aboutScroll - viewBot);
            DrawPageScrollbar(g_aboutScroll, g_aboutMax, viewTop, viewBot, W);
        }

        if (railTip) {                                   /* icon rail tooltip, drawn over the page */
            int tw = ShsMeasureText(railTip, 15);
            Rectangle tr = { SIDEBAR_W + 4, railTipY - 14, (float)tw + 20, 28 };
            ShsDrawRoundedBox(tr, 7.0f, SHS_COLOR_CARD, SHS_COLOR_BORDER);
            ShsDrawText(railTip, (int)tr.x + 10, (int)tr.y + 6, 15, SHS_COLOR_TEXT_MAIN);
        }

        /* ================= status bar ================= */
        const bool busy = ShsSearchBusy();
        FillRoundBottom((Rectangle){ 0, (float)(H - STATUSBAR_H), (float)W, STATUSBAR_H }, 0.0f, ShsThemeClassic() ? SHS_COLOR_BG : SHS_COLOR_CARD);
        DrawRectangle(0, H - STATUSBAR_H, W, 1, SHS_COLOR_BORDER);
        const char *ver = SHS_APP_TITLE " v" SHS_VERSION;
        if (ShsThemeBevel()) {
            const float spy = (float)(H - STATUSBAR_H + 3), sph = STATUSBAR_H - 5;
            const float verW = (float)ShsMeasureText(ver, 14);
            ShsDrawBevelFrame((Rectangle){ 4, spy, 218, sph }, true);
            ShsDrawBevelFrame((Rectangle){ 224, spy, 430, sph }, true);
            ShsDrawBevelFrame((Rectangle){ (float)W - verW - 30, spy, verW + 26, sph }, true);
        }
        int sy = H - STATUSBAR_H + 8;
        DrawCircle(20, H - STATUSBAR_H / 2, 4.5f, busy ? SHS_COLOR_PRIMARY : SHS_COLOR_SUCCESS);
        ShsDrawText(busy ? "Engine Status: SEARCHING" : "Engine Status: READY", 32, sy, 14, SHS_COLOR_TEXT_MAIN);
        char mid[160] = "";
        if (g_toast[0] && GetTime() < g_toastUntil) snprintf(mid, sizeof mid, "%s", g_toast);
        else if (g_searched) snprintf(mid, sizeof mid, "%d results for \"%s\"", g_resN, g_lastQuery);
        else snprintf(mid, sizeof mid, "Favorites: %d    History: %d", g_fav.count, g_hist.count);
        ShsDrawText(mid, 230, sy, 14, (g_toast[0] && GetTime() < g_toastUntil) ? SHS_COLOR_PRIMARY : SHS_COLOR_TEXT_MUTED);
        ShsDrawText(ver, W - ShsMeasureText(ver, 14) - 16, sy, 14, SHS_COLOR_TEXT_MUTED);
        if (g_updateFound) {                                                   /* newer release: click to open its page */
            char un[96];
            const float rightEdge = (float)(W - ShsMeasureText(ver, 14) - 40);
            const float leftLimit = 230.0f + ShsMeasureText(mid, 14) + 20.0f;
            snprintf(un, sizeof un, "Update available: %s - click to download", g_updateTag);
            if (rightEdge - ShsMeasureText(un, 14) < leftLimit) snprintf(un, sizeof un, "Update available: %s", g_updateTag);
            if (rightEdge - ShsMeasureText(un, 14) < leftLimit) snprintf(un, sizeof un, "Update %s", g_updateTag);
            const int uw = ShsMeasureText(un, 14), ux = (int)rightEdge - uw;
            Rectangle ur = { (float)ux - 6, (float)(H - STATUSBAR_H), (float)uw + 12, STATUSBAR_H };
            bool hot = CheckCollisionPointRec(GetMousePosition(), ur);
            ShsDrawText(un, ux, sy, 14, SHS_COLOR_PRIMARY);
            if (hot) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ShsOpenUrl(g_updateUrl); }
            else SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }

        if (sugR.width > 0) {                                      /* suggestion drop-down, over everything */
            ShsDrawRoundedBox(sugR, 10.0f, SHS_COLOR_CARD, SHS_COLOR_BORDER);
            for (int i = 0; i < g_sugN; i++) {
                Rectangle row = { sugR.x + 4, sugR.y + 4 + i * 34.0f, sugR.width - 8, 34 };
                if (i == g_sugSel || CheckCollisionPointRec(GetMousePosition(), row)) FillRound(row, 8.0f, SHS_COLOR_CARD_HOVER);
                ShsDrawIcon(g_sugLocal[i] ? SHS_ICON_CLOCK : SHS_ICON_SEARCH, row.x + 20, row.y + 17, 16.0f, SHS_COLOR_TEXT_MUTED);
                char fit[SHS_QUERY_MAX];
                FitText(fit, sizeof fit, g_sug[i], 15, row.width - 56);
                ShsDrawText(fit, (int)row.x + 40, (int)row.y + 9, 15, SHS_COLOR_TEXT_MAIN);
            }
        }
        g_block = false;
        if (ShsThemeBevel()) ShsDrawBevelFrame((Rectangle){ 0, 0, (float)W, (float)H }, false);
        else                 DrawRectangleLines(0, 0, W, H, SHS_COLOR_BORDER);   /* no OS frame */
        EndDrawing();
    }

    ShsFontsShutdown();
    if (g_logo.id) UnloadTexture(g_logo);
    CloseWindow();
    if (g_restart) ShsRelaunch(argc, argv);
    return 0;
}
