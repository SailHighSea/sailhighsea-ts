/* Headless tests for the search engine parsers, magnet builder and the favourites / history files (no network, no window). */
#include "search_engine.h"
#include "json_lite.h"
#include "result_store.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void TestJson(void)
{
    ShsJson *j = ShsJsonParse("{\"a\": \"x\\u00e9\\n\", \"n\": 12.5, \"s\": \"7\", \"l\": [1, 2, {\"k\": true}], \"z\": null}");
    CHECK(j && j->type == SHS_JSON_OBJ);
    CHECK(j && !strcmp(ShsJsonStr(j, "a", ""), "x\xC3\xA9\n"));
    CHECK(j && ShsJsonNum(j, "n", 0) == 12.5);
    CHECK(j && ShsJsonNum(j, "s", 0) == 7.0);                 /* quoted number */
    CHECK(j && ShsJsonNum(j, "missing", 3) == 3.0);
    CHECK(j && ShsJsonGet(j, "l")->type == SHS_JSON_ARR);
    ShsJsonFree(j);
    CHECK(ShsJsonParse("<html>not json</html>") == NULL);
    CHECK(ShsJsonParse("{\"a\": 1} trailing") == NULL);
    CHECK(ShsJsonParse("[1, 2,") == NULL);
    CHECK(ShsJsonParse("{\"a\": \"unterminated}") == NULL);
    char deep[200];
    memset(deep, '[', sizeof deep - 1);
    deep[sizeof deep - 1] = 0;
    CHECK(ShsJsonParse(deep) == NULL);                        /* depth limit, no stack overflow */
}

static void TestTpb(void)
{
    const char *body =
        "[{\"id\":\"1\",\"name\":\"Ubuntu 24.04 \\\"LTS\\\" &amp; d&eacute;j&#224; &#x41; desktop\",\"info_hash\":\"ABCDEF0123456789ABCDEF0123456789ABCDEF01\","
        "\"leechers\":\"4\",\"seeders\":\"120\",\"num_files\":\"1\",\"size\":\"6144000000\",\"username\":\"canonical\",\"added\":\"1700000000\"},"
        "{\"id\":\"2\",\"name\":\"bad hash\",\"info_hash\":\"zz\",\"seeders\":\"1\"}]";
    ShsResult r[10];
    int n = ShsParseTpb(body, r, 10);
    CHECK(n == 1);
    CHECK(!strcmp(r[0].name, "Ubuntu 24.04 \"LTS\" & déjà A desktop"));
    CHECK(!strcmp(r[0].hash, "abcdef0123456789abcdef0123456789abcdef01"));
    CHECK(r[0].seeders == 120 && r[0].leechers == 4);
    CHECK(r[0].size == 6144000000LL && r[0].added == 1700000000LL);
    CHECK(r[0].source == SHS_SRC_TPB && r[0].tid == 1 && r[0].files == 1);

    /* apibay answers "no results" with one all-zero row */
    CHECK(ShsParseTpb("[{\"id\":\"0\",\"name\":\"No results returned\",\"info_hash\":\"0000000000000000000000000000000000000000\"}]", r, 10) == 0);
    CHECK(ShsParseTpb("not json", r, 10) == -1);               /* not the expected reply: the caller tries a mirror */
    CHECK(ShsParseTpb("{\"a\":1}", r, 10) == -1);
    CHECK(ShsParseTpb("[]", r, 10) == 0);
}

static void TestYts(void)
{
    const char *body =
        "{\"status\":\"ok\",\"data\":{\"movie_count\":1,\"movies\":[{\"title_long\":\"Big Buck Bunny (2008)\",\"torrents\":["
        "{\"hash\":\"1111111111111111111111111111111111111111\",\"quality\":\"720p\",\"type\":\"web\",\"seeds\":50,\"peers\":5,\"size_bytes\":900000000,\"date_uploaded_unix\":1600000000},"
        "{\"hash\":\"2222222222222222222222222222222222222222\",\"quality\":\"1080p\",\"type\":\"bluray\",\"seeds\":80,\"peers\":9,\"size_bytes\":1900000000,\"date_uploaded_unix\":1600000100}]}]}}";
    ShsResult r[10];
    int n = ShsParseYts(body, r, 10);
    CHECK(n == 2);
    CHECK(!strcmp(r[0].name, "Big Buck Bunny (2008) [720p web]"));
    CHECK(r[1].seeders == 80 && r[1].size == 1900000000LL);
    CHECK(ShsParseYts("{\"status\":\"ok\",\"data\":{\"movie_count\":0}}", r, 10) == 0);
    CHECK(ShsParseYts("<html>blocked</html>", r, 10) == -1);
    CHECK(ShsParseYts(body, r, 1) == 1);                      /* max respected */
}

