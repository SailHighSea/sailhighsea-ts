//! Parsers for every source. Each returns `None` for a reply that is not in the expected format (a block page, an
//! error) so the caller can try a mirror, and `Some(vec)` otherwise (an empty list is a valid "no results").

use super::*;
use serde_json::Value;

// ---- JSON helpers ("12" and 12 are both fine) -----------------------------------------------------------------------

fn jget<'a>(v: &'a Value, key: &str) -> Option<&'a Value> {
    v.get(key)
}

fn jstr(v: &Value, key: &str) -> Option<String> {
    match v.get(key)? {
        Value::String(s) => Some(s.clone()),
        Value::Number(n) => Some(n.to_string()),
        Value::Bool(b) => Some(if *b { "true" } else { "false" }.into()),
        _ => None,
    }
}

fn jnum(v: &Value, key: &str, fallback: f64) -> f64 {
    match v.get(key) {
        Some(Value::Number(n)) => n.as_f64().unwrap_or(fallback),
        Some(Value::String(s)) => leading_f64(s).unwrap_or(fallback),
        Some(Value::Bool(b)) => *b as i32 as f64,
        _ => fallback,
    }
}

fn leading_f64(s: &str) -> Option<f64> {
    let s = s.trim_start();
    let mut end = 0;
    let b = s.as_bytes();
    let mut seen_dot = false;
    let mut seen_digit = false;
    while end < b.len() {
        match b[end] {
            b'0'..=b'9' => seen_digit = true,
            b'.' if !seen_dot => seen_dot = true,
            b'-' | b'+' if end == 0 => {}
            _ => break,
        }
        end += 1;
    }
    if !seen_digit {
        return None;
    }
    s[..end].parse().ok()
}

fn parse_json(body: &str) -> Option<Value> {
    serde_json::from_str(body).ok()
}

// ---- dates and sizes ------------------------------------------------------------------------------------------------

/// Days since 1970-01-01 for a civil date (proleptic Gregorian).
pub fn days_from_civil(y: i64, m: i64, d: i64) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let doy = (153 * (m + if m > 2 { -3 } else { 9 }) + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146097 + doe - 719468
}

fn leading_int(s: &str) -> Option<(i64, &str)> {
    let t = s.trim_start();
    let n = t.bytes().take_while(|c| c.is_ascii_digit()).count();
    if n == 0 {
        return None;
    }
    Some((t[..n].parse().ok()?, &t[n..]))
}

/// "h:m:s" (seconds optional) at the start of `s`.
fn parse_hms(s: &str) -> (i64, i64, i64) {
    let mut out = [0i64; 3];
    let mut rest = s.trim_start();
    for (i, slot) in out.iter_mut().enumerate() {
        match leading_int(rest) {
            Some((v, r)) => {
                *slot = v;
                rest = r;
                if i < 2 {
                    if let Some(r2) = rest.strip_prefix(':') {
                        rest = r2;
                    } else {
                        break;
                    }
                }
            }
            None => break,
        }
    }
    (out[0], out[1], out[2])
}

/// RFC 822 date as used by RSS: "Sat, 01 Nov 2025 12:00:00 -0000". 0 when it does not parse.
pub fn parse_rss_date(s: &str) -> i64 {
    const MON: [&str; 12] = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
    let c = match s.find(',') {
        Some(i) => &s[i + 1..],
        None => s,
    };
    let mut it = c.split_whitespace();
    let (Some(d), Some(mn), Some(y)) = (it.next(), it.next(), it.next()) else { return 0 };
    let (Ok(d), Ok(y)) = (d.parse::<i64>(), y.parse::<i64>()) else { return 0 };
    let Some(mi) = MON.iter().position(|m| mn.len() >= 3 && mn.is_char_boundary(3) && mn[..3].eq_ignore_ascii_case(m)) else { return 0 };
    let (hh, mm, ss) = parse_hms(it.next().unwrap_or(""));
    (days_from_civil(y, mi as i64 + 1, d) * 24 + hh) * 3600 + mm * 60 + ss
}

/// "2026-11-03T15:00:14.260Z" / "2026-04-25T15:35:00+00:00" -> unix time (UTC; a zone offset is ignored). 0 when it does not parse.
pub fn parse_iso_date(s: &str) -> i64 {
    let Some((y, r)) = leading_int(s) else { return 0 };
    let Some(r) = r.strip_prefix('-') else { return 0 };
    let Some((mo, r)) = leading_int(r) else { return 0 };
    let Some(r) = r.strip_prefix('-') else { return 0 };
    let Some((d, r)) = leading_int(r) else { return 0 };
    let (hh, mm, ss) = match r.strip_prefix(['T', ' ']) {
        Some(t) => parse_hms(t),
        None => (0, 0, 0),
    };
    (days_from_civil(y, mo, d) * 24 + hh) * 3600 + mm * 60 + ss
}

