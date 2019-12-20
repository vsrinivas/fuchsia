// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Temporarily allow deprecated functions while rust-tuf is updated
#![allow(deprecated)]

use {
    crate::{cache::MerkleForError, clock, inspect_util},
    failure::format_err,
    fidl_fuchsia_pkg_ext::{BlobId, MirrorConfig, RepositoryConfig, RepositoryKey},
    fuchsia_hyper::HyperConnector,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::lock::Mutex as AsyncMutex,
    hyper_rustls::HttpsConnector,
    serde_derive::{Deserialize, Serialize},
    std::sync::Arc,
    tuf::{
        client::Config,
        crypto::PublicKey,
        error::Error as TufError,
        interchange::Json,
        metadata::{MetadataVersion, TargetPath},
        repository::{EphemeralRepository, HttpRepository, HttpRepositoryBuilder},
    },
};

mod updating_tuf_client;

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

pub struct Repository {
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
        config: &RepositoryConfig,
        node: inspect::Node,
    ) -> Result<Self, failure::Error> {
        let local = EphemeralRepository::<Json>::new();
        let mirror_config = config
            .mirrors()
            .get(0)
            .ok_or_else(|| format_err!("Repo config has no mirrors: {:?}", config))?;
        let remote_url = mirror_config.mirror_url();
        let remote = HttpRepositoryBuilder::new_with_uri(
            remote_url.parse().map_err(|e| {
                format_err!("Unable to parse url {:?}, received error {:?}", remote_url, e)
            })?,
            fuchsia_hyper::new_https_client(),
        )
        .build();

        Self::new_from_local_and_remote(local, remote, config, mirror_config, node).await
    }

    async fn new_from_local_and_remote(
        local: EphemeralRepository<Json>,
        remote: HttpRepository<HttpsConnector<HyperConnector>, Json>,
        config: &RepositoryConfig,
        mirror_config: &MirrorConfig,
        node: inspect::Node,
    ) -> Result<Self, failure::Error> {
        let root_keys = config
            .root_keys()
            .iter()
            .map(|key| match key {
                RepositoryKey::Ed25519(bytes) => {
                    PublicKey::from_ed25519_with_keyid_hash_algorithms(
                        bytes.clone(),
                        Some(vec!["sha256".to_string()]),
                    )
                }
            })
            .collect::<Result<Vec<PublicKey>, _>>()?;
        Ok(Self {
            updating_client:
                updating_tuf_client::UpdatingTufClient::from_tuf_client_and_mirror_config(
                    tuf::client::Client::with_trusted_root_keys(
                        Config::default(),
                        &MetadataVersion::Number(config.root_version()),
                        config.root_threshold(),
                        &root_keys,
                        local,
                        remote,
                    )
                    .await
                    .map_err(|e| {
                        format_err!("Unable to create rust tuf client, received error {:?}", e)
                    })?,
                    mirror_config,
                    node.create_child("updating_tuf_client"),
                ),
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
        updating_client.update_if_stale().await.map_err(|e| match e {
            TufError::NotFound => MerkleForError::NotFound,
            other => {
                fx_log_err!("failed to update with TUF error {:?}", other);
                MerkleForError::TufError(other)
            }
        })?;

        let description =
            updating_client.fetch_target_description(&target_path).await.map_err(|e| match e {
                TufError::NotFound => MerkleForError::NotFound,
                other => {
                    fx_log_err!(
                        "failed to lookup merkle for {:?} with TUF error {:?}",
                        target_path,
                        other
                    );
                    MerkleForError::TufError(other)
                }
            })?;

        let custom = description.custom().ok_or(MerkleForError::NoCustomMetadata)?.to_owned();
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        fuchsia_pkg_testing::{
            serve::{
                handler,
                handler::{ForPath, NotifyWhenRequested},
                AtomicToggle, ServedRepository,
            },
            PackageBuilder, RepositoryBuilder,
        },
        fuchsia_url::pkg_url::RepoUrl,
        futures::{channel::mpsc, stream::StreamExt},
        http_sse::Event,
        matches::assert_matches,
        std::sync::Arc,
        updating_tuf_client::SUBSCRIBE_CACHE_STALE_TIMEOUT,
    };

    impl Repository {
        pub async fn new_no_inspect(config: &RepositoryConfig) -> Result<Self, failure::Error> {
            Repository::new(config, inspect::Inspector::new().root().create_child("inner-node"))
                .await
        }
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore] // TODO(42573) re-enable all these tests
    async fn test_get_merkle_at_path() {
        // Serve static repo and connect to it
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut repo = Repository::new_no_inspect(&repo_config).await.expect("created opened repo");
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
    #[ignore]
    async fn test_get_merkle_at_path_fails_when_no_package() {
        let repo = Arc::new(RepositoryBuilder::new().build().await.expect("created repo"));
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut repo = Repository::new_no_inspect(&repo_config).await.expect("created opened repo");
        let target_path =
            TargetPath::new("path_that_doesnt_exist/0".to_string()).expect("created target path");

        // We still updated, but didn't fetch any packages
        assert_matches!(repo.get_merkle_at_path(&target_path).await, Err(MerkleForError::NotFound));
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn test_get_merkle_at_path_fails_when_remote_repo_down() {
        // Serve static repo
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let should_fail = AtomicToggle::new(false);
        let served_repository = repo
            .build_server()
            .uri_path_override_handler(handler::Toggleable::new(
                &should_fail,
                handler::StaticResponseCode::not_found(),
            ))
            .start()
            .expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut repo = Repository::new_no_inspect(&repo_config).await.expect("created opened repo");
        let target_path =
            TargetPath::new("just-meta-far/0".to_string()).expect("created target path");

        // When the server is blocked, we should fail at get_merkle_at_path
        // TODO(fxb/39651) if the Repository can't connect to the remote server AND
        // we've updated our local repo recently, then it should return the merkle that is stored locally
        should_fail.set();
        assert_matches!(repo.get_merkle_at_path(&target_path).await, Err(MerkleForError::NotFound));

        // When the server is unblocked, we should succeed again
        should_fail.unset();
        let CustomTargetMetadata { merkle, size } =
            repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");
        assert_eq!(merkle.as_bytes(), pkg.meta_far_merkle_root().as_bytes());
        assert_eq!(size, pkg.meta_far().unwrap().metadata().unwrap().len());
    }

    async fn make_repo_with_auto_and_watched_timestamp_metadata(
    ) -> (ServedRepository, mpsc::UnboundedReceiver<()>, Repository) {
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let (notify_on_request_handler, notified) = NotifyWhenRequested::new();
        let served_repository = repo
            .build_server()
            .uri_path_override_handler(ForPath::new("/timestamp.json", notify_on_request_handler))
            .start()
            .expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config_with_subscribe(repo_url);
        let repo = Repository::new_no_inspect(&repo_config).await.expect("created opened repo");
        served_repository.wait_for_n_connected_auto_clients(1).await;
        (served_repository, notified, repo)
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn update_subscribed_repo_on_auto_event() {
        let (served_repository, mut ts_metadata_fetched, _repo) =
            make_repo_with_auto_and_watched_timestamp_metadata().await;

        served_repository
            .send_auto_event(&Event::from_type_and_data("timestamp.json", "dummy-data").unwrap())
            .await;

        // Will hang if auto event does not trigger tuf repo update
        ts_metadata_fetched.next().await;
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn only_update_subscribed_repo_if_stale() {
        let initial_time = zx::Time::from_nanos(0);
        clock::mock::set(initial_time);
        let (served_repository, mut ts_metadata_fetched, mut repo) =
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
        clock::mock::set(initial_time + SUBSCRIBE_CACHE_STALE_TIMEOUT);
        repo.get_merkle_at_path(&TargetPath::new("just-meta-far/0".to_string()).unwrap())
            .await
            .unwrap();

        // the two calls to get_merkle_at_path should only have caused /timestamp.json
        // to be fetched once
        assert_eq!(ts_metadata_fetched.next().await, Some(()));
        assert_matches!(ts_metadata_fetched.try_next(), Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn auto_client_reconnects() {
        let (served_repository, _ts_metadata_fetched, _repo) =
            make_repo_with_auto_and_watched_timestamp_metadata().await;

        served_repository.drop_all_auto_clients().await;

        // Will hang if auto client never reconnects
        served_repository.wait_for_n_connected_auto_clients(1).await;
    }
}

#[cfg(test)]
mod inspect_tests {
    use {
        super::*,
        fuchsia_async as fasync,
        fuchsia_inspect::assert_inspect_tree,
        fuchsia_pkg_testing::{
            serve::handler::{ForPath, NotifyWhenRequested},
            PackageBuilder, RepositoryBuilder,
        },
        fuchsia_url::pkg_url::RepoUrl,
        futures::stream::StreamExt,
        http_sse::Event,
        std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    // TODO(42445) figure out why these tests are flaking
    #[ignore]
    async fn initialization_and_destruction() {
        let inspector = inspect::Inspector::new();
        let repo = Arc::new(RepositoryBuilder::new().build().await.expect("created repo"));
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);

        let repo = Repository::new(&repo_config, inspector.root().create_child("repo-node"))
            .await
            .expect("created Repository");
        assert_inspect_tree!(
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
                    }
                }
            }
        );

        drop(repo);
        assert_inspect_tree!(
            inspector,
            root: {}
        );
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn get_merkle_at_path_updates_inspect() {
        clock::mock::set(zx::Time::from_nanos(0));
        let inspector = inspect::Inspector::new();
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut repo = Repository::new(&repo_config, inspector.root().create_child("repo-node"))
            .await
            .expect("created Repository");
        let target_path = tuf::metadata::TargetPath::new("just-meta-far/0".to_string())
            .expect("created target path");

        repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");

        assert_inspect_tree!(
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
                    }
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn subscribed_repo_after_event() {
        clock::mock::set(zx::Time::from_nanos(0));
        let inspector = inspect::Inspector::new();
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let (notify_on_request_handler, mut notified) = NotifyWhenRequested::new();
        let served_repository = repo
            .build_server()
            .uri_path_override_handler(ForPath::new("/timestamp.json", notify_on_request_handler))
            .start()
            .expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config_with_subscribe(repo_url);
        let repo = Repository::new(&repo_config, inspector.root().create_child("repo-node"))
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

        assert_inspect_tree!(
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
