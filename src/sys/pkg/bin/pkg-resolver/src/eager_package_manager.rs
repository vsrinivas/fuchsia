// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resolver_service::Resolver,
    anyhow::{anyhow, Context as _, Error},
    async_lock::RwLock as AsyncRwLock,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{
        CupData, CupRequest, CupRequestStream, GetInfoError, PackageUrl, WriteError,
    },
    fidl_fuchsia_pkg_ext::{cache, BlobInfo},
    fidl_fuchsia_pkg_internal::{PersistentEagerPackage, PersistentEagerPackages},
    fuchsia_pkg::PackageDirectory,
    fuchsia_syslog::fx_log_err,
    fuchsia_url::pkg_url::{Hash, PinnedPkgUrl, PkgUrl},
    fuchsia_zircon as zx,
    futures::prelude::*,
    omaha_client::{cup_ecdsa::PublicKeys, protocol::response::Response},
    serde::Deserialize,
    std::{collections::BTreeMap, sync::Arc},
};

const EAGER_PACKAGE_CONFIG_PATH: &str = "/config/data/eager_package_config.json";
const EAGER_PACKAGE_PERSISTENT_FIDL_NAME: &str = "eager_packages.pf";

#[derive(Clone, Debug)]
struct EagerPackage {
    #[allow(dead_code)]
    executable: bool,
    package_directory: Option<PackageDirectory>,
    cup: Option<CupData>,
}

#[derive(Debug)]
pub struct EagerPackageManager<T: Resolver> {
    // Map from unpinned eager package URL to `EagerPackage`.
    packages: BTreeMap<PkgUrl, EagerPackage>,
    package_resolver: T,
    data_proxy: Option<fio::DirectoryProxy>,
}

impl<T: Resolver> EagerPackageManager<T> {
    pub async fn from_namespace(
        package_resolver: T,
        pkg_cache: cache::Client,
        data_proxy: Option<fio::DirectoryProxy>,
    ) -> Result<Self, Error> {
        let config =
            EagerPackageConfigs::from_namespace().await.context("loading eager package config")?;
        Self::from_config(config, package_resolver, pkg_cache, data_proxy).await
    }

    async fn from_config(
        config: EagerPackageConfigs,
        package_resolver: T,
        pkg_cache: cache::Client,
        data_proxy: Option<fio::DirectoryProxy>,
    ) -> Result<Self, Error> {
        let mut packages = config
            .packages
            .into_iter()
            .map(|EagerPackageConfig { url, executable, public_keys: _ }| {
                if url.package_hash().is_none() {
                    Ok((url, EagerPackage { executable, package_directory: None, cup: None }))
                } else {
                    Err(anyhow!("pinned url not allowed in eager package config: '{}'", url))
                }
            })
            .collect::<Result<BTreeMap<PkgUrl, EagerPackage>, _>>()?;

        if let Some(ref data_proxy) = data_proxy {
            if let Err(e) =
                Self::load_persistent_eager_packages(&mut packages, pkg_cache, &data_proxy).await
            {
                fx_log_err!("failed to load persistent eager packages: {:#}", anyhow!(e));
            }
        }

        Ok(Self { packages, package_resolver, data_proxy })
    }