static void TestNyaa(void)
{
    const char *body =
        "<?xml version=\"1.0\"?><rss><channel>"
        "<item><title>[Group] Show &amp; Friends - 01 [1080p]</title><pubDate>Sat, 01 Nov 2025 12:00:00 -0000</pubDate>"
        "<nyaa:seeders>33</nyaa:seeders><nyaa:leechers>2</nyaa:leechers><nyaa:infoHash>ABCDEF0123456789ABCDEF0123456789ABCDEF01</nyaa:infoHash>"
        "<nyaa:size>1.5 GiB</nyaa:size><nyaa:category>Anime - English-translated</nyaa:category></item>"
        "<item><title><![CDATA[Second <one>]]></title><nyaa:infoHash>nothex</nyaa:infoHash></item>"
        "</channel></rss>";
    ShsResult r[10];
    int n = ShsParseNyaa(body, r, 10);
    CHECK(n == 1);
    CHECK(!strcmp(r[0].name, "[Group] Show & Friends - 01 [1080p]"));
    CHECK(r[0].seeders == 33 && r[0].leechers == 2);
    CHECK(r[0].size == (long long)(1.5 * 1073741824.0));
    CHECK(r[0].added == 1761998400LL);                        /* 2025-11-01 12:00:00 UTC */
    CHECK(!strcmp(r[0].detail, "Anime - English-translated"));
    CHECK(ShsParseNyaa("<rss></rss>", r, 10) == 0);
    CHECK(ShsParseNyaa("<html>blocked</html>", r, 10) == -1);
}

static void TestHtml(void)
{
    const char *page =
        "<html><body>"
        "<a href=\"magnet:?xt=urn:btih:ABCDEF0123456789ABCDEF0123456789ABCDEF01&amp;dn=My+Movie%20%282020%29&amp;tr=udp%3A%2F%2Fx\">m</a>"
        "<a href='magnet:?xt=urn:btih:MFRGGZDFMZTWQ2LKNNWG23TPOBYXE43U&dn=base32'>b</a>"
        "<a href=\"/torrent/1111111111111111111111111111111111111111/some-name\"><b>Some</b> Name &amp; Co</a>"
        "<a href=\"/torrent/ABCDEF0123456789ABCDEF0123456789ABCDEF01/dup\">dup</a>"
        "<a href=\"/torrent/abcdef0123456789abcdef0123456789abcdef0123/too-long\">x</a>"
        "</body></html>";
    ShsResult r[10];
    int n = ShsParseHtml(page, 7, r, 10);
    CHECK(n == 3);
    CHECK(!strcmp(r[0].hash, "abcdef0123456789abcdef0123456789abcdef01") && !strcmp(r[0].name, "My Movie (2020)") && r[0].source == 7);
    CHECK(!strcmp(r[1].name, "base32") && strlen(r[1].hash) == 40);
    CHECK(!strcmp(r[2].hash, "1111111111111111111111111111111111111111") && !strcmp(r[2].name, "Some Name & Co"));
    CHECK(ShsParseHtml("plain text, no markup", 7, r, 10) == -1);
    CHECK(ShsParseHtml("<html>nothing</html>", 7, r, 10) == 0);
    CHECK(ShsParseHtml(page, 7, r, 1) == 1);
}

