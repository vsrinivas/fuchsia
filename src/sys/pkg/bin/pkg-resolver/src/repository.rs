// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        cache::MerkleForError, clock, error, inspect_util,
        metrics_util::tuf_error_as_create_tuf_client_event_code, TCP_KEEPALIVE_TIMEOUT,
    },
    anyhow::{anyhow, format_err, Context as _},
    cobalt_sw_delivery_registry as metrics,
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_metrics::MetricEvent,
    fidl_fuchsia_pkg::LocalMirrorProxy,
    fidl_fuchsia_pkg_ext::{
        BlobId, MirrorConfig, RepositoryConfig, RepositoryKey, RepositoryStorageType,
    },
    fuchsia_async::TimeoutExt as _,
    fuchsia_cobalt_builders::MetricEventExt as _,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{future::TryFutureExt as _, lock::Mutex as AsyncMutex},
    serde::{Deserialize, Serialize},
    std::{sync::Arc, time::Duration},
    tuf::{
        client::Config,
        crypto::PublicKey,
        error::Error as TufError,
        metadata::{MetadataVersion, TargetPath},
        pouf::Pouf1,
        repository::{
            EphemeralRepository, HttpRepositoryBuilder, RepositoryProvider,
            RepositoryStorageProvider,
        },
    },
};

mod updating_tuf_client;
use updating_tuf_client::UpdateResult;

mod local_provider;
use local_provider::LocalMirrorRepositoryProvider;

mod filesystem_repository;
use filesystem_repository::{FuchsiaFileSystemRepository, RWRepository};

#[derive(Debug, Serialize, Deserialize)]
pub struct CustomTargetMetadata {
    merkle: BlobId,
    size: u64,
}

impl CustomTargetMetadata {
    pub fn merkle(&self) -> BlobId {
        self.merkle
    }
    pub fn size(&self) -> u64 {
        self.size
    }
}

#[derive(Debug)]
struct LogContext {
    repo_url: String,
}

pub struct Repository {
    log_ctx: LogContext,
    updating_client: Arc<AsyncMutex<updating_tuf_client::UpdatingTufClient>>,
    inspect: RepositoryInspectState,
}

struct RepositoryInspectState {
    /// Time that this repository was last used to lookup target metadata, or None if no targets
    /// have been resolved throught this repository.
    last_merkle_successfully_resolved_time: inspect::StringProperty,

    /// Count of the number of merkle roots resolved through this repository.
    merkles_successfully_resolved_count: inspect_util::Counter,

    _node: inspect::Node,
}

impl Repository {
    pub async fn new(
        data_proxy: Option<fio::DirectoryProxy>,
        persisted_repos_dir: Option<&str>,
        config: &RepositoryConfig,
        mut cobalt_sender: ProtocolSender<MetricEvent>,
        node: inspect::Node,
        local_mirror: Option<LocalMirrorProxy>,
        tuf_metadata_timeout: Duration,
    ) -> Result<Self, anyhow::Error> {
        let mirror_config = config.mirrors().get(0);
        let local = get_local_repo(data_proxy, persisted_repos_dir, config).await?;
        let local = RWRepository::new(local);
        let remote = get_remote_repo(config, mirror_config, local_mirror)?;
        let root_keys = get_root_keys(config)?;

        let updating_client =
            updating_tuf_client::UpdatingTufClient::from_tuf_client_and_mirror_config(
                tuf::client::Client::with_trusted_root_keys(
                    Config::default(),
                    MetadataVersion::Number(config.root_version()),
                    config.root_threshold(),
                    &root_keys,
                    local,
                    remote,
                )
                .map_err(error::TufOrTimeout::Tuf)
                .on_timeout(tuf_metadata_timeout, || Err(error::TufOrTimeout::Timeout))
                .await
                .map_err(|e| {
                    cobalt_sender.send(
                        MetricEvent::builder(metrics::CREATE_TUF_CLIENT_MIGRATED_METRIC_ID)
                            .with_event_codes(tuf_error_as_create_tuf_client_event_code(&e))
                            .as_occurrence(1),
                    );
                    anyhow!(e).context("creating rust-tuf client")
                })?,
                mirror_config,
                tuf_metadata_timeout,
                node.create_child("updating_tuf_client"),
                cobalt_sender.clone(),
            );

        cobalt_sender.send(
            MetricEvent::builder(metrics::CREATE_TUF_CLIENT_MIGRATED_METRIC_ID)
                .with_event_codes(metrics::CreateTufClientMigratedMetricDimensionResult::Success)
                .as_occurrence(1),
        );

        // We no longer need to read from the local repository after we've created the client.
        // Switch the local repository into write-only mode.
        updating_client.lock().await.switch_local_repo_to_write_only_mode();

        Ok(Self {
            log_ctx: LogContext { repo_url: config.repo_url().to_string() },
            updating_client,
            inspect: RepositoryInspectState {
                last_merkle_successfully_resolved_time: node.create_string(
                    "last_merkle_successfully_resolved_time",
                    &format!("{:?}", Option::<zx::Time>::None),
                ),
                merkles_successfully_resolved_count: inspect_util::Counter::new(
                    &node,
                    "merkles_successfully_resolved_count",
                ),
                _node: node,
            },
        })
    }