    async fn load_persistent_eager_packages(
        packages: &mut BTreeMap<PkgUrl, EagerPackage>,
        pkg_cache: cache::Client,
        data_proxy: &fio::DirectoryProxy,
    ) -> Result<(), Error> {
        let file_proxy = io_util::directory::open_file(
            data_proxy,
            EAGER_PACKAGE_PERSISTENT_FIDL_NAME,
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .context("while opening eager_packages.pf")?;
        let persistent_packages = io_util::read_file_fidl::<PersistentEagerPackages>(&file_proxy)
            .await
            .context("while reading eager_packages.pf")?;
        for PersistentEagerPackage { url, cup, .. } in persistent_packages
            .packages
            .ok_or_else(|| anyhow!("PersistentEagerPackages does not contain `packages` field"))?
        {
            async {
                let url = url.ok_or_else(|| {
                    anyhow!("PersistentEagerPackage does not contain `url` field")
                })?;
                let cup = cup.ok_or_else(|| {
                    anyhow!("PersistentEagerPackage does not contain `cup` field")
                })?;
                // TODO(fxbug.dev/95296): verify CUP signature before parsing.
                let response = parse_omaha_response_from_cup(&cup)
                    .with_context(|| format!("while parsing omaha response {:?}", cup.response))?;
                let pinned_url = response
                    .apps
                    .iter()
                    .find_map(|app| {
                        app.update_check
                            .as_ref()
                            .and_then(|uc| uc.get_all_full_urls().find(|u| u.starts_with(&url.url)))
                    })
                    .ok_or_else(|| {
                        anyhow!("could not find pinned url in CUP omaha response for {}", url.url)
                    })?;
                let pinned_url: PinnedPkgUrl =
                    pinned_url.parse().with_context(|| format!("while parsing {}", url.url))?;
                let unpinned_url = pinned_url.strip_hash();
                let package = packages
                    .get_mut(&unpinned_url)
                    .ok_or_else(|| anyhow!("unknown pkg url: {}", url.url))?;
                package.cup = Some(cup);
                let pkg_dir =
                    Self::resolve_pinned_from_cache(&pkg_cache, pinned_url).await.with_context(
                        || format!("while resolving eager package {} from cache", url.url),
                    )?;
                package.package_directory = Some(pkg_dir);
                Ok(())
            }
            .await
            .unwrap_or_else(|e: Error| {
                fx_log_err!("failed to load persistent eager package: {:#}", anyhow!(e))
            })
        }
        Ok(())
    }

    // Returns eager package directory.
    // Returns Ok(None) if that's not an eager package, or the package is pinned.
    pub fn get_package_dir(&self, url: &PkgUrl) -> Result<Option<PackageDirectory>, Error> {
        if url.package_hash().is_some() {
            // Optimization: since all URLs in self.packages are unpinned, return early.
            return Ok(None);
        }
        if let Some(eager_package) = self.packages.get(url) {
            if eager_package.package_directory.is_some() {
                Ok(eager_package.package_directory.clone())
            } else {
                Err(anyhow!("eager package dir not found for {}", url))
            }
        } else {
            Ok(None)
        }
    }

    async fn resolve_pinned(
        package_resolver: &T,
        url: PinnedPkgUrl,
    ) -> Result<PackageDirectory, ResolvePinnedError> {
        let expected_hash = url.package_hash();

        let pkg_dir = package_resolver.resolve(url.into(), None).await?;

        let hash = pkg_dir.merkle_root().await?;
        if hash != expected_hash {
            return Err(ResolvePinnedError::HashMismatch(hash));
        }
        Ok(pkg_dir)
    }

    async fn resolve_pinned_from_cache(
        pkg_cache: &cache::Client,
        url: PinnedPkgUrl,
    ) -> Result<PackageDirectory, Error> {
        let mut get = pkg_cache
            .get(BlobInfo { blob_id: url.package_hash().into(), length: 0 })
            .context("pkg cache get")?;
        if let Some(_needed_blob) = get.open_meta_blob().await.context("open_meta_blob")? {
            return Err(anyhow!("meta blob missing"));
        }
        let missing_blobs = get.get_missing_blobs().await.context("get_missing_blobs")?;
        if !missing_blobs.is_empty() {
            return Err(anyhow!("at least one blob missing: {:?}", missing_blobs));
        }
        Ok(get.finish().await.context("finish")?)
    }

    async fn persist(&self, packages: &BTreeMap<PkgUrl, EagerPackage>) -> Result<(), PersistError> {
        let data_proxy = self.data_proxy.as_ref().ok_or(PersistError::DataProxyNotAvailable)?;

        let packages = packages
            .iter()
            .map(|(url, package)| PersistentEagerPackage {
                url: Some(PackageUrl { url: url.to_string() }),
                cup: package.cup.clone(),
                ..PersistentEagerPackage::EMPTY
            })
            .collect();
        let mut packages =
            PersistentEagerPackages { packages: Some(packages), ..PersistentEagerPackages::EMPTY };

        let temp_path = &format!("{EAGER_PACKAGE_PERSISTENT_FIDL_NAME}.new");
        crate::util::do_with_atomic_file(
            &data_proxy,
            temp_path,
            EAGER_PACKAGE_PERSISTENT_FIDL_NAME,
            |proxy| async move {
                io_util::write_file_fidl(&proxy, &mut packages)
                    .await
                    .with_context(|| format!("writing file: {}", temp_path))
            },
        )
        .await
        .map_err(PersistError::AtomicWrite)
    }

    async fn cup_write(&mut self, url: &PackageUrl, cup: CupData) -> Result<(), CupWriteError> {
        // TODO(fxbug.dev/95296): verify CUP signature before parsing.
        let response = parse_omaha_response_from_cup(&cup)?;
        // The full URL must appear in the omaha response.
        let _app = response
            .apps
            .iter()
            .find(|app| {
                app.update_check
                    .as_ref()
                    .and_then(|uc| uc.get_all_full_urls().find(|u| u == &url.url))
                    .is_some()
            })
            .ok_or(CupWriteError::CupResponseURLNotFound)?;

        let pinned_url: PinnedPkgUrl = url.url.parse()?;
        let unpinned_url = pinned_url.strip_hash();
        let mut packages = self.packages.clone();
        // Make sure the url is an eager package before trying to resolve it.
        let package = packages
            .get_mut(&unpinned_url)
            .ok_or_else(|| CupWriteError::UnknownURL(unpinned_url))?;
        let pkg_dir = Self::resolve_pinned(&self.package_resolver, pinned_url).await?;
        package.package_directory = Some(pkg_dir);
        package.cup = Some(cup);

        self.persist(&packages).await?;
        // Only update self.packages if persist succeed, to prevent rollback after reboot.
        self.packages = packages;
        Ok(())
    }

    async fn cup_get_info(&self, url: &PackageUrl) -> Result<(String, String), CupGetInfoError> {
        let pkg_url = PkgUrl::parse(&url.url)?;
        let package =
            self.packages.get(&pkg_url).ok_or_else(|| CupGetInfoError::UnknownURL(pkg_url))?;
        let cup = package.cup.as_ref().ok_or(CupGetInfoError::CupDataNotAvailable)?;
        let response = parse_omaha_response_from_cup(&cup)?;
        let app = response
            .apps
            .iter()
            .find(|app| {
                app.update_check
                    .as_ref()
                    .and_then(|uc| uc.get_all_full_urls().find(|u| u.starts_with(&url.url)))
                    .is_some()
            })
            .ok_or(CupGetInfoError::CupResponseURLNotFound)?;
        let version = app.get_manifest_version().ok_or(CupGetInfoError::CupDataMissingVersion)?;
        let channel = app.cohort.name.clone().ok_or(CupGetInfoError::CupDataMissingChannel)?;
        Ok((version, channel))
    }
}

fn parse_omaha_response_from_cup(cup: &CupData) -> Result<Response, ParseCupResponseError> {
    Ok(omaha_client::protocol::response::parse_json_response(
        cup.response.as_ref().ok_or(ParseCupResponseError::CupDataNoResponse)?,
    )?)
}

#[derive(Debug, thiserror::Error)]
enum ParseCupResponseError {
    #[error("CUP data does not include a response")]
    CupDataNoResponse,
    #[error("while parsing JSON")]
    ParseJSON(#[from] serde_json::Error),
}

#[derive(Debug, thiserror::Error)]
enum ResolvePinnedError {
    #[error("while resolving package")]
    Resolve(#[from] fidl_fuchsia_pkg_ext::ResolveError),
    #[error("while reading package hash")]
    ReadHash(#[from] fuchsia_pkg::ReadHashError),
    #[error("resolved package hash '{0}' does not match")]
    HashMismatch(Hash),
}

#[derive(Debug, thiserror::Error)]
enum PersistError {
    #[error("directory proxy to /data is not available")]
    DataProxyNotAvailable,
    #[error("while opening temp file")]
    Open(#[from] io_util::node::OpenError),
    #[error("while writing persistent fidl")]
    AtomicWrite(#[source] anyhow::Error),
}

#[derive(Debug, thiserror::Error)]
enum CupWriteError {
    #[error("while parsing package URL")]
    ParseURL(#[from] fuchsia_url::errors::ParseError),
    #[error("URL is not a known eager package: {0}")]
    UnknownURL(PkgUrl),
    #[error("while resolving package")]
    ResolvePinned(#[from] ResolvePinnedError),
    #[error("while parsing CUP omaha response")]
    ParseCupResponse(#[from] ParseCupResponseError),
    #[error("URL not found in CUP omaha response")]
    CupResponseURLNotFound,
    #[error("while persisting CUP data")]
    Persist(#[from] PersistError),
}

impl From<&CupWriteError> for WriteError {
    fn from(err: &CupWriteError) -> WriteError {
        match err {
            CupWriteError::ParseURL(_) => WriteError::UnknownUrl,
            CupWriteError::UnknownURL(_) => WriteError::UnknownUrl,
            CupWriteError::ResolvePinned(_) => WriteError::Download,
            CupWriteError::ParseCupResponse(_) => WriteError::Verification,
            CupWriteError::CupResponseURLNotFound => WriteError::Verification,
            CupWriteError::Persist(_) => WriteError::Storage,
        }
    }
}

#[derive(Debug, thiserror::Error)]
enum CupGetInfoError {
    #[error("while parsing package URL")]
    ParseURL(#[from] fuchsia_url::errors::ParseError),
    #[error("URL is not a known eager package: {0}")]
    UnknownURL(PkgUrl),
    #[error("CUP data is not available for this eager package")]
    CupDataNotAvailable,
    #[error("URL not found in CUP omaha response")]
    CupResponseURLNotFound,
    #[error("CUP data does not include version")]
    CupDataMissingVersion,
    #[error("CUP data does not include channel")]
    CupDataMissingChannel,
    #[error("while parsing CUP omaha response")]
    ParseCupResponse(#[from] ParseCupResponseError),
}

impl From<&CupGetInfoError> for GetInfoError {
    fn from(err: &CupGetInfoError) -> GetInfoError {
        match err {
            CupGetInfoError::ParseURL(_) => GetInfoError::UnknownUrl,
            CupGetInfoError::UnknownURL(_) => GetInfoError::UnknownUrl,
            CupGetInfoError::CupDataNotAvailable => GetInfoError::NotAvailable,
            CupGetInfoError::CupResponseURLNotFound => GetInfoError::Verification,
            CupGetInfoError::CupDataMissingVersion => GetInfoError::Verification,
            CupGetInfoError::CupDataMissingChannel => GetInfoError::Verification,
            CupGetInfoError::ParseCupResponse(_) => GetInfoError::Verification,
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
struct EagerPackageConfigs {
    packages: Vec<EagerPackageConfig>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
struct EagerPackageConfig {
    url: PkgUrl,
    #[serde(default)]
    executable: bool,
    public_keys: PublicKeys,
}

impl EagerPackageConfigs {
    /// Read eager config from namespace. Returns an empty instance of `EagerPackageConfigs` in
    /// case config was not found.
    async fn from_namespace() -> Result<Self, Error> {
        match io_util::file::read_in_namespace(EAGER_PACKAGE_CONFIG_PATH).await {
            Ok(json) => Ok(serde_json::from_slice(&json).context("parsing eager package config")?),
            Err(e) => match e.into_inner() {
                io_util::file::ReadError::Open(io_util::node::OpenError::OpenError(e))
                    if e == zx::Status::NOT_FOUND =>
                {
                    // This error is only reachable if /config/data exists, but the file is missing.
                    Ok(EagerPackageConfigs { packages: Vec::new() })
                }
                err => Err(anyhow!(
                    "Error reading eager package config file {}: {}",
                    EAGER_PACKAGE_CONFIG_PATH,
                    err
                )),
            },
        }
    }
}

pub async fn run_cup_service<T: Resolver>(
    manager: Arc<Option<AsyncRwLock<EagerPackageManager<T>>>>,
    mut stream: CupRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            CupRequest::Write { url, cup, responder } => {
                let mut response = match manager.as_ref() {
                    Some(manager) => {
                        manager.write().await.cup_write(&url, cup).await.map_err(|e| {
                            let write_error = (&e).into();
                            fx_log_err!("cup_write failed for url '{}': {:#}", url.url, anyhow!(e));
                            write_error
                        })
                    }
                    None => Err(WriteError::Storage),
                };
                responder.send(&mut response)?;
            }
            CupRequest::GetInfo { url, responder } => {
                let mut response = match manager.as_ref() {
                    Some(manager) => manager.read().await.cup_get_info(&url).await.map_err(|e| {
                        let get_info_error = (&e).into();
                        fx_log_err!("cup_get_info failed for url '{}': {:#}", url.url, anyhow!(e));
                        get_info_error
                    }),
                    None => Err(GetInfoError::NotAvailable),
                };
                responder.send(&mut response)?;
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::resolver_service::MockResolver,
        assert_matches::assert_matches,
        fidl_fuchsia_pkg::{
            BlobInfoIteratorRequest, NeededBlobsRequest, PackageCacheMarker, PackageCacheRequest,
            PackageCacheRequestStream,
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
        omaha_client::cup_ecdsa::{PublicKey, PublicKeyAndId, PublicKeys},
        p256::ecdsa::SigningKey,
        signature::rand_core::OsRng,
        typed_builder::TypedBuilder,
    };

    fn make_public_keys_for_test() -> PublicKeys {
        let signing_key = SigningKey::random(&mut OsRng);
        let public_key = PublicKey::from(&signing_key);
        let public_key_id = 42;
        PublicKeys {
            latest: PublicKeyAndId { id: public_key_id, key: public_key },
            historical: vec![],
        }
    }

    const TEST_URL: &str = "fuchsia-pkg://example.com/package";
    const TEST_HASH: &str = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    const TEST_PINNED_URL: &str = "fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    fn get_test_package_resolver() -> MockResolver {
        let pkg_dir = PackageDirectory::open_from_namespace().unwrap();
        MockResolver::new(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok(pkg_dir) }
        })
    }

    fn get_mock_pkg_cache() -> (cache::Client, PackageCacheRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PackageCacheMarker>().unwrap();
        (cache::Client::from_proxy(proxy), stream)
    }

    async fn handle_pkg_cache(mut stream: PackageCacheRequestStream) {
        while let Some(request) = stream.try_next().await.unwrap() {
            match request {
                PackageCacheRequest::Get { meta_far_blob, needed_blobs, dir: _, responder } => {
                    if meta_far_blob.blob_id.merkle_root
                        != TEST_HASH.parse::<Hash>().unwrap().as_bytes()
                    {
                        responder.send(&mut Err(zx::Status::NOT_FOUND.into_raw())).unwrap();
                        continue;
                    }
                    let mut needed_blobs = needed_blobs.into_stream().unwrap();
                    while let Some(request) = needed_blobs.try_next().await.unwrap() {
                        match request {
                            NeededBlobsRequest::OpenMetaBlob { file: _, responder } => {
                                responder.send(&mut Ok(false)).unwrap();
                            }
                            NeededBlobsRequest::GetMissingBlobs { iterator, control_handle: _ } => {
                                let mut stream = iterator.into_stream().unwrap();
                                let BlobInfoIteratorRequest::Next { responder } =
                                    stream.next().await.unwrap().unwrap();
                                responder.send(&mut std::iter::empty()).unwrap();
                            }
                            r => panic!("Unexpected request: {:?}", r),
                        }
                    }
                    responder.send(&mut Ok(())).unwrap();
                }
                r => panic!("Unexpected request: {:?}", r),
            }
        }
    }

    fn get_test_package_resolver_with_hash(hash: &str) -> (MockResolver, tempfile::TempDir) {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("meta"), hash).unwrap();
        let proxy = io_util::open_directory_in_namespace(
            dir.path().to_str().unwrap(),
            io_util::OpenFlags::RIGHT_READABLE,
        )
        .unwrap();
        let pkg_dir = PackageDirectory::from_proxy(proxy);
        let package_resolver = MockResolver::new(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok(pkg_dir) }
        });
        (package_resolver, dir)
    }

    #[derive(Clone, Debug, Eq, PartialEq, TypedBuilder)]
    struct CupDataForTest {
        #[builder(default, setter(into))]
        pub request: Option<Vec<u8>>,
        #[builder(default, setter(into))]
        pub key_id: Option<u64>,
        #[builder(default, setter(into))]
        pub nonce: Option<String>,
        #[builder(default=Some(get_default_cup_response()), setter(into))]
        pub response: Option<Vec<u8>>,
        #[builder(default, setter(into))]
        pub signature: Option<Vec<u8>>,
    }

    impl From<CupDataForTest> for CupData {
        fn from(c: CupDataForTest) -> Self {
            CupData {
                request: c.request,
                key_id: c.key_id,
                nonce: c.nonce,
                response: c.response,
                signature: c.signature,
                ..CupData::EMPTY
            }
        }
    }

    fn get_default_cup_response() -> Vec<u8> {
        get_cup_response_with_name(&format!("package?hash={TEST_HASH}"))
    }
    fn get_cup_response_with_name(package_name: &str) -> Vec<u8> {
        let response = serde_json::json!({"response":{
          "server": "prod",
          "protocol": "3.0",
          "app": [{
            "appid": "appid",
            "cohortname": "stable",
            "status": "ok",
            "updatecheck": {
              "status": "ok",
              "urls":{
                "url":[
                  {"codebase": "fuchsia-pkg://example.com/"},
                ]
              },
              "manifest": {
                "version": "1.2.3.4",
                "actions": {
                  "action": [],
                },
                "packages": {
                  "package": [
                    {
                     "name": package_name,
                     "required": true,
                     "fp": "",
                    }
                  ],
                },
              }
            }
          }],
        }});
        serde_json::to_vec(&response).unwrap()
    }

    async fn write_persistent_fidl(
        data_proxy: &fio::DirectoryProxy,
        packages: impl IntoIterator<Item = (impl std::fmt::Display, CupData)>,
    ) {
        let file_proxy = io_util::directory::open_file(
            data_proxy,
            EAGER_PACKAGE_PERSISTENT_FIDL_NAME,
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::CREATE,
        )
        .await
        .unwrap();
        let mut packages = PersistentEagerPackages {
            packages: Some(
                packages
                    .into_iter()
                    .map(|(url, cup)| PersistentEagerPackage {
                        url: Some(PackageUrl { url: url.to_string() }),
                        cup: Some(cup),
                        ..PersistentEagerPackage::EMPTY
                    })
                    .collect(),
            ),
            ..PersistentEagerPackages::EMPTY
        };
        io_util::write_file_fidl(&file_proxy, &mut packages).await.unwrap();
    }

    #[test]
    fn parse_eager_package_configs_json() {
        use std::convert::TryInto;
        use std::str::FromStr;

        let public_keys = PublicKeys {
            latest: PublicKeyAndId {
                id: 123.try_into().unwrap(),
                key: PublicKey::from_str(
                    r#"-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt
7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==
-----END PUBLIC KEY-----"#,
                )
                .unwrap(),
            },
            historical: vec![],
        };

        let json = br#"
        {
            "packages":
            [
                {
                    "url": "fuchsia-pkg://example.com/package",
                    "executable": true,
                    "public_keys": {
                        "latest": {
                            "id": 123,
                            "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----"
                        },
                        "historical": []
                    }
                },
                {
                    "url": "fuchsia-pkg://example.com/package2",
                    "executable": false,
                    "public_keys": {
                        "latest": {
                            "id": 123,
                            "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----"
                        },
                        "historical": []
                    }
                },
                {
                    "url": "fuchsia-pkg://example.com/package3",
                    "public_keys": {
                        "latest": {
                            "id": 123,
                            "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----"
                        },
                        "historical": []
                    }
                }
            ]
        }"#;
        assert_eq!(
            serde_json::from_slice::<EagerPackageConfigs>(json).unwrap(),
            EagerPackageConfigs {
                packages: vec![
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package").unwrap(),
                        executable: true,
                        public_keys: public_keys.clone(),
                    },
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap(),
                        executable: false,
                        public_keys: public_keys.clone(),
                    },
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package3").unwrap(),
                        executable: false,
                        public_keys,
                    },
                ]
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_eager_package_with_empty_config() {
        let config = EagerPackageConfigs { packages: Vec::new() };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();
        let manager = EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
            .await
            .unwrap();

        assert!(manager.packages.is_empty());

        for url in [
            "fuchsia-pkg://example2.com/package",
            "fuchsia-pkg://example.com/package3",
            "fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        ] {
            assert_matches!(manager.get_package_dir(&PkgUrl::parse(url).unwrap()), Ok(None));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn config_reject_pinned_urls() {
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: PkgUrl::parse(TEST_PINNED_URL).unwrap(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();
        assert_matches!(
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, None).await,
            Err(_)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_pinned_hash_mismatch() {
        let pinned_url =
            PinnedPkgUrl::from_url_and_hash(TEST_URL.parse().unwrap(), TEST_HASH.parse().unwrap());
        assert_matches!(
            EagerPackageManager::resolve_pinned(&get_test_package_resolver(), pinned_url).await,
            Err(ResolvePinnedError::HashMismatch(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_persistent_eager_packages() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let url2 = PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![
                EagerPackageConfig {
                    url: url.clone(),
                    executable: true,
                    public_keys: make_public_keys_for_test(),
                },
                EagerPackageConfig {
                    url: url2.clone(),
                    executable: false,
                    public_keys: make_public_keys_for_test(),
                },
            ],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = io_util::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        // this will fail to resolve because hash doesn't match
        let cup2: CupData = CupDataForTest::builder()
            .response(Some(get_cup_response_with_name(&format!(
                "package2?hash={}",
                "1".repeat(64)
            ))))
            .build()
            .into();
        write_persistent_fidl(
            &data_proxy,
            [
                // packages can be out of order
                (url2.clone(), cup2.clone()),
                // bad packages won't break loading of other valid packages
                (PkgUrl::parse("fuchsia-pkg://unknown/package").unwrap(), cup.clone()),
                (url.clone(), CupData::EMPTY),
                (url.clone(), cup.clone()),
            ],
        )
        .await;
        let (manager, ()) = future::join(
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, Some(data_proxy)),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        let manager = manager.unwrap();
        assert_matches!(
            manager.get_package_dir(
                &PkgUrl::parse("fuchsia-pkg://example.com/non-eager-package").unwrap()
            ),
            Ok(None)
        );
        assert!(manager.packages[&url].package_directory.is_some());
        assert!(manager.get_package_dir(&url).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup));
        assert!(manager.packages[&url2].package_directory.is_none());
        assert_matches!(manager.get_package_dir(&url2), Err(_));
        // cup is still loaded even if resolve fails
        assert_eq!(manager.packages[&url2].cup, Some(cup2));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = io_util::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let mut manager = EagerPackageManager::from_config(
            config.clone(),
            package_resolver.clone(),
            pkg_cache.clone(),
            Some(Clone::clone(&data_proxy)),
        )
        .await
        .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        manager.cup_write(&PackageUrl { url: TEST_PINNED_URL.into() }, cup.clone()).await.unwrap();
        assert!(manager.packages[&url].package_directory.is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup.clone()));

        // create a new manager which should load the persisted cup data.
        let (manager2, ()) = future::join(
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, Some(data_proxy)),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        let manager2 = manager2.unwrap();
        assert!(manager2.packages[&url].package_directory.is_some());
        assert_eq!(manager2.packages[&url].cup, Some(cup));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_persist_fail() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager =
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
                .await
                .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        assert_matches!(
            manager.cup_write(&PackageUrl { url: TEST_PINNED_URL.into() }, cup).await,
            Err(CupWriteError::Persist(_))
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_omaha_response_different_url() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager =
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
                .await
                .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        assert_matches!(
            manager
                .cup_write(&PackageUrl { url: format!("{url}?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead") }, cup)
                .await,
            Err(CupWriteError::CupResponseURLNotFound)
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_unknown_url() {
        let url = PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager =
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
                .await
                .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        assert_matches!(
            manager.cup_write(&PackageUrl { url: TEST_PINNED_URL.into() }, cup).await,
            Err(CupWriteError::UnknownURL(_))
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager =
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
                .await
                .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        let (version, channel) =
            manager.cup_get_info(&PackageUrl { url: TEST_URL.into() }).await.unwrap();
        assert_eq!(version, "1.2.3.4");
        assert_eq!(channel, "stable");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_not_available() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let manager = EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
            .await
            .unwrap();
        assert_matches!(
            manager.cup_get_info(&PackageUrl { url: TEST_URL.into() }).await,
            Err(CupGetInfoError::CupDataNotAvailable)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_unknown_url() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager =
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
                .await
                .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        assert_matches!(
            manager
                .cup_get_info(&PackageUrl { url: "fuchsia-pkg://example.com/package2".into() })
                .await,
            Err(CupGetInfoError::UnknownURL(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_omaha_response_different_url() {
        let url = PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager =
            EagerPackageManager::from_config(config, package_resolver, pkg_cache, None)
                .await
                .unwrap();
        let cup: CupData = CupDataForTest::builder().build().into();
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        assert_matches!(
            manager
                .cup_get_info(&PackageUrl { url: "fuchsia-pkg://example.com/package2".into() })
                .await,
            Err(CupGetInfoError::CupResponseURLNotFound)
        );
    }
}
