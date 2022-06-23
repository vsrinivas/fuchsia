// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mapping::{postprocess, preprocess, replace_regex as replace},
    lazy_static::lazy_static,
    regex::Regex,
    serde_json::Value,
};

pub(crate) fn home(value: Value) -> Option<Value> {
    lazy_static! {
        static ref REGEX: Regex = Regex::new(r"\$(HOME)").unwrap();
    }

    preprocess(&value)
        .as_ref()
        .map(|s| {
            replace(s, &*REGEX, |v| {
                Ok(home::home_dir().map_or(v.to_string(), |home_path| {
                    home_path.to_str().map_or(v.to_string(), |s| s.to_string())
                }))
            })
        })
        .map(postprocess)
        .or(Some(value))
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;

    fn home_dir(default: &str) -> String {
        home::home_dir().map_or(default.to_string(), |home_path| {
            home_path.to_str().map_or(default.to_string(), |s| s.to_string())
        })
    }

    #[test]
    fn test_mapper() {
        let value = home_dir("$HOME");
        let test = Value::String("$HOME".to_string());
        assert_eq!(home(test), Some(Value::String(value.to_string())));
    }

    #[test]
    fn test_mapper_multiple() {
        let value = home_dir("$HOME");
        let test = Value::String("$HOME/$HOME".to_string());
        assert_eq!(home(test), Some(Value::String(format!("{}/{}", value, value))));
    }

    #[test]
    fn test_mapper_returns_pass_through() {
        let test = Value::String("$WHATEVER".to_string());
        assert_eq!(home(test), Some(Value::String("$WHATEVER".to_string())));
    }
}
