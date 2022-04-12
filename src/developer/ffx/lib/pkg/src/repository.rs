// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resource::{Resource, ResourceRange},
    anyhow::{anyhow, Context, Result},
    async_lock::Mutex as AsyncMutex,
    camino::Utf8PathBuf,
    fidl_fuchsia_developer_ffx::{ListFields, PackageEntry, RepositoryPackage},
    fidl_fuchsia_developer_ffx_ext::{RepositorySpec, RepositoryStorageType},
    fidl_fuchsia_pkg as pkg,
    fuchsia_archive::AsyncReader,
    fuchsia_pkg::MetaContents,
    futures::{
        future::{join_all, try_join_all},
        io::Cursor,
        stream::BoxStream,
        AsyncReadExt,
    },
    io_util::file::Adapter,
    parking_lot::Mutex as SyncMutex,
    serde::{Deserialize, Serialize},
    std::{
        io,
        sync::{
            atomic::{AtomicUsize, Ordering},
            Arc,
        },
        time::SystemTime,
    },
    tuf::{
        client::{Client, Config},
        crypto::KeyType,
        interchange::Json,
        metadata::{
            Metadata as _, MetadataPath, MetadataVersion, RawSignedMetadata, Role,
            TargetDescription, TargetPath, TargetsMetadata,
        },
        repository::{EphemeralRepository, RepositoryProvider},
        verify::Verified,
    },
    url::ParseError,
};

mod file_system;
mod pm;

pub mod http_repository;

pub use file_system::FileSystemRepository;
pub use http_repository::{package_download, HttpRepository};
pub use pm::PmRepository;

/// A unique ID which is given to every repository.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct RepositoryId(usize);

impl RepositoryId {
    fn new() -> Self {
        static NEXT_ID: AtomicUsize = AtomicUsize::new(0);
        RepositoryId(NEXT_ID.fetch_add(1, Ordering::Relaxed))
    }
}

/// The below types exist to provide definitions with Serialize.
/// TODO(fxbug.dev/76041) They should be removed in favor of the
/// corresponding fidl-fuchsia-pkg-ext types.
#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct RepositoryConfig {
    pub repo_url: Option<String>,
    pub root_keys: Option<Vec<RepositoryKeyConfig>>,
    pub root_version: Option<u32>,
    pub mirrors: Option<Vec<MirrorConfig>>,
    pub storage_type: Option<RepositoryStorageType>,
}

impl From<RepositoryConfig> for pkg::RepositoryConfig {
    fn from(repo_config: RepositoryConfig) -> Self {
        pkg::RepositoryConfig {
            repo_url: repo_config.repo_url,
            root_keys: repo_config.root_keys.map(|v| v.into_iter().map(|r| r.into()).collect()),
            root_version: repo_config.root_version,
            mirrors: repo_config.mirrors.map(|v| v.into_iter().map(|m| m.into()).collect()),
            storage_type: repo_config.storage_type.map(|v| match v {
                RepositoryStorageType::Ephemeral => pkg::RepositoryStorageType::Ephemeral,
                RepositoryStorageType::Persistent => pkg::RepositoryStorageType::Persistent,
            }),
            ..pkg::RepositoryConfig::EMPTY
        }
    }
}

#[derive(Debug, Clone, Deserialize, PartialEq, Serialize)]
pub enum RepositoryKeyConfig {
    /// The raw ed25519 public key as binary data.
    Ed25519Key(Vec<u8>),
}

impl Into<pkg::RepositoryKeyConfig> for RepositoryKeyConfig {
    fn into(self) -> pkg::RepositoryKeyConfig {
        match self {
            Self::Ed25519Key(keys) => pkg::RepositoryKeyConfig::Ed25519Key(keys),
        }
    }
}

#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct MirrorConfig {
    /// The base URL of the TUF metadata on this mirror. Required.
    pub mirror_url: Option<String>,
    /// Whether or not to automatically monitor the mirror for updates. Required.
    pub subscribe: Option<bool>,
}

