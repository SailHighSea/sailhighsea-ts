//! Torrent details: the file list of one torrent (from apibay or a .torrent cache) and a small bencode reader.

use super::*;
use serde::Serialize;

#[derive(Clone, Debug, Default, Serialize)]
pub struct FileEntry {
    pub path: String,
    pub size: i64,
}

#[derive(Clone, Debug, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Detail {
    pub hash: String,
    /// "running" | "ok" | "failed"
    pub state: String,
    pub file_count: i32,
    pub shown: i32,
    pub total: i64,
    pub files: Vec<FileEntry>,
}

impl Detail {
    pub fn new(hash: &str) -> Self {
        Detail { hash: hash.into(), state: "running".into(), ..Default::default() }
    }

    fn add(&mut self, path: &str, size: i64) {
        if self.files.len() < DETAIL_FILES {
            self.files.push(FileEntry { path: trunc(path.to_string(), 239), size });
            self.shown = self.files.len() as i32;
        }
        self.file_count += 1;
        self.total += size.max(0);
    }
}

/// apibay f.php: [{"name":["dir/file"],"size":[123]}, ...]; the values come as one-element arrays or as {"0": ...}.
/// Returns the number of files added, None when the reply is not that.
pub fn parse_tpb_files(body: &str, d: &mut Detail) -> Option<usize> {
    let root: serde_json::Value = serde_json::from_str(body).ok()?;
    let arr = root.as_array()?;
    let first = |v: Option<&serde_json::Value>| -> Option<serde_json::Value> {
        match v? {
            serde_json::Value::Array(a) => a.first().cloned(),
            serde_json::Value::Object(o) => o.values().next().cloned(),
            other => Some(other.clone()),
        }
    };
    let mut n = 0;
    for it in arr {
        let Some(serde_json::Value::String(name)) = first(it.get("name")) else { continue };
        if name == "Filelist not found" {
            continue;
        }
        let size = match first(it.get("size")) {
            Some(serde_json::Value::Number(x)) => x.as_i64().unwrap_or(0),
            Some(serde_json::Value::String(s)) => s.trim().parse().unwrap_or(0),
            _ => 0,
        };
        d.add(&name, size);
        n += 1;
    }
    Some(n)
}

// ---- bencode ---------------------------------------------------------------------------------------------------------

#[derive(Debug)]
enum Bc {
    Int(i64),
    Bytes(Vec<u8>),
    List(Vec<Bc>),
    Dict(Vec<(Vec<u8>, Bc)>),
}

struct Reader<'a> {
    b: &'a [u8],
    p: usize,
}

impl<'a> Reader<'a> {
    fn peek(&self) -> Option<u8> {
        self.b.get(self.p).copied()
    }

    fn int(&mut self) -> Option<i64> {
        if self.peek()? != b'i' {
            return None;
        }
        self.p += 1;
        let neg = self.peek() == Some(b'-');
        if neg {
            self.p += 1;
        }
        let mut x: i64 = 0;
        let mut digits = 0;
        while let Some(c @ b'0'..=b'9') = self.peek() {
            if digits >= 18 {
                break;
            }
            x = x * 10 + (c - b'0') as i64;
            digits += 1;
            self.p += 1;
        }
        if digits == 0 || self.peek()? != b'e' {
            return None;
        }
        self.p += 1;
        Some(if neg { -x } else { x })
    }

    fn bytes(&mut self) -> Option<Vec<u8>> {
        let mut len = 0usize;
        let mut digits = 0;
        while let Some(c @ b'0'..=b'9') = self.peek() {
            if digits >= 10 {
                break;
            }
            len = len * 10 + (c - b'0') as usize;
            digits += 1;
            self.p += 1;
        }
        if digits == 0 || self.peek()? != b':' {
            return None;
        }
        self.p += 1;
        if len > self.b.len() - self.p {
            return None;
        }
        let v = self.b[self.p..self.p + len].to_vec();
        self.p += len;
        Some(v)
    }

