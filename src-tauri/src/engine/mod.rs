//! Search engine: source table, parsers, merge, magnet builder. No Tauri types in here, so `cargo test` runs headless.

pub mod net;
pub mod parsers;
pub mod torrent;
pub mod update;

use serde::{Deserialize, Serialize};

pub const SRC_COUNT: usize = 32;
pub const SRC_ALL: u32 = ((1u64 << SRC_COUNT) - 1) as u32;
pub const SRC_TPB: usize = 0;
pub const SRC_YTS: usize = 1;
pub const SRC_NYAA: usize = 2;

pub const PER_SOURCE_MAX: usize = 100;
pub const MAX_RESULTS: usize = 400;
pub const BODY_MAX: usize = 4 * 1024 * 1024;
pub const DETAIL_FILES: usize = 300;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    Tpb,
    Yts,
    Rss,
    Html,
    Csv,
    Bit,
    Knaben,
    Rarbg,
}

pub struct Source {
    pub name: &'static str,
    pub kind: Kind,
    pub on: bool,
    pub urls: &'static [&'static str],
}

const fn s(name: &'static str, kind: Kind, on: bool, urls: &'static [&'static str]) -> Source {
    Source { name, kind, on, urls }
}

/// Every site the original app knew. Sites without an API are scraped from their search page; the first address that
/// answers is used, so mirrors can be listed. Several of these sites are dead or blocked in places: they show as offline.
pub static SOURCES: [Source; SRC_COUNT] = [
    s("Pirate Bay", Kind::Tpb, true, &["https://apibay.org/q.php?q=%s&cat=0"]),
    s("YTS", Kind::Yts, true, &[
        "https://movies-api.accel.li/api/v2/list_movies.json?query_term=%s&limit=30",
        "https://yts.mx/api/v2/list_movies.json?query_term=%s&limit=30",
    ]),
    s("Nyaa", Kind::Rss, true, &["https://nyaa.si/?page=rss&q=%s&c=0_0&f=0", "https://nyaa.land/?page=rss&q=%s&c=0_0&f=0"]),
    s("Sukebei", Kind::Rss, false, &["https://sukebei.nyaa.si/?page=rss&q=%s&c=0_0&f=0"]),
    s("1337x", Kind::Html, true, &["https://www.1337x.st/search/%s/1/"]),
    s("TorrentGalaxy", Kind::Html, true, &["https://torrentgalaxy.to/torrents.php?search=%s"]),
    s("LimeTorrents", Kind::Html, true, &["https://www.limetorrents.info/search/all/%s/"]),
    s("Torlock", Kind::Html, true, &["https://www.torlock.com/all/torrents/%s.html"]),
    s("EZTV", Kind::Html, true, &["https://eztv.io/search/%s"]),
    s("ETTV", Kind::Html, true, &["https://www.ettv.to/torrents-search.php?search=%s"]),
    s("KickassTorrents", Kind::Html, true, &["https://kickasstorrent.cr/usearch/%s/"]),
    s("Zooqle", Kind::Html, true, &["https://zooqle.com/search?q=%s"]),
    s("TorrentKitty", Kind::Html, true, &["https://www.torrentkitty.tv/search/%s/"]),
    s("Torrentz2", Kind::Html, true, &["https://torrentz2.eu/search?f=%s"]),
    s("BTDB", Kind::Html, true, &["https://btdb.eu/?search=%s"]),
    s("BTSOW", Kind::Html, true, &["https://btsow.pw/search/%s"]),
    s("SkyTorrents", Kind::Html, true, &["https://www.skytorrents.lol/?query=%s"]),
    s("SeedPeer", Kind::Html, true, &["https://www.seedpeer.me/search/%s"]),
    s("ExtraTorrent", Kind::Html, true, &["https://extratorrent.si/search/?search=%s&new=1&x=0&y=0"]),
    s("Monova", Kind::Html, true, &["https://monova.org/search?term=%s"]),
    s("TheHiddenBay", Kind::Html, true, &["https://thehiddenbay.com/search/%s/1/99/0"]),
    s("BTDigg", Kind::Html, true, &["http://btdiggba.pw/search/%s/1/0/0.html"]),
    s("BTBaocai", Kind::Html, true, &["http://www.btbaocai.in/search/%s/"]),
    s("BTZhizhu", Kind::Html, true, &["https://www.btzhizhu.me/search/%s/"]),
    s("Diaosisou", Kind::Html, true, &["http://www.diaosisou.in/list/%s/1"]),
    s("NMS000", Kind::Html, true, &["http://www.nms000.com/l/%s-hot-desc-1"]),
    s("Aliciliba", Kind::Html, true, &["http://aliciliba.ws/list/%s/1-0-0/"]),
    s("Dabaicai", Kind::Html, true, &["https://dabaicai.ml/infolist.php?q=%s"]),
    s("Torrents.csv", Kind::Csv, true, &["https://torrents-csv.com/service/search?q=%s&size=100"]),
    s("BitSearch", Kind::Bit, true, &["https://bitsearch.to/api/v1/search?q=%s", "https://solidtorrents.to/api/v1/search?q=%s"]),
    s("Knaben", Kind::Knaben, true, &["https://api.knaben.org/v1"]),
    s("TheRARBG", Kind::Rarbg, true, &["https://therarbg.com/api/rss/search/?q=%s"]),
];

pub fn source_name(src: usize) -> &'static str {
    SOURCES.get(src).map(|s| s.name).unwrap_or("?")
}