static void TestDetail(void)
{
    static ShsDetail d;
    memset(&d, 0, sizeof d);
    const char *multi = "d4:infod5:filesld6:lengthi100e4:pathl1:a5:b.txteed6:lengthi50e4:pathl5:c.txteee4:name3:direee";
    CHECK(ShsParseTorrent((const unsigned char *)multi, strlen(multi), &d) == 2);
    CHECK(d.fileCount == 2 && d.total == 150 && !strcmp(d.files[0].path, "dir/a/b.txt") && d.files[0].size == 100 && !strcmp(d.files[1].path, "dir/c.txt"));
    memset(&d, 0, sizeof d);
    const char *single = "d8:announce3:url4:infod6:lengthi7e4:name5:a.isoee";
    CHECK(ShsParseTorrent((const unsigned char *)single, strlen(single), &d) == 1 && !strcmp(d.files[0].path, "a.iso") && d.total == 7);
    CHECK(ShsParseTorrent((const unsigned char *)"<html>", 6, &d) == -1);
    CHECK(ShsParseTorrent((const unsigned char *)"d4:infod5:filesld6:length", 25, &d) == -1);    /* cut off: no overrun */
    memset(&d, 0, sizeof d);
    CHECK(ShsParseTpbFiles("[{\"name\":[\"x/y.mkv\"],\"size\":[1234]},{\"name\":{\"0\":\"z.nfo\"},\"size\":{\"0\":\"5\"}}]", &d) == 2);
    CHECK(d.fileCount == 2 && d.total == 1239 && !strcmp(d.files[1].path, "z.nfo"));
    memset(&d, 0, sizeof d);
    CHECK(ShsParseTpbFiles("[{\"name\":{\"0\":\"Filelist not found\"},\"size\":{\"0\":\"0\"}}]", &d) == 0);
    CHECK(ShsParseTpbFiles("nope", &d) == -1);
    CHECK(!strcmp(ShsTpbCategory(207), "HD Movies") && !strcmp(ShsTpbCategory(250), "Video"));
}

static void TestApiSources(void)
{
    ShsResult r[5];
    int n = ShsParseTorrentsCsv("{\"torrents\":[{\"infohash\":\"232cd67eb3ffbd7c37bf9ec3ee887417e5ae1ee6\",\"name\":\"Ubuntu Bible\",\"size_bytes\":8554738,\"created_unix\":1700000000,\"seeders\":27,\"leechers\":3}]}", r, 5);
    CHECK(n == 1 && r[0].seeders == 27 && r[0].leechers == 3 && r[0].size == 8554738 && r[0].added == 1700000000LL && !strcmp(r[0].name, "Ubuntu Bible"));
    CHECK(ShsParseTorrentsCsv("<html>", r, 5) == -1);
    n = ShsParseBitSearch("{\"success\":true,\"results\":[{\"infohash\":\"D540FC48EB12F2833163EED6421D449DD8F1CE1F\",\"title\":\"ubuntu.iso\",\"size\":2097152000,\"seeders\":28,\"leechers\":41,\"updatedAt\":\"2026-11-03T15:00:14.260Z\"}]}", r, 5);
    CHECK(n == 1 && !strcmp(r[0].hash, "d540fc48eb12f2833163eed6421d449dd8f1ce1f") && r[0].added == 1793718014LL && r[0].size == 2097152000LL);
    CHECK(ShsParseBitSearch("{\"success\":false}", r, 5) == -1);
    n = ShsParseKnaben("{\"hits\":[{\"hash\":\"DAFC8C076CA2F3ED376EEAE7C76A0D6BE2415C45\",\"title\":\"u.iso\",\"bytes\":6517612871,\"date\":\"2026-04-25T15:35:00+00:00\",\"seeders\":130,\"peers\":55,\"category\":\"PC / Unix\"},{\"hash\":null,\"title\":\"x\"}]}", r, 5);
    CHECK(n == 1 && r[0].seeders == 130 && r[0].leechers == 55 && r[0].size == 6517612871LL && !strcmp(r[0].detail, "PC / Unix"));
    CHECK(ShsParseKnaben("{\"hits\":[]}", r, 5) == 0);
}