    pub async fn get_merkle_at_path(
        &mut self,
        target_path: &TargetPath,
    ) -> Result<CustomTargetMetadata, MerkleForError> {
        let mut updating_client = self.updating_client.lock().await;
        match updating_client.update_if_stale().await {
            // These are the common cases and can be inferred from AutoClient inspect.
            Ok(UpdateResult::Deferred) | Ok(UpdateResult::UpToDate) => (),
            Ok(UpdateResult::Updated) => fx_log_info!(
                "updated local TUF metadata for {:?} to version {:?} while getting merkle for {:?}",
                self.log_ctx.repo_url,
                updating_client.metadata_versions(),
                target_path
            ),
            Err(error::TufOrTimeout::Tuf(TufError::MetadataNotFound { path, version })) => {
                return Err(MerkleForError::MetadataNotFound { path, version })
            }
            Err(error::TufOrTimeout::Tuf(TufError::TargetNotFound(path))) => {
                return Err(MerkleForError::TargetNotFound(path))
            }
            Err(other) => {
                fx_log_err!(
                    "failed to update local TUF metadata for {:?} while getting merkle for {:?} with error: {:#}",
                    self.log_ctx.repo_url,
                    target_path,
                    anyhow!(other)
                );
                // TODO(fxbug.dev/43646) Should this bubble up a MerkleForError::TufError(other)?
            }
        }

        let description =
            updating_client.fetch_target_description(&target_path).await.map_err(|e| match e {
                TufError::MetadataNotFound { path, version } => {
                    MerkleForError::MetadataNotFound { path, version }
                }
                TufError::TargetNotFound(path) => MerkleForError::TargetNotFound(path),
                other => MerkleForError::FetchTargetDescription(target_path.as_str().into(), other),
            })?;

        let custom = description.custom().to_owned();
        let custom = serde_json::Value::from(custom.into_iter().collect::<serde_json::Map<_, _>>());
        let mut custom: CustomTargetMetadata =
            serde_json::from_value(custom).map_err(MerkleForError::SerdeError)?;
        custom.size = description.length();

        self.inspect
            .last_merkle_successfully_resolved_time
            .set(&format!("{:?}", Some(clock::now())));
        self.inspect.merkles_successfully_resolved_count.increment();

        Ok(custom)
    }
}

