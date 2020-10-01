// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_diagnostics::Selector,
    fuchsia_inspect as inspect,
    selectors::parse_selector_file,
    serde::Deserialize,
    serde_json5,
    std::path::{Path, PathBuf},
    std::{collections::BTreeMap, fs},
};

static DISABLE_FILTER_FILE_NAME: &'static str = "DISABLE_FILTERING.txt";

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
    /// Map of file paths for inspect pipeline configurations to the number of selectors they
    /// contain.
    inspect_configs: BTreeMap<PathBuf, usize>,

    /// The selectors parsed from this config.
    inspect_selectors: Option<Vec<Selector>>,

    /// Accumulated errors from reading config files.
    errors: Vec<String>,

    /// If true, filtering is disabled for this pipeline.
    /// The selector files will still be parsed and verified, but they will not be applied to
    /// returned data.
    pub disable_filtering: bool,
}

/// Configures behavior if no configuration files are found for the pipeline.
#[derive(PartialEq)]
pub enum EmptyBehavior {
    /// Disable the pipeline if no configuration files are found.
    Disable,
    /// Show unfiltered results if no configuration files are found.
    DoNotFilter,
}

impl PipelineConfig {
    /// Read a pipeline config from the given directory.
    ///
    /// empty_behavior instructs this config on what to do when there are no configuration files
    /// found.
    pub fn from_directory(dir: impl AsRef<Path>, empty_behavior: EmptyBehavior) -> Self {
        let suffix = std::ffi::OsStr::new("cfg");
        let disable_filter_file_name = std::ffi::OsStr::new(DISABLE_FILTER_FILE_NAME);
        let mut inspect_configs = BTreeMap::new();
        let mut inspect_selectors = Some(vec![]);
        let mut errors = vec![];
        let mut disable_filtering = false;

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
                                inspect_configs.insert(path, selectors.len());
                                inspect_selectors.as_mut().unwrap().extend(selectors);
                            }
                            Err(e) => {
                                errors.push(format!(
                                    "Failed to parse {}: {}",
                                    path.to_string_lossy(),
                                    e.to_string()
                                ));
                            }
                        }
                    } else if path.file_name() == Some(&disable_filter_file_name) {
                        disable_filtering = true;
                    }
                }
            }
        }

        if inspect_configs.is_empty() && empty_behavior == EmptyBehavior::DoNotFilter {
            inspect_selectors = None;
            disable_filtering = true;
        }

        Self { inspect_configs, inspect_selectors, errors, disable_filtering }
    }

    /// Take the inspect selectors from this pipeline config.
    pub fn take_inspect_selectors(&mut self) -> Option<Vec<Selector>> {
        self.inspect_selectors.take()
    }

    /// Record stats about this pipeline config to an Inspect Node.
    pub fn record_to_inspect(&self, node: &inspect::Node) {
        node.record_bool("filtering_enabled", !self.disable_filtering);
        let files = node.create_child("config_files");
        let mut selector_sum = 0;
        for (name, count) in self.inspect_configs.iter() {
            let c = files.create_child(name.file_stem().unwrap_or_default().to_string_lossy());
            c.record_uint("selector_count", *count as u64);
            files.record(c);
            selector_sum += count;
        }
        node.record(files);
        node.record_uint("selector_count", selector_sum as u64);

        if self.errors.len() != 0 {
            let errors = node.create_child("errors");
            for (i, error) in self.errors.iter().enumerate() {
                let error_node = errors.create_child(format!("{}", i));
                error_node.record_string("message", error);
                errors.record(error_node);
            }
            node.record(errors);
        }
    }

    /// Returns true if this pipeline config had errors.
    pub fn has_error(&self) -> bool {
        self.errors.len() > 0
    }
}

pub fn parse_config(path: impl AsRef<Path>) -> Result<Config, Error> {
    let path = path.as_ref();
    let json_string: String =
        fs::read_to_string(path).with_context(|| format!("parsing config: {}", path.display()))?;
    let config: Config = serde_json5::from_str(&json_string).context("parsing json config")?;
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

        let config = PipelineConfig::from_directory("config/missing", EmptyBehavior::Disable);

        assert!(config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            filtering_enabled: true,
            selector_count: 0u64,
            errors: {
                "0": {
                    message: "Failed to read directory config/missing"
                }
            },
            config_files: {}
        });
        assert!(!config.disable_filtering);
    }

    #[test]
    fn parse_partially_valid_pipeline() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("ok.cfg"), "my_component.cmx:root:status").unwrap();
        fs::write(config_path.join("ignored.txt"), "This file is ignored").unwrap();
        fs::write(config_path.join("bad.cfg"), "This file fails to parse").unwrap();

        let mut config = PipelineConfig::from_directory(&config_path, EmptyBehavior::Disable);

        assert!(config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            filtering_enabled: true,
            selector_count: 1u64,
            errors: {
                "0": {
                    message: AnyProperty
                }
            },
            config_files: {
                ok: {
                    selector_count: 1u64
                },
            }
        });

        assert!(!config.disable_filtering);
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

        let mut config = PipelineConfig::from_directory(&config_path, EmptyBehavior::Disable);

        assert!(!config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            filtering_enabled: true,
            selector_count: 3u64,
            config_files: {
                ok: {
                    selector_count: 1u64,
                },
                also_ok: {
                    selector_count: 2u64,
                }
            }
        });

        assert!(!config.disable_filtering);
        assert_eq!(3, config.take_inspect_selectors().unwrap_or_default().len());
    }

    #[test]
    fn parse_allow_empty_pipeline() {
        // If a pipeline is left unconfigured, do not filter results for the pipeline.
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let mut config = PipelineConfig::from_directory(&config_path, EmptyBehavior::DoNotFilter);

        assert!(!config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            filtering_enabled: false,
            selector_count: 0u64,
            config_files: {
            }
        });

        assert!(config.disable_filtering);
        assert_eq!(None, config.take_inspect_selectors());
    }

    #[test]
    fn parse_disabled_valid_pipeline() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("DISABLE_FILTERING.txt"), "This file disables filtering.")
            .unwrap();
        fs::write(config_path.join("ok.cfg"), "my_component.cmx:root:status").unwrap();
        fs::write(config_path.join("ignored.txt"), "This file is ignored").unwrap();
        fs::write(
            config_path.join("also_ok.cfg"),
            "my_component.cmx:root:a\nmy_component.cmx:root/b:c\n",
        )
        .unwrap();

        let mut config = PipelineConfig::from_directory(&config_path, EmptyBehavior::Disable);

        assert!(!config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_inspect_tree!(inspector, root: {
            filtering_enabled: false,
            selector_count: 3u64,
            config_files: {
                ok: {
                    selector_count: 1u64,
                },
                also_ok: {
                    selector_count: 2u64,
                }
            }
        });

        assert!(config.disable_filtering);
        assert_eq!(3, config.take_inspect_selectors().unwrap_or_default().len());
    }
}
