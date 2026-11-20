//! Update check: asks GitHub for the newest release. Nothing is downloaded or installed; the app only says a newer one exists.

use super::net::http_get;
use serde::Serialize;

const API_STABLE: &str = "https://api.github.com/repos/SailHighSea/sailhighsea-ts/releases/latest";
const API_ALL: &str = "https://api.github.com/repos/SailHighSea/sailhighsea-ts/releases?per_page=5";

#[derive(Clone, Debug, Serialize)]
pub struct Release {
    pub tag: String,
    pub url: String,
}

/// "v0.0.4-dev-2" -> ([0,0,4], pre = true, dev = 2). A final release has pre = false (and counts as newer than its dev builds).
fn parse_version(v: &str) -> ([i64; 3], bool, i64) {
    let v = v.trim_start_matches(['v', 'V']);
    let (core, rest) = match v.find('-') {
        Some(i) => (&v[..i], Some(&v[i + 1..])),
        None => (v, None),
    };
    let mut out = [0i64; 3];
    for (i, part) in core.split('.').take(3).enumerate() {
        out[i] = part.chars().take_while(|c| c.is_ascii_digit()).collect::<String>().parse().unwrap_or(0);
    }
    let dev = rest.and_then(|r| r.rsplit('-').next()).and_then(|n| n.parse().ok()).unwrap_or(0);
    (out, rest.is_some(), dev)
}

/// True when `candidate` ("v0.0.4" or "0.0.4") is newer than `current` ("0.0.3", "0.0.4-dev-2").
pub fn version_newer(candidate: &str, current: &str) -> bool {
    let (c, cpre, cdev) = parse_version(candidate);
    let (u, upre, udev) = parse_version(current);
    if c != u {
        return c > u;
    }
    if cpre != upre {
        return upre; // the final release beats its own pre-releases, never the other way round
    }
    cpre && cdev > udev
}

/// Like `version_newer`, but only the version number counts (0.0.4-dev-9 vs 0.0.4 is not an update either way).
pub fn version_core_newer(candidate: &str, current: &str) -> bool {
    parse_version(candidate).0 > parse_version(current).0
}

pub async fn latest_release(c: &reqwest::Client, include_dev: bool) -> Option<Release> {
    let body = http_get(c, if include_dev { API_ALL } else { API_STABLE }, 96 * 1024).await?;
    let v: serde_json::Value = serde_json::from_slice(&body).ok()?;
    let rel = if include_dev {
        v.as_array()?.iter().find(|r| r.get("draft").and_then(|d| d.as_bool()) != Some(true))?.clone()
    } else {
        v
    };
    let tag = rel.get("tag_name")?.as_str()?.to_string();
    let url = rel.get("html_url")?.as_str()?.to_string();
    url.starts_with("https://github.com/").then_some(Release { tag, url }) // it will be opened in the browser
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions() {
        assert!(version_newer("v0.0.4", "0.0.3"));
        assert!(!version_newer("v0.0.3", "0.0.3"));
        assert!(version_newer("v0.0.4", "0.0.4-dev-2"));
        assert!(!version_newer("v0.0.4-dev-2", "0.0.4"));
        assert!(version_newer("v0.0.4-dev-3", "0.0.4-dev-2"));
        assert!(version_newer("v0.1.0", "0.0.9"));
        assert!(!version_core_newer("v0.0.4-dev-9", "0.0.4"));
        assert!(version_core_newer("v0.0.5", "0.0.4-dev-1"));
    }
}
