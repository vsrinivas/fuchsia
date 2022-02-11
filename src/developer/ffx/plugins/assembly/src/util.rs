// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use fuchsia_pkg::PackageManifest;
use serde::Serialize;
use std::fs;
use std::fs::File;
use std::io::{BufReader, Read};
use std::path::Path;

/// Parse a PackageManifest from a path to a JSON file.
pub fn pkg_manifest_from_path(path: impl AsRef<Path>) -> Result<PackageManifest> {
    let manifest_file = File::open(path)?;
    let pkg_manifest_reader = BufReader::new(manifest_file);
    serde_json::from_reader(pkg_manifest_reader).map_err(Into::into)
}

/// Deserialize an instance of type T from an IO stream of JSON5.
pub fn from_reader<R, T>(reader: &mut R) -> Result<T>
where
    R: Read,
    T: serde::de::DeserializeOwned,
{
    let mut data = String::default();
    reader.read_to_string(&mut data).context("Cannot read the config")?;
    serde_json5::from_str(&data).context("Cannot parse the config")
}

/// Read a config file (or really any JSON/JSON5 file) into a instance of type
/// T, with a useful error context if it fails.
pub fn read_config<T>(path: impl AsRef<Path>) -> Result<T>
where
    T: serde::de::DeserializeOwned,
{
    let mut file = File::open(path.as_ref())
        .context(format!("Unable to open file: {}", path.as_ref().display()))?;
    from_reader(&mut file).context(format!("Unable to read file: {}", path.as_ref().display()))
}

/// Helper fn to insert into an empty Option, or return an Error.
pub fn set_option_once_or<T, E>(
    opt: &mut Option<T>,
    value: impl Into<Option<T>>,
    e: E,
) -> Result<(), E> {
    let value = value.into();
    if value.is_none() {
        Ok(())
    } else {
        if opt.is_some() {
            Err(e)
        } else {
            *opt = value;
            Ok(())
        }
    }
}

/// Serializes the given object to a JSON file.
pub fn write_json_file<T: ?Sized>(json_path: &Path, value: &T) -> Result<()>
where
    T: Serialize,
{
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
    use anyhow::anyhow;
    use fuchsia_hash::Hash;
    use serde::Deserialize;
    use serde_json::json;
    use std::io::Cursor;
    use std::str::FromStr;
    use tempfile::NamedTempFile;
    use tempfile::TempDir;

    #[derive(Debug, Deserialize, PartialEq)]
    struct MyStruct {
        key1: String,
    }

    #[test]
    fn invalid_path() {
        assert!(pkg_manifest_from_path("invalid.json").is_err());
    }

    #[test]
    fn valid_path() {
        let value = json!(
            {
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "test",
                    "version": "0.0.0",
                },
                "blobs": [
                    {
                        "source_path": "path/to/file.txt",
                        "path": "meta/",
                        "merkle":
                            "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },
                ]
            }
        );
        let file = NamedTempFile::new().unwrap();
        serde_json::ser::to_writer(&file, &value).unwrap();

        let manifest = pkg_manifest_from_path(file.path()).unwrap();
        assert_eq!(manifest.name().as_ref(), "test");
        let blobs = manifest.into_blobs();
        assert_eq!(blobs.len(), 1);
        assert_eq!(blobs[0].source_path, "path/to/file.txt");
        assert_eq!(blobs[0].path, "meta/");
        assert_eq!(
            blobs[0].merkle,
            Hash::from_str("0000000000000000000000000000000000000000000000000000000000000000")
                .unwrap()
        );
        assert_eq!(blobs[0].size, 1);
    }

    #[test]
    fn reader_valid_json5() {
        let json5: String = r#"{key1: "value1",}"#.to_string();
        let mut cursor = Cursor::new(json5);
        let value: MyStruct = from_reader(&mut cursor).unwrap();
        assert_eq!(value.key1, "value1");
    }

    #[test]
    fn reader_invalid_json5() {
        #[derive(Deserialize)]
        #[serde(deny_unknown_fields)]
        struct MyStruct {}
        let json5: String = r#"{key1: "value1",}"#.to_string();
        let mut cursor = Cursor::new(json5);
        let value: Result<MyStruct> = from_reader(&mut cursor);
        assert!(value.is_err());
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
    fn test_set_option_once() {
        let mut opt = None;

        // should be able to set None on None.
        assert!(
            set_option_once_or(&mut opt, None, anyhow!("an error")).is_ok(),
            "Setting None on None failed"
        );

        // should be able to set Value on None.
        assert!(
            set_option_once_or(&mut opt, Some("some value"), anyhow!("an error")).is_ok(),
            "initial set value failed"
        );
        assert_eq!(opt, Some("some value"));

        // setting None on Some should be a no-op.
        assert!(
            set_option_once_or(&mut opt, None, anyhow!("an error")).is_ok(),
            "Setting None on Some failed"
        );
        assert_eq!(opt, Some("some value"), "Setting None on Some was not a no-op");

        // setting Some on Some should fail.
        assert!(
            set_option_once_or(&mut opt, "other value", anyhow!("an error")).is_err(),
            "Setting Some on Some did not fail"
        );
        assert_eq!(
            opt,
            Some("some value"),
            "Setting Some(other) on Some(value) changed the value with an error"
        );
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
