// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    std::collections::HashMap,
};

/// Given a String containing potentially multiple lines of foo=bar returns
/// a HashMap with the keys and values. This assumes that each key is unique.
pub fn parse_key_value<'a>(contents: &'a str) -> Result<HashMap<String, String>> {
    let lines = contents.split('\n');
    let mut kv_map: HashMap<String, String> = HashMap::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let mut pair = line.splitn(2, "=");
        let key = pair.next().ok_or_else(|| anyhow!("Couldn't find key on line: {}", line))?;
        let value = pair.next().ok_or_else(|| anyhow!("Couldn't find value on line: {}", line))?;
        kv_map.insert(String::from(key), String::from(value));
    }
    Ok(kv_map)
}

#[cfg(test)]
mod tests {
    use super::parse_key_value;

    #[test]
    fn test_single_line() {
        let contents = "foo=bar";
        let kv_map = parse_key_value(contents).unwrap();
        assert_eq!(kv_map.len(), 1);
        assert_eq!(kv_map["foo"], String::from("bar"));
    }

    #[test]
    fn test_empty_line() {
        let contents = "";
        let kv_map = parse_key_value(contents).unwrap();
        assert_eq!(kv_map.len(), 0);
    }

    #[test]
    fn test_multiple_lines() {
        let contents = "foo=bar\nbaz=bazz\nmagic=foobar";
        let kv_map = parse_key_value(contents).unwrap();
        assert_eq!(kv_map.len(), 3);
        assert_eq!(kv_map["foo"], String::from("bar"));
        assert_eq!(kv_map["baz"], String::from("bazz"));
        assert_eq!(kv_map["magic"], String::from("foobar"));
    }

    #[test]
    fn test_multiple_lines_with_empty_lines() {
        let contents = "foo=bar\n\nbaz=bazz\n\nmagic=foobar\n";
        let kv_map = parse_key_value(contents).unwrap();
        assert_eq!(kv_map.len(), 3);
        assert_eq!(kv_map["foo"], String::from("bar"));
        assert_eq!(kv_map["baz"], String::from("bazz"));
        assert_eq!(kv_map["magic"], String::from("foobar"));
    }

    #[test]
    fn test_invalid() {
        let contents = "invalid";
        assert_eq!(parse_key_value(contents).is_ok(), false);
    }
}
