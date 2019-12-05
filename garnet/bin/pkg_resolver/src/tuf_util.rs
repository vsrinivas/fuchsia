// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::cache::MerkleForError,
    chrono::{DateTime, Utc},
    failure::format_err,
    fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig, RepositoryKey},
    fuchsia_hyper::HyperConnector,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_contrib::inspectable::{Inspectable, Watch},
    fuchsia_syslog::fx_log_err,
    hyper_rustls::HttpsConnector,
    serde_derive::{Deserialize, Serialize},
    std::sync::Arc,
    tuf::{
        client::Config,
        crypto::{KeyId, PublicKey},
        error::Error as TufError,
        interchange::Json,
        metadata::TargetPath,
        repository::{EphemeralRepository, HttpRepository, HttpRepositoryBuilder},
    },
};

pub type OpenedRepository = Arc<futures::lock::Mutex<InspectableInner>>;

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

pub struct Inner {
    client: tuf::client::Client<
        Json,
        EphemeralRepository<Json>,
        HttpRepository<HttpsConnector<HyperConnector>, Json>,
        tuf::client::DefaultTranslator,
    >,
    last_updated_time: DateTime<chrono::offset::Utc>,
    num_packages_fetched: u64,
}

impl Inner {
    pub async fn new(config: &RepositoryConfig) -> Result<Self, failure::Error> {
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

        Self::new_from_local_and_remote(local, remote, config).await
    }

