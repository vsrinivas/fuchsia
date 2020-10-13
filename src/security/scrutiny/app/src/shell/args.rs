// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde_json::{self, json, Value},
    std::collections::HashMap,
    std::collections::VecDeque,
};

/// Converts a series of tokens into a single json value. For example:
/// --foo bar --baz a b would produce the json:
/// {
///     "foo": "bar",
///     "baz": "a b",
/// }
pub fn args_to_json(tokens: &VecDeque<String>) -> Value {
    let mut name: Option<String> = None;
    let mut map: HashMap<String, String> = HashMap::new();

    for token in tokens.iter() {
        if token.starts_with("--") {
            let stripped_name = token.strip_prefix("--").unwrap().to_string();
            map.insert(stripped_name.clone(), String::new());
            name = Some(stripped_name);
        } else if let Some(name) = &name {
            if let Some(entry) = map.get_mut(name) {
                if !entry.is_empty() {
                    entry.push_str(" ");
                }
                entry.push_str(token);
            }
        }
    }
    json!(map)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_parse() {
        let input = "--foo bar --baz a b".to_string();
        let tokens: VecDeque<String> = input.split_whitespace().map(|s| String::from(s)).collect();
        let value = args_to_json(&tokens);
        let map: HashMap<String, String> = serde_json::from_value(value).unwrap();
        assert_eq!(map.len(), 2);
        assert_eq!(map["foo"], "bar");
        assert_eq!(map["baz"], "a b");
    }
}
