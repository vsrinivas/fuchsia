// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        amber_connector::AmberConnect,
        experiment::{Experiment, Experiments},
    },
    failure::Fail,
    fidl_fuchsia_amber::{
        self, ControlProxy as AmberProxy, FetchResultEvent, FetchResultMarker,
        OpenedRepositoryMarker, OpenedRepositoryProxy,
    },
    fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig, RepositoryConfigs},
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_url::pkg_url::{PkgUrl, RepoUrl},
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::RwLock,
    std::{
        collections::{btree_set, hash_map::Entry, BTreeSet, HashMap},
        fmt, fs, io,
        path::{Path, PathBuf},
        sync::Arc,
    },
};

/// [RepositoryManager] controls access to all the repository configs used by the package resolver.
#[derive(Debug)]
pub struct RepositoryManager<A: AmberConnect> {
    experiments: Experiments,
    dynamic_configs_path: PathBuf,
    static_configs: HashMap<RepoUrl, RepositoryConfig>,
    dynamic_configs: HashMap<RepoUrl, RepositoryConfig>,
    amber: A,
    conns: Arc<RwLock<HashMap<RepoUrl, OpenedRepositoryProxy>>>,
}

impl<A: AmberConnect> RepositoryManager<A> {
    /// Returns a reference to the [RepositoryConfig] config identified by the config `repo_url`,
    /// or `None` if it does not exist.
    pub fn get(&self, repo_url: &RepoUrl) -> Option<&RepositoryConfig> {
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
        // Clear any connections so we aren't talking to stale repositories.
        if self.conns.write().remove(config.repo_url()).is_some() {
            fx_log_info!("closing connection to {} repo because config changed", config.repo_url());
        }

        let result = self.dynamic_configs.insert(config.repo_url().clone(), config);
        self.save();
        result
    }

