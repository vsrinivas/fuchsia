// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::priority_config::Priority;
use anyhow::{anyhow, bail, Context, Result};
use serde_json::{Map, Value};
use std::{fs::File, io::BufReader, path::Path};

fn try_split_name_value_pairs(config: &String) -> Result<Option<Value>> {
    let mut runtime_config = Map::new();
    for pair in config.split(',') {
        let s: Vec<&str> = pair.trim().split('=').collect();
        if s.len() == 2 {
            let key_vec: Vec<&str> = s[0].split('.').collect();
            Priority::nested_set(
                &mut runtime_config,
                key_vec[0],
                key_vec[1..].to_vec(),
                Value::String(s[1].to_string()),
            );
        } else {
            bail!(
                "--config must either be a file path, /
            a valid JSON object, or comma separated key=value pairs."
            )
        }
    }
    Ok(Some(Value::Object(runtime_config)))
}

fn try_parse_json(config: &String) -> Result<Option<Value>> {
    match serde_json::from_str(config) {
        Ok(v) => Ok(Some(v)),
        Err(_) => bail!("could not parse json from --config flag"),
    }
}

fn try_read_file(config: &String) -> Result<Option<Value>> {
    let path = Path::new(config);
    if path.is_file() {
        let file = File::open(path).map(|f| BufReader::new(f)).context("opening read buffer")?;
        serde_json::from_reader(file).map_err(|e| anyhow!("Could not parse \"{}\": {}", config, e))
    } else {
        bail!("--config input is not a file path")
    }
}

pub(crate) fn populate_runtime_config(config: &Option<String>) -> Result<Option<Value>> {
    match config {
        Some(c) => try_read_file(c)
            .or_else(|_| try_parse_json(c))
            .or_else(|_| try_split_name_value_pairs(c)),
        None => Ok(None),
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Result;
    use std::io::Write;
    use tempfile::NamedTempFile;

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

    #[test]
    fn test_dot_notation_config_runtime() -> Result<()> {
        let (key_1, value_1) = ("test.nested", "test");
        let (key_2, value_2) = ("test.another_nested", "another_test");
        let config = populate_runtime_config(&Some(format!(
            "{}={}, {}={}",
            key_1, value_1, key_2, value_2
        )))?
        .expect("expected test configuration");

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        let key_vec_1: Vec<&str> = key_1.split('.').collect();
        if let Some(c) = config.get(key_vec_1[0]) {
            assert_eq!(Some(&Value::String(value_1.to_string())), c.get(key_vec_1[1]));
        } else {
            bail!("failed to get nested config");
        }
        let key_vec_2: Vec<&str> = key_2.split('.').collect();
        if let Some(c) = config.get(key_vec_2[0]) {
            assert_eq!(Some(&Value::String(value_2.to_string())), c.get(key_vec_2[1]));
        } else {
            bail!("failed to get nested config");
        }
        Ok(())
    }

    #[test]
    fn test_file_load() -> Result<()> {
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let mut file = tmp_file.as_file();
        file.write_all(b"{\"test\": {\"nested\": true, \"another_nested\":false}}")
            .context("writing configuration file")?;
        file.sync_all().context("syncing configuration file to filesystem")?;
        let config = populate_runtime_config(&tmp_file.path().to_str().map(|s| s.to_string()))?
            .expect("config");
        if let Some(c) = config.get("test") {
            assert_eq!(Some(&Value::Bool(true)), c.get("nested"));
            assert_eq!(Some(&Value::Bool(false)), c.get("another_nested"));
            Ok(())
        } else {
            bail!("failed to get nested config");
        }
    }
}
