// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Temporarily allow deprecated functions while rust-tuf is updated
#![allow(deprecated)]

use {
    crate::{cache::MerkleForError, clock, inspect_util},
    failure::format_err,
    fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig, RepositoryKey},
    fuchsia_hyper::HyperConnector,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_contrib::inspectable::InspectableDebugString,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    hyper_rustls::HttpsConnector,
    serde_derive::{Deserialize, Serialize},
    tuf::{
        client::Config,
        crypto::PublicKey,
        error::Error as TufError,
        interchange::Json,
        metadata::{MetadataVersion, TargetPath},
        repository::{EphemeralRepository, HttpRepository, HttpRepositoryBuilder},
    },
};

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
    client: tuf::client::Client<
        Json,
        EphemeralRepository<Json>,
        HttpRepository<HttpsConnector<HyperConnector>, Json>,
        tuf::client::DefaultTranslator,
    >,

    /// Time that this repository was last successfully updated, or None if the repository has
    /// never successfully fetched target metadata.
    last_updated_time: InspectableDebugString<Option<zx::Time>>,

    inspect: RepositoryInspectState,
}

struct RepositoryInspectState {
    /// Time that this repository was last used to lookup target metadata, or None if no targets
    /// have been resolved throught this repository.
    last_used_time: inspect::StringProperty,

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
        let remote_url = config
            .mirrors()
            .get(0)
            .ok_or_else(|| format_err!("Cannot obtain remote url from repo config: {:?}", config))?
            .mirror_url();
        let remote = HttpRepositoryBuilder::new(
            url::Url::parse(remote_url).map_err(|e| {
                format_err!("Unable to parse url {:?}, received error {:?}", remote_url, e)
            })?,
            fuchsia_hyper::new_https_client(),
        )
        .build();

        Self::new_from_local_and_remote(local, remote, config, node).await
    }

    async fn new_from_local_and_remote(
        local: EphemeralRepository<Json>,
        remote: HttpRepository<HttpsConnector<HyperConnector>, Json>,
        config: &RepositoryConfig,
        node: inspect::Node,
    ) -> Result<Self, failure::Error> {
        let root_keys = config
            .root_keys()
            .iter()
            .map(|key| match key {
                RepositoryKey::Ed25519(bytes) => PublicKey::from_ed25519(bytes.clone()),
            })
            .collect::<Result<Vec<PublicKey>, _>>()?;
        Ok(Self {
            client: tuf::client::Client::with_trusted_root_keys(
                Config::default(),
                &MetadataVersion::Number(config.root_version()),
                config.root_threshold(),
                &root_keys,
                local,
                remote,
            )
            .await
            .map_err(|e| format_err!("Unable to create rust tuf client, received error {:?}", e))?,
            last_updated_time: InspectableDebugString::new(None, &node, "last_updated_time"),
            inspect: RepositoryInspectState {
                last_used_time: node
                    .create_string("last_used_time", &format!("{:?}", Option::<zx::Time>::None)),
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
        self.client.update().await.map_err(|e| match e {
            TufError::NotFound => MerkleForError::NotFound,
            other => {
                fx_log_err!("failed to update with TUF error {:?}", other);
                MerkleForError::TufError(other)
            }
        })?;
        self.last_updated_time.get_mut().replace(clock::now());

        let description =
            self.client.fetch_target_description(&target_path).await.map_err(|e| match e {
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

        self.inspect.last_used_time.set(&format!("{:?}", Some(clock::now())));
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
            serve::{handler, AtomicToggle},
            PackageBuilder, RepositoryBuilder,
        },
        fuchsia_url::pkg_url::RepoUrl,
        matches::assert_matches,
        std::sync::Arc,
    };

    impl Repository {
        pub async fn new_no_inspect(config: &RepositoryConfig) -> Result<Self, failure::Error> {
            Repository::new(config, inspect::Inspector::new().root().create_child("inner-node"))
                .await
        }
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
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
}

#[cfg(test)]
mod inspectable_inner_tests {
    use {
        super::*,
        fuchsia_async as fasync,
        fuchsia_inspect::assert_inspect_tree,
        fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder},
        fuchsia_url::pkg_url::RepoUrl,
        std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    // TODO(42445) figure out why these tests are flaking
    #[ignore]
    async fn test_initialization_and_destruction() {
        let inspector = inspect::Inspector::new();
        let repo = Arc::new(RepositoryBuilder::new().build().await.expect("created repo"));
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);

        let inner = Repository::new(&repo_config, inspector.root().create_child("repo-node"))
            .await
            .expect("created Repository");
        assert_inspect_tree!(
            inspector,
            root: {
                "repo-node": {
                  last_updated_time: "None",
                  last_used_time: "None",
                  merkles_successfully_resolved_count: 0u64,
                }
            }
        );

        drop(inner);
        assert_inspect_tree!(
            inspector,
            root: {}
        );
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn test_watcher() {
        clock::mock::set(zx::Time::from_nanos(0));
        let inspector = inspect::Inspector::new();
        let pkg = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut inner = Repository::new(&repo_config, inspector.root().create_child("repo-node"))
            .await
            .expect("created Repository");
        let target_path = tuf::metadata::TargetPath::new("just-meta-far/0".to_string())
            .expect("created target path");

        inner.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");

        assert_inspect_tree!(
            inspector,
            root: {
                "repo-node": {
                    last_updated_time: "Some(Time(0))",
                    last_used_time: "Some(Time(0))",
                    merkles_successfully_resolved_count: 1u64,
                }
            }
        );
    }
}