    /// Removes a [RepositoryConfig] identified by the config `repo_url`.
    pub fn remove(
        &mut self,
        repo_url: &RepoUrl,
    ) -> Result<Option<RepositoryConfig>, CannotRemoveStaticRepositories> {
        if let Some(config) = self.dynamic_configs.remove(repo_url) {
            // Clear any connections so we aren't talking to stale repositories.
            if self.conns.write().remove(repo_url).is_some() {
                fx_log_info!(
                    "closing connection to {} repo because config removed",
                    config.repo_url()
                );
            }

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
    pub fn list(&self) -> List<A> {
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

    /// Connect to the amber service.
    pub fn connect_to_amber(&self) -> Result<AmberProxy, Status> {
        self.amber.connect()
    }

    pub fn get_package<'a>(
        &self,
        url: &'a PkgUrl,
    ) -> impl Future<Output = Result<BlobId, Status>> + 'a {
        let repo = self.connect_to_repo(url.repo());
        get_package(self.experiments.clone(), repo, url)
    }

    fn connect_to_repo(&self, url: &RepoUrl) -> Result<OpenedRepositoryProxy, Status> {
        // Exit early if we've already connected to this repository.
        if let Some(conn) = self.conns.read().get(url) {
            if !conn.is_closed() {
                return Ok(conn.clone());
            }
        }

        // Next, check if we actually have a repository defined for this URI. If not, exit early
        // with NOT_FOUND.
        let config = if let Some(config) = self.get(url) {
            config.clone()
        } else {
            return Err(Status::NOT_FOUND);
        };

        // Create the proxy to Amber. In order to minimize our time with the lock held, we'll
        // create the proxy first, even if it proves to be redundant because we lost the race with
        // another thread.
        let (repo, repo_server_end) = fidl::endpoints::create_proxy::<OpenedRepositoryMarker>()
            .map_err(|err| {
                fx_log_err!("failed to create proxy: {}", err);
                Status::INTERNAL
            })?;

        // The repo is defined, so we might actually need to connect to the device.
        {
            let mut conns = self.conns.write();

            // It's still possible we raced with some other connection attempt, so exit early if
            // they created a valid connection.
            if let Some(conn) = conns.get(url) {
                if !conn.is_closed() {
                    return Ok(conn.clone());
                }
            }

            conns.insert(url.clone(), repo.clone());
        };

        let amber = self.connect_to_amber()?;

        // We'll actually do the connection in a separate async context. It will log any errors it
        // finds.
        fasync::spawn(async move {
            let status = match amber.open_repository(config.into(), repo_server_end).await {
                Ok(status) => status,
                Err(err) => {
                    fx_log_err!("failed to open repository: {}", err);
                    return;
                }
            };

            if let Err(err) = Status::ok(status) {
                fx_log_err!("failed to open repository: {}", err);
            }
        });

        return Ok(repo);
    }
}

async fn get_package(
    experiments: Experiments,
    repo: Result<OpenedRepositoryProxy, Status>,
    url: &PkgUrl,
) -> Result<BlobId, Status> {
    let repo = repo?;

    // While the fuchsia-pkg:// spec doesn't require a package name, we do.
    let name = url.name().ok_or_else(|| {
        fx_log_err!("package url is missing a package name: {}", url);
        Err(Status::INVALID_ARGS)
    })?;

    if experiments.get(Experiment::DownloadBlob) {
        // not implemented yet
        fx_log_err!("Experiment::DownloadBlob not implemented");
        Err(Status::INTERNAL)
    } else {
        let (result, result_server_end) = fidl::endpoints::create_proxy::<FetchResultMarker>()
            .map_err(|err| {
                fx_log_err!("failed to create proxy: {}", err);
                Status::INTERNAL
            })?;

        // Ask amber to cache the package.
        repo.get_update_complete(&name, url.variant(), url.package_hash(), result_server_end)
            .map_err(|err| {
                fx_log_err!("error communicating with amber: {:?}", err);
                Status::INTERNAL
            })?;

        match result.take_event_stream().into_future().await {
            (Some(Ok(FetchResultEvent::OnSuccess { merkle })), _) => match merkle.parse() {
                Ok(merkle) => Ok(merkle),
                Err(err) => {
                    fx_log_err!("{:?} is not a valid merkleroot: {:?}", merkle, err);
                    return Err(Status::INTERNAL);
                }
            },
            (Some(Ok(FetchResultEvent::OnError { result, message })), _) => {
                let status = Status::from_raw(result);
                fx_log_err!("error fetching package: {}: {}", status, message);
                return Err(status);
            }
            (Some(Err(err)), _) => {
                fx_log_err!("error communicating with amber: {}", err);
                return Err(Status::INTERNAL);
            }
            (None, _) => {
                fx_log_err!("amber unexpectedly closed fetch result channel");
                return Err(Status::INTERNAL);
            }
        }
    }
}

/// [RepositoryManagerBuilder] constructs a [RepositoryManager], optionally initializing it
/// with [RepositoryConfig]s passed in directly or loaded out of the filesystem.
#[derive(Clone, Debug)]
pub struct RepositoryManagerBuilder<A: AmberConnect> {
    dynamic_configs_path: PathBuf,
    static_configs: HashMap<RepoUrl, RepositoryConfig>,
    dynamic_configs: HashMap<RepoUrl, RepositoryConfig>,
    amber: A,
    experiments: Experiments,
}

impl<A: AmberConnect> RepositoryManagerBuilder<A> {
    /// Create a new builder and initialize it with the dynamic
    /// [RepositoryConfigs](RepositoryConfig) from this path if it exists, and add it to the
    /// [RepositoryManager], or error out if we encounter errors during the load. The
    /// [RepositoryManagerBuilder] is also returned on error in case the errors should be ignored.
    pub fn new<P>(
        dynamic_configs_path: P,
        amber: A,
        experiments: Experiments,
    ) -> Result<Self, (Self, LoadError)>
    where
        P: Into<PathBuf>,
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
            amber,
            experiments,
        };

        if let Some(err) = err {
            Err((builder, err))
        } else {
            Ok(builder)
        }
    }

    /// Create a new builder with no enabled experiments.
    #[cfg(test)]
    pub fn new_test<P>(dynamic_configs_path: P, amber: A) -> Result<Self, (Self, LoadError)>
    where
        P: Into<PathBuf>,
    {
        Self::new(dynamic_configs_path, amber, Experiments::none())
    }

    /// Adds these static [RepoConfigs](RepoConfig) to the [RepositoryManager].
    #[cfg(test)]
    pub fn static_configs<I>(mut self, iter: I) -> Self
    where
        I: IntoIterator<Item = RepositoryConfig>,
    {
        for config in iter.into_iter() {
            self.static_configs.insert(config.repo_url().clone(), config);
        }

        self
    }

    /// Load a directory of [RepositoryConfigs](RepositoryConfig) files into the
    /// [RepositoryManager], or error out if we encounter errors during the load. The
    /// [RepositoryManagerBuilder] is also returned on error in case the errors should be ignored.
    pub fn load_static_configs_dir<P>(
        mut self,
        static_configs_dir: P,
    ) -> Result<Self, (Self, Vec<LoadError>)>
    where
        P: AsRef<Path>,
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
    pub fn build(self) -> RepositoryManager<A> {
        RepositoryManager {
            dynamic_configs_path: self.dynamic_configs_path,
            static_configs: self.static_configs,
            dynamic_configs: self.dynamic_configs,
            amber: self.amber,
            experiments: self.experiments,
            conns: Arc::new(RwLock::new(HashMap::new())),
        }
    }
}