async fn get_local_repo(
    data_proxy: Option<fio::DirectoryProxy>,
    persisted_repos_dir: Option<&str>,
    config: &RepositoryConfig,
) -> Result<Box<dyn RepositoryStorageProvider<Pouf1> + Sync + Send>, anyhow::Error> {
    match config.repo_storage_type() {
        RepositoryStorageType::Ephemeral => {
            let local = EphemeralRepository::new();
            Ok(Box::new(local))
        }
        RepositoryStorageType::Persistent => {
            // This can only be true when config_repos.json was present, parsed, and contained a
            // non-empty string value for the persistence directory. Therefore, even in cases where
            // `enable_dynamic_configuration` is set, a `RST::Persistent` repo will still yield the
            // error in the else case below.
            let persisted_repos_dir = if let Some(persisted_repos_dir) = persisted_repos_dir {
                persisted_repos_dir
            } else {
                return Err(format_err!(
                    "Support for persistent repositories is disabled, cannot create repo with persistent storage"
                ));
            };

            let data_proxy = if let Some(data_proxy) = data_proxy {
                data_proxy
            } else {
                return Err(format_err!(
                    "/data proxy is not available, cannot create repo with persistent storage"
                ));
            };

            let repos_proxy = fuchsia_fs::directory::open_directory(
                &data_proxy,
                persisted_repos_dir,
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::CREATE,
            )
            .await
            .with_context(|| format!("opening {}", persisted_repos_dir))?;
            let host = config.repo_url().host();
            let proxy = fuchsia_fs::directory::open_directory(
                &repos_proxy,
                host,
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::CREATE,
            )
            .await
            .with_context(|| format!("opening {}", host))?;
            let local = FuchsiaFileSystemRepository::new(proxy);
            Ok(Box::new(local))
        }
    }
}

fn get_remote_repo(
    config: &RepositoryConfig,
    mirror_config: Option<&MirrorConfig>,
    local_mirror: Option<LocalMirrorProxy>,
) -> Result<Box<dyn RepositoryProvider<Pouf1> + Send>, anyhow::Error> {
    if config.use_local_mirror() && mirror_config.is_some() {
        return Err(format_err!("Cannot have a local mirror and remote mirrors!"));
    }

    let remote: Box<dyn RepositoryProvider<Pouf1> + Send> =
        match (local_mirror, config.use_local_mirror(), mirror_config.as_ref()) {
            (Some(local_mirror), true, _) => Box::new(LocalMirrorRepositoryProvider::new(
                local_mirror,
                config.repo_url().clone(),
            )),
            (_, false, Some(mirror_config)) => {
                let remote_url = mirror_config.mirror_url().to_owned();
                Box::new(
                    HttpRepositoryBuilder::new_with_uri(
                        remote_url,
                        fuchsia_hyper::new_https_client_from_tcp_options(
                            fuchsia_hyper::TcpOptions::keepalive_timeout(TCP_KEEPALIVE_TIMEOUT),
                        ),
                    )
                    .build(),
                )
            }
            (local_mirror, _, _) => {
                return Err(format_err!(
            "Repo config has invalid mirror configuration: config={:?}, use_local_mirror={}",
            config,
            local_mirror.is_some()
        ))
            }
        };

    Ok(remote)
}