    async fn new_from_local_and_remote(
        local: EphemeralRepository<Json>,
        remote: HttpRepository<HttpsConnector<HyperConnector>, Json>,
        config: &RepositoryConfig,
    ) -> Result<Self, failure::Error> {
        let root_keys = config
            .root_keys()
            .iter()
            .map(|key| match key {
                RepositoryKey::Ed25519(bytes) => PublicKey::from_ed25519(bytes.clone()),
            })
            .collect::<Result<Vec<PublicKey>, _>>()?;
        let root_key_ids: Vec<KeyId> = root_keys.iter().map(|key| key.key_id().clone()).collect();
        Ok(Self {
            client: tuf::client::Client::with_root_pinned(
                &root_key_ids,
                Config::default(),
                local,
                remote,
                1,
            )
            .await
            .map_err(|e| format_err!("Unable to create rust tuf client, received error {:?}", e))?,
            num_packages_fetched: 0,
            last_updated_time: Utc::now(),
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
        self.last_updated_time = chrono::Utc::now();

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
        self.num_packages_fetched += 1;

        let custom = description.custom().ok_or(MerkleForError::NoCustomMetadata)?.to_owned();
        let custom = serde_json::Value::from(custom.into_iter().collect::<serde_json::Map<_, _>>());
        let mut custom: CustomTargetMetadata =
            serde_json::from_value(custom).map_err(MerkleForError::SerdeError)?;
        custom.size = description.length();

        Ok(custom)
    }

    pub fn last_updated_time(&self) -> &DateTime<chrono::offset::Utc> {
        &self.last_updated_time
    }

    pub fn num_packages_fetched(&self) -> u64 {
        self.num_packages_fetched
    }
}

pub type InspectableInner = Inspectable<Inner, InspectableInnerWatcher>;

pub struct InspectableInnerWatcher {
    num_packages_fetched_property: inspect::UintProperty,
    last_updated_property: inspect::StringProperty,
    _node: inspect::Node,
}

impl Watch<Inner> for InspectableInnerWatcher {
    fn new(inner: &Inner, node: &inspect::Node, name: impl AsRef<str>) -> Self {
        let rust_tuf_repo_node = node.create_child(name);
        Self {
            num_packages_fetched_property: rust_tuf_repo_node
                .create_uint("num_packages_fetched", inner.num_packages_fetched()),
            last_updated_property: rust_tuf_repo_node
                .create_string("last_updated_time", format!("{:?}", inner.last_updated_time())),
            _node: rust_tuf_repo_node,
        }
    }

    fn watch(&mut self, inner: &Inner) {
        self.num_packages_fetched_property.set(inner.num_packages_fetched());
        self.last_updated_property.set(&format!("{:?}", inner.last_updated_time()));
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
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_get_merkle_at_path() {
        // Serve static repo and connect to it
        let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut repo = Inner::new(&repo_config).await.expect("created opened repo");
        let first_updated_time = repo.last_updated_time.clone();
        let target_path =
            TargetPath::new("just_meta_far/0".to_string()).expect("created target path");

        // Obtain merkle root and meta far size
        let CustomTargetMetadata { merkle, size } =
            repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");

        // Verify what we got from tuf was correct
        assert_eq!(merkle.as_bytes(), pkg.meta_far_merkle_root().as_bytes());
        assert_eq!(size, pkg.meta_far().unwrap().metadata().unwrap().len());
        assert_eq!(repo.num_packages_fetched, 1);
        assert_ne!(repo.last_updated_time, first_updated_time);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_merkle_at_path_fails_when_no_package() {
        let repo = Arc::new(RepositoryBuilder::new().build().await.expect("created repo"));
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut repo = Inner::new(&repo_config).await.expect("created opened repo");
        let first_updated_time = repo.last_updated_time.clone();
        let target_path =
            TargetPath::new("path_that_doesnt_exist/0".to_string()).expect("created target path");

        // We still updated, but didn't fetch any packages
        assert_matches!(repo.get_merkle_at_path(&target_path).await, Err(MerkleForError::NotFound));
        assert_eq!(repo.num_packages_fetched, 0);
        assert_ne!(repo.last_updated_time, first_updated_time);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_merkle_at_path_fails_when_remote_repo_down() {
        // Serve static repo
        let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
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
        let mut repo = Inner::new(&repo_config).await.expect("created opened repo");
        let first_updated_time = repo.last_updated_time.clone();
        let target_path =
            TargetPath::new("just_meta_far/0".to_string()).expect("created target path");

        // When the server is blocked, we should fail at get_merkle_at_path
        // TODO(fxb/39651) if the Inner can't connect to the remote server AND
        // we've updated our local repo recently, then it should return the merkle that is stored locally
        should_fail.set();
        assert_matches!(repo.get_merkle_at_path(&target_path).await, Err(MerkleForError::NotFound));
        assert_eq!(repo.num_packages_fetched, 0);
        assert_eq!(repo.last_updated_time, first_updated_time);

        // When the server is unblocked, we should succeed again
        should_fail.unset();
        let CustomTargetMetadata { merkle, size } =
            repo.get_merkle_at_path(&target_path).await.expect("fetched merkle from tuf");
        assert_eq!(merkle.as_bytes(), pkg.meta_far_merkle_root().as_bytes());
        assert_eq!(size, pkg.meta_far().unwrap().metadata().unwrap().len());
        assert_eq!(repo.num_packages_fetched, 1);
        assert_ne!(repo.last_updated_time, first_updated_time);
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
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_initialization() {
        let inspector = inspect::Inspector::new();
        let repo = Arc::new(RepositoryBuilder::new().build().await.expect("created repo"));
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);

        let inspectable = InspectableInner::new(
            Inner::new(&repo_config).await.expect("created Inner"),
            inspector.root(),
            "test-property",
        );

        assert_inspect_tree!(
            inspector,
            root: {
                "test-property": {
                  last_updated_time: format!("{:?}",inspectable.last_updated_time()),
                  num_packages_fetched: inspectable.num_packages_fetched(),
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_watcher() {
        let inspector = inspect::Inspector::new();
        let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
        let repo = Arc::new(
            RepositoryBuilder::new().add_package(&pkg).build().await.expect("created repo"),
        );
        let served_repository = repo.build_server().start().expect("create served repo");
        let repo_url = RepoUrl::parse("fuchsia-pkg://test").expect("created repo url");
        let repo_config = served_repository.make_repo_config(repo_url);
        let mut inspectable = InspectableInner::new(
            Inner::new(&repo_config).await.expect("created Inner"),
            inspector.root(),
            "test-property",
        );
        let target_path = tuf::metadata::TargetPath::new("just_meta_far/0".to_string())
            .expect("created target path");

        inspectable
            .get_mut()
            .get_merkle_at_path(&target_path)
            .await
            .expect("fetched merkle from tuf");

        assert_inspect_tree!(
            inspector,
            root: {
                "test-property": {
                  last_updated_time: format!("{:?}",inspectable.last_updated_time()),
                  num_packages_fetched: inspectable.num_packages_fetched(),
                }
            }
        );
    }
}