/// Load a directory of [RepositoryConfigs] files into a [RepositoryManager], or error out if we
/// encounter io errors during the load. It returns a [RepositoryManager], as well as all the
/// individual [LoadError] errors encountered during the load.
fn load_configs_dir<T: AsRef<Path>>(
    dir: T,
) -> (HashMap<RepoUrl, RepositoryConfig>, Vec<LoadError>) {
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

        let expected_url = path
            .file_stem()
            .and_then(|name| name.to_str())
            .and_then(|name| RepoUrl::new(name.to_string()).ok());

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
                    let replaced_config = if Some(entry.key()) == expected_url.as_ref() {
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
pub struct List<'a, A: AmberConnect> {
    keys: btree_set::IntoIter<&'a RepoUrl>,
    repo_mgr: &'a RepositoryManager<A>,
}

impl<'a, A: AmberConnect> Iterator for List<'a, A> {
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

    use crate::test_util::{create_dir, ClosedAmberConnector};
    use fidl_fuchsia_pkg_ext::{RepositoryConfigBuilder, RepositoryKey};
    use fuchsia_url::pkg_url::RepoUrl;
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
        let mut repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .build();
        assert_eq!(dynamic_configs_path, dynamic_configs_path.clone());
        assert_eq!(repomgr.static_configs, HashMap::new());
        assert_eq!(repomgr.dynamic_configs, HashMap::new());

        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        assert_eq!(repomgr.get(&fuchsia_url), None);

        let config1 = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();
        let config2 = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        assert_eq!(repomgr.insert(config1.clone()), None);
        assert_eq!(dynamic_configs_path, dynamic_configs_path.clone());
        assert_eq!(repomgr.static_configs, HashMap::new());
        assert_eq!(repomgr.dynamic_configs, hashmap! { fuchsia_url.clone() => config1.clone() });

        assert_eq!(repomgr.insert(config2.clone()), Some(config1.clone()));
        assert_eq!(dynamic_configs_path, dynamic_configs_path.clone());
        assert_eq!(repomgr.static_configs, HashMap::new());
        assert_eq!(repomgr.dynamic_configs, hashmap! { fuchsia_url.clone() => config2.clone() });

        assert_eq!(repomgr.get(&fuchsia_url), Some(&config2));
        assert_eq!(repomgr.remove(&fuchsia_url), Ok(Some(config2.clone())));
        assert_eq!(repomgr.static_configs, HashMap::new());
        assert_eq!(repomgr.dynamic_configs, HashMap::new());
        assert_eq!(repomgr.remove(&fuchsia_url), Ok(None));
    }

    #[test]
    fn shadowing_static_config() {
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config1 = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        let fuchsia_config2 = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![2]))
            .build();

        let static_dir = create_dir(vec![(
            "fuchsia.com.json",
            RepositoryConfigs::Version1(vec![fuchsia_config1.clone()]),
        )]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let mut repomgr = RepositoryManagerBuilder::new_test(
            dynamic_dir.path().join("config"),
            ClosedAmberConnector,
        )
        .unwrap()
        .load_static_configs_dir(static_dir.path())
        .unwrap()
        .build();

        assert_eq!(repomgr.get(&fuchsia_url), Some(&fuchsia_config1));
        assert_eq!(repomgr.insert(fuchsia_config2.clone()), None);
        assert_eq!(repomgr.get(&fuchsia_url), Some(&fuchsia_config2));
        assert_eq!(repomgr.remove(&fuchsia_url), Ok(Some(fuchsia_config2)));
        assert_eq!(repomgr.get(&fuchsia_url), Some(&fuchsia_config1));
    }

    #[test]
    fn cannot_remove_static_config() {
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config1 = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        let static_dir = create_dir(vec![(
            "fuchsia.com.json",
            RepositoryConfigs::Version1(vec![fuchsia_config1.clone()]),
        )]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let mut repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .load_static_configs_dir(static_dir.path())
                .unwrap()
                .build();

        assert_eq!(repomgr.get(&fuchsia_url), Some(&fuchsia_config1));
        assert_eq!(repomgr.remove(&fuchsia_url), Err(CannotRemoveStaticRepositories));
        assert_eq!(repomgr.get(&fuchsia_url), Some(&fuchsia_config1));
    }

    #[test]
    fn test_builder_static_configs_dir_not_exists() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let static_dir = tempfile::tempdir().unwrap();
        let does_not_exist_dir = static_dir.path().join("not-exists");

        let (_, errors) =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
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

        let example_url = RepoUrl::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
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

        let (builder, errors) =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .load_static_configs_dir(dir.path())
                .unwrap_err();
        assert_eq!(errors.len(), 1, "{:?}", errors);
        assert_parse_error(&errors[0], &invalid_path);
        let repomgr = builder.build();

        assert_eq!(repomgr.dynamic_configs_path, dynamic_configs_path);
        assert_eq!(
            repomgr.static_configs,
            hashmap! {
                example_url => example_config,
                fuchsia_url => fuchsia_config,
            }
        );
        assert_eq!(repomgr.dynamic_configs, HashMap::new());
    }

    #[test]
    fn test_builder_static_configs_dir() {
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_url.clone()).build();

        let example_url = RepoUrl::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_url.clone()).build();

        let dir = create_dir(vec![
            ("example.com.json", RepositoryConfigs::Version1(vec![example_config.clone()])),
            ("fuchsia.com.json", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
        ]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .load_static_configs_dir(dir.path())
                .unwrap()
                .build();

        assert_eq!(repomgr.dynamic_configs_path, dynamic_configs_path);
        assert_eq!(
            repomgr.static_configs,
            hashmap! {
                example_url => example_config,
                fuchsia_url => fuchsia_config,
            }
        );
        assert_eq!(repomgr.dynamic_configs, HashMap::new());
    }

    #[test]
    fn test_builder_static_configs_dir_overlapping_filename_wins() {
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let fuchsia_com_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();

        let fuchsia_com_json_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![2]))
            .build();

        let example_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![3]))
            .build();

        let oem_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
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
        let (builder, errors) =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .load_static_configs_dir(dir.path())
                .unwrap_err();

        assert_eq!(errors.len(), 4);
        assert_overridden_error(&errors[0], &fuchsia_config);
        assert_overridden_error(&errors[1], &fuchsia_com_config);
        assert_overridden_error(&errors[2], &example_config);
        assert_overridden_error(&errors[3], &oem_config);

        let repomgr = builder.build();

        assert_eq!(repomgr.dynamic_configs_path, dynamic_configs_path);
        assert_eq!(repomgr.static_configs, hashmap! { fuchsia_url => fuchsia_com_json_config });
        assert_eq!(repomgr.dynamic_configs, HashMap::new());
    }

    #[test]
    fn test_builder_static_configs_dir_overlapping_first_wins() {
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let fuchsia_config1 = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let fuchsia_config2 = RepositoryConfigBuilder::new(fuchsia_url.clone())
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
        let (builder, errors) =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .load_static_configs_dir(dir.path())
                .unwrap_err();

        assert_eq!(errors.len(), 1);
        assert_overridden_error(&errors[0], &fuchsia_config2);

        let repomgr = builder.build();

        assert_eq!(repomgr.dynamic_configs_path, dynamic_configs_path);
        assert_eq!(repomgr.static_configs, hashmap! { fuchsia_url => fuchsia_config1 });
        assert_eq!(repomgr.dynamic_configs, HashMap::new());
    }

    #[test]
    fn test_builder_dynamic_configs_path_ignores_if_not_exists() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .build();

        assert_eq!(repomgr.dynamic_configs_path, dynamic_configs_path);
        assert_eq!(repomgr.static_configs, HashMap::new());
        assert_eq!(repomgr.dynamic_configs, HashMap::new());
    }

    #[test]
    fn test_builder_dynamic_configs_path_invalid_config() {
        let dir = tempfile::tempdir().unwrap();
        let invalid_path = dir.path().join("invalid");

        {
            let mut f = File::create(&invalid_path).unwrap();
            f.write(b"hello world").unwrap();
        }

        let (builder, err) =
            RepositoryManagerBuilder::new_test(&invalid_path, ClosedAmberConnector).unwrap_err();
        assert_parse_error(&err, &invalid_path);
        let repomgr = builder.build();

        assert_eq!(repomgr.dynamic_configs_path, invalid_path);
        assert_eq!(repomgr.static_configs, HashMap::new());
        assert_eq!(repomgr.dynamic_configs, HashMap::new());
    }

    #[test]
    fn test_builder_dynamic_configs_path() {
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![0]))
            .build();

        let dynamic_dir =
            create_dir(vec![("config", RepositoryConfigs::Version1(vec![config.clone()]))]);
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .build();

        assert_eq!(repomgr.dynamic_configs_path, dynamic_configs_path);
        assert_eq!(repomgr.static_configs, HashMap::new());
        assert_eq!(repomgr.dynamic_configs, hashmap! { fuchsia_url => config });
    }

    #[test]
    fn test_persistence() {
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();

        let static_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![1]))
            .build();
        let static_configs = RepositoryConfigs::Version1(vec![static_config.clone()]);
        let static_dir = create_dir(vec![("config", static_configs.clone())]);

        let old_dynamic_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![2]))
            .build();
        let old_dynamic_configs = RepositoryConfigs::Version1(vec![old_dynamic_config.clone()]);
        let dynamic_dir = create_dir(vec![("config", old_dynamic_configs.clone())]);
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let mut repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .load_static_configs_dir(&static_dir)
                .unwrap()
                .build();

        // make sure the dynamic config file didn't change just from opening it.
        let f = File::open(&dynamic_configs_path).unwrap();
        let actual: RepositoryConfigs = serde_json::from_reader(f).unwrap();
        assert_eq!(actual, old_dynamic_configs);

        let new_dynamic_config = RepositoryConfigBuilder::new(fuchsia_url.clone())
            .add_root_key(RepositoryKey::Ed25519(vec![3]))
            .build();
        let new_dynamic_configs = RepositoryConfigs::Version1(vec![new_dynamic_config.clone()]);

        // Inserting a new repo should update the config file.
        assert_eq!(repomgr.insert(new_dynamic_config.clone()), Some(old_dynamic_config));
        let f = File::open(&dynamic_configs_path).unwrap();
        let actual: RepositoryConfigs = serde_json::from_reader(f).unwrap();
        assert_eq!(actual, new_dynamic_configs);

        // Removing the repo should empty out the file.
        assert_eq!(repomgr.remove(&fuchsia_url), Ok(Some(new_dynamic_config)));
        let f = File::open(&dynamic_configs_path).unwrap();
        let actual: RepositoryConfigs = serde_json::from_reader(f).unwrap();
        assert_eq!(actual, RepositoryConfigs::Version1(vec![]));

        // We should now be back to the static config.
        assert_eq!(repomgr.get(&fuchsia_url), Some(&static_config));
        assert_eq!(repomgr.remove(&fuchsia_url), Err(CannotRemoveStaticRepositories));
    }

    #[test]
    fn test_list_empty() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .build();
        assert_eq!(repomgr.list().collect::<Vec<_>>(), Vec::<&RepositoryConfig>::new());
    }

    #[test]
    fn test_list() {
        let example_url = RepoUrl::parse("fuchsia-pkg://example.com").unwrap();
        let example_config = RepositoryConfigBuilder::new(example_url).build();

        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let fuchsia_config = RepositoryConfigBuilder::new(fuchsia_url).build();

        let static_dir = create_dir(vec![
            ("example.com", RepositoryConfigs::Version1(vec![example_config.clone()])),
            ("fuchsia.com", RepositoryConfigs::Version1(vec![fuchsia_config.clone()])),
        ]);

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        let repomgr =
            RepositoryManagerBuilder::new_test(&dynamic_configs_path, ClosedAmberConnector)
                .unwrap()
                .load_static_configs_dir(static_dir.path())
                .unwrap()
                .build();

        assert_eq!(repomgr.list().collect::<Vec<_>>(), vec![&example_config, &fuchsia_config,]);
    }
}