fn get_root_keys(config: &RepositoryConfig) -> Result<Vec<PublicKey>, anyhow::Error> {
    let mut root_keys = vec![];

    // FIXME(42863) we used keyid_hash_algorithms in order to verify compatibility with the
    // TUF-1.0 spec against python-tuf. python-tuf is thinking about removing
    // keyid_hash_algorithms, so there's no real reason for us to use them anymore. In order to
    // do this in a forward-compatible way, we need to create 2 `tuf::PublicKey` keys, one with
    // a keyid_hash_algorithms specified, and one without. This will let us migrate the
    // metadata without needing to modify the resolver. Once everyone has migrated over, we can
    // remove our use of `PublicKey::from_ed25519_with_keyid_hash_algorithms`.
    for key in config.root_keys().iter() {
        match key {
            RepositoryKey::Ed25519(bytes) => {
                root_keys.push(PublicKey::from_ed25519(bytes.clone())?);
                root_keys.push(PublicKey::from_ed25519_with_keyid_hash_algorithms(
                    bytes.clone(),
                    Some(vec!["sha256".to_string()]),
                )?);
                root_keys.push(PublicKey::from_ed25519_with_keyid_hash_algorithms(
                    bytes.clone(),
                    Some(vec!["sha256".to_string(), "sha512".to_string()]),
                )?);
            }
        }
    }

    Ok(root_keys)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::DEFAULT_TUF_METADATA_TIMEOUT,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        fuchsia_pkg_testing::{
            serve::{responder, HttpResponder, ServedRepository, ServedRepositoryBuilder},
            Package, PackageBuilder, Repository as TestRepository, RepositoryBuilder,
        },
        fuchsia_url::RepositoryUrl,
        futures::{channel::mpsc, stream::StreamExt},
        http_sse::Event,
        std::{
            path::{Path, PathBuf},
            sync::Arc,
        },
        tuf::metadata::MetadataPath,
        updating_tuf_client::METADATA_CACHE_STALE_TIMEOUT,
    };

    const TEST_REPO_URL: &str = "fuchsia-pkg://test";
    const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";

    struct TestEnvBuilder<'a> {
        server_repo_builder: RepositoryBuilder<'a>,
        persisted_repos: bool,
    }

    impl<'a> TestEnvBuilder<'a> {
        fn add_package(mut self, pkg: &'a Package) -> Self {
            self.server_repo_builder = self.server_repo_builder.add_package(pkg);
            self
        }

        fn enable_persisted_repos(mut self) -> Self {
            self.persisted_repos = true;
            self
        }

        async fn build(self) -> TestEnv {
            let data_dir = tempfile::tempdir().unwrap();
            let persisted_repos_dir =
                if self.persisted_repos { Some("repos".to_string()) } else { None };
            let repo = self.server_repo_builder.build().await.expect("created repo");

            TestEnv { persisted_repos_dir, repo: Arc::new(repo), data_dir }
        }
    }

    struct TestEnv {
        repo: Arc<TestRepository>,
        persisted_repos_dir: Option<String>,
        data_dir: tempfile::TempDir,
    }

    impl TestEnv {
        fn builder<'a>() -> TestEnvBuilder<'a> {
            TestEnvBuilder {
                server_repo_builder: RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH),
                persisted_repos: false,
            }
        }

        fn persisted_repos_dir(&self) -> Option<PathBuf> {
            self.persisted_repos_dir.as_ref().map(|p| self.data_dir.path().join(p))
        }

        async fn new() -> Self {
            Self::builder().build().await
        }

        fn serve_repo(&self) -> ServerBuilder {
            ServerBuilder { builder: Arc::clone(&self.repo).server(), subscribe: false }
        }

        async fn repo(&self, config: &RepositoryConfig) -> Result<Repository, anyhow::Error> {
            let (sender, _) = futures::channel::mpsc::channel(0);
            let cobalt_sender = ProtocolSender::new(sender);
            let proxy = fuchsia_fs::directory::open_in_namespace(
                self.data_dir.path().to_str().unwrap(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .unwrap();
            Repository::new(
                Some(proxy),
                self.persisted_repos_dir.as_ref().map(|s| s.as_str()),
                config,
                cobalt_sender,
                inspect::Inspector::new().root().create_child("inner-node"),
                None,
                DEFAULT_TUF_METADATA_TIMEOUT,
            )
            .await
        }
    }

    struct ServerBuilder {
        builder: ServedRepositoryBuilder,
        subscribe: bool,
    }

    impl ServerBuilder {
        fn subscribe(mut self) -> Self {
            self.subscribe = true;
            self
        }

        fn response_overrider(mut self, responder: impl HttpResponder) -> Self {
            self.builder = self.builder.response_overrider(responder);
            self
        }

        fn start(self, repo_url: &str) -> (ServedRepository, RepositoryConfig) {
            let served_repository = self.builder.start().expect("create served repo");
            let repo_url = RepositoryUrl::parse(repo_url).expect("created repo url");

            let repo_config = if self.subscribe {
                served_repository.make_repo_config_with_subscribe(repo_url)
            } else {
                served_repository.make_repo_config(repo_url)
            };

            (served_repository, repo_config)
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_log_ctx_correctly_set() {
        // Serve static repo and connect to it
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let env = TestEnv::builder().add_package(&pkg).build().await;

        let (_served_repository, repo_config) = env.serve_repo().start(TEST_REPO_URL);
        let repo = env.repo(&repo_config).await.expect("created opened repo");

        assert_matches!(repo.log_ctx, LogContext { repo_url } if repo_url == TEST_REPO_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_merkle_at_path() {
        // Serve static repo and connect to it
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let env = TestEnv::builder().add_package(&pkg).build().await;

        let (_served_repository, repo_config) = env.serve_repo().start(TEST_REPO_URL);
        let mut repo = env.repo(&repo_config).await.expect("created opened repo");

        let target_path =
            TargetPath::new("just-meta-far/0".to_string()).expect("created target path");

        // Obtain merkle root and meta far size
        let CustomTargetMetadata { merkle, size } =
            repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");

        // Verify what we got from tuf was correct
        assert_eq!(merkle.as_bytes(), pkg.meta_far_merkle_root().as_bytes());
        assert_eq!(size, pkg.meta_far().unwrap().metadata().unwrap().len());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_merkle_at_path_fails_when_no_package() {
        let env = TestEnv::new().await;
        let (_served_repository, repo_config) = env.serve_repo().start(TEST_REPO_URL);
        let mut repo = env.repo(&repo_config).await.expect("created opened repo");

        let target_path =
            TargetPath::new("path_that_doesnt_exist/0".to_string()).expect("created target path");

        // We still updated, but didn't fetch any packages
        assert_matches!(
            repo.get_merkle_at_path(&target_path).await,
            Err(MerkleForError::TargetNotFound(path))
            if path == target_path
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_merkle_at_path_fails_when_remote_repo_down() {
        // Serve static repo
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let env = TestEnv::builder().add_package(&pkg).build().await;
        let should_fail = responder::AtomicToggle::new(false);
        let (_served_repository, repo_config) = env
            .serve_repo()
            .response_overrider(responder::Toggleable::new(
                &should_fail,
                responder::StaticResponseCode::not_found(),
            ))
            .start(TEST_REPO_URL);
        let mut repo = env.repo(&repo_config).await.expect("created opened repo");
        let target_path =
            TargetPath::new("just-meta-far/0".to_string()).expect("created target path");

        // When the server is blocked, we should fail at get_merkle_at_path
        // TODO(fxbug.dev/39651) if the Repository can't connect to the remote server AND
        // we've updated our local repo recently, then it should return the merkle that is stored locally
        should_fail.set();
        assert_matches!(
            repo.get_merkle_at_path(&target_path).await,
            Err(MerkleForError::MetadataNotFound {
                path: metadata_path,
                version: MetadataVersion::None,
            })
            if metadata_path == MetadataPath::timestamp()
        );

        // When the server is unblocked, we should succeed again
        should_fail.unset();
        let CustomTargetMetadata { merkle, size } =
            repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");
        assert_eq!(merkle.as_bytes(), pkg.meta_far_merkle_root().as_bytes());
        assert_eq!(size, pkg.meta_far().unwrap().metadata().unwrap().len());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_merkle_path_fails_and_logs_when_remote_server_500s() {
        // Serve static repo
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let env = TestEnv::builder().add_package(&pkg).build().await;
        let should_fail = responder::AtomicToggle::new(false);
        let (_served_repository, repo_config) = env
            .serve_repo()
            .response_overrider(responder::Toggleable::new(
                &should_fail,
                responder::StaticResponseCode::server_error(),
            ))
            .start(TEST_REPO_URL);
        let mut repo = env.repo(&repo_config).await.expect("created opened repo");
        let target_path =
            TargetPath::new("just-meta-far/0".to_owned()).expect("created target path");

        // When the server is blocked, we should fail at get_merkle_at_path.
        // Since the error was unexpected, we should see an error in the log.
        should_fail.set();
        assert_matches!(
            repo.get_merkle_at_path(&target_path).await,
            Err(MerkleForError::MetadataNotFound {
                path: metadata_path,
                version: MetadataVersion::None,
            })
            if metadata_path == MetadataPath::snapshot()
        );
    }

    async fn make_repo_with_auto_and_watched_timestamp_metadata(
    ) -> (TestEnv, ServedRepository, mpsc::UnboundedReceiver<()>, Repository) {
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let env = TestEnv::builder().add_package(&pkg).build().await;
        let (notify_on_request_responder, notified) = responder::NotifyWhenRequested::new();
        let (served_repository, repo_config) = env
            .serve_repo()
            .response_overrider(responder::ForPath::new(
                "/timestamp.json",
                notify_on_request_responder,
            ))
            .subscribe()
            .start(TEST_REPO_URL);
        let repo = env.repo(&repo_config).await.expect("created opened repo");
        served_repository.wait_for_n_connected_auto_clients(1).await;
        (env, served_repository, notified, repo)
    }

    #[fasync::run_singlethreaded(test)]
    async fn update_subscribed_repo_on_auto_event() {
        let (_env, served_repository, mut ts_metadata_fetched, _repo) =
            make_repo_with_auto_and_watched_timestamp_metadata().await;

        served_repository
            .send_auto_event(&Event::from_type_and_data("timestamp.json", "dummy-data").unwrap())
            .await;

        // Will hang if auto event does not trigger tuf repo update
        ts_metadata_fetched.next().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn only_update_subscribed_repo_if_stale() {
        let initial_time = zx::Time::from_nanos(0);
        clock::mock::set(initial_time);
        let (_env, served_repository, mut ts_metadata_fetched, mut repo) =
            make_repo_with_auto_and_watched_timestamp_metadata().await;

        served_repository
            .send_auto_event(&Event::from_type_and_data("timestamp.json", "dummy-data").unwrap())
            .await;
        ts_metadata_fetched.next().await; // wait for AutoClient to start the update
        repo.updating_client.lock().await; // wait for update to finish

        // cache will not be stale, so this should not trigger an update
        repo.get_merkle_at_path(&TargetPath::new("just-meta-far/0".to_string()).unwrap())
            .await
            .unwrap();

        // cache will now be stale, should trigger an update
        clock::mock::set(initial_time + METADATA_CACHE_STALE_TIMEOUT);
        repo.get_merkle_at_path(&TargetPath::new("just-meta-far/0".to_string()).unwrap())
            .await
            .unwrap();

        // the two calls to get_merkle_at_path should only have caused /timestamp.json
        // to be fetched once
        assert_eq!(ts_metadata_fetched.next().await, Some(()));
        assert_matches!(ts_metadata_fetched.try_next(), Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_client_reconnects() {
        let (_env, served_repository, _ts_metadata_fetched, _repo) =
            make_repo_with_auto_and_watched_timestamp_metadata().await;

        served_repository.drop_all_auto_clients().await;

        // Will hang if auto client never reconnects
        served_repository.wait_for_n_connected_auto_clients(1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn persisted_repos() {
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let env = TestEnv::builder().add_package(&pkg).enable_persisted_repos().build().await;

        let (served_repository, _repo_config) = env.serve_repo().start(TEST_REPO_URL);

        let repo_url = "fuchsia-pkg://test".parse().unwrap();
        let repo_config = env
            .repo
            .make_repo_config_builder(repo_url)
            .add_mirror(served_repository.get_mirror_config_builder().build())
            .repo_storage_type(RepositoryStorageType::Persistent)
            .build();

        let mut repo = env.repo(&repo_config).await.expect("created opened repo");

        let target_path =
            TargetPath::new("just-meta-far/0".to_string()).expect("created target path");

        // Obtain merkle root and meta far size
        let CustomTargetMetadata { merkle, size } =
            repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");

        // Verify what we got from tuf was correct
        assert_eq!(merkle.as_bytes(), pkg.meta_far_merkle_root().as_bytes());
        assert_eq!(size, pkg.meta_far().unwrap().metadata().unwrap().len());

        // Make sure the metadata was persisted to disk
        let dir = env.persisted_repos_dir().unwrap().join("test").join("metadata");

        assert!(dir.join("1.root.json").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolve_caches_metadata() {
        clock::mock::set(zx::Time::from_nanos(0));

        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let env = TestEnv::builder().add_package(&pkg).build().await;

        let target_path =
            TargetPath::new("just-meta-far/0".to_string()).expect("created target path");

        let (responder, history) = responder::Record::new();
        let (_served_repository, repo_config) =
            env.serve_repo().response_overrider(responder).start(TEST_REPO_URL);
        let mut repo = env.repo(&repo_config).await.expect("created opened repo");

        // Resolve the package, which should succeed.
        assert_matches!(repo.get_merkle_at_path(&target_path).await, Ok(_));

        let entries = history.take();
        let uri_paths = entries.iter().map(|e| e.uri_path().to_str().unwrap()).collect::<Vec<_>>();
        assert_eq!(
            uri_paths,
            vec![
                "/1.root.json",
                "/2.root.json",
                "/timestamp.json",
                "/2.snapshot.json",
                "/2.targets.json",
            ],
        );

        // Advance time right before the timeout, and make sure we don't access the server.
        clock::mock::set(
            zx::Time::from_nanos(0) + METADATA_CACHE_STALE_TIMEOUT - zx::Duration::from_seconds(1),
        );
        assert_matches!(repo.get_merkle_at_path(&target_path).await, Ok(_));

        let entries = history.take();
        assert!(entries.is_empty(), "{:#?}", entries);

        // Advance time right after the timeout, and make sure we access the server.
        clock::mock::set(
            zx::Time::from_nanos(0) + METADATA_CACHE_STALE_TIMEOUT + zx::Duration::from_seconds(1),
        );
        assert_matches!(repo.get_merkle_at_path(&target_path).await, Ok(_));

        let entries = history.take();
        assert_eq!(entries.len(), 2, "{:#?}", entries);
        assert_eq!(entries[0].uri_path(), Path::new("/2.root.json"), "{:#?}", entries);
        assert_eq!(entries[1].uri_path(), Path::new("/timestamp.json"), "{:#?}", entries);
    }
}

#[cfg(test)]
mod inspect_tests {
    use {
        super::*,
        crate::DEFAULT_TUF_METADATA_TIMEOUT,
        fuchsia_async as fasync,
        fuchsia_inspect::assert_data_tree,
        fuchsia_pkg_testing::{serve::responder, PackageBuilder, RepositoryBuilder},
        fuchsia_url::RepositoryUrl,
        futures::stream::StreamExt,
        http_sse::Event,
        std::sync::Arc,
    };

    const TEST_REPO_URL: &str = "fuchsia-pkg://test";
    const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";

    fn dummy_sender() -> ProtocolSender<MetricEvent> {
        let (sender, _) = futures::channel::mpsc::channel(0);
        ProtocolSender::new(sender)
    }

    #[fasync::run_singlethreaded(test)]
    async fn initialization_and_destruction() {
        let inspector = inspect::Inspector::new();
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .build()
                .await
                .expect("created repo"),
        );
        let served_repository = repo.server().start().expect("create served repo");
        let repo_url = RepositoryUrl::parse(TEST_REPO_URL).expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);

        let repo = Repository::new(
            None,
            None,
            &repo_config,
            dummy_sender(),
            inspector.root().create_child("repo-node"),
            None,
            DEFAULT_TUF_METADATA_TIMEOUT,
        )
        .await
        .expect("created Repository");
        assert_data_tree!(
            inspector,
            root: {
                "repo-node": {
                    merkles_successfully_resolved_count: 0u64,
                    last_merkle_successfully_resolved_time: "None",
                    "updating_tuf_client": {
                        update_check_success_count: 0u64,
                        update_check_failure_count: 0u64,
                        last_update_successfully_checked_time: "None",
                        updated_count: 0u64,
                        root_version: 1u64,
                        timestamp_version: -1i64,
                        snapshot_version: -1i64,
                        targets_version: -1i64,
                    }
                }
            }
        );

        drop(repo);
        assert_data_tree!(
            inspector,
            root: {}
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_merkle_at_path_updates_inspect() {
        clock::mock::set(zx::Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(&pkg)
                .build()
                .await
                .expect("created repo"),
        );
        let served_repository = repo.server().start().expect("create served repo");
        let repo_url = RepositoryUrl::parse(TEST_REPO_URL).expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);

        let mut repo = Repository::new(
            None,
            None,
            &repo_config,
            dummy_sender(),
            inspector.root().create_child("repo-node"),
            None,
            DEFAULT_TUF_METADATA_TIMEOUT,
        )
        .await
        .expect("created Repository");
        let target_path = tuf::metadata::TargetPath::new("just-meta-far/0".to_string())
            .expect("created target path");

        repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");

        assert_data_tree!(
            inspector,
            root: {
                "repo-node": {
                    merkles_successfully_resolved_count: 1u64,
                    last_merkle_successfully_resolved_time: "Some(Time(0))",
                    "updating_tuf_client": {
                        update_check_success_count: 1u64,
                        update_check_failure_count: 0u64,
                        last_update_successfully_checked_time: "Some(Time(0))",
                        updated_count: 1u64,
                        root_version: 1u64,
                        timestamp_version: 2i64,
                        snapshot_version: 2i64,
                        targets_version: 2i64,
                    }
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn subscribed_repo_after_event() {
        clock::mock::set(zx::Time::from_nanos(0));
        let inspector = inspect::Inspector::new();
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(&pkg)
                .build()
                .await
                .expect("created repo"),
        );
        let (notify_on_request_responder, mut notified) = responder::NotifyWhenRequested::new();
        let served_repository = repo
            .server()
            .response_overrider(responder::ForPath::new(
                "/timestamp.json",
                notify_on_request_responder,
            ))
            .start()
            .expect("create served repo");
        let repo_url = RepositoryUrl::parse(TEST_REPO_URL).expect("created repo url");
        let repo_config = served_repository.make_repo_config_with_subscribe(repo_url);

        let repo = Repository::new(
            None,
            None,
            &repo_config,
            dummy_sender(),
            inspector.root().create_child("repo-node"),
            None,
            DEFAULT_TUF_METADATA_TIMEOUT,
        )
        .await
        .expect("created opened repo");
        served_repository.wait_for_n_connected_auto_clients(1).await;

        served_repository
            .send_auto_event(&Event::from_type_and_data("timestamp.json", "dummy-data").unwrap())
            .await;

        // Will hang if auto event does not trigger tuf repo update
        notified.next().await;

        // Wait for AutoClient to finish updating and release the UpdatingTufClient
        repo.updating_client.lock().await;

        assert_data_tree!(
            inspector,
            root: {
                "repo-node": {
                    merkles_successfully_resolved_count: 0u64,
                    last_merkle_successfully_resolved_time: "None",
                    "updating_tuf_client": {
                        update_check_success_count: 1u64,
                        update_check_failure_count: 0u64,
                        last_update_successfully_checked_time: "Some(Time(0))",
                        updated_count: 1u64,
                        root_version: 1u64,
                        timestamp_version: 2i64,
                        snapshot_version: 2i64,
                        targets_version: 2i64,
                        "auto_client" : {
                            connect_failure_count: 0u64,
                            connect_success_count: 1u64,
                            update_attempt_count: 1u64,
                        }
                    }
                }
            }
        );
    }
}
