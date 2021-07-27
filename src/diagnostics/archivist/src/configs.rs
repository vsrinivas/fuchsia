// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::DEFAULT_PIPELINES_PATH;
use anyhow::{Context as _, Error};
use fidl_fuchsia_diagnostics::Selector;
use fuchsia_inspect as inspect;
use selectors::{contains_recursive_glob, parse_selector_file};
use serde::Deserialize;
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
};

static DISABLE_FILTER_FILE_NAME: &'static str = "DISABLE_FILTERING.txt";

fn default_pipelines_path() -> PathBuf {
    DEFAULT_PIPELINES_PATH.into()
}

#[derive(Deserialize, Debug, PartialEq, Eq)]
pub struct Config {
    /// Number of threads the archivist has available to use.
    pub num_threads: usize,

    /// path where pipeline configuration data should be looked for
    #[serde(default = "default_pipelines_path")]
    pub pipelines_path: PathBuf,

    /// Configuration for Archivist's log subsystem.
    pub logs: LogsConfig,
}

#[derive(Deserialize, Debug, PartialEq, Eq)]
pub struct LogsConfig {
    /// The maximum number of "raw logs bytes" Archivist will keep cached at one time.
    ///
    /// Note: because the Archivist does not preserve the original messages' bytes, the amount of
    /// memory consumed by the cache will be a multiple of this value. See https://fxbug.dev/67022
    /// for more information and future work.
    pub max_cached_original_bytes: usize,
}

#[derive(Deserialize, Debug, PartialEq, Eq)]
pub struct ServiceConfig {
    /// The list of services to connect to at startup.
    ///
    /// Archivist is responsible for starting up diagnostics processing components listed here.
    pub service_list: Vec<String>,
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
                                let mut validated_selectors = vec![];
                                for selector in selectors.into_iter() {
                                    match validate_static_selector(&selector) {
                                        Ok(()) => validated_selectors.push(selector),
                                        Err(e) => {
                                            errors.push(format!("Invalid static selector: {}", e))
                                        }
                                    }
                                }
                                inspect_configs.insert(path, validated_selectors.len());
                                inspect_selectors.as_mut().unwrap().extend(validated_selectors);
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
    let config: Config = serde_json::from_str(&json_string).context("parsing json config")?;
    Ok(config)
}

pub fn parse_service_config(path: impl AsRef<Path>) -> Result<ServiceConfig, Error> {
    let path = path.as_ref();
    let json_string: String = fs::read_to_string(path)
        .with_context(|| format!("parsing service config: {}", path.display()))?;
    let config: ServiceConfig =
        serde_json::from_str(&json_string).context("parsing json service config")?;
    Ok(config)
}

/// Validates a static selector against rules that apply specifically to a static selector and
/// do not apply to selectors in general. Assumes the selector is already validated against the
/// rules in selectors::validate_selector.
fn validate_static_selector(static_selector: &Selector) -> Result<(), String> {
    match static_selector.component_selector.as_ref() {
        Some(selector) if contains_recursive_glob(selector) => {
            Err(format!("Recursive glob not allowed in static selector configs"))
        }
        Some(_) => Ok(()),
        None => Err(format!("A selector does not contain a component selector")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::testing::{assert_data_tree, AnyProperty};
    use std::io::Write;
    use std::path::Path;

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
                  "logs": {
                    "max_cached_original_bytes": 500
                  },
                  "num_threads": 4
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config = parse_config(&test_config_file_name).unwrap();
        assert_eq!(parsed_config.logs.max_cached_original_bytes, 500);
        assert_eq!(parsed_config.num_threads, 4);
    }

    #[test]
    fn parse_valid_config_missing_optional() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let test_config_file_name = config_path.join("test_config.json");
        let test_config = r#"
                {
                  "logs": {
                    "max_cached_original_bytes": 500
                  },
                  "num_threads": 1
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config = parse_config(&test_config_file_name).unwrap();
        assert_eq!(parsed_config.logs.max_cached_original_bytes, 500);
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
                  "num_threads": 4,
                  "bad_field": "hello world",
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config_result = parse_config(&test_config_file_name);
        assert!(parsed_config_result.is_err(), "Config had a missing field, and invalid field.");
    }

    #[test]
    fn parse_valid_services_config() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let test_config_file_name = config_path.join("test_config.json");
        let test_config = r#"
                {
                  "service_list": ["a", "b", "c"]
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        let parsed_config_result =
            parse_service_config(&test_config_file_name).expect("failed to parse config");
        assert_eq!(
            parsed_config_result.service_list,
            vec!["a", "b", "c"].into_iter().map(|s| s.to_string()).collect::<Vec<_>>()
        );
    }

    #[test]
    fn parse_invalid_services_config() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();

        let test_config_file_name = config_path.join("test_config.json");
        let test_config = r#"
                {
                  "service_list": [1]
                }"#;

        write_test_config_to_file(&test_config_file_name, test_config);
        assert!(parse_service_config(&test_config_file_name).is_err());
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
        assert_data_tree!(inspector, root: {
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
        assert_data_tree!(inspector, root: {
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
        assert_data_tree!(inspector, root: {
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
        assert_data_tree!(inspector, root: {
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
        assert_data_tree!(inspector, root: {
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

    #[test]
    fn parse_pipeline_disallow_recursive_glob() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("glob.cfg"), "core/a/**:root:status").unwrap();
        fs::write(config_path.join("ok.cfg"), "core/b:root:status").unwrap();

        let mut config = PipelineConfig::from_directory(&config_path, EmptyBehavior::Disable);

        assert!(config.has_error());

        let inspector = inspect::Inspector::new();
        config.record_to_inspect(inspector.root());
        assert_data_tree!(inspector, root: {
            filtering_enabled: true,
            selector_count: 1u64,
            errors: {
                "0": {
                    message: AnyProperty
                }
            },
            config_files: {
                ok: {
                    selector_count: 1u64,
                },
                glob: {
                    selector_count: 0u64,
                }
            }
        });

        assert!(!config.disable_filtering);
        assert_eq!(1, config.take_inspect_selectors().unwrap_or_default().len());
    }
}
