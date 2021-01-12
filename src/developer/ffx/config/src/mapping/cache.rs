// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mapping::replace, crate::paths::get_cache_base_path, lazy_static::lazy_static,
    regex::Regex, serde_json::Value,
};

pub(crate) fn cache<'a, T: Fn(Value) -> Option<Value> + Sync>(
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
    lazy_static! {
        static ref REGEX: Regex = Regex::new(r"\$(CACHE)").unwrap();
    }

    replace(&*REGEX, get_cache_base_path, next)
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::mapping::identity::identity;

    fn cache_dir(default: &str) -> String {
        match get_cache_base_path() {
            Ok(p) => p.to_str().map_or(default.to_string(), |s| s.to_string()),
            Err(_) => default.to_string(),
        }
    }

    #[test]
    fn test_mapper() {
        let value = cache_dir("$CACHE");
        let test = Value::String("$CACHE".to_string());
        assert_eq!(cache(&identity)(test), Some(Value::String(value.to_string())));
    }

    #[test]
    fn test_mapper_multiple() {
        let value = cache_dir("$CACHE");
        let test = Value::String("$CACHE/$CACHE".to_string());
        assert_eq!(cache(&identity)(test), Some(Value::String(format!("{}/{}", value, value))));
    }

    #[test]
    fn test_mapper_returns_pass_through() {
        let test = Value::String("$WHATEVER".to_string());
        assert_eq!(cache(&identity)(test), Some(Value::String("$WHATEVER".to_string())));
    }
}
