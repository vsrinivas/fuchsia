// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Fail,
    fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigs},
    fuchsia_syslog::fx_log_err,
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
    dynamic_configs_path: PathBuf,
    static_configs: HashMap<RepoUri, RepositoryConfig>,
    dynamic_configs: HashMap<RepoUri, RepositoryConfig>,
}

impl RepositoryManager {
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
        let result = self.dynamic_configs.insert(config.repo_url().clone(), config);
        self.save();
        result
    }

    /// Removes a [RepositoryConfig] identified by the config `repo_url`.
    pub fn remove(
        &mut self,
        repo_url: &RepoUri,
    ) -> Result<Option<RepositoryConfig>, CannotRemoveStaticRepositories> {
        if let Some(config) = self.dynamic_configs.remove(repo_url) {
            self.save();
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

    /// If persistent dynamic configs are enabled, save the current configs to disk. Log, and
    /// ultimately ignore, any errors that occur to make sure forward progress can always be made.
    fn save(&self) {
        let configs = self.dynamic_configs.iter().map(|(_, c)| c.clone()).collect::<Vec<_>>();

        let result = (|| {
            let mut temp_path = self.dynamic_configs_path.clone().into_os_string();
            temp_path.push(".new");
            let temp_path = PathBuf::from(temp_path);
            {
                let f = fs::File::create(&temp_path)?;
                serde_json::to_writer(f, &RepositoryConfigs::Version1(configs))?;
            }
            fs::rename(temp_path, &self.dynamic_configs_path)
        })();

        match result {
            Ok(()) => {}
            Err(err) => {
                fx_log_err!("error while saving repositories: {}", err);
            }
        }
    }
}

/// [RepositoryManagerBuilder] constructs a [RepositoryManager], optionally initializing it
/// with [RepositoryConfig]s passed in directly or loaded out of the filesystem.
#[derive(Clone, Debug)]
pub struct RepositoryManagerBuilder {
    dynamic_configs_path: PathBuf,
    static_configs: HashMap<RepoUri, RepositoryConfig>,
    dynamic_configs: HashMap<RepoUri, RepositoryConfig>,
}

impl RepositoryManagerBuilder {
    /// Create a new builder and initialize it with the dynamic
    /// [RepositoryConfigs](RepositoryConfig) from this path if it exists, and add it to the
    /// [RepositoryManager], or error out if we encounter errors during the load. The
    /// [RepositoryManagerBuilder] is also returned on error in case the errors should be ignored.
    pub fn new<T>(dynamic_configs_path: T) -> Result<Self, (Self, LoadError)>
    where
        T: Into<PathBuf>,
    {
        let dynamic_configs_path = dynamic_configs_path.into();

        let (dynamic_configs, err) = if dynamic_configs_path.exists() {
            match load_configs_file(&dynamic_configs_path) {
                Ok(dynamic_configs) => (dynamic_configs, None),
                Err(err) => (vec![], Some(err)),
            }
        } else {
            (vec![], None)
        };

        let builder = RepositoryManagerBuilder {
            dynamic_configs_path: dynamic_configs_path.into(),
            static_configs: HashMap::new(),
            dynamic_configs: dynamic_configs
                .into_iter()
                .map(|config| (config.repo_url().clone(), config))
                .collect(),
        };

        if let Some(err) = err {
            Err((builder, err))
        } else {
            Ok(builder)
        }
    }

    /// Adds these static [RepoConfigs](RepoConfig) to the [RepositoryManager].
    #[cfg(test)]
    pub fn static_configs<T>(mut self, iter: T) -> Self
    where
        T: IntoIterator<Item = RepositoryConfig>,
    {
        for config in iter.into_iter() {
            self.static_configs.insert(config.repo_url().clone(), config);
        }

        self
    }

    /// Load a directory of [RepositoryConfigs](RepositoryConfig) files into the
    /// [RepositoryManager], or error out if we encounter errors during the load. The
    /// [RepositoryManagerBuilder] is also returned on error in case the errors should be ignored.
    pub fn load_static_configs_dir<T>(
        mut self,
        static_configs_dir: T,
    ) -> Result<Self, (Self, Vec<LoadError>)>
    where
        T: AsRef<Path>,
    {
        let static_configs_dir = static_configs_dir.as_ref();

        let (static_configs, errs) = load_configs_dir(static_configs_dir);
        self.static_configs = static_configs;
        if errs.is_empty() {
            Ok(self)
        } else {
            Err((self, errs))
        }
    }

    /// Build the [RepositoryManager].
    pub fn build(self) -> RepositoryManager {
        RepositoryManager {
            dynamic_configs_path: self.dynamic_configs_path,
            static_configs: self.static_configs,
            dynamic_configs: self.dynamic_configs,
        }
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

    fn assert_does_not_exist_error(err: &LoadError, missing_path: &Path) {
        match &err {
            LoadError::Io { path, error } => {
                assert_eq!(path, missing_path);
                assert_eq!(error.kind(), std::io::ErrorKind::NotFound, "{}", error);
            }
            err => {
                panic!("unexpected error: {}", err);
            }
        }
    }

    fn assert_parse_error(err: &LoadError, invalid_path: &Path) {
        match err {
            LoadError::Parse { path, .. } => {
                assert_eq!(path, invalid_path);
            }
            err => {
                panic!("unexpected error: {}", err);
            }
        }
    }

    fn assert_overridden_error(err: &LoadError, config: &RepositoryConfig) {
        match err {
            LoadError::Overridden { replaced_config } => {
                assert_eq!(replaced_config, config);
            }
            err => {
                panic!("unexpected error: {}", err);
            }
        }
    }

    #[test]
    fn test_insert_get_remove() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let mut repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path).unwrap().build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path.clone(),
                static_configs: HashMap::new(),
                dynamic_configs: HashMap::new(),
            }
        );

        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        assert_eq!(repomgr.get(&fuchsia_uri), None);

        let config1 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();
        let config2 = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        assert_eq!(repomgr.insert(config1.clone()), None);
        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path.clone(),
                static_configs: HashMap::new(),
                dynamic_configs: hashmap! {
                    fuchsia_uri.clone() => config1.clone(),
                },
            }
        );

        assert_eq!(repomgr.insert(config2.clone()), Some(config1.clone()));
        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path.clone(),
                static_configs: HashMap::new(),
                dynamic_configs: hashmap! {
                    fuchsia_uri.clone() => config2.clone(),
                },
            }
        );

        assert_eq!(repomgr.get(&fuchsia_uri), Some(&config2));
        assert_eq!(repomgr.remove(&fuchsia_uri), Ok(Some(config2.clone())));
        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path.clone(),
                static_configs: HashMap::new(),
                dynamic_configs: HashMap::new()
            }
        );
        assert_eq!(repomgr.remove(&fuchsia_uri), Ok(None));
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

        let static_dir = create_dir(vec![(
            "fuchsia.com.json",
            RepositoryConfigs::Version1(vec![fuchsia_config1.clone()]),
        )]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let mut repomgr = RepositoryManagerBuilder::new(dynamic_dir.path().join("config"))
            .unwrap()
            .load_static_configs_dir(static_dir.path())
            .unwrap()
            .build();

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

        let static_dir = create_dir(vec![(
            "fuchsia.com.json",
            RepositoryConfigs::Version1(vec![fuchsia_config1.clone()]),
        )]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let mut repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(static_dir.path())
            .unwrap()
            .build();

        assert_eq!(repomgr.get(&fuchsia_uri), Some(&fuchsia_config1));
        assert_eq!(repomgr.remove(&fuchsia_uri), Err(CannotRemoveStaticRepositories));
        assert_eq!(repomgr.get(&fuchsia_uri), Some(&fuchsia_config1));
    }

    #[test]
    fn test_builder_static_configs_dir_not_exists() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let static_dir = tempfile::tempdir().unwrap();
        let does_not_exist_dir = static_dir.path().join("not-exists");

        let (_, errors) = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(&does_not_exist_dir)
            .unwrap_err();
        assert_eq!(errors.len(), 1, "{:?}", errors);
        assert_does_not_exist_error(&errors[0], &does_not_exist_dir);
    }

    #[test]
    fn test_builder_static_configs_dir_invalid_config() {
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

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let (builder, errors) = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(dir.path())
            .unwrap_err();
        assert_eq!(errors.len(), 1, "{:?}", errors);
        assert_parse_error(&errors[0], &invalid_path);
        let repomgr = builder.build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path,
                static_configs: hashmap! {
                    example_uri => example_config,
                    fuchsia_uri => fuchsia_config,
                },
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_builder_static_configs_dir() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri.clone()).build();

        let example_uri = RepoUri::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_uri.clone()).build();

        let dir = create_dir(vec![
            ("example.com.json", RepositoryConfigs::Version1(vec![example_config.clone()])),
            ("fuchsia.com.json", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
        ]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(dir.path())
            .unwrap()
            .build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path,
                static_configs: hashmap! {
                    example_uri => example_config,
                    fuchsia_uri => fuchsia_config,
                },
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_builder_static_configs_dir_overlapping_filename_wins() {
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

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let (builder, errors) = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(dir.path())
            .unwrap_err();

        assert_eq!(errors.len(), 4);
        assert_overridden_error(&errors[0], &fuchsia_config);
        assert_overridden_error(&errors[1], &fuchsia_com_config);
        assert_overridden_error(&errors[2], &example_config);
        assert_overridden_error(&errors[3], &oem_config);

        let repomgr = builder.build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path,
                static_configs: hashmap! {
                    fuchsia_uri => fuchsia_com_json_config,
                },
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_builder_static_configs_dir_overlapping_first_wins() {
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

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let (builder, errors) = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(dir.path())
            .unwrap_err();

        assert_eq!(errors.len(), 1);
        assert_overridden_error(&errors[0], &fuchsia_config2);

        let repomgr = builder.build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path,
                static_configs: hashmap! {
                    fuchsia_uri => fuchsia_config1,
                },
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_builder_dynamic_configs_path_ignores_if_not_exists() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path).unwrap().build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: dynamic_configs_path,
                static_configs: HashMap::new(),
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_builder_dynamic_configs_path_invalid_config() {
        let dir = tempfile::tempdir().unwrap();
        let invalid_path = dir.path().join("invalid");

        {
            let mut f = File::create(&invalid_path).unwrap();
            f.write(b"hello world").unwrap();
        }

        let (builder, err) = RepositoryManagerBuilder::new(&invalid_path).unwrap_err();
        assert_parse_error(&err, &invalid_path);
        let repomgr = builder.build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path: invalid_path,
                static_configs: HashMap::new(),
                dynamic_configs: HashMap::new(),
            }
        );
    }

    #[test]
    fn test_builder_dynamic_configs_path() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let dynamic_dir =
            create_dir(vec![("config", RepositoryConfigs::Version1(vec![config.clone()]))]);
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path).unwrap().build();

        assert_eq!(
            repomgr,
            RepositoryManager {
                dynamic_configs_path,
                static_configs: HashMap::new(),
                dynamic_configs: hashmap! {
                    fuchsia_uri => config,
                },
            }
        );
    }

    #[test]
    fn test_persistence() {
        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let static_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();
        let static_configs = RepositoryConfigs::Version1(vec![static_config.clone()]);
        let static_dir = create_dir(vec![("config", static_configs.clone())]);

        let old_dynamic_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![2]))
            .build();
        let old_dynamic_configs = RepositoryConfigs::Version1(vec![old_dynamic_config.clone()]);
        let dynamic_dir = create_dir(vec![("config", old_dynamic_configs.clone())]);
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let mut repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(&static_dir)
            .unwrap()
            .build();

        // make sure the dynamic config file didn't change just from opening it.
        let f = File::open(&dynamic_configs_path).unwrap();
        let actual: RepositoryConfigs = serde_json::from_reader(f).unwrap();
        assert_eq!(actual, old_dynamic_configs);

        let new_dynamic_config = RepositoryConfigBuilder::new(fuchsia_uri.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![3]))
            .build();
        let new_dynamic_configs = RepositoryConfigs::Version1(vec![new_dynamic_config.clone()]);

        // Inserting a new repo should update the config file.
        assert_eq!(repomgr.insert(new_dynamic_config.clone()), Some(old_dynamic_config));
        let f = File::open(&dynamic_configs_path).unwrap();
        let actual: RepositoryConfigs = serde_json::from_reader(f).unwrap();
        assert_eq!(actual, new_dynamic_configs);

        // Removing the repo should empty out the file.
        assert_eq!(repomgr.remove(&fuchsia_uri), Ok(Some(new_dynamic_config)));
        let f = File::open(&dynamic_configs_path).unwrap();
        let actual: RepositoryConfigs = serde_json::from_reader(f).unwrap();
        assert_eq!(actual, RepositoryConfigs::Version1(vec![]));

        // We should now be back to the static config.
        assert_eq!(repomgr.get(&fuchsia_uri), Some(&static_config));
        assert_eq!(repomgr.remove(&fuchsia_uri), Err(CannotRemoveStaticRepositories));
    }

    #[test]
    fn test_list_empty() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path).unwrap().build();
        assert_eq!(repomgr.list().collect::<Vec<_>>(), Vec::<&RepositoryConfig>::new());
    }

    #[test]
    fn test_list() {
        let example_uri = RepoUri::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_uri).build();

        let fuchsia_uri = RepoUri::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_uri).build();

        let static_dir = create_dir(vec![
            ("example.com", RepositoryConfigs::Version1(vec![example_config.clone()])),
            ("fuchsia.com", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
        ]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr = RepositoryManagerBuilder::new(&dynamic_configs_path)
            .unwrap()
            .load_static_configs_dir(static_dir.path())
            .unwrap()
            .build();

        assert_eq!(repomgr.list().collect::<Vec<_>>(), vec![&example_config, &fuchsia_config,]);
    }
}