/// Sources that are on until the user changes it, as a bit mask.
pub fn default_mask() -> u32 {
    SOURCES.iter().enumerate().fold(0u32, |m, (i, s)| if s.on { m | (1 << i) } else { m })
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct TorrentResult {
    pub name: String,
    pub hash: String,
    pub detail: String,
    pub source: usize,
    pub seeders: i32,
    pub leechers: i32,
    pub size: i64,
    pub added: i64,
    pub tid: i64,
    pub files: i32,
    /// Filled in when results are sent to the UI.
    pub magnet: String,
}

impl TorrentResult {
    pub fn with_magnet(mut self) -> Self {
        self.magnet = magnet(&self.hash, &self.name);
        self
    }
}

// ---- small helpers ------------------------------------------------------------------------------------------------

/// Cuts `s` to at most `max` bytes without splitting a character (the original used fixed-size buffers).
pub fn trunc(mut s: String, max: usize) -> String {
    if s.len() > max {
        let mut n = max;
        while n > 0 && !s.is_char_boundary(n) {
            n -= 1;
        }
        s.truncate(n);
    }
    s
}

/// Lower-cases a 40-digit hex hash. Anything else, and the all-zero "no results" row, gives None.
pub fn take_hash(input: &str) -> Option<String> {
    if input.len() != 40 || !input.bytes().all(|c| c.is_ascii_hexdigit()) || input.bytes().all(|c| c == b'0') {
        return None;
    }
    Some(input.to_ascii_lowercase())
}

pub fn format_size(b: i64) -> String {
    const GB: f64 = 1073741824.0;
    if b <= 0 {
        "-".into()
    } else if b as f64 >= GB * 1024.0 {
        format!("{:.2} TB", b as f64 / (GB * 1024.0))
    } else if b as f64 >= GB {
        format!("{:.2} GB", b as f64 / GB)
    } else if b >= 1048576 {
        format!("{:.0} MB", b as f64 / 1048576.0)
    } else if b >= 1024 {
        format!("{:.0} KB", b as f64 / 1024.0)
    } else {
        format!("{} B", b)
    }
}

pub fn format_age(then: i64, now: i64) -> String {
    if then <= 0 {
        return "-".into();
    }
    let d = (now - then).max(0);
    if d < 3600 {
        format!("{}m ago", d / 60)
    } else if d < 86400 {
        format!("{}h ago", d / 3600)
    } else if d < 86400 * 60 {
        format!("{}d ago", d / 86400)
    } else if d < 86400 * 730 {
        format!("{}mo ago", d / (86400 * 30))
    } else {
        format!("{}y ago", d / (86400 * 365))
    }
}

/// Percent-encodes `s` for a query string (spaces become %20).
pub fn url_encode(s: &str) -> String {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    let mut out = String::with_capacity(s.len() * 3);
    for &c in s.as_bytes() {
        if c.is_ascii_alphanumeric() || matches!(c, b'-' | b'_' | b'.' | b'~') {
            out.push(c as char);
        } else {
            out.push('%');
            out.push(HEX[(c >> 4) as usize] as char);
            out.push(HEX[(c & 15) as usize] as char);
        }
    }
    out
}

const TRACKERS: [&str; 5] = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://open.demonii.com:1337/announce",
];

/// magnet:?xt=urn:btih:...&dn=...&tr=...
pub fn magnet(hash: &str, name: &str) -> String {
    let mut m = format!("magnet:?xt=urn:btih:{}&dn={}", hash, url_encode(name));
    for t in TRACKERS {
        m.push_str("&tr=");
        m.push_str(&url_encode(t));
    }
    m
}

/// Adds one result to the merged list; a hash seen before keeps whichever copy has more seeders.
pub fn merge(list: &mut Vec<TorrentResult>, r: TorrentResult) {
    if let Some(old) = list.iter_mut().find(|o| o.hash == r.hash) {
        if r.seeders > old.seeders {
            *old = r;
        }
        return;
    }
    if list.len() < MAX_RESULTS {
        list.push(r);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn magnet_and_formats() {
        let m = magnet("abcdef0123456789abcdef0123456789abcdef01", "My File & Co (2024).mkv");
        assert!(m.starts_with("magnet:?xt=urn:btih:abcdef0123456789abcdef0123456789abcdef01&dn=My%20File%20%26%20Co%20%282024%29.mkv&tr=udp%3A%2F%2F"));
        assert!(!m.contains(' ') && !m.contains('\'') && !m.contains('"'));
        assert_eq!(format_size(0), "-");
        assert_eq!(format_size(3221225472), "3.00 GB");
        assert_eq!(format_age(1000, 1000 + 86400 * 3), "3d ago");
        assert_eq!(format_age(0, 5), "-");
        assert_eq!(url_encode("a b&c=d/é"), "a%20b%26c%3Dd%2F%C3%A9");
        assert!(SRC_COUNT <= 32 && default_mask() != 0 && source_name(SRC_NYAA) == "Nyaa");
        assert_eq!(SOURCES.len(), SRC_COUNT);
    }

    #[test]
    fn merge_keeps_more_seeded_copy() {
        let mut v = Vec::new();
        let a = TorrentResult { hash: "a".repeat(40), seeders: 1, ..Default::default() };
        let b = TorrentResult { hash: "a".repeat(40), seeders: 9, ..Default::default() };
        merge(&mut v, a);
        merge(&mut v, b);
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].seeders, 9);
    }
}