impl Into<pkg::MirrorConfig> for MirrorConfig {
    fn into(self) -> pkg::MirrorConfig {
        pkg::MirrorConfig {
            mirror_url: self.mirror_url,
            subscribe: self.subscribe,
            ..pkg::MirrorConfig::EMPTY
        }
    }
}
#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("not found")]
    NotFound,
    #[error("invalid path '{0}'")]
    InvalidPath(Utf8PathBuf),
    #[error("I/O error")]
    Io(#[source] io::Error),
    #[error("URL Parsing Error")]
    URLParseError(#[source] ParseError),
    #[error(transparent)]
    Tuf(#[from] tuf::Error),
    #[error(transparent)]
    Far(#[from] fuchsia_archive::Error),
    #[error(transparent)]
    Meta(#[from] fuchsia_pkg::MetaContentsError),
    #[error(transparent)]
    ParseInt(#[from] std::num::ParseIntError),
    #[error(transparent)]
    ToStr(#[from] hyper::header::ToStrError),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
    #[error("range not satisfiable")]
    RangeNotSatisfiable,
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        if err.kind() == std::io::ErrorKind::NotFound {
            Error::NotFound
        } else {
            Error::Io(err)
        }
    }
}

impl From<ParseError> for Error {
    fn from(err: ParseError) -> Self {
        Error::URLParseError(err)
    }
}

fn is_component_manifest(s: &str) -> bool {
    return s.ends_with(".cm") || s.ends_with(".cmx");
}

pub struct Repository {
    /// The name of the repository.
    name: String,

    /// A unique ID for the repository, scoped to this instance of the daemon.
    id: RepositoryId,

    /// Backend for this repository
    backend: Box<dyn RepositoryBackend + Send + Sync>,

    /// Call these functions upon drop. This is synchronous since it's used in the Drop impl.
    drop_handlers: SyncMutex<Vec<Box<dyn FnOnce() + Send + Sync>>>,

    /// The TUF client for this repository
    client:
        Arc<AsyncMutex<Client<Json, EphemeralRepository<Json>, Box<dyn RepositoryProvider<Json>>>>>,
}

impl Repository {
    pub async fn new(
        name: &str,
        backend: Box<dyn RepositoryBackend + Send + Sync>,
    ) -> Result<Self, Error> {
        let tuf_repo = backend.get_tuf_repo()?;
        let tuf_client = get_tuf_client(tuf_repo).await?;

        Ok(Self {
            name: name.to_string(),
            id: RepositoryId::new(),
            backend,
            client: Arc::new(AsyncMutex::new(tuf_client)),
            drop_handlers: SyncMutex::new(Vec::new()),
        })
    }

    pub fn id(&self) -> RepositoryId {
        self.id
    }

    pub fn repo_url(&self) -> String {
        format!("fuchsia-pkg://{}", self.name)
    }

    /// Stores the given function to be run when the repository is dropped.
    pub fn on_drop<F: FnOnce() + Send + Sync + 'static>(&self, f: F) {
        self.drop_handlers.lock().push(Box::new(f));
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    /// Get a [RepositorySpec] for this [Repository]
    pub fn spec(&self) -> RepositorySpec {
        self.backend.spec()
    }

    /// Returns if the repository supports watching for timestamp changes.
    pub fn supports_watch(&self) -> bool {
        return self.backend.supports_watch();
    }

    /// Return a stream that yields whenever the repository's timestamp changes.
    pub fn watch(&self) -> anyhow::Result<BoxStream<'static, ()>> {
        self.backend.watch()
    }

    /// Return a stream of bytes for the metadata resource.
    pub async fn fetch_metadata(&self, path: &str) -> Result<Resource, Error> {
        self.fetch_metadata_range(path, ResourceRange::RangeFull).await
    }

    /// Return a stream of bytes for the metadata resource in given range.
    pub async fn fetch_metadata_range(
        &self,
        path: &str,
        range: ResourceRange,
    ) -> Result<Resource, Error> {
        self.backend.fetch_metadata(path, range).await
    }

    /// Return a stream of bytes for the blob resource.
    pub async fn fetch_blob(&self, path: &str) -> Result<Resource, Error> {
        self.fetch_blob_range(path, ResourceRange::RangeFull).await
    }

    /// Return a stream of bytes for the blob resource in given range.
    pub async fn fetch_blob_range(
        &self,
        path: &str,
        range: ResourceRange,
    ) -> Result<Resource, Error> {
        self.backend.fetch_blob(path, range).await
    }

    /// Return the target description for a TUF target path.
    pub async fn get_target_description(
        &self,
        path: &str,
    ) -> Result<Option<TargetDescription>, Error> {
        let trusted_targets = {
            let mut client = self.client.lock().await;

            // Get the latest TUF metadata.
            let _ = client.update().await?;

            client.database().trusted_targets().cloned()
        };

        match trusted_targets {
            Some(trusted_targets) => Ok(trusted_targets
                .targets()
                .get(&TargetPath::new(path).map_err(|e| anyhow::anyhow!(e))?)
                .map(|t| t.clone())),
            None => Ok(None),
        }
    }

    pub async fn get_config(
        &self,
        mirror_url: &str,
        storage_type: Option<RepositoryStorageType>,
    ) -> Result<RepositoryConfig, Error> {
        let trusted_root = {
            let mut client = self.client.lock().await;

            // Update the root metadata to the latest version. We don't care if the other metadata has
            // expired.
            //
            // FIXME: This can be replaced with `client.update_root()` once
            // https://github.com/heartsucker/rust-tuf/pull/318 lands.
            match client.update().await {
                Ok(_) => {}
                Err(err @ tuf::Error::ExpiredMetadata(Role::Root)) => {
                    return Err(err.into());
                }
                Err(tuf::Error::ExpiredMetadata(_)) => {}
                Err(err) => {
                    return Err(err.into());
                }
            }

            client.database().trusted_root().clone()
        };

        let root_keys = trusted_root
            .root_keys()
            .filter(|k| *k.typ() == KeyType::Ed25519)
            .map(|key| RepositoryKeyConfig::Ed25519Key(key.as_bytes().to_vec()))
            .collect::<Vec<_>>();

        let root_version = trusted_root.version();

        Ok(RepositoryConfig {
            repo_url: Some(self.repo_url()),
            root_keys: Some(root_keys),
            root_version: Some(root_version),
            mirrors: Some(vec![MirrorConfig {
                mirror_url: Some(format!("http://{}", mirror_url)),
                subscribe: Some(self.backend.supports_watch()),
            }]),
            storage_type: storage_type,
        })
    }

    async fn get_components_for_package(
        &self,
        trusted_targets: &Verified<TargetsMetadata>,
        package: &RepositoryPackage,
    ) -> Result<Option<Vec<PackageEntry>>> {
        let package_entries = self
            .show_target_package(trusted_targets, package.name.as_ref().unwrap().to_string())
            .await?;

        if package_entries.is_none() {
            return Ok(None);
        }

        let components = package_entries
            .unwrap()
            .into_iter()
            .filter(|e| is_component_manifest(&e.path.as_ref().unwrap()))
            .collect();

        Ok(Some(components))
    }

    // TODO(fxbug.dev/79915) add tests for this method.
    pub async fn list_packages(
        &self,
        include_fields: ListFields,
    ) -> Result<Vec<RepositoryPackage>, Error> {
        let trusted_targets = {
            let mut client = self.client.lock().await;

            // Get the latest TUF metadata.
            client.update().await.context("updating TUF metadata")?;

            client.database().trusted_targets().context("missing target information")?.clone()
        };

        let packages: Result<Vec<RepositoryPackage>, Error> =
            join_all(trusted_targets.targets().into_iter().filter_map(|(k, v)| {
                let size = v.length();
                let custom = v.custom();
                let hash = custom.get("merkle")?.as_str().unwrap_or("").to_string();
                Some(async move {
                    let mut bytes = vec![];
                    self.fetch_blob(&hash)
                        .await
                        .with_context(|| format!("fetching blob {}", hash))?
                        .read_to_end(&mut bytes)
                        .await
                        .with_context(|| format!("reading blob {}", hash))?;
                    let mut archive = AsyncReader::new(Adapter::new(Cursor::new(bytes))).await?;
                    let contents = archive.read_file("meta/contents").await?;
                    let contents = MetaContents::deserialize(contents.as_slice())?;

                    let size = size
                        + try_join_all(contents.contents().into_iter().map(
                            |(_, hash)| async move {
                                self.fetch_blob(&hash.to_string()).await.map(|x| x.content_len)
                            },
                        ))
                        .await?
                        .into_iter()
                        .sum::<u64>();

                    let modified = self
                        .backend
                        .blob_modification_time(&hash)
                        .await
                        .with_context(|| format!("fetching blob modification time {}", hash))?
                        .map(|x| -> anyhow::Result<u64> {
                            Ok(x.duration_since(SystemTime::UNIX_EPOCH)?.as_secs())
                        })
                        .transpose()?;
                    Ok(RepositoryPackage {
                        name: Some(k.to_string()),
                        size: Some(size),
                        hash: Some(hash),
                        modified,
                        ..RepositoryPackage::EMPTY
                    })
                })
            }))
            .await
            .into_iter()
            .collect();

        let mut packages = packages?;

        if include_fields.intersects(ListFields::COMPONENTS) {
            for package in packages.iter_mut() {
                match self.get_components_for_package(&trusted_targets, &package).await {
                    Ok(components) => package.entries = components,
                    Err(e) => {
                        log::error!(
                            "failed to get components for package '{}': {}",
                            package.name.as_ref().unwrap_or(&String::from("<unknown>")),
                            e
                        )
                    }
                };
            }
        }

        Ok(packages)
    }

    pub async fn show_package(&self, package_name: String) -> Result<Option<Vec<PackageEntry>>> {
        let trusted_targets = {
            let mut client = self.client.lock().await;

            // Get the latest TUF metadata.
            client.update().await?;

            client.database().trusted_targets().context("expected targets information")?.clone()
        };

        self.show_target_package(&trusted_targets, package_name).await
    }

    async fn show_target_package(
        &self,
        trusted_targets: &Verified<TargetsMetadata>,
        package_name: String,
    ) -> Result<Option<Vec<PackageEntry>>> {
        let target_path = TargetPath::new(&package_name)?;
        let target = if let Some(target) = trusted_targets.targets().get(&target_path) {
            target
        } else {
            return Ok(None);
        };

        let size = target.length();
        let custom = target.custom();

        let hash = custom
            .get("merkle")
            .ok_or_else(|| anyhow!("package {:?} is missing the `merkle` field", package_name))?;

        let hash = hash
            .as_str()
            .ok_or_else(|| {
                anyhow!("package {:?} hash should be a string, not {:?}", package_name, hash)
            })?
            .to_string();

        // Read the meta.far.
        let mut meta_far_bytes = vec![];
        self.fetch_blob(&hash)
            .await
            .with_context(|| format!("fetching blob {}", hash))?
            .read_to_end(&mut meta_far_bytes)
            .await
            .with_context(|| format!("reading blob {}", hash))?;

        let mut archive = AsyncReader::new(Adapter::new(Cursor::new(meta_far_bytes))).await?;

        let modified = self
            .backend
            .blob_modification_time(&hash)
            .await?
            .map(|x| -> anyhow::Result<u64> {
                Ok(x.duration_since(SystemTime::UNIX_EPOCH)?.as_secs())
            })
            .transpose()?;

        // Add entry for meta.far
        let mut entries = vec![PackageEntry {
            path: Some("meta.far".to_string()),
            hash: Some(hash),
            size: Some(size),
            modified,
            ..PackageEntry::EMPTY
        }];

        entries.extend(archive.list().map(|item| PackageEntry {
            path: Some(item.path().to_string()),
            size: Some(item.length()),
            modified,
            ..PackageEntry::EMPTY
        }));

        match archive.read_file("meta/contents").await {
            Ok(c) => {
                let contents = MetaContents::deserialize(c.as_slice())?;
                for (name, hash) in contents.contents() {
                    let hash_string = hash.to_string();
                    let size = self.fetch_blob(&hash_string).await?.content_len;
                    let modified = self
                        .backend
                        .blob_modification_time(&hash_string)
                        .await?
                        .map(|x| -> anyhow::Result<u64> {
                            Ok(x.duration_since(SystemTime::UNIX_EPOCH)?.as_secs())
                        })
                        .transpose()?;

                    entries.push(PackageEntry {
                        path: Some(name.to_owned()),
                        hash: Some(hash_string),
                        size: Some(size),
                        modified,
                        ..PackageEntry::EMPTY
                    });
                }
            }
            Err(e) => {
                log::warn!("failed to read meta/contents for package {}: {}", package_name, e);
            }
        }

        Ok(Some(entries))
    }
}

impl Drop for Repository {
    fn drop(&mut self) {
        for handler in std::mem::take(&mut *self.drop_handlers.lock()) {
            (handler)()
        }
    }
}

async fn get_tuf_client(
    tuf_repo: Box<dyn RepositoryProvider<Json>>,
) -> Result<Client<Json, EphemeralRepository<Json>, Box<dyn RepositoryProvider<Json>>>, Error> {
    let metadata_repo = EphemeralRepository::<Json>::new();

    let raw_signed_meta = {
        // FIXME(http://fxbug.dev/92126) we really should be initializing trust, rather than just
        // trusting 1.root.json.
        let md = tuf_repo
            .fetch_metadata(&MetadataPath::from_role(&Role::Root), MetadataVersion::Number(1))
            .await;

        let mut metadata = match md {
            Err(_) => {
                tuf_repo
                    .fetch_metadata(&MetadataPath::from_role(&Role::Root), MetadataVersion::None)
                    .await?
            }
            Ok(meta) => meta,
        };

        let mut buf = Vec::new();
        metadata.read_to_end(&mut buf).await.context("reading metadata")?;

        RawSignedMetadata::<Json, _>::new(buf)
    };

    let client =
        Client::with_trusted_root(Config::default(), &raw_signed_meta, metadata_repo, tuf_repo)
            .await?;

    Ok(client)
}

#[async_trait::async_trait]
pub trait RepositoryBackend: std::fmt::Debug {
    /// Get a [RepositorySpec] for this [Repository]
    fn spec(&self) -> RepositorySpec;

    /// Fetch a metadata [Resource] from this repository.
    async fn fetch_metadata(&self, path: &str, range: ResourceRange) -> Result<Resource, Error>;

    /// Fetch a blob [Resource] from this repository.
    async fn fetch_blob(&self, path: &str, range: ResourceRange) -> Result<Resource, Error>;

    /// Whether or not the backend supports watching for file changes.
    fn supports_watch(&self) -> bool {
        false
    }

    /// Returns a stream which sends a unit value every time the given path is modified.
    fn watch(&self) -> anyhow::Result<BoxStream<'static, ()>> {
        Err(anyhow::anyhow!("Watching not supported for this repo type"))
    }

    /// Get the modification time of a blob in this repository if available.
    async fn blob_modification_time(&self, path: &str) -> anyhow::Result<Option<SystemTime>>;

    /// Produces the backing TUF [RepositoryProvider] for this repository.
    fn get_tuf_repo(&self) -> Result<Box<dyn RepositoryProvider<Json>>, Error>;
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::test_utils::{
            make_pm_repository, make_readonly_empty_repository, repo_key, repo_private_key,
            PKG1_BIN_HASH, PKG1_HASH, PKG1_LIB_HASH, PKG2_HASH,
        },
        camino::Utf8Path,
        pretty_assertions::assert_eq,
        std::fs::create_dir_all,
        tuf::{
            database::Database, repo_builder::RepoBuilder, repository::FileSystemRepositoryBuilder,
        },
    };

    const ROOT_VERSION: u32 = 1;
    const REPO_NAME: &str = "fake-repo";

    fn get_modtime(path: Utf8PathBuf) -> u64 {
        std::fs::metadata(path)
            .unwrap()
            .modified()
            .unwrap()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap()
            .as_secs()
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_config() {
        let repo = make_readonly_empty_repository(REPO_NAME).await.unwrap();

        let server_url = "some-url:1234";

        assert_eq!(
            repo.get_config(server_url, None).await.unwrap(),
            RepositoryConfig {
                repo_url: Some(format!("fuchsia-pkg://{}", REPO_NAME)),
                root_keys: Some(vec![repo_key()]),
                root_version: Some(ROOT_VERSION),
                storage_type: None,
                mirrors: Some(vec![MirrorConfig {
                    mirror_url: Some(format!("http://{}", server_url)),
                    subscribe: Some(true),
                }]),
            },
        );

        assert_eq!(
            repo.get_config(server_url, Some(RepositoryStorageType::Persistent)).await.unwrap(),
            RepositoryConfig {
                repo_url: Some(format!("fuchsia-pkg://{}", REPO_NAME)),
                root_keys: Some(vec![repo_key()]),
                root_version: Some(ROOT_VERSION),
                storage_type: Some(RepositoryStorageType::Persistent),
                mirrors: Some(vec![MirrorConfig {
                    mirror_url: Some(format!("http://{}", server_url)),
                    subscribe: Some(true),
                }]),
            },
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_packages() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let repo = make_pm_repository(REPO_NAME, &dir).await;

        // Look up the timestamp for the meta.far for the modified setting.
        let pkg1_modified = get_modtime(dir.join("repository").join("blobs").join(PKG1_HASH));
        let pkg2_modified = get_modtime(dir.join("repository").join("blobs").join(PKG2_HASH));

        let mut packages = repo.list_packages(ListFields::empty()).await.unwrap();

        // list_packages returns the contents out of order. Sort the entries so they are consistent.
        packages.sort_unstable_by(|lhs, rhs| lhs.name.cmp(&rhs.name));

        assert_eq!(
            packages,
            vec![
                RepositoryPackage {
                    name: Some("package1".into()),
                    hash: Some(PKG1_HASH.into()),
                    size: Some(24603),
                    modified: Some(pkg1_modified),
                    entries: None,
                    ..RepositoryPackage::EMPTY
                },
                RepositoryPackage {
                    name: Some("package2".into()),
                    hash: Some(PKG2_HASH.into()),
                    size: Some(24603),
                    modified: Some(pkg2_modified),
                    entries: None,
                    ..RepositoryPackage::EMPTY
                },
            ],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_packages_with_components() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let repo = make_pm_repository(REPO_NAME, &dir).await;

        // Look up the timestamp for the meta.far for the modified setting.
        let pkg1_modified = get_modtime(dir.join("repository").join("blobs").join(PKG1_HASH));
        let pkg2_modified = get_modtime(dir.join("repository").join("blobs").join(PKG2_HASH));

        let mut packages = repo.list_packages(ListFields::COMPONENTS).await.unwrap();

        // list_packages returns the contents out of order. Sort the entries so they are consistent.
        packages.sort_unstable_by(|lhs, rhs| lhs.name.cmp(&rhs.name));

        assert_eq!(
            packages,
            vec![
                RepositoryPackage {
                    name: Some("package1".into()),
                    hash: Some(PKG1_HASH.into()),
                    size: Some(24603),
                    modified: Some(pkg1_modified),
                    entries: Some(vec![
                        PackageEntry {
                            path: Some("meta/package1.cm".into()),
                            hash: None,
                            size: Some(11),
                            modified: Some(pkg1_modified),
                            ..PackageEntry::EMPTY
                        },
                        PackageEntry {
                            path: Some("meta/package1.cmx".into()),
                            hash: None,
                            size: Some(12),
                            modified: Some(pkg1_modified),
                            ..PackageEntry::EMPTY
                        },
                    ]),
                    ..RepositoryPackage::EMPTY
                },
                RepositoryPackage {
                    name: Some("package2".into()),
                    hash: Some(PKG2_HASH.into()),
                    size: Some(24603),
                    modified: Some(pkg2_modified),
                    entries: Some(vec![
                        PackageEntry {
                            path: Some("meta/package2.cm".into()),
                            hash: None,
                            size: Some(11),
                            modified: Some(pkg2_modified),
                            ..PackageEntry::EMPTY
                        },
                        PackageEntry {
                            path: Some("meta/package2.cmx".into()),
                            hash: None,
                            size: Some(12),
                            modified: Some(pkg2_modified),
                            ..PackageEntry::EMPTY
                        },
                    ]),
                    ..RepositoryPackage::EMPTY
                },
            ],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_tuf_client() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();
        let metadata_dir = dir.join("repository");
        create_dir_all(&metadata_dir).unwrap();

        let mut repo = FileSystemRepositoryBuilder::<Json>::new(metadata_dir.clone())
            .targets_prefix("targets")
            .build()
            .unwrap();

        let key = repo_private_key();
        let metadata = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&key])
            .trusted_targets_keys(&[&key])
            .trusted_snapshot_keys(&[&key])
            .trusted_timestamp_keys(&[&key])
            .commit()
            .await
            .unwrap();

        let database = Database::from_trusted_metadata(&metadata).unwrap();

        RepoBuilder::from_database(&mut repo, &database)
            .trusted_root_keys(&[&key])
            .trusted_targets_keys(&[&key])
            .trusted_snapshot_keys(&[&key])
            .trusted_timestamp_keys(&[&key])
            .stage_root()
            .unwrap()
            .commit()
            .await
            .unwrap();

        let backend = PmRepository::new(dir.to_path_buf());
        assert!(Repository::new("name", Box::new(backend)).await.is_ok());

        std::fs::remove_file(dir.join("repository").join("1.root.json")).unwrap();
        let backend = PmRepository::new(dir.to_path_buf());
        assert!(Repository::new("name", Box::new(backend)).await.is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_package() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let repo = make_pm_repository(REPO_NAME, &dir).await;

        // Look up the timestamps for the blobs.
        let blob_dir = dir.join("repository").join("blobs");
        let meta_far_modified = get_modtime(blob_dir.join(PKG1_HASH));

        let bin_modified = get_modtime(blob_dir.join(PKG1_BIN_HASH));
        let lib_modified = get_modtime(blob_dir.join(PKG1_LIB_HASH));

        let mut entries = repo.show_package("package1".into()).await.unwrap().unwrap();

        // show_packages returns contents out of order. Sort the entries so they are consistent.
        entries.sort_unstable_by(|lhs, rhs| lhs.path.cmp(&rhs.path));

        assert_eq!(
            entries,
            vec![
                PackageEntry {
                    path: Some("bin/package1".into()),
                    hash: Some(PKG1_BIN_HASH.into()),
                    size: Some(15),
                    modified: Some(bin_modified),
                    ..PackageEntry::EMPTY
                },
                PackageEntry {
                    path: Some("lib/package1".into()),
                    hash: Some(PKG1_LIB_HASH.into()),
                    size: Some(12),
                    modified: Some(lib_modified),
                    ..PackageEntry::EMPTY
                },
                PackageEntry {
                    path: Some("meta.far".into()),
                    hash: Some(PKG1_HASH.into()),
                    size: Some(24576),
                    modified: Some(meta_far_modified),
                    ..PackageEntry::EMPTY
                },
                PackageEntry {
                    path: Some("meta/contents".into()),
                    hash: None,
                    size: Some(156),
                    modified: Some(meta_far_modified),
                    ..PackageEntry::EMPTY
                },
                PackageEntry {
                    path: Some("meta/fuchsia.abi/abi-revision".into()),
                    hash: None,
                    size: Some(8),
                    modified: Some(meta_far_modified),
                    ..PackageEntry::EMPTY
                },
                PackageEntry {
                    path: Some("meta/package".into()),
                    hash: None,
                    size: Some(33),
                    modified: Some(meta_far_modified),
                    ..PackageEntry::EMPTY
                },
                PackageEntry {
                    path: Some("meta/package1.cm".into()),
                    hash: None,
                    size: Some(11),
                    modified: Some(meta_far_modified),
                    ..PackageEntry::EMPTY
                },
                PackageEntry {
                    path: Some("meta/package1.cmx".into()),
                    hash: None,
                    size: Some(12),
                    modified: Some(meta_far_modified),
                    ..PackageEntry::EMPTY
                },
            ]
        );
    }
}
