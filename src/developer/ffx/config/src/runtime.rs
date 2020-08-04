// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_json::{Map, Value};

pub(crate) fn populate_runtime_config(config: &Option<String>) -> Option<Value> {
    config.as_ref().and_then(|c| {
        serde_json::from_str(&c).ok().or_else(|| {
            let mut runtime_config = Map::new();
            c.split(',').for_each(|c| {
                let s: Vec<&str> = c.trim().split('=').collect();
                if s.len() == 2 {
                    runtime_config.insert(s[0].to_string(), Value::String(s[1].to_string()));
                } else {
                    println!("--config flag ignored - not properly formatted");
                }
            });
            Some(Value::Object(runtime_config))
        })
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Result;

    #[test]
    fn test_config_runtime() -> Result<()> {
        let (key_1, value_1) = ("test 1", "test 2");
        let (key_2, value_2) = ("test 3", "test 4");
        let config =
            populate_runtime_config(&Some(format!("{}={}, {}={}", key_1, value_1, key_2, value_2)))
                .expect("expected test configuration");

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        assert_eq!(Some(&Value::String(value_1.to_string())), config.get(key_1));
        assert_eq!(Some(&Value::String(value_2.to_string())), config.get(key_2));
        Ok(())
    }
}