/// "1.4 GiB" / "700 MiB" -> bytes
pub fn parse_human_size(s: &str) -> i64 {
    let v = leading_f64(s).unwrap_or(0.0);
    let t = s.trim_start();
    let num_len = t.bytes().take_while(|c| c.is_ascii_digit() || matches!(c, b'.' | b'-' | b'+')).count();
    let unit = t[num_len..].trim_start().chars().next().map(|c| c.to_ascii_uppercase());
    let mul = match unit {
        Some('K') => 1024.0,
        Some('M') => 1048576.0,
        Some('G') => 1073741824.0,
        Some('T') => 1099511627776.0,
        _ => 1.0,
    };
    (v * mul) as i64
}

// ---- entities / urls --------------------------------------------------------------------------------------------------

const ENTS: &[(&str, u32)] = &[
    ("amp", '&' as u32), ("lt", '<' as u32), ("gt", '>' as u32), ("quot", '"' as u32), ("apos", '\'' as u32), ("nbsp", ' ' as u32),
    ("auml", 0xE4), ("ouml", 0xF6), ("uuml", 0xFC), ("euml", 0xEB), ("iuml", 0xEF),
    ("Auml", 0xC4), ("Ouml", 0xD6), ("Uuml", 0xDC), ("szlig", 0xDF),
    ("aacute", 0xE1), ("eacute", 0xE9), ("iacute", 0xED), ("oacute", 0xF3), ("uacute", 0xFA),
    ("agrave", 0xE0), ("egrave", 0xE8), ("ograve", 0xF2), ("ntilde", 0xF1), ("ccedil", 0xE7),
    ("acirc", 0xE2), ("ecirc", 0xEA), ("ocirc", 0xF4), ("Eacute", 0xC9), ("ndash", 0x2013), ("mdash", 0x2014),
];

/// Replaces the HTML entities that show up in titles (&amp; &#39; &eacute; ...).
pub fn decode_entities(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut rest = s;
    while let Some(i) = rest.find('&') {
        out.push_str(&rest[..i]);
        let tail = &rest[i..];
        let mut done = false;
        if let Some(semi) = tail.find(';') {
            if semi <= 10 {
                let name = &tail[1..semi];
                let cp = if let Some(num) = name.strip_prefix('#') {
                    let (digits, radix) = match num.strip_prefix(['x', 'X']) {
                        Some(h) => (h, 16),
                        None => (num, 10),
                    };
                    u32::from_str_radix(digits, radix).ok()
                } else {
                    ENTS.iter().find(|(n, _)| *n == name).map(|(_, c)| *c)
                };
                if let Some(ch) = cp.filter(|&c| c >= 32).and_then(char::from_u32) {
                    out.push(ch);
                    rest = &tail[semi + 1..];
                    done = true;
                }
            }
        }
        if !done {
            out.push('&');
            rest = &tail[1..];
        }
    }
    out.push_str(rest);
    out
}

