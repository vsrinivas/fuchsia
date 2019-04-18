// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Fail,
    fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigs},
    fuchsia_uri::pkg_uri::RepoUri,
    std::collections::btree_set,
    std::collections::hash_map::Entry,
    std::collections::{BTreeSet, HashMap},
    std::fmt,
    std::fs,
    std::io,
    std::path::{Path, PathBuf},
};

/// [RepositoryManager] controls access to all the repository configs used by the package resolver.
#[derive(Debug, PartialEq, Eq)]
pub struct RepositoryManager {
    static_configs: HashMap<RepoUri, RepositoryConfig>,
    dynamic_configs: HashMap<RepoUri, RepositoryConfig>,
}

impl RepositoryManager {
    /// Construct a new [RepositoryManager].
    pub fn new() -> Self {
        RepositoryManager { static_configs: HashMap::new(), dynamic_configs: HashMap::new() }
    }

    /// Load a directory of [RepositoryConfigs] files into a [RepositoryManager], or error out if we
    /// encounter io errors during the load. It returns a [RepositoryManager], as well as all the
    /// individual [LoadError] errors encountered during the load.
    pub fn load_dir<T: AsRef<Path>>(static_config_dir: T) -> (Self, Vec<LoadError>) {
        let (static_configs, errors) = load_configs_dir(static_config_dir);
        let mgr = RepositoryManager { static_configs, dynamic_configs: HashMap::new() };
        (mgr, errors)
    }

    /// Returns a reference to the [RepositoryConfig] config identified by the config `repo_url`,
    /// or `None` if it does not exist.
    pub fn get(&self, repo_url: &RepoUri) -> Option<&RepositoryConfig> {
        self.dynamic_configs.get(repo_url).or_else(|| self.static_configs.get(repo_url))
    }

    /// Inserts a [RepositoryConfig] into this manager.
    ///
    /// If the manager did not have a [RepositoryConfig] with a corresponding repository url for
    /// the repository, `None` is returned.
    ///
    /// If the manager did have this repository present as a dynamic config, the value is replaced
    /// and the old [RepositoryConfig] is returned. If this repository is a static config, the
    /// static config is shadowed by the dynamic config until it is removed.
    pub fn insert(&mut self, config: RepositoryConfig) -> Option<RepositoryConfig> {
        self.dynamic_configs.insert(config.repo_url().clone(), config)
    }

    /// Removes a [RepositoryConfig] identified by the config `repo_url`.
    pub fn remove(
        &mut self,
        repo_url: &RepoUri,
    ) -> Result<Option<RepositoryConfig>, CannotRemoveStaticRepositories> {
        if let Some(config) = self.dynamic_configs.remove(repo_url) {
            return Ok(Some(config));
        }

        if self.static_configs.get(repo_url).is_some() {
            Err(CannotRemoveStaticRepositories)
        } else {
            Ok(None)
        }
    }

    /// Returns an iterator over all the managed [RepositoryConfig]s.
    pub fn list(&self) -> List {
        let keys = self
            .dynamic_configs
            .iter()
            .chain(self.static_configs.iter())
            .map(|(k, _)| k)
            .collect::<BTreeSet<_>>();

        List { keys: keys.into_iter(), repo_mgr: self }
    }
}