static void TestHelpers(void)
{
    ShsResult r = { 0 };
    snprintf(r.name, sizeof r.name, "My File & Co (2024).mkv");
    snprintf(r.hash, sizeof r.hash, "abcdef0123456789abcdef0123456789abcdef01");
    char m[1024];
    ShsMagnet(&r, m, sizeof m);
    CHECK(!strncmp(m, "magnet:?xt=urn:btih:abcdef0123456789abcdef0123456789abcdef01&dn=My%20File%20%26%20Co%20%282024%29.mkv&tr=udp%3A%2F%2F", 107));
    CHECK(!strchr(m, ' ') && !strchr(m, '\'') && !strchr(m, '"'));

    char s[32];
    ShsFormatSize(0, s, sizeof s);            CHECK(!strcmp(s, "-"));
    ShsFormatSize(1536, s, sizeof s);         CHECK(!strcmp(s, "2 KB") || !strcmp(s, "1 KB"));
    ShsFormatSize(3221225472LL, s, sizeof s); CHECK(!strcmp(s, "3.00 GB"));
    ShsFormatAge(1000, 1000 + 86400 * 3, s, sizeof s); CHECK(!strcmp(s, "3d ago"));
    ShsFormatAge(0, 5, s, sizeof s);          CHECK(!strcmp(s, "-"));

    char e[64];
    ShsUrlEncode("a b&c=d/é", e, sizeof e);
    CHECK(!strcmp(e, "a%20b%26c%3Dd%2F%C3%A9"));
    ShsUrlEncode("0123456789", e, 6);                          /* output is cut, never overflows */
    CHECK(strlen(e) < 6);
    CHECK(SHS_SRC_COUNT <= 32 && ShsSourceDefaultMask() != 0 && !strcmp(ShsSourceName(SHS_SRC_NYAA), "Nyaa"));
    {
        char sg[4][SHS_SUGGEST_LEN];
        CHECK(ShsParseSuggest("[\"ubu\",[\"ubuntu\",\"ubuntu download\",\"\"]]", sg, 4) == 2);
        CHECK(!strcmp(sg[1], "ubuntu download"));
        CHECK(ShsParseSuggest("<html></html>", sg, 4) == -1);
        CHECK(ShsParseSuggest("[\"x\"]", sg, 4) == -1);
    }
    CHECK(ShsHttpGet("file:///etc/passwd", 1000, NULL) == NULL);
    CHECK(ShsHttpGet("https://example.com/\"; rm -rf /", 1000, NULL) == NULL);
}

static void TestStore(const char *dir)
{
    char fav[600], hist[600];
    snprintf(fav, sizeof fav, "%s/fav.tsv", dir);
    snprintf(hist, sizeof hist, "%s/history.txt", dir);

    static ShsFavorites f;
    f.count = 0;
    ShsResult a = { 0 }, b = { 0 };
    snprintf(a.hash, sizeof a.hash, "abcdef0123456789abcdef0123456789abcdef01");
    snprintf(a.name, sizeof a.name, "First\twith tab");
    a.source = SHS_SRC_NYAA; a.seeders = 5; a.size = 1234; a.added = 99;
    snprintf(b.hash, sizeof b.hash, "1111111111111111111111111111111111111111");
    snprintf(b.name, sizeof b.name, "Second");
    CHECK(ShsFavAdd(&f, &a));
    CHECK(!ShsFavAdd(&f, &a));                                /* duplicate */
    CHECK(ShsFavAdd(&f, &b));
    CHECK(f.count == 2 && !strcmp(f.items[0].name, "Second"));
    CHECK(ShsFavSave(&f, fav));
    static ShsFavorites g;
    ShsFavLoad(&g, fav);
    CHECK(g.count == 2);
    CHECK(ShsFavFind(&g, a.hash) == 1);
    CHECK(g.items[1].source == SHS_SRC_NYAA && g.items[1].seeders == 5 && g.items[1].size == 1234 && g.items[1].added == 99);
    CHECK(!strcmp(g.items[1].name, "First with tab"));
    ShsFavRemove(&g, 0);
    CHECK(g.count == 1 && ShsFavFind(&g, b.hash) < 0);

    static ShsHistory h;
    h.count = 0;
    ShsHistoryAdd(&h, "ubuntu");
    ShsHistoryAdd(&h, "debian");
    ShsHistoryAdd(&h, "ubuntu");                              /* moves to the top */
    ShsHistoryAdd(&h, "   ");                                 /* blank: ignored */
    CHECK(h.count == 2 && !strcmp(h.queries[0], "ubuntu") && !strcmp(h.queries[1], "debian"));
    CHECK(ShsHistorySave(&h, hist));
    static ShsHistory h2;
    ShsHistoryLoad(&h2, hist);
    CHECK(h2.count == 2 && !strcmp(h2.queries[1], "debian"));
    for (int i = 0; i < 80; i++) { char q[32]; snprintf(q, sizeof q, "q%d", i); ShsHistoryAdd(&h, q); }
    CHECK(h.count == SHS_HISTORY_MAX && !strcmp(h.queries[0], "q79"));
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    TestJson();
    TestTpb();
    TestYts();
    TestNyaa();
    TestHtml();
    TestDetail();
    TestApiSources();
    TestHelpers();
    TestStore(dir);
    printf(g_fail ? "%d check(s) failed\n" : "all engine tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