fn url_decode(b: &[u8]) -> String {
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'%' && i + 2 < b.len() && b[i + 1].is_ascii_hexdigit() && b[i + 2].is_ascii_hexdigit() {
            let h = std::str::from_utf8(&b[i + 1..i + 3]).unwrap_or("00");
            out.push(u8::from_str_radix(h, 16).unwrap_or(b'?'));
            i += 3;
        } else if b[i] == b'+' {
            out.push(b' ');
            i += 1;
        } else {
            out.push(b[i]);
            i += 1;
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

// ---- Pirate Bay -------------------------------------------------------------------------------------------------------

pub fn tpb_category(c: i32) -> &'static str {
    const T: &[(i32, &str)] = &[
        (101, "Music"), (102, "Audio books"), (103, "Sound clips"), (104, "FLAC"), (199, "Other audio"),
        (201, "Movies"), (202, "Movies DVDR"), (203, "Music videos"), (204, "Movie clips"), (205, "TV shows"),
        (206, "Handheld video"), (207, "HD Movies"), (208, "HD TV shows"), (209, "3D"), (211, "4K Movies"), (212, "4K TV shows"), (299, "Other video"),
        (301, "Windows"), (302, "Mac"), (303, "UNIX"), (304, "Handheld apps"), (305, "iOS"), (306, "Android"), (399, "Other software"),
        (401, "PC games"), (402, "Mac games"), (403, "PSx games"), (404, "XBOX360 games"), (405, "Wii games"), (406, "Handheld games"),
        (407, "iOS games"), (408, "Android games"), (499, "Other games"),
        (601, "E-books"), (602, "Comics"), (603, "Pictures"), (604, "Covers"), (605, "Physibles"), (699, "Other"),
    ];
    if let Some((_, n)) = T.iter().find(|(code, _)| *code == c) {
        return n;
    }
    match c / 100 {
        1 => "Audio",
        2 => "Video",
        3 => "Applications",
        4 => "Games",
        5 => "Adult",
        6 => "Other",
        _ => "",
    }
}

/// apibay.org: a JSON array of flat objects whose numbers are quoted strings.
pub fn parse_tpb(body: &str, max: usize) -> Option<Vec<TorrentResult>> {
    let root = parse_json(body)?;
    let arr = root.as_array()?;
    let mut out = Vec::new();
    for it in arr {
        if out.len() >= max {
            break;
        }
        let Some(hash) = jstr(it, "info_hash").and_then(|h| take_hash(&h)) else { continue };
        let user = jstr(it, "username").unwrap_or_default();
        let cat = tpb_category(jnum(it, "category", 0.0) as i32);
        out.push(TorrentResult {
            name: trunc(decode_entities(&jstr(it, "name").unwrap_or_default()), 199),
            hash,
            source: SRC_TPB,
            seeders: jnum(it, "seeders", 0.0) as i32,
            leechers: jnum(it, "leechers", 0.0) as i32,
            size: jnum(it, "size", 0.0) as i64,
            added: jnum(it, "added", 0.0) as i64,
            tid: jnum(it, "id", 0.0) as i64,
            files: jnum(it, "num_files", 0.0) as i32,
            detail: trunc(format!("{}{}{}", cat, if user.is_empty() { "" } else { " - " }, user), 39),
            ..Default::default()
        });
    }
    Some(out)
}

// ---- YTS ---------------------------------------------------------------------------------------------------------------

/// yts.mx: data.movies[] each with torrents[] (one result per quality).
pub fn parse_yts(body: &str, max: usize) -> Option<Vec<TorrentResult>> {
    let root = parse_json(body)?;
    let data = jget(&root, "data")?;
    let movies = data.get("movies").and_then(|m| m.as_array());
    if data.get("movie_count").is_none() && movies.is_none() {
        return None;
    }
    let mut out = Vec::new();
    for m in movies.into_iter().flatten() {
        let Some(tors) = m.get("torrents").and_then(|t| t.as_array()) else { continue };
        for t in tors {
            if out.len() >= max {
                break;
            }
            let Some(hash) = jstr(t, "hash").and_then(|h| take_hash(&h)) else { continue };
            let q = jstr(t, "quality").unwrap_or_default();
            let ty = jstr(t, "type").unwrap_or_default();
            let title = jstr(m, "title_long").or_else(|| jstr(m, "title")).unwrap_or_default();
            out.push(TorrentResult {
                name: trunc(format!("{} [{} {}]", title, q, ty), 199),
                hash,
                source: SRC_YTS,
                detail: trunc(format!("{} {}", q, ty), 39),
                seeders: jnum(t, "seeds", 0.0) as i32,
                leechers: jnum(t, "peers", 0.0) as i32,
                size: jnum(t, "size_bytes", 0.0) as i64,
                added: jnum(t, "date_uploaded_unix", 0.0) as i64,
                ..Default::default()
            });
        }
    }
    Some(out)
}

// ---- Nyaa (RSS) ----------------------------------------------------------------------------------------------------------

/// Text between <tag> and </tag> inside `item`; decodes CDATA and the XML entities.
fn xml_tag(item: &str, tag: &str) -> Option<String> {
    let open = format!("<{}>", tag);
    let close = format!("</{}>", tag);
    let s = item.find(&open)? + open.len();
    let e = s + item[s..].find(&close)?;
    let mut inner = &item[s..e];
    if let Some(c) = inner.strip_prefix("<![CDATA[") {
        inner = c.strip_suffix("]]>").unwrap_or(c);
    }
    Some(decode_entities(inner))
}

pub fn parse_nyaa(body: &str, max: usize) -> Option<Vec<TorrentResult>> {
    if !body.contains("<rss") {
        return None;
    }
    let mut out = Vec::new();
    let mut p = 0;
    while out.len() < max {
        let Some(i) = body[p..].find("<item>") else { break };
        let start = p + i;
        let Some(j) = body[start..].find("</item>") else { break };
        let item = &body[start..start + j];
        if let Some(hash) = xml_tag(item, "nyaa:infoHash").and_then(|h| take_hash(&h)) {
            out.push(TorrentResult {
                hash,
                source: SRC_NYAA,
                name: trunc(xml_tag(item, "title").unwrap_or_default(), 199),
                seeders: xml_tag(item, "nyaa:seeders").map(|s| leading_int(&s).map_or(0, |v| v.0 as i32)).unwrap_or(0),
                leechers: xml_tag(item, "nyaa:leechers").map(|s| leading_int(&s).map_or(0, |v| v.0 as i32)).unwrap_or(0),
                size: xml_tag(item, "nyaa:size").map(|s| parse_human_size(&s)).unwrap_or(0),
                added: xml_tag(item, "pubDate").map(|s| parse_rss_date(&s)).unwrap_or(0),
                detail: trunc(xml_tag(item, "nyaa:category").unwrap_or_default(), 39),
                ..Default::default()
            });
        }
        p = start + j + 7;
    }
    Some(out)
}

// ---- Torrents.csv / BitSearch / Knaben ---------------------------------------------------------------------------------------

/// torrents-csv.com: {"torrents":[{"infohash","name","size_bytes","created_unix","seeders","leechers"}, ...]}
pub fn parse_torrents_csv(body: &str, max: usize) -> Option<Vec<TorrentResult>> {
    let root = parse_json(body)?;
    let list = root.get("torrents")?.as_array()?;
    let mut out = Vec::new();
    for it in list {
        if out.len() >= max {
            break;
        }
        let Some(hash) = jstr(it, "infohash").and_then(|h| take_hash(&h)) else { continue };
        out.push(TorrentResult {
            hash,
            name: trunc(jstr(it, "name").unwrap_or_default(), 199),
            seeders: jnum(it, "seeders", 0.0) as i32,
            leechers: jnum(it, "leechers", 0.0) as i32,
            size: jnum(it, "size_bytes", 0.0) as i64,
            added: jnum(it, "created_unix", 0.0) as i64,
            ..Default::default()
        });
    }
    Some(out)
}

/// bitsearch.to: {"success":true,"results":[{"infohash","title","size","seeders","leechers","updatedAt"}, ...]}
pub fn parse_bitsearch(body: &str, max: usize) -> Option<Vec<TorrentResult>> {
    let root = parse_json(body)?;
    let list = root.get("results")?.as_array()?;
    let mut out = Vec::new();
    for it in list {
        if out.len() >= max {
            break;
        }
        let Some(hash) = jstr(it, "infohash").and_then(|h| take_hash(&h)) else { continue };
        out.push(TorrentResult {
            hash,
            name: trunc(jstr(it, "title").unwrap_or_default(), 199),
            seeders: jnum(it, "seeders", 0.0) as i32,
            leechers: jnum(it, "leechers", 0.0) as i32,
            size: jnum(it, "size", 0.0) as i64,
            added: jstr(it, "updatedAt").map(|d| parse_iso_date(&d)).unwrap_or(0),
            ..Default::default()
        });
    }
    Some(out)
}

/// api.knaben.org: {"hits":[{"hash","title","bytes","date","seeders","peers","category"}, ...]}
pub fn parse_knaben(body: &str, max: usize) -> Option<Vec<TorrentResult>> {
    let root = parse_json(body)?;
    let list = root.get("hits")?.as_array()?;
    let mut out = Vec::new();
    for it in list {
        if out.len() >= max {
            break;
        }
        let Some(hash) = jstr(it, "hash").and_then(|h| take_hash(&h)) else { continue };
        out.push(TorrentResult {
            hash,
            name: trunc(jstr(it, "title").unwrap_or_default(), 199),
            seeders: jnum(it, "seeders", 0.0) as i32,
            leechers: jnum(it, "peers", 0.0) as i32,
            size: jnum(it, "bytes", 0.0) as i64,
            added: jstr(it, "date").map(|d| parse_iso_date(&d)).unwrap_or(0),
            detail: trunc(jstr(it, "category").unwrap_or_default(), 39),
            ..Default::default()
        });
    }
    Some(out)
}

// ---- TheRARBG (RSS search feed) -----------------------------------------------------------------------------------------

/// Text of the first <tag ...>...</tag> in `item`, where the opening tag may carry attributes (like `<guid isPermaLink="false">`).
fn xml_tag_attr(item: &str, tag: &str) -> Option<String> {
    let open = format!("<{}", tag);
    let mut from = 0;
    loop {
        let i = from + item[from..].find(&open)?;
        let after = i + open.len();
        match item.as_bytes().get(after) {
            Some(b'>') | Some(b' ') => {
                let gt = after + item[after..].find('>')?;
                let end = gt + 1 + item[gt + 1..].find(&format!("</{}>", tag))?;
                let mut inner = &item[gt + 1..end];
                if let Some(c) = inner.strip_prefix("<![CDATA[") {
                    inner = c.strip_suffix("]]>").unwrap_or(c);
                }
                return Some(decode_entities(inner));
            }
            _ => from = after,
        }
    }
}

/// therarbg.com/api/rss/search/?q=...: <item> with <title>, <guid> (the info hash), <pubDate>, <category>, a magnet <enclosure>
/// and a description like "5.8 GB - 53 seeders - quality 0/100". The site gives no leecher count.
pub fn parse_rarbg(body: &str, max: usize) -> Option<Vec<TorrentResult>> {
    if !body.contains("<rss") {
        return None;
    }
    let mut out = Vec::new();
    let mut p = 0;
    while out.len() < max {
        let Some(i) = body[p..].find("<item>") else { break };
        let start = p + i;
        let Some(j) = body[start..].find("</item>") else { break };
        let item = &body[start..start + j];
        p = start + j + 7;
        let hash = xml_tag_attr(item, "guid").and_then(|g| take_hash(g.trim()));
        let Some(hash) = hash else { continue };
        let desc = xml_tag_attr(item, "description").unwrap_or_default();
        let mut parts = desc.split(" - ");
        let size = parts.next().map(parse_human_size).unwrap_or(0);
        let seeders = parts.find(|s| s.contains("seeders")).and_then(|s| leading_int(s)).map_or(0, |v| v.0 as i32);
        out.push(TorrentResult {
            hash,
            name: trunc(xml_tag_attr(item, "title").unwrap_or_default(), 199),
            seeders,
            size,
            added: xml_tag_attr(item, "pubDate").map(|s| parse_rss_date(&s)).unwrap_or(0),
            detail: trunc(xml_tag_attr(item, "category").unwrap_or_default(), 39),
            ..Default::default()
        });
    }
    Some(out)
}

// ---- generic HTML scraper ---------------------------------------------------------------------------------------------------

fn is_b32(c: u8) -> bool {
    c.is_ascii_alphabetic() || (b'2'..=b'7').contains(&c)
}

/// 32 base32 characters -> 40 hex digits
fn base32_to_hex(input: &[u8]) -> Option<String> {
    if input.len() != 32 || !input.iter().all(|&c| is_b32(c)) {
        return None;
    }
    let mut acc: u64 = 0;
    let mut bits = 0u32;
    let mut out = String::with_capacity(40);
    for &c in input {
        let v = if c.is_ascii_uppercase() { c - b'A' } else if c.is_ascii_lowercase() { c - b'a' } else { c - b'2' + 26 };
        acc = (acc << 5) | v as u64;
        bits += 5;
        while bits >= 4 && out.len() < 40 {
            bits -= 4;
            out.push(b"0123456789abcdef"[((acc >> bits) & 15) as usize] as char);
        }
        acc &= (1u64 << bits) - 1;
    }
    (out.len() == 40).then_some(out)
}

fn find_from(hay: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    if from >= hay.len() || needle.is_empty() {
        return None;
    }
    hay[from..].windows(needle.len()).position(|w| w == needle).map(|i| i + from)
}


/// Generic scraper for sites without an API. Takes (1) every magnet link on the page (hash + "dn" name) and (2) every link
/// that carries a 40-digit hash, named after the link text. Seeders and sizes are not read from arbitrary HTML, so they
/// stay 0 / unknown for these sources. None when the reply is not a web page.
pub fn parse_html(body: &str, src: usize, max: usize) -> Option<Vec<TorrentResult>> {
    if !body.contains('<') {
        return None;
    }
    let b = body.as_bytes();
    let mut out: Vec<TorrentResult> = Vec::new();
    let have = |out: &Vec<TorrentResult>, h: &str| out.iter().any(|r| r.hash == h);

    // 1. magnet links
    let mut pos = 0;
    while out.len() < max {
        let Some(i) = find_from(b, b"btih:", pos) else { break };
        let p = i + 5;
        pos = p;
        let run = b[p..].iter().take(41).take_while(|c| c.is_ascii_alphanumeric()).count();
        let hash = if run == 40 {
            take_hash(&String::from_utf8_lossy(&b[p..p + 40]))
        } else if run == 32 && is_b32(b[p]) {
            base32_to_hex(&b[p..p + 32]).and_then(|h| take_hash(&h))
        } else {
            None
        };
        let Some(hash) = hash else { continue };
        if have(&out, &hash) {
            continue;
        }
        let mut end = p;
        while end < b.len() && end - p < 1500 && !matches!(b[end], b'"' | b'\'' | b'<' | b' ' | b'\n') {
            end += 1;
        }
        let mut name = String::new();
        let mut dn = p + run;
        while dn < end && !b[dn..].starts_with(b"dn=") {
            dn += 1;
        }
        if dn < end {
            dn += 3;
            let mut l = 0;
            while dn + l < end && b[dn + l] != b'&' && l < 199 {
                l += 1;
            }
            name = trunc(url_decode(&b[dn..dn + l]), 199);
        }
        if name.is_empty() {
            name = hash.clone();
        }
        out.push(TorrentResult { hash, name, source: src, ..Default::default() });
    }

    // 2. links containing a hash
    let mut pos = 0;
    while out.len() < max {
        let Some(i) = find_from(b, b"href=", pos) else { break };
        let p = i + 5;
        pos = p;
        let Some(&q) = b.get(p) else { break };
        if q != b'"' && q != b'\'' {
            continue;
        }
        let v = p + 1;
        let Some(rel) = b[v..].iter().position(|&c| c == q) else { continue };
        let e = v + rel;
        if e - v > 400 {
            continue;
        }
        let mut found: Option<String> = None;
        let mut h = v;
        while h + 40 <= e {
            let skip = (h > v && b[h - 1].is_ascii_alphanumeric()) || (h + 40 < e && b[h + 40].is_ascii_alphanumeric());
            if !skip {
                if let Some(x) = take_hash(&String::from_utf8_lossy(&b[h..h + 40])) {
                    found = Some(x);
                    break;
                }
            }
            h += 1;
        }
        let Some(hash) = found else { continue };
        if have(&out, &hash) {
            continue;
        }
        // link text up to </a>, tags skipped
        let mut text: Vec<u8> = Vec::new();
        if let Some(gt) = b[e..].iter().position(|&c| c == b'>') {
            if gt < 300 {
                let mut tag = false;
                let mut t = e + gt + 1;
                while t < b.len() && text.len() < 199 && !(b[t] == b'<' && b.get(t + 1) == Some(&b'/') && matches!(b.get(t + 2), Some(b'a' | b'A'))) {
                    match b[t] {
                        b'<' => tag = true,
                        b'>' => tag = false,
                        c if !tag && c != b'\n' && c != b'\r' && c != b'\t' => text.push(c),
                        _ => {}
                    }
                    t += 1;
                }
            }
        }
        let mut name = decode_entities(&String::from_utf8_lossy(&text)).trim().to_string();
        if name.chars().count() < 3 {
            name = hash.clone();
        }
        out.push(TorrentResult { hash, name: trunc(name, 199), source: src, ..Default::default() });
    }
    Some(out)
}

// ---- suggestions ------------------------------------------------------------------------------------------------------------

/// ["query", ["a", "b", ...]]; None when it is not that.
pub fn parse_suggest(body: &str, max: usize) -> Option<Vec<String>> {
    let root = parse_json(body)?;
    let arr = root.as_array()?;
    let list = arr.get(1)?.as_array()?;
    Some(list.iter().filter_map(|s| s.as_str()).filter(|s| !s.is_empty()).take(max).map(|s| trunc(s.to_string(), 159)).collect())
}

/// Dispatches to the parser for a source's kind.
pub fn parse_for(kind: Kind, body: &str, src: usize) -> Option<Vec<TorrentResult>> {
    let mut rows = match kind {
        Kind::Tpb => parse_tpb(body, PER_SOURCE_MAX),
        Kind::Yts => parse_yts(body, PER_SOURCE_MAX),
        Kind::Rss => parse_nyaa(body, PER_SOURCE_MAX),
        Kind::Html => parse_html(body, src, PER_SOURCE_MAX),
        Kind::Csv => parse_torrents_csv(body, PER_SOURCE_MAX),
        Kind::Bit => parse_bitsearch(body, PER_SOURCE_MAX),
        Kind::Knaben => parse_knaben(body, PER_SOURCE_MAX),
        Kind::Rarbg => parse_rarbg(body, PER_SOURCE_MAX),
    }?;
    for r in &mut rows {
        r.source = src;
    }
    Some(rows)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tpb() {
        let body = r#"[{"id":"1","name":"Ubuntu 24.04 \"LTS\" &amp; d&eacute;j&#224; &#x41; desktop","info_hash":"ABCDEF0123456789ABCDEF0123456789ABCDEF01","leechers":"4","seeders":"120","num_files":"1","size":"6144000000","username":"canonical","added":"1700000000"},{"id":"2","name":"bad hash","info_hash":"zz","seeders":"1"}]"#;
        let r = parse_tpb(body, 10).unwrap();
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].name, "Ubuntu 24.04 \"LTS\" & déjà A desktop");
        assert_eq!(r[0].hash, "abcdef0123456789abcdef0123456789abcdef01");
        assert_eq!((r[0].seeders, r[0].leechers), (120, 4));
        assert_eq!((r[0].size, r[0].added), (6144000000, 1700000000));
        assert_eq!((r[0].source, r[0].tid, r[0].files), (SRC_TPB, 1, 1));
        // apibay answers "no results" with one all-zero row
        assert_eq!(parse_tpb(r#"[{"id":"0","name":"No results returned","info_hash":"0000000000000000000000000000000000000000"}]"#, 10).unwrap().len(), 0);
        assert!(parse_tpb("not json", 10).is_none());
        assert!(parse_tpb(r#"{"a":1}"#, 10).is_none());
        assert_eq!(parse_tpb("[]", 10).unwrap().len(), 0);
    }

    #[test]
    fn yts() {
        let body = r#"{"status":"ok","data":{"movie_count":1,"movies":[{"title_long":"Big Buck Bunny (2008)","torrents":[
            {"hash":"1111111111111111111111111111111111111111","quality":"720p","type":"web","seeds":50,"peers":5,"size_bytes":900000000,"date_uploaded_unix":1600000000},
            {"hash":"2222222222222222222222222222222222222222","quality":"1080p","type":"bluray","seeds":80,"peers":9,"size_bytes":1900000000,"date_uploaded_unix":1600000100}]}]}}"#;
        let r = parse_yts(body, 10).unwrap();
        assert_eq!(r.len(), 2);
        assert_eq!(r[0].name, "Big Buck Bunny (2008) [720p web]");
        assert_eq!((r[1].seeders, r[1].size), (80, 1900000000));
        assert_eq!(parse_yts(r#"{"status":"ok","data":{"movie_count":0}}"#, 10).unwrap().len(), 0);
        assert!(parse_yts("<html>blocked</html>", 10).is_none());
        assert_eq!(parse_yts(body, 1).unwrap().len(), 1);
    }

    #[test]
    fn nyaa() {
        let body = "<?xml version=\"1.0\"?><rss><channel>\
            <item><title>[Group] Show &amp; Friends - 01 [1080p]</title><pubDate>Sat, 01 Nov 2025 12:00:00 -0000</pubDate>\
            <nyaa:seeders>33</nyaa:seeders><nyaa:leechers>2</nyaa:leechers><nyaa:infoHash>ABCDEF0123456789ABCDEF0123456789ABCDEF01</nyaa:infoHash>\
            <nyaa:size>1.5 GiB</nyaa:size><nyaa:category>Anime - English-translated</nyaa:category></item>\
            <item><title><![CDATA[Second <one>]]></title><nyaa:infoHash>nothex</nyaa:infoHash></item>\
            </channel></rss>";
        let r = parse_nyaa(body, 10).unwrap();
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].name, "[Group] Show & Friends - 01 [1080p]");
        assert_eq!((r[0].seeders, r[0].leechers), (33, 2));
        assert_eq!(r[0].size, (1.5 * 1073741824.0) as i64);
        assert_eq!(r[0].added, 1761998400); // 2025-11-01 12:00:00 UTC
        assert_eq!(r[0].detail, "Anime - English-translated");
        assert_eq!(parse_nyaa("<rss></rss>", 10).unwrap().len(), 0);
        assert!(parse_nyaa("<html>blocked</html>", 10).is_none());
    }

    #[test]
    fn html() {
        let page = "<html><body>\
            <a href=\"magnet:?xt=urn:btih:ABCDEF0123456789ABCDEF0123456789ABCDEF01&amp;dn=My+Movie%20%282020%29&amp;tr=udp%3A%2F%2Fx\">m</a>\
            <a href='magnet:?xt=urn:btih:MFRGGZDFMZTWQ2LKNNWG23TPOBYXE43U&dn=base32'>b</a>\
            <a href=\"/torrent/1111111111111111111111111111111111111111/some-name\"><b>Some</b> Name &amp; Co</a>\
            <a href=\"/torrent/ABCDEF0123456789ABCDEF0123456789ABCDEF01/dup\">dup</a>\
            <a href=\"/torrent/abcdef0123456789abcdef0123456789abcdef0123/too-long\">x</a>\
            </body></html>";
        let r = parse_html(page, 7, 10).unwrap();
        assert_eq!(r.len(), 3);
        assert_eq!((r[0].hash.as_str(), r[0].name.as_str(), r[0].source), ("abcdef0123456789abcdef0123456789abcdef01", "My Movie (2020)", 7));
        assert_eq!(r[1].name, "base32");
        assert_eq!(r[1].hash.len(), 40);
        assert_eq!((r[2].hash.as_str(), r[2].name.as_str()), ("1111111111111111111111111111111111111111", "Some Name & Co"));
        assert!(parse_html("plain text, no markup", 7, 10).is_none());
        assert_eq!(parse_html("<html>nothing</html>", 7, 10).unwrap().len(), 0);
        assert_eq!(parse_html(page, 7, 1).unwrap().len(), 1);
    }

    #[test]
    fn api_sources() {
        let r = parse_torrents_csv(r#"{"torrents":[{"infohash":"232cd67eb3ffbd7c37bf9ec3ee887417e5ae1ee6","name":"Ubuntu Bible","size_bytes":8554738,"created_unix":1700000000,"seeders":27,"leechers":3}]}"#, 5).unwrap();
        assert_eq!(r.len(), 1);
        assert_eq!((r[0].seeders, r[0].leechers, r[0].size, r[0].added), (27, 3, 8554738, 1700000000));
        assert_eq!(r[0].name, "Ubuntu Bible");
        assert!(parse_torrents_csv("<html>", 5).is_none());
        let r = parse_bitsearch(r#"{"success":true,"results":[{"infohash":"D540FC48EB12F2833163EED6421D449DD8F1CE1F","title":"ubuntu.iso","size":2097152000,"seeders":28,"leechers":41,"updatedAt":"2026-11-03T15:00:14.260Z"}]}"#, 5).unwrap();
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].hash, "d540fc48eb12f2833163eed6421d449dd8f1ce1f");
        assert_eq!((r[0].added, r[0].size), (1793718014, 2097152000));
        assert!(parse_bitsearch(r#"{"success":false}"#, 5).is_none());
        let r = parse_knaben(r#"{"hits":[{"hash":"DAFC8C076CA2F3ED376EEAE7C76A0D6BE2415C45","title":"u.iso","bytes":6517612871,"date":"2026-04-25T15:35:00+00:00","seeders":130,"peers":55,"category":"PC / Unix"},{"hash":null,"title":"x"}]}"#, 5).unwrap();
        assert_eq!(r.len(), 1);
        assert_eq!((r[0].seeders, r[0].leechers, r[0].size), (130, 55, 6517612871));
        assert_eq!(r[0].detail, "PC / Unix");
        assert_eq!(parse_knaben(r#"{"hits":[]}"#, 5).unwrap().len(), 0);
    }

    #[test]
    fn rarbg_rss() {
        let body = "<?xml version=\"1.0\"?><rss version=\"2.0\"><channel><title>x</title>\n            <item><title>ubuntu-24.04.1-desktop-amd64.iso</title><link>https://therarbg.com/post-detail/8a6ee9/x/</link>\n            <guid isPermaLink=\"false\">4a3f5e08bcef825718eda30637230585e3330599</guid><pubDate>Thu, 23 Apr 2026 12:56:50 +0000</pubDate>\n            <description>5.8 GB - 53 seeders - quality 0/100</description><category>Apps</category>\n            <enclosure url=\"magnet:?xt=urn:btih:4a3f5e08bcef825718eda30637230585e3330599&amp;dn=x\" length=\"6203355136\" type=\"application/x-bittorrent\"/></item>\n            <item><title>Tom &amp; Jerry</title><guid isPermaLink=\"false\">not-a-hash</guid><description>1 MB - 2 seeders</description></item>\n            <item><title><![CDATA[Second <one>]]></title><guid>1111111111111111111111111111111111111111</guid><description>700 MB - 7 seeders - quality 1/100</description></item>\n            </channel></rss>";
        let r = parse_rarbg(body, 10).unwrap();
        assert_eq!(r.len(), 2); // the row without a real hash is skipped
        assert_eq!(r[0].name, "ubuntu-24.04.1-desktop-amd64.iso");
        assert_eq!(r[0].hash, "4a3f5e08bcef825718eda30637230585e3330599");
        assert_eq!((r[0].seeders, r[0].leechers), (53, 0));
        assert_eq!(r[0].size, (5.8 * 1073741824.0) as i64);
        assert_eq!(r[0].added, 1776949010); // 2026-04-23 12:56:50 UTC
        assert_eq!(r[0].detail, "Apps");
        assert_eq!((r[1].name.as_str(), r[1].seeders, r[1].size), ("Second <one>", 7, 734003200));
        assert_eq!(parse_rarbg("<rss></rss>", 10).unwrap().len(), 0);
        assert!(parse_rarbg("<html>blocked</html>", 10).is_none());
        assert_eq!(parse_rarbg(body, 1).unwrap().len(), 1);
    }

    #[test]
    fn suggest_and_misc() {
        let s = parse_suggest(r#"["ubu",["ubuntu","ubuntu download",""]]"#, 4).unwrap();
        assert_eq!(s, vec!["ubuntu", "ubuntu download"]);
        assert!(parse_suggest("<html></html>", 4).is_none());
        assert!(parse_suggest(r#"["x"]"#, 4).is_none());
        assert_eq!(tpb_category(207), "HD Movies");
        assert_eq!(tpb_category(250), "Video");
        assert_eq!(parse_human_size("700 MiB"), 734003200);
        assert_eq!(parse_iso_date("2026-11-03T15:00:14.260Z"), 1793718014);
        assert_eq!(decode_entities("a &unknown; b &amp c"), "a &unknown; b &amp c");
    }
}