/// Load a directory of [RepositoryConfigs] files into a [RepositoryManager], or error out if we
/// encounter io errors during the load. It returns a [RepositoryManager], as well as all the
/// individual [LoadError] errors encountered during the load.
fn load_configs_dir<T: AsRef<Path>>(
    dir: T,
) -> (HashMap<RepoUri, RepositoryConfig>, Vec<LoadError>) {
    let dir = dir.as_ref();

    let mut entries = match dir.read_dir() {
        Ok(entries) => {
            let entries: Result<Vec<_>, _> = entries.collect();

            match entries {
                Ok(entries) => entries,
                Err(err) => {
                    return (HashMap::new(), vec![LoadError::Io { path: dir.into(), error: err }]);
                }
            }
        }
        Err(err) => {
            return (HashMap::new(), vec![LoadError::Io { path: dir.into(), error: err }]);
        }
    };

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

        let expected_uri = path
            .file_stem()
            .and_then(|name| name.to_str())
            .and_then(|name| RepoUri::new(name.to_string()).ok());

        let configs = match load_configs_file(&path) {
            Ok(configs) => configs,
            Err(err) => {
                errors.push(err);
                continue;
            }
        };

        // Insert the configs in filename lexographical order, and treating any duplicated
        // configs as a recoverable error. As a special case, if the file the config comes from
        // happens to be named the same as the repository hostname, use that config over some
        // other config that came from some other file.
        for config in configs {
            match map.entry(config.repo_url().clone()) {
                Entry::Occupied(mut entry) => {
                    let replaced_config = if Some(entry.key()) == expected_uri.as_ref() {
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

    (map, errors)
}

fn load_configs_file<T: AsRef<Path>>(path: T) -> Result<Vec<RepositoryConfig>, LoadError> {
    let path = path.as_ref();
    match fs::File::open(&path) {
        Ok(f) => match serde_json::from_reader(f) {
            Ok(RepositoryConfigs::Version1(configs)) => Ok(configs),
            Err(err) => Err(LoadError::Parse { path: path.into(), error: err }),
        },
        Err(err) => Err(LoadError::Io { path: path.into(), error: err }),
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

/// `List` is an iterator over all the [RepoConfig].
///
/// See its documentation for more.
pub struct List<'a> {
    keys: btree_set::IntoIter<&'a RepoUri>,
    repo_mgr: &'a RepositoryManager,
}

impl<'a> Iterator for List<'a> {
    type Item = &'a RepositoryConfig;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(key) = self.keys.next() {
            self.repo_mgr.get(key)
        } else {
            None
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Fail)]
#[fail(display = "cannot remove static repositories")]
pub struct CannotRemoveStaticRepositories;

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
    fn test_insert_get_remove() {
        let mut repos = RepositoryManager::new();
        assert_eq!(
            repos,
            RepositoryManager { static_configs: HashMap::new(), dynamic_configs: HashMap::new() }
        );

        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        assert_eq!(repos.get(&fuchsia_uri), None);

        let config1 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();
        let config2 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        assert_eq!(repos.insert(config1.clone()), None);
        assert_eq!(
            repos,
            RepositoryManager {
                static_configs: HashMap::new(),
                dynamic_configs: hashmap! {
                    fuchsia_uri.clone() => config1.clone(),
                },
            }
        );

        assert_eq!(repos.insert(config2.clone()), Some(config1.clone()));
        assert_eq!(
            repos,
            RepositoryManager {
                static_configs: HashMap::new(),
                dynamic_configs: hashmap! {
                    fuchsia_uri.clone() => config2.clone(),
                },
            }
        );

        assert_eq!(repos.get(&fuchsia_uri), Some(&config2));
        assert_eq!(repos.remove(&fuchsia_uri), Ok(Some(config2.clone())));
        assert_eq!(
            repos,
            RepositoryManager { static_configs: HashMap::new(), dynamic_configs: HashMap::new() }
        );
        assert_eq!(repos.remove(&fuchsia_uri), Ok(None));
    }

    #[test]
    fn shadowing_static_config() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config1 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        let fuchsia_config2 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![2]))
            .build();

        let dir = create_dir(vec![(
            "fuchsia.com.json",
            RepositoryConfigs::Version1(vec![fuchsia_config1.clone()]),
        )]);

        let (mut repomgr, errors) = RepositoryManager::load_dir(dir.path());
        assert!(errors.is_empty(), "errors: {:?}", errors);

        assert_eq!(repomgr.get(&fuchsia_uri), Some(&fuchsia_config1));
        assert_eq!(repomgr.insert(fuchsia_config2.clone()), None);
        assert_eq!(repomgr.get(&fuchsia_uri), Some(&fuchsia_config2));
        assert_eq!(repomgr.remove(&fuchsia_uri), Ok(Some(fuchsia_config2)));
        assert_eq!(repomgr.get(&fuchsia_uri), Some(&fuchsia_config1));
    }

    #[test]
    fn cannot_remove_static_config() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config1 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        let dir = create_dir(vec![(
            "fuchsia.com.json",
            RepositoryConfigs::Version1(vec![fuchsia_config1.clone()]),
        )]);

        let (mut repomgr, errors) = RepositoryManager::load_dir(dir.path());
        assert!(errors.is_empty(), "errors: {:?}", errors);

        assert_eq!(repomgr.get(&fuchsia_uri), Some(&fuchsia_config1));
        assert_eq!(repomgr.remove(&fuchsia_uri), Err(CannotRemoveStaticRepositories));
        assert_eq!(repomgr.get(&fuchsia_uri), Some(&fuchsia_config1));
    }

    #[test]
    fn test_load_dir_not_exists() {
        let dir = tempfile::tempdir().unwrap();

        let does_not_exist_dir = dir.path().join("not-exists");
        let (_, errors) = RepositoryManager::load_dir(&does_not_exist_dir);
        assert_eq!(errors.len(), 1, "{:?}", errors);

        match &errors[0] {
            LoadError::Io { path, error } => {
                assert_eq!(path, &does_not_exist_dir);
                assert_eq!(error.kind(), std::io::ErrorKind::NotFound, "{}", error);
            }
            err => {
                panic!("unexpected error: {}", err);
            }
        }
    }

    #[test]
    fn test_load_dir_invalid() {
        let dir = tempfile::tempdir().unwrap();
        let invalid_path = dir.path().join("invalid");

        let example_uri = RepoUri::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
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

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path());
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
                static_configs: hashmap! {
                    example_uri => example_config,
                    fuchsia_uri => fuchsia_config,
                },
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_load_dir() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri.clone()).build();

        let example_uri = RepoUri::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_uri.clone()).build();

        let dir = create_dir(vec![
            ("example.com.json", RepositoryConfigs::Version1(vec![example_config.clone()])),
            ("fuchsia.com.json", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
        ]);

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path());
        assert!(errors.is_empty(), "errors: {:?}", errors);

        assert_eq!(
            repomgr,
            RepositoryManager {
                static_configs: hashmap! {
                    example_uri => example_config,
                    fuchsia_uri => fuchsia_config,
                },
                dynamic_configs: HashMap::new(),
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

        let oem_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
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

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path());

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
                static_configs: hashmap! {
                    fuchsia_uri => fuchsia_com_json_config,
                },
                dynamic_configs: HashMap::new(),
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

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path());

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
                static_configs: hashmap! {
                    fuchsia_uri => fuchsia_config1,
                },
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_list_empty() {
        let repomgr = RepositoryManager::new();
        assert_eq!(repomgr.list().collect::<Vec<_>>(), Vec::<&RepositoryConfig>::new());
    }

    #[test]
    fn test_list() {
        let example_uri = RepoUri::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_uri).build();

        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri).build();

        let dir = create_dir(vec![
            ("example.com", RepositoryConfigs::Version1(vec![example_config.clone()])),
            ("fuchsia.com", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
        ]);

        let (repomgr, errors) = RepositoryManager::load_dir(dir.path());
        assert_eq!(errors.len(), 0);

        assert_eq!(repomgr.list().collect::<Vec<_>>(), vec![&example_config, &fuchsia_config,]);
    }
}