    fn value(&mut self, depth: u32) -> Option<Bc> {
        if depth > 20 {
            return None;
        }
        match self.peek()? {
            b'i' => self.int().map(Bc::Int),
            b'l' => {
                self.p += 1;
                let mut v = Vec::new();
                while self.peek()? != b'e' {
                    v.push(self.value(depth + 1)?);
                }
                self.p += 1;
                Some(Bc::List(v))
            }
            b'd' => {
                self.p += 1;
                let mut v = Vec::new();
                while self.peek()? != b'e' {
                    let k = self.bytes()?;
                    v.push((k, self.value(depth + 1)?));
                }
                self.p += 1;
                Some(Bc::Dict(v))
            }
            _ => self.bytes().map(Bc::Bytes),
        }
    }
}

fn dict_get<'a>(d: &'a [(Vec<u8>, Bc)], key: &str) -> Option<&'a Bc> {
    d.iter().find(|(k, _)| k == key.as_bytes()).map(|(_, v)| v)
}

fn clean_part(b: &[u8], max: usize) -> String {
    let s: String = String::from_utf8_lossy(b).chars().map(|c| if c == '/' || c == '\\' || (c as u32) < 32 { '_' } else { c }).collect();
    trunc(s, max)
}

/// .torrent file contents -> file list. Returns the file count, None when it is not a valid .torrent.
pub fn parse_torrent(data: &[u8], d: &mut Detail) -> Option<usize> {
    if data.len() < 2 || data[0] != b'd' {
        return None;
    }
    let mut r = Reader { b: data, p: 1 };
    let info = loop {
        if r.peek()? == b'e' {
            return None;
        }
        let key = r.bytes()?;
        let val = r.value(1)?;
        if key == b"info" {
            break val;
        }
    };
    let Bc::Dict(info) = info else { return None };
    let name = match dict_get(&info, "name") {
        Some(Bc::Bytes(n)) => clean_part(n, 199),
        _ => String::new(),
    };
    if let Some(files) = dict_get(&info, "files") {
        let Bc::List(files) = files else { return None };
        for f in files {
            let Bc::Dict(fd) = f else { return None };
            let len = match dict_get(fd, "length") {
                Some(Bc::Int(l)) => *l,
                _ => 0,
            };
            if let Some(Bc::List(parts)) = dict_get(fd, "path") {
                let mut path = name.clone();
                for p in parts {
                    let Bc::Bytes(pb) = p else { return None };
                    if !path.is_empty() {
                        path.push('/');
                    }
                    path.push_str(&clean_part(pb, 159));
                }
                d.add(&path, len);
            }
        }
    } else if let Some(Bc::Int(len)) = dict_get(&info, "length") {
        if *len >= 0 && !name.is_empty() {
            d.add(&name, *len);
        }
    }
    (d.file_count > 0).then_some(d.file_count as usize)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn torrent_files() {
        let mut d = Detail::new("x");
        let multi = b"d4:infod5:filesld6:lengthi100e4:pathl1:a5:b.txteed6:lengthi50e4:pathl5:c.txteee4:name3:direee";
        assert_eq!(parse_torrent(multi, &mut d), Some(2));
        assert_eq!((d.file_count, d.total), (2, 150));
        assert_eq!((d.files[0].path.as_str(), d.files[0].size), ("dir/a/b.txt", 100));
        assert_eq!(d.files[1].path, "dir/c.txt");

        let mut d = Detail::new("x");
        let single = b"d8:announce3:url4:infod6:lengthi7e4:name5:a.isoee";
        assert_eq!(parse_torrent(single, &mut d), Some(1));
        assert_eq!((d.files[0].path.as_str(), d.total), ("a.iso", 7));

        let mut d = Detail::new("x");
        assert_eq!(parse_torrent(b"<html>", &mut d), None);
        assert_eq!(parse_torrent(b"d4:infod5:filesld6:length", &mut d), None); // cut off: no overrun
    }

    #[test]
    fn tpb_files() {
        let mut d = Detail::new("x");
        assert_eq!(parse_tpb_files(r#"[{"name":["x/y.mkv"],"size":[1234]},{"name":{"0":"z.nfo"},"size":{"0":"5"}}]"#, &mut d), Some(2));
        assert_eq!((d.file_count, d.total), (2, 1239));
        assert_eq!(d.files[1].path, "z.nfo");
        let mut d = Detail::new("x");
        assert_eq!(parse_tpb_files(r#"[{"name":{"0":"Filelist not found"},"size":{"0":"0"}}]"#, &mut d), Some(0));
        assert_eq!(parse_tpb_files("nope", &mut d), None);
    }
}
