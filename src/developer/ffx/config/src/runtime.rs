// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use serde_json::{Map, Value};

pub(crate) fn populate_runtime_config(config: &Option<String>) -> Result<Option<Value>> {
    match config {
        Some(c) => match serde_json::from_str(&c) {
            Ok(v) => Ok(Some(v)),
            Err(_) => {
                let mut runtime_config = Map::new();
                for pair in c.split(',') {
                    let s: Vec<&str> = pair.trim().split('=').collect();
                    if s.len() == 2 {
                        runtime_config.insert(s[0].to_string(), Value::String(s[1].to_string()));
                    } else {
                        bail!("--config flag not properly formatted");
                    }
                }
                Ok(Some(Value::Object(runtime_config)))
            }
        },
        None => Ok(None),
    }
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
        let config = populate_runtime_config(&Some(format!(
            "{}={}, {}={}",
            key_1, value_1, key_2, value_2
        )))?
        .expect("expected test configuration");

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        assert_eq!(Some(&Value::String(value_1.to_string())), config.get(key_1));
        assert_eq!(Some(&Value::String(value_2.to_string())), config.get(key_2));
        Ok(())
    }
}
