// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Fail,
    fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigs},
    std::collections::hash_map::Entry,
    std::collections::HashMap,
    std::fmt,
    std::fs,
    std::io,
    std::path::{Path, PathBuf},
};

/// [RepositoryManager] controls access to all the repository configs used by the package resolver.
#[derive(Debug, PartialEq, Eq)]
pub struct RepositoryManager {
    configs: HashMap<String, RepositoryConfig>,
}

impl RepositoryManager {
    /// Construct a new [RepositoryManager].
    pub fn new() -> Self {
        RepositoryManager { configs: HashMap::new() }
    }

    /// Returns a reference to the [RepositoryConfig] config identified by the config `repo_host`.
    #[allow(dead_code)]
    pub fn get(&self, repo_host: &str) -> Option<&RepositoryConfig> {
        self.configs.get(repo_host)
    }

    /// Load a directory of [RepositoryConfigs] files into a [RepositoryManager], or error out if we
    /// encounter io errors during the load. It returns a [RepositoryManager], as well as all the
    /// individual [LoadError] errors encountered during the load.
    pub fn load_dir<T: AsRef<Path>>(dir: T) -> io::Result<(Self, Vec<LoadError>)> {
        let dir = dir.as_ref();
        let entries: Result<Vec<_>, _> = dir.read_dir()?.collect();
        let mut entries = entries?;

        // Make sure we always process entries in order to make config loading order deterministic.
        entries.sort_by_key(|e| e.file_name());

        let mut map = HashMap::new();
        let mut errors = Vec::new();

        for entry in entries {
            let path = entry.path();

            // Skip over any directories in this path.
            match entry.file_type() {
                Ok(file_type) => {
                    if !file_type.is_file() {
                        continue;
                    }
                }
                Err(err) => {
                    errors.push(LoadError::Io { path, error: err });
                    continue;
                }
            }

            let expected_host = path.file_stem().and_then(|name| name.to_str());

            let configs = match serde_json::from_reader(fs::File::open(&path)?) {
                Ok(RepositoryConfigs::Version1(configs)) => configs,
                Err(err) => {
                    errors.push(LoadError::Parse { path: path, error: err });
                    continue;
                }
            };

            // Insert the configs in filename lexographical order, and treating any duplicated
            // configs as a recoverable error. As a special case, if the file the config comes from
            // happens to be named the same as the repository hostname, use that config over some
            // other config that came from some other file.
            for config in configs {
                match map.entry(config.repo_url().host().to_string()) {
                    Entry::Occupied(mut entry) => {
                        let replaced_config = if Some(&**entry.key()) == expected_host {
                            entry.insert(config)
                        } else {
                            config
                        };
                        errors.push(LoadError::Overridden { replaced_config });
                    }
                    Entry::Vacant(entry) => {
                        entry.insert(config);
                    }
                }
            }
        }

        Ok((RepositoryManager { configs: map }, errors))
    }
}

/// [LoadError] describes all the recoverable error conditions that can be encountered when
/// parsing a [RepositoryConfigs] struct from a directory.
#[derive(Debug, Fail)]
pub enum LoadError {
    /// This [std::io::Error] error occurred while reading the file.
    Io {
        path: PathBuf,
        #[cause]
        error: io::Error,
    },

    /// This file failed to parse into a valid [RepositoryConfigs].
    Parse {
        path: PathBuf,
        #[cause]
        error: serde_json::Error,
    },

    /// This [RepositoryManager] already contains a config for this repo_url.
    Overridden { replaced_config: RepositoryConfig },
}

