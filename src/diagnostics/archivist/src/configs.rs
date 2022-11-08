// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics::Selector;
use fuchsia_inspect as inspect;
use selectors::{contains_recursive_glob, parse_selector_file, FastError};
use std::{
    collections::BTreeMap,
    path::{Path, PathBuf},
};

static DISABLE_FILTER_FILE_NAME: &str = "DISABLE_FILTERING.txt";

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
                    if path.extension() == Some(suffix) {
                        match parse_selector_file::<FastError>(&path) {
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
                                    e
                                ));
                            }
                        }
                    } else if path.file_name() == Some(disable_filter_file_name) {
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

        if !self.errors.is_empty() {
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
        !self.errors.is_empty()
    }
}

/// Validates a static selector against rules that apply specifically to a static selector and
/// do not apply to selectors in general. Assumes the selector is already validated against the
/// rules in selectors::validate_selector.
fn validate_static_selector(static_selector: &Selector) -> Result<(), String> {
    match static_selector.component_selector.as_ref() {
        Some(selector) if contains_recursive_glob(selector) => {
            Err("Recursive glob not allowed in static selector configs".to_string())
        }
        Some(_) => Ok(()),
        None => Err("A selector does not contain a component selector".to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::testing::{assert_data_tree, AnyProperty};
    use std::fs;

    #[fuchsia::test]
    fn parse_missing_pipeline() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(config_path).unwrap();

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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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
