// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use {
    failure::Error,
    json5,
    serde_derive::Deserialize,
    std::path::PathBuf,
    std::{collections::BTreeMap, fs},
};

#[derive(Deserialize, Debug, PartialEq, Eq)]
pub struct Config {
    /// The maximum size the archive can be.
    pub max_archive_size_bytes: u64,

    /// The maximum size of a single event file group.
    pub max_event_group_size_bytes: u64,

    /// Number of threads the archivist has available to use.
    pub num_threads: Option<usize>,

    /// Paths to summarize in our own diagnostics output.
    pub summarized_dirs: Option<BTreeMap<String, String>>,
}

pub fn parse_config(path: impl Into<PathBuf>) -> Result<Config, Error> {
    let path: PathBuf = path.into();

    let json_string: String = fs::read_to_string(path)?;
    Ok(json5::from_str(&json_string)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use std::path::Path;
    extern crate tempfile;

    fn write_test_config_to_file<T: AsRef<Path>>(path: T, test_config: &str) {
        let mut file = fs::File::create(path).expect("failed to create file");
        write!(file, "{}", test_config).expect("failed to write file");
        file.sync_all().expect("failed to sync file");
    }

    #[test]
    fn parse_valid_config() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let test_config_file_name = config_path.join("test_config.json");
        let test_config = r#"
                {
                  // Test comment for json5 portability.
                  "max_archive_size_bytes": 10485760,
                  "max_event_group_size_bytes": 262144,
                  "num_threads": 4,
                  "summarized_dirs": {
                      "global_data": "/global_data",
                      "global_tmp": "/global_tmp"
                  }
                }"#;
        let mut expected_dirs: BTreeMap<String, String> = BTreeMap::new();
        expected_dirs.insert("global_data".into(), "/global_data".into());
        expected_dirs.insert("global_tmp".into(), "/global_tmp".into());

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config = parse_config(&test_config_file_name).unwrap();
        assert_eq!(parsed_config.max_archive_size_bytes, 10485760);
        assert_eq!(parsed_config.max_event_group_size_bytes, 262144);
        assert_eq!(parsed_config.num_threads, Some(4));
        assert_eq!(parsed_config.summarized_dirs, Some(expected_dirs));
    }

    #[test]
    fn parse_valid_config_missing_optional() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let test_config_file_name = config_path.join("test_config.json");
        let test_config = r#"
                {
                  // Test comment for json5 portability.
                  "max_archive_size_bytes": 10485760,
                  "max_event_group_size_bytes": 262144,
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config = parse_config(&test_config_file_name).unwrap();
        assert_eq!(parsed_config.max_archive_size_bytes, 10485760);
        assert_eq!(parsed_config.max_event_group_size_bytes, 262144);
        assert_eq!(parsed_config.num_threads, None);
    }

    #[test]
    fn parse_invalid_config() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let test_config_file_name = config_path.join("test_config.json");
        let test_config = r#"
                {
                  "max_archive_size_bytes": 10485760,
                  "bad_field": "hello world"
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config_result = parse_config(&test_config_file_name);
        assert!(parsed_config_result.is_err(), "Config had a missing field, and invalid field.");
    }
}