impl fmt::Display for LoadError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            LoadError::Io { path, error } => {
                write!(f, "file {} failed to parse: {}", path.display(), error)
            }
            LoadError::Parse { path, error } => {
                write!(f, "file {} failed to parse: {}", path.display(), error)
            }
            LoadError::Overridden { replaced_config } => {
                write!(f, "repository config for {} was overridden", replaced_config.repo_url())
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::test_util::create_dir;
    use fidl_fuchsia_pkg_ext::{RepositoryConfigBuilder, RepositoryKey};
    use fuchsia_uri::pkg_uri::RepoUri;
    use maplit::hashmap;
    use std::fs::File;
    use std::io::Write;

    #[test]
    fn test_load_dir_not_exists() {
        let dir = tempfile::tempdir().unwrap();

        match RepositoryManager::load_dir(dir.path().join("not-exists")) {
            Ok(_) => panic!("should not have created repo"),
            Err(err) => {
                assert_eq!(err.kind(), std::io::ErrorKind::NotFound, "{}", err);
            }
        }
    }

    #[test]
    fn test_load_dir_invalid() {
        let dir = tempfile::tempdir().unwrap();
        let invalid_path = dir.path().join("invalid");

        let example_uri = RepoUri::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_uri)
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri)
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        {
            let mut f = File::create(&invalid_path).unwrap();
            f.write(b"hello world").unwrap();

            let f = File::create(dir.path().join("a")).unwrap();
            serde_json::to_writer(f, &RepositoryConfigs::Version1(vec![example_config.clone()]))
                .unwrap();

            let f = File::create(dir.path().join("z")).unwrap();
            serde_json::to_writer(f, &RepositoryConfigs::Version1(vec![fuchsia_config.clone()]))
                .unwrap();
        }

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path()).unwrap();
        assert_eq!(errors.len(), 1, "{:?}", errors);

        match &errors[0] {
            LoadError::Parse { path, .. } => {
                assert_eq!(path, &invalid_path);
            }
            err => {
                panic!("unexpected error: {}", err);
            }
        }

        assert_eq!(
            repomgr,
            RepositoryManager {
                configs: hashmap! {
                    "example.com".to_string() => example_config,
                    "fuchsia.com".to_string() => fuchsia_config,
                }
            }
        );
    }

    #[test]
    fn test_load_dir() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri).build();

        let example_uri = RepoUri::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_uri).build();

        let dir = create_dir(vec![
            ("example.com.json", RepositoryConfigs::Version1(vec![example_config.clone()])),
            ("fuchsia.com.json", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
        ]);

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path()).unwrap();
        assert!(errors.is_empty(), "errors: {:?}", errors);

        assert_eq!(
            repomgr,
            RepositoryManager {
                configs: hashmap! {
                    "example.com".to_string() => example_config,
                    "fuchsia.com".to_string() => fuchsia_config,
                }
            }
        );
    }

    #[test]
    fn test_load_dir_overlapping_filename_wins() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let fuchsia_com_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        let fuchsia_com_json_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![2]))
            .build();

        let example_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![3]))
            .build();

        let oem_config = RepositoryConfigBuilder::new(fuchsia_uri)
            .add_root_key(RepositoryKey::Ed25519(vec![4]))
            .build();

        // Even though the example file comes first, the fuchsia repo should take priority over the
        // example file.
        let dir = create_dir(vec![
            ("fuchsia", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
            ("fuchsia.com", RepositoryConfigs::Version1(vec![fuchsia_com_config.clone()])),
            ("example.com.json", RepositoryConfigs::Version1(vec![example_config.clone()])),
            (
                "fuchsia.com.json",
                RepositoryConfigs::Version1(vec![
                    oem_config.clone(),
                    fuchsia_com_json_config.clone(),
                ]),
            ),
        ]);

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path()).unwrap();

        let overridden_configs =
            vec![fuchsia_config, fuchsia_com_config, example_config, oem_config];
        assert_eq!(errors.len(), overridden_configs.len(), "errors: {:?}", errors);

        for (err, config) in errors.into_iter().zip(overridden_configs) {
            match err {
                LoadError::Overridden { replaced_config } => {
                    assert_eq!(replaced_config, config);
                }
                _ => {
                    panic!("unexpected error: {}", err);
                }
            }
        }

        assert_eq!(
            repomgr,
            RepositoryManager {
                configs: hashmap! {
                    "fuchsia.com".to_string() => fuchsia_com_json_config,
                }
            }
        );
    }

    #[test]
    fn test_load_dir_overlapping_first_wins() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config1 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let fuchsia_config2 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        // Even though the example file comes first, the fuchsia repo should take priority over the
        // example file.
        let dir = create_dir(vec![
            ("1", RepositoryConfigs::Version1(vec![fuchsia_config1.clone()])),
            ("2", RepositoryConfigs::Version1(vec![fuchsia_config2.clone()])),
        ]);

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path()).unwrap();

        assert_eq!(errors.len(), 1);
        match &errors[0] {
            LoadError::Overridden { replaced_config } => {
                assert_eq!(replaced_config, &fuchsia_config2);
            }
            err => {
                panic!("unexpected error: {}", err);
            }
        }

        assert_eq!(
            repomgr,
            RepositoryManager {
                configs: hashmap! {
                    "fuchsia.com".to_string() => fuchsia_config1,
                }
            }
        );
    }
}
