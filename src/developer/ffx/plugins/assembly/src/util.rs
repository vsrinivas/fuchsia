// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use serde::Serialize;
use std::fs;
use std::fs::File;
use std::path::Path;

/// Read a config file (or really any JSON/JSON5 file) into a instance of type
/// T, with a useful error context if it fails.
pub fn read_config<T>(path: impl AsRef<Path>) -> Result<T>
where
    T: serde::de::DeserializeOwned,
{
    let mut file = File::open(path.as_ref())
        .context(format!("Unable to open file: {}", path.as_ref().display()))?;
    assembly_util::from_reader(&mut file)
        .context(format!("Unable to read file: {}", path.as_ref().display()))
}

/// Serializes the given object to a JSON file.
pub fn write_json_file<T: ?Sized>(json_path: impl AsRef<Path>, value: &T) -> Result<()>
where
    T: Serialize,
{
    let json_path = json_path.as_ref();
    if let Some(parent) = json_path.parent() {
        fs::create_dir_all(parent)
            .with_context(|| format!("cannot create {}", parent.display()))?;
    }
    let file = File::create(json_path)
        .with_context(|| format!("cannot create {}", json_path.display()))?;
    serde_json::to_writer_pretty(&file, &value)
        .with_context(|| format!("cannot serialize {}", json_path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde::Deserialize;
    use serde_json::json;
    use tempfile::TempDir;

    #[derive(Debug, Deserialize, PartialEq)]
    struct MyStruct {
        key1: String,
    }

    #[test]
    fn test_read_config() {
        let json = json!({
            "key1": "value1",
        });
        let file = tempfile::NamedTempFile::new().unwrap();
        serde_json::ser::to_writer(&file, &json).unwrap();

        let value: MyStruct = read_config(file.path()).unwrap();
        let expected: MyStruct = serde_json::from_value(json).unwrap();
        assert_eq!(expected, value);
    }

    #[test]
    fn test_write_json_file() {
        let expected = json!({
            "key1": "value1",
        });
        let temp_dir = TempDir::new().unwrap();
        let path = temp_dir.path().join("config.json");
        write_json_file(&path, &expected).unwrap();

        let actual: serde_json::Value = read_config(path).unwrap();
        assert_eq!(expected, actual);
    }
}
