//! Network side: blocking-free HTTP helpers plus one function per kind of request (search a source, suggestions, details,
//! fetching a .torrent). Uses rustls, so nothing depends on the system TLS (important on Android).

use super::parsers::{parse_for, parse_suggest};
use super::torrent::{parse_torrent, parse_tpb_files, Detail};
use super::*;
use std::time::Duration;

const AGENT: &str = "SailHighSea-TorrentSearch";

pub fn client() -> reqwest::Client {
    reqwest::Client::builder()
        .user_agent(AGENT)
        .timeout(Duration::from_secs(15))
        .connect_timeout(Duration::from_secs(8))
        .redirect(reqwest::redirect::Policy::limited(5))
        .build()
        .expect("http client")
}

/// Only plain URL characters are accepted (the query part is percent-encoded before it gets here).
pub fn url_plain(url: &str) -> bool {
    (url.starts_with("https://") || url.starts_with("http://"))
        && url.bytes().all(|c| c.is_ascii_alphanumeric() || b":/?&=.%_-~+,".contains(&c))
}

async fn read_body(resp: reqwest::Response, max: usize) -> Option<Vec<u8>> {
    if !resp.status().is_success() {
        return None;
    }
    if resp.content_length().map_or(false, |l| l as usize > max) {
        return None;
    }
    let mut resp = resp;
    let mut buf = Vec::new();
    while let Ok(Some(chunk)) = resp.chunk().await {
        buf.extend_from_slice(&chunk);
        if buf.len() > max {
            return None;
        }
    }
    Some(buf)
}

/// GET; None on a network error, a non-200 status or a body larger than `max`.
pub async fn http_get(c: &reqwest::Client, url: &str, max: usize) -> Option<Vec<u8>> {
    if !url_plain(url) {
        return None;
    }
    read_body(c.get(url).send().await.ok()?, max).await
}

pub async fn http_post_json(c: &reqwest::Client, url: &str, body: &serde_json::Value, max: usize) -> Option<Vec<u8>> {
    if !url_plain(url) {
        return None;
    }
    read_body(c.post(url).json(body).send().await.ok()?, max).await
}

fn text(b: &[u8]) -> String {
    String::from_utf8_lossy(b).into_owned()
}

fn build_url(src: usize, attempt: usize, query: &str) -> Option<String> {
    let tpl = SOURCES.get(src)?.urls.get(attempt)?;
    Some(tpl.replace("%s", &url_encode(query)))
}

/// Asks one source (trying its mirrors in turn). Err(()) means offline / not the expected reply; Ok(vec) may be empty.
pub async fn search_source(c: &reqwest::Client, src: usize, query: &str) -> Result<Vec<TorrentResult>, ()> {
    let kind = SOURCES[src].kind;
    let query: String = query.chars().take(160).collect();
    let mut attempt = 0;
    while let Some(url) = build_url(src, attempt, &query) {
        attempt += 1;
        let body = if kind == Kind::Knaben {
            let j = serde_json::json!({
                "search_type": "100%", "search_field": "title", "query": query, "order_by": "seeders",
                "order_direction": "desc", "from": 0, "size": 100, "hide_unsafe": true
            });
            http_post_json(c, &url, &j, BODY_MAX).await
        } else {
            http_get(c, &url, BODY_MAX).await
        };
        if let Some(rows) = body.and_then(|b| parse_for(kind, &text(&b), src)) {
            return Ok(rows); // zero rows is a valid empty answer; None means "not the expected reply", try the next mirror
        }
    }
    Err(())
}

pub async fn suggest(c: &reqwest::Client, query: &str) -> Option<Vec<String>> {
    let url = format!("https://suggestqueries.google.com/complete/search?client=firefox&q={}", url_encode(query));
    let body = http_get(c, &url, 64 * 1024).await?;
    parse_suggest(&text(&body), 8)
}

const CACHES: [&str; 2] = ["https://itorrents.net/torrent/%s.torrent", "https://itorrents.org/torrent/%s.torrent"];

/// A real .torrent for the hash from a public cache (only valid files are returned).
pub async fn fetch_torrent(c: &reqwest::Client, hash: &str) -> Option<Vec<u8>> {
    if take_hash(hash).is_none() {
        return None;
    }
    let up = hash.to_ascii_uppercase();
    for tpl in CACHES {
        let url = tpl.replace("%s", &up);
        if let Some(b) = http_get(c, &url, 8 * 1024 * 1024).await {
            if parse_torrent(&b, &mut Detail::new(hash)).is_some() {
                return Some(b);
            }
        }
    }
    None
}

/// File list: the Pirate Bay keeps lists of its own; for any torrent a .torrent cache keyed by the info-hash is tried.
pub async fn load_detail(c: &reqwest::Client, hash: &str, tid: i64) -> Detail {
    let mut d = Detail::new(hash);
    let mut ok = false;
    if tid > 0 {
        let url = format!("https://apibay.org/f.php?id={}", tid);
        if let Some(b) = http_get(c, &url, 2 * 1024 * 1024).await {
            ok = parse_tpb_files(&text(&b), &mut d).map_or(false, |n| n > 0);
        }
    }
    if !ok {
        d = Detail::new(hash);
        if let Some(b) = fetch_torrent(c, hash).await {
            ok = parse_torrent(&b, &mut d).is_some();
        }
    }
    d.state = if ok { "ok" } else { "failed" }.into();
    d
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn url_rules() {
        assert!(!url_plain("file:///etc/passwd"));
        assert!(!url_plain("https://example.com/\"; rm -rf /"));
        assert!(url_plain("https://apibay.org/q.php?q=ubuntu%20x&cat=0"));
        assert_eq!(build_url(SRC_TPB, 0, "a b").unwrap(), "https://apibay.org/q.php?q=a%20b&cat=0");
        assert!(build_url(SRC_TPB, 1, "a").is_none());
    }
}

/// Live check of every source with a real query. Not part of the normal test run:
///   cargo test --lib live_sources -- --ignored --nocapture
#[cfg(test)]
mod live {
    use super::*;

    #[tokio::test]
    #[ignore]
    async fn live_sources() {
        let c = client();
        let mut ok = 0;
        for src in 0..SRC_COUNT {
            let r = search_source(&c, src, "ubuntu").await;
            match &r {
                Ok(rows) => {
                    ok += 1;
                    println!("OK      {:<16} {} results", source_name(src), rows.len());
                }
                Err(()) => println!("OFFLINE {}", source_name(src)),
            }
        }
        println!("{} of {} sources answered", ok, SRC_COUNT);
        let sug = suggest(&c, "ubuntu").await;
        println!("suggestions: {:?}", sug);
    }
}
