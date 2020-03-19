// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_diagnostics::Selector,
    fuchsia_inspect as inspect, json5,
    selectors::parse_selector_file,
    serde_derive::Deserialize,
    std::path::{Path, PathBuf},
    std::{collections::BTreeMap, fs},
};

#[derive(Deserialize, Debug, PartialEq, Eq)]
pub struct Config {
    /// Path to which archived data will be written. No storage will be performed if left empty.
    pub archive_path: Option<PathBuf>,

    /// The maximum size the archive can be.
    pub max_archive_size_bytes: u64,

    /// The maximum size of a single event file group.
    pub max_event_group_size_bytes: u64,

    /// Number of threads the archivist has available to use.
    pub num_threads: usize,

    /// Paths to summarize in our own diagnostics output.
    pub summarized_dirs: Option<BTreeMap<String, String>>,
}

/// Configuration for pipeline selection.
pub struct PipelineConfig {
    /// Vector of file paths to inspect pipeline configurations.
    inspect_configs: Vec<PathBuf>,

    /// The selectors parsed from this config.
    inspect_selectors: Option<Vec<Selector>>,

    /// Accumulated errors from reading config files.
    errors: Vec<String>,
}

impl PipelineConfig {
    /// Read a pipeline config from the given directory.
    pub fn from_directory(dir: impl AsRef<Path>) -> Self {
        let suffix = std::ffi::OsStr::new("cfg");
        let mut inspect_configs = vec![];
        let mut inspect_selectors = Some(vec![]);
        let mut errors = vec![];

        let readdir = dir.as_ref().read_dir();

        match readdir {
            Err(_) => {
                errors.push(format!("Failed to read directory {}", dir.as_ref().to_string_lossy()));
            }
            Ok(mut readdir) => {
                while let Some(Ok(entry)) = readdir.next() {
                    let path = entry.path();
                    if path.extension() == Some(&suffix) {
                        match parse_selector_file(&path) {
                            Ok(selectors) => {
                                inspect_selectors.as_mut().unwrap().extend(selectors);
                                inspect_configs.push(path);
                            }
                            Err(e) => {
                                errors.push(format!(
                                    "Failed to parse {}: {}",
                                    path.to_string_lossy(),
                                    e.to_string()
                                ));
                            }
                        }
                    }
                }
            }
        }

        Self { inspect_configs, inspect_selectors, errors }
    }

    /// Take the inspect selectors from this pipeline config.
    pub fn take_inspect_selectors(&mut self) -> Option<Vec<Selector>> {
        self.inspect_selectors.take()
    }

    /// Record stats about this pipeline config to an Inspect Node.
    pub fn record_to_inspect(&self, node: &inspect::Node) {
        let files = node.create_child("config_files");
        for name in self.inspect_configs.iter() {
            let c = files.create_child(name.file_stem().unwrap_or_default().to_string_lossy());
            files.record(c);
        }
        node.record(files);

        if self.errors.len() != 0 {
            let errors = node.create_child("errors");
            for (i, error) in self.errors.iter().enumerate() {
                errors.record_string(format!("{}", i), error);
            }
            node.record(errors);
        }
    }

    /// Returns true if this pipeline config had errors.
    pub fn has_error(&self) -> bool {
        self.errors.len() > 0
    }
}

pub fn parse_config(path: impl Into<PathBuf>) -> Result<Config, Error> {
    let path: PathBuf = path.into();

    let json_string: String = fs::read_to_string(path)?;
    let config: Config = json5::from_str(&json_string)?;
    if let Some(summarized_dirs) = &config.summarized_dirs {
        if summarized_dirs.iter().any(|(dir, _)| dir == "archive") {
            return Err(format_err!("Invalid name 'archive' in summarized dirs"));
        }
    }
    Ok(config)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty};
    use std::io::Write;
    use std::path::Path;

    fn write_test_config_to_file<T: AsRef<Path>>(path: T, test_config: &str) {
        let mut file = fs::File::create(path).expect("failed to create file");
        write!(file, "{}", test_config).expect("failed to write file");
        file.sync_all().expect("failed to sync file");
    }

    #[test]
    fn parse_config_with_invalid_summarized_dir() {
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
                      "archive": "/data/archive"
                  }
                }"#;
        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config_result = parse_config(&test_config_file_name);
        assert!(parsed_config_result.is_err());
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
        assert_eq!(parsed_config.num_threads, 4);
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
                  "num_threads": 1
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config = parse_config(&test_config_file_name).unwrap();
        assert_eq!(parsed_config.max_archive_size_bytes, 10485760);
        assert_eq!(parsed_config.max_event_group_size_bytes, 262144);
        assert_eq!(parsed_config.num_threads, 1);
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
                  "bad_field": "hello world",
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config_result = parse_config(&test_config_file_name);
        assert!(parsed_config_result.is_err(), "Config had a missing field, and invalid field.");
    }

    #[test]
    fn parse_missing_pipeline() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let config = PipelineConfig::from_directory("config/missing");

        assert!(config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            errors: {
                "0": "Failed to read directory config/missing"
            },
            config_files: {}
        });
    }

    #[test]
    fn parse_partially_valid_pipeline() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("ok.cfg"), "my_component.cmx:root:status").unwrap();
        fs::write(config_path.join("ignored.txt"), "This file is ignored").unwrap();
        fs::write(config_path.join("bad.cfg"), "This file fails to parse").unwrap();

        let mut config = PipelineConfig::from_directory(&config_path);

        assert!(config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            errors: {
                "0": AnyProperty
            },
            config_files: {
                ok: {},
            }
        });

        assert_eq!(1, config.take_inspect_selectors().unwrap_or_default().len());
    }

    #[test]
    fn parse_valid_pipeline() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("ok.cfg"), "my_component.cmx:root:status").unwrap();
        fs::write(config_path.join("ignored.txt"), "This file is ignored").unwrap();
        fs::write(
            config_path.join("also_ok.cfg"),
            "my_component.cmx:root:a\nmy_component.cmx:root/b:c\n",
        )
        .unwrap();

        let mut config = PipelineConfig::from_directory(&config_path);

        assert!(!config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            config_files: {
                ok: {},
                also_ok: {}
            }
        });

        assert_eq!(3, config.take_inspect_selectors().unwrap_or_default().len());
    }
}
