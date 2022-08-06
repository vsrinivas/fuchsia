// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resolver_service::Resolver,
    anyhow::{anyhow, Context as _, Error},
    async_lock::RwLock as AsyncRwLock,
    cobalt_sw_delivery_registry as metrics,
    eager_package_config::pkg_resolver::{EagerPackageConfig, EagerPackageConfigs},
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_metrics::MetricEvent,
    fidl_fuchsia_pkg::{self as fpkg, CupRequest, CupRequestStream, GetInfoError, WriteError},
    fidl_fuchsia_pkg_ext::{cache, BlobInfo, CupData, CupMissingField, ResolutionContext},
    fidl_fuchsia_pkg_internal::{PersistentEagerPackage, PersistentEagerPackages},
    fuchsia_cobalt_builders::MetricEventExt as _,
    fuchsia_pkg::PackageDirectory,
    fuchsia_syslog::fx_log_err,
    fuchsia_url::{AbsolutePackageUrl, Hash, PinnedAbsolutePackageUrl, UnpinnedAbsolutePackageUrl},
    fuchsia_zircon as zx,
    futures::prelude::*,
    omaha_client::{
        cup_ecdsa::{CupVerificationError, Cupv2Verifier, PublicKeys, StandardCupv2Handler},
        protocol::response::Response,
    },
    p256::ecdsa::{signature::Signature, DerSignature},
    std::{collections::BTreeMap, convert::TryInto, sync::Arc},
    system_image::CachePackages,
};

const EAGER_PACKAGE_PERSISTENT_FIDL_NAME: &str = "eager_packages.pf";

#[derive(Clone, Debug)]
struct EagerPackage {
    #[allow(dead_code)]
    executable: bool,
    package_directory: Option<PackageDirectory>,
    cup: Option<CupData>,
    public_keys: PublicKeys,
}

#[derive(Debug)]
pub struct EagerPackageManager<T: Resolver> {
    packages: BTreeMap<UnpinnedAbsolutePackageUrl, EagerPackage>,
    package_resolver: T,
    data_proxy: Option<fio::DirectoryProxy>,
}

fn verify_cup_signature(
    cup_handler: &StandardCupv2Handler,
    cup: &CupData,
) -> Result<(), ParseCupResponseError> {
    let der_signature = DerSignature::from_bytes(&cup.signature)?;
    cup_handler
        .verify_response_with_signature(
            &der_signature,
            &cup.request,
            &cup.response,
            cup.key_id,
            &cup.nonce.into(),
        )
        .map_err(ParseCupResponseError::VerificationError)
}

impl<T: Resolver> EagerPackageManager<T> {
    pub async fn from_namespace(
        package_resolver: T,
        pkg_cache: cache::Client,
        data_proxy: Option<fio::DirectoryProxy>,
        cache_packages: &CachePackages,
    ) -> Result<Self, Error> {
        let config =
            EagerPackageConfigs::from_namespace().await.context("loading eager package config")?;
        Ok(Self::from_config(config, package_resolver, pkg_cache, data_proxy, cache_packages).await)
    }

    async fn from_config(
        config: EagerPackageConfigs,
        package_resolver: T,
        pkg_cache: cache::Client,
        data_proxy: Option<fio::DirectoryProxy>,
        cache_packages: &CachePackages,
    ) -> Self {
        let mut packages = config
            .packages
            .into_iter()
            .map(|EagerPackageConfig { url, executable, public_keys }| {
                (url, EagerPackage { executable, package_directory: None, cup: None, public_keys })
            })
            .collect();

        if let Some(ref data_proxy) = data_proxy {
            if let Err(e) = Self::load_persistent_eager_packages(&mut packages, &data_proxy).await {
                fx_log_err!("failed to load persistent eager packages: {:#}", anyhow!(e));
            }
        }

        for (url, package) in packages.iter_mut() {
            Self::get_package_directory_from_cache(url, package, &pkg_cache, cache_packages)
                .await
                .unwrap_or_else(|e| {
                    fx_log_err!("failed to resolve package directory: {:#}", anyhow!(e))
                });
        }

        Self { packages, package_resolver, data_proxy }
    }

    async fn load_persistent_eager_packages(
        packages: &mut BTreeMap<UnpinnedAbsolutePackageUrl, EagerPackage>,
        data_proxy: &fio::DirectoryProxy,
    ) -> Result<(), Error> {
        let file_proxy = match fuchsia_fs::directory::open_file(
            data_proxy,
            EAGER_PACKAGE_PERSISTENT_FIDL_NAME,
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        {
            Ok(proxy) => proxy,
            Err(fuchsia_fs::node::OpenError::OpenError(s)) if s == zx::Status::NOT_FOUND => {
                return Ok(())
            }
            Err(e) => Err(e).context("while opening eager_packages.pf")?,
        };

        let persistent_packages =
            fuchsia_fs::read_file_fidl::<PersistentEagerPackages>(&file_proxy)
                .await
                .context("while reading eager_packages.pf")?;
        for PersistentEagerPackage { url, cup, .. } in persistent_packages
            .packages
            .ok_or_else(|| anyhow!("PersistentEagerPackages does not contain `packages` field"))?
        {
            (|| {
                let url = url.ok_or_else(|| {
                    anyhow!("PersistentEagerPackage does not contain `url` field")
                })?;
                let cup: CupData = cup
                    .ok_or_else(|| anyhow!("PersistentEagerPackage does not contain `cup` field"))?
                    .try_into()?;
                let package = packages
                    .get_mut(&url.url.parse().with_context(|| format!("parsing '{}'", url.url))?)
                    .ok_or_else(|| anyhow!("unknown pkg url: {}", url.url))?;

                let cup_handler = StandardCupv2Handler::new(&package.public_keys);

                verify_cup_signature(&cup_handler, &cup)
                    .map_err(|e| anyhow!("could not verify cup signature {:?}", e))?;

                package.cup = Some(cup);
                Ok(())
            })()
            .unwrap_or_else(|e: Error| {
                fx_log_err!("failed to load persistent eager package: {:#}", anyhow!(e))
            })
        }
        Ok(())
    }

    async fn get_package_directory_from_cache(
        url: &UnpinnedAbsolutePackageUrl,
        package: &mut EagerPackage,
        pkg_cache: &cache::Client,
        cache_packages: &CachePackages,
    ) -> Result<(), Error> {
        let pinned_url = if let Some(cup) = &package.cup {
            let response = parse_omaha_response_from_cup(&cup)
                .with_context(|| format!("while parsing omaha response {:?}", cup.response))?;
            let pinned_url = response
                .apps
                .iter()
                .find_map(|app| {
                    app.update_check.as_ref().and_then(|uc| {
                        uc.get_all_full_urls().find_map(|u| {
                            PinnedAbsolutePackageUrl::parse(&u)
                                .ok()
                                .and_then(|u| (u.path() == url.path()).then_some(u))
                        })
                    })
                })
                .ok_or_else(|| {
                    anyhow!("could not find pinned url in CUP omaha response for {}", url)
                })?;

            Some(pinned_url)
        } else {
            // The config includes an eager package, but no CUP is persisted, try to load it
            // from cache packages.
            cache_packages.contents().find(|pinned_url| pinned_url.as_unpinned() == url).cloned()
        };

        if let Some(pinned_url) = pinned_url {
            let pkg_dir = Self::resolve_pinned_from_cache(&pkg_cache, pinned_url)
                .await
                .with_context(|| format!("while resolving eager package {} from cache", url))?;
            package.package_directory = Some(pkg_dir);
        }
        Ok(())
    }

    // Returns eager package directory.
    // Returns Ok(None) if that's not an eager package, or the package is pinned.
    pub fn get_package_dir(
        &self,
        url: &AbsolutePackageUrl,
    ) -> Result<Option<PackageDirectory>, Error> {
        let url = match url {
            AbsolutePackageUrl::Unpinned(unpinned) => unpinned,
            AbsolutePackageUrl::Pinned(_) => return Ok(None),
        };
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
        url: PinnedAbsolutePackageUrl,
    ) -> Result<(PackageDirectory, ResolutionContext), ResolvePinnedError> {
        let expected_hash = url.hash();
        let (pkg_dir, resolution_context) = package_resolver.resolve(url.into(), None).await?;
        let hash = pkg_dir.merkle_root().await?;
        if hash != expected_hash {
            return Err(ResolvePinnedError::HashMismatch(hash));
        }
        Ok((pkg_dir, resolution_context))
    }

    async fn resolve_pinned_from_cache(
        pkg_cache: &cache::Client,
        url: PinnedAbsolutePackageUrl,
    ) -> Result<PackageDirectory, Error> {
        let mut get = pkg_cache
            .get(BlobInfo { blob_id: url.hash().into(), length: 0 })
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

    async fn persist(
        &self,
        packages: &BTreeMap<UnpinnedAbsolutePackageUrl, EagerPackage>,
    ) -> Result<(), PersistError> {
        let data_proxy = self.data_proxy.as_ref().ok_or(PersistError::DataProxyNotAvailable)?;

        let packages = packages
            .iter()
            .map(|(url, package)| PersistentEagerPackage {
                url: Some(fpkg::PackageUrl { url: url.to_string() }),
                cup: package.cup.clone().map(Into::into),
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
                fuchsia_fs::write_file_fidl(&proxy, &mut packages)
                    .await
                    .with_context(|| format!("writing file: {}", temp_path))
            },
        )
        .await
        .map_err(PersistError::AtomicWrite)
    }

    async fn cup_write(
        &mut self,
        url: &fpkg::PackageUrl,
        cup: fpkg::CupData,
    ) -> Result<(), CupWriteError> {
        let cup_data: CupData = cup.try_into()?;
        let response = parse_omaha_response_from_cup(&cup_data)?;
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

        let pinned_url: PinnedAbsolutePackageUrl = url.url.parse()?;
        let mut packages = self.packages.clone();
        // Make sure the url is an eager package before trying to resolve it.
        let package = packages
            .iter_mut()
            .find(|(url, _package)| url.path() == pinned_url.path())
            .map(|(_url, package)| package)
            .ok_or_else(|| CupWriteError::UnknownURL(pinned_url.as_unpinned().clone()))?;
        let (pkg_dir, _resolution_context) =
            Self::resolve_pinned(&self.package_resolver, pinned_url).await?;
        package.package_directory = Some(pkg_dir);

        let cup_handler = StandardCupv2Handler::new(&package.public_keys);
        verify_cup_signature(&cup_handler, &cup_data)?;

        package.cup = Some(cup_data);

        self.persist(&packages).await?;
        // Only update self.packages if persist succeed, to prevent rollback after reboot.
        self.packages = packages;
        Ok(())
    }

    async fn cup_get_info(
        &self,
        url: &fpkg::PackageUrl,
    ) -> Result<(String, String), CupGetInfoError> {
        let pkg_url = UnpinnedAbsolutePackageUrl::parse(&url.url)?;
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
    Ok(omaha_client::protocol::response::parse_json_response(&cup.response)?)
}

#[derive(Debug, thiserror::Error)]
enum ParseCupResponseError {
    #[error("CUP data signature is invalid")]
    CupDataInvalidSignature(#[from] p256::ecdsa::Error),
    #[error("while validating CUP response")]
    VerificationError(#[from] CupVerificationError),
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
    Open(#[from] fuchsia_fs::node::OpenError),
    #[error("while writing persistent fidl")]
    AtomicWrite(#[source] anyhow::Error),
}

#[derive(Debug, thiserror::Error)]
enum CupWriteError {
    #[error("CUP data does not include a field")]
    Missing(#[from] CupMissingField),
    #[error("while parsing package URL")]
    ParseURL(#[from] fuchsia_url::errors::ParseError),
    #[error("URL is not a known eager package: {0}")]
    UnknownURL(UnpinnedAbsolutePackageUrl),
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
            CupWriteError::Missing(_) => WriteError::Verification,
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
    UnknownURL(UnpinnedAbsolutePackageUrl),
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

pub async fn run_cup_service<T: Resolver>(
    manager: Arc<Option<AsyncRwLock<EagerPackageManager<T>>>>,
    mut stream: CupRequestStream,
    mut cobalt_sender: ProtocolSender<MetricEvent>,
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

                cobalt_sender.send(
                    MetricEvent::builder(metrics::CUP_WRITE_METRIC_ID)
                        .with_event_codes(cup_write_result_to_event_code(&response))
                        .as_occurrence(1),
                );

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

                cobalt_sender.send(
                    MetricEvent::builder(metrics::CUP_GETINFO_METRIC_ID)
                        .with_event_codes(cup_getinfo_result_to_event_code(&response))
                        .as_occurrence(1),
                );

                responder.send(&mut response)?;
            }
        }
    }
    Ok(())
}

fn cup_write_result_to_event_code(
    result: &Result<(), WriteError>,
) -> metrics::CupWriteMetricDimensionResult {
    use metrics::CupWriteMetricDimensionResult as EventCodes;
    match result {
        Ok(_) => EventCodes::Success,
        Err(WriteError::UnknownUrl) => EventCodes::UnknownUrl,
        Err(WriteError::Verification) => EventCodes::Verification,
        Err(WriteError::Download) => EventCodes::Download,
        Err(WriteError::Storage) => EventCodes::Storage,
    }
}

fn cup_getinfo_result_to_event_code(
    result: &Result<(String, String), GetInfoError>,
) -> metrics::CupGetinfoMetricDimensionResult {
    use metrics::CupGetinfoMetricDimensionResult as EventCodes;
    match result {
        Ok(_) => EventCodes::Success,
        Err(GetInfoError::UnknownUrl) => EventCodes::UnknownUrl,
        Err(GetInfoError::Verification) => EventCodes::Verification,
        Err(GetInfoError::NotAvailable) => EventCodes::NotAvailable,
    }
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
        fidl_fuchsia_pkg_ext as pkg, fuchsia_async as fasync, fuchsia_zircon as zx,
        omaha_client::{
            cup_ecdsa::{
                test_support::{
                    make_default_public_key_for_test, make_default_public_key_id_for_test,
                    make_default_public_keys_for_test, make_expected_signature_for_test,
                    make_keys_for_test, make_public_keys_for_test,
                    make_standard_intermediate_for_test, RAW_PUBLIC_KEY_FOR_TEST,
                },
                Cupv2RequestHandler, PublicKeyAndId, PublicKeyId, PublicKeys,
            },
            protocol::request::Request,
        },
    };

    const TEST_URL: &str = "fuchsia-pkg://example.com/package";
    const TEST_HASH: &str = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    const TEST_PINNED_URL: &str = "fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    fn get_test_package_resolver() -> MockResolver {
        let pkg_dir = PackageDirectory::open_from_namespace().unwrap();
        MockResolver::new(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok((pkg_dir, pkg::ResolutionContext::new(vec![]))) }
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
        let proxy = fuchsia_fs::open_directory_in_namespace(
            dir.path().to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .unwrap();
        let pkg_dir = PackageDirectory::from_proxy(proxy);
        let package_resolver = MockResolver::new(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok((pkg_dir, pkg::ResolutionContext::new(vec![]))) }
        });
        (package_resolver, dir)
    }
    fn get_default_cup_response() -> Vec<u8> {
        get_cup_response("fuchsia-pkg://example.com/", format!("package?hash={TEST_HASH}"))
    }
    fn get_cup_response(url_codebase: impl AsRef<str>, package_name: impl AsRef<str>) -> Vec<u8> {
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
                  {"codebase": url_codebase.as_ref()},
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
                     "name": package_name.as_ref(),
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

    fn make_cup_data(cup_response: &[u8]) -> CupData {
        let (priv_key, public_key) = make_keys_for_test();
        let public_key_id: PublicKeyId = make_default_public_key_id_for_test();
        let public_keys = make_public_keys_for_test(public_key_id, public_key);
        let cup_handler = StandardCupv2Handler::new(&public_keys);
        let request = Request::default();
        let mut intermediate = make_standard_intermediate_for_test(request);
        let request_metadata = cup_handler.decorate_request(&mut intermediate).unwrap();
        let request_body = intermediate.serialize_body().unwrap();
        let expected_signature: Vec<u8> =
            make_expected_signature_for_test(&priv_key, &request_metadata, &cup_response);
        CupData::builder()
            .key_id(public_key_id)
            .nonce(request_metadata.nonce)
            .request(request_body)
            .response(cup_response)
            .signature(expected_signature)
            .build()
    }

    async fn write_persistent_fidl(
        data_proxy: &fio::DirectoryProxy,
        packages: impl IntoIterator<Item = (impl std::fmt::Display, CupData)>,
    ) {
        let file_proxy = fuchsia_fs::directory::open_file(
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
                        url: Some(fpkg::PackageUrl { url: url.to_string() }),
                        cup: Some(cup.into()),
                        ..PersistentEagerPackage::EMPTY
                    })
                    .collect(),
            ),
            ..PersistentEagerPackages::EMPTY
        };
        fuchsia_fs::write_file_fidl(&file_proxy, &mut packages).await.unwrap();
    }

    #[test]
    fn parse_eager_package_configs_json() {
        use std::convert::TryInto;

        let public_keys = PublicKeys {
            latest: PublicKeyAndId {
                id: 123.try_into().unwrap(),
                key: make_default_public_key_for_test(),
            },
            historical: vec![],
        };

        let json = serde_json::json!(
        {
            "packages":
            [
                {
                    "url": "fuchsia-pkg://example.com/package",
                    "executable": true,
                    "public_keys": {
                        "latest": {
                            "id": 123,
                            "key": RAW_PUBLIC_KEY_FOR_TEST,
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
                            "key": RAW_PUBLIC_KEY_FOR_TEST,
                        },
                        "historical": []
                    }
                },
                {
                    "url": "fuchsia-pkg://example.com/package3",
                    "public_keys": {
                        "latest": {
                            "id": 123,
                            "key": RAW_PUBLIC_KEY_FOR_TEST,
                        },
                        "historical": []
                    }
                }
            ]
        });
        assert_eq!(
            serde_json::from_value::<EagerPackageConfigs>(json).unwrap(),
            EagerPackageConfigs {
                packages: vec![
                    EagerPackageConfig {
                        url: "fuchsia-pkg://example.com/package".parse().unwrap(),
                        executable: true,
                        public_keys: public_keys.clone(),
                    },
                    EagerPackageConfig {
                        url: "fuchsia-pkg://example.com/package2".parse().unwrap(),
                        executable: false,
                        public_keys: public_keys.clone(),
                    },
                    EagerPackageConfig {
                        url: "fuchsia-pkg://example.com/package3".parse().unwrap(),
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
        let manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;

        assert!(manager.packages.is_empty());

        for url in [
            "fuchsia-pkg://example2.com/package",
            "fuchsia-pkg://example.com/package3",
            "fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        ] {
            assert_matches!(manager.get_package_dir(&url.parse().unwrap()), Ok(None));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_pinned_hash_mismatch() {
        let pinned_url = PinnedAbsolutePackageUrl::from_unpinned(
            TEST_URL.parse().unwrap(),
            TEST_HASH.parse().unwrap(),
        );
        assert_matches!(
            EagerPackageManager::resolve_pinned(&get_test_package_resolver(), pinned_url).await,
            Err(ResolvePinnedError::HashMismatch(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_persistent_eager_packages() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let url2 = UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![
                EagerPackageConfig {
                    url: url.clone(),
                    executable: true,
                    public_keys: make_default_public_keys_for_test(),
                },
                EagerPackageConfig {
                    url: url2.clone(),
                    executable: false,
                    public_keys: make_default_public_keys_for_test(),
                },
            ],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();

        let cup1_response: Vec<u8> = get_default_cup_response();
        let cup1: CupData = make_cup_data(&cup1_response);
        let cup2_response = get_cup_response(
            "fuchsia-pkg://example.com/",
            format!("package2?hash={}", "1".repeat(64)),
        );
        // this will fail to resolve because hash doesn't match
        let cup2: CupData = make_cup_data(&cup2_response);
        write_persistent_fidl(
            &data_proxy,
            [
                // packages can be out of order
                (url2.clone(), cup2.clone()),
                // bad packages won't break loading of other valid packages
                (
                    UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://unknown/package").unwrap(),
                    cup1.clone(),
                ),
                // If CupData is empty, we should skip and log the error but not crash or fail.
                (url.clone(), CupData::builder().build()),
                (url.clone(), cup1.clone()),
            ],
        )
        .await;
        let (manager, ()) = future::join(
            EagerPackageManager::from_config(
                config,
                package_resolver,
                pkg_cache,
                Some(data_proxy),
                &CachePackages::from_entries(vec![]),
            ),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        assert_matches!(
            manager
                .get_package_dir(&"fuchsia-pkg://example.com/non-eager-package".parse().unwrap()),
            Ok(None)
        );
        assert!(manager.packages[&url].package_directory.is_some());
        assert!(manager.get_package_dir(&url.clone().into()).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup1));
        assert!(manager.packages[&url2].package_directory.is_none());
        assert_matches!(manager.get_package_dir(&url2.clone().into()), Err(_));
        // cup is still loaded even if resolve fails
        assert_eq!(manager.packages[&url2].cup, Some(cup2));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_persistent_eager_packages_with_different_host_name_in_cup() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();

        let cup_response =
            get_cup_response("fuchsia-pkg://real.host.name/", format!("package?hash={TEST_HASH}"));
        let cup: CupData = make_cup_data(&cup_response);
        write_persistent_fidl(&data_proxy, [(url.clone(), cup.clone())]).await;
        let (manager, ()) = future::join(
            EagerPackageManager::from_config(
                config,
                package_resolver,
                pkg_cache,
                Some(data_proxy),
                &CachePackages::from_entries(vec![]),
            ),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        assert!(manager.packages[&url].package_directory.is_some());
        assert!(manager.get_package_dir(&url.clone().into()).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup));
        // `get_package_dir` still only accept non-rewritten url.
        assert_matches!(
            manager.get_package_dir(&"fuchsia-pkg://real.host.name/package".parse().unwrap()),
            Ok(None)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_persistent_eager_packages_signature_invalid() {
        // We intentionally stage a mismatch between the PublicKeys used
        // to configure the EagerPackageManager (latest.key_id = 777) and the
        // PublicKeys generated in make_cup_data() (latest.key_id = 123456789),
        // which are written to persistent fidl and which we attempt to validate
        // at startup.

        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();

        let mut public_keys = make_default_public_keys_for_test();
        public_keys.latest.id = 777;

        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true, public_keys }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();

        let cup: CupData = make_cup_data(&get_default_cup_response());
        write_persistent_fidl(&data_proxy, [(url.clone(), cup.clone())]).await;
        let (manager, ()) = future::join(
            EagerPackageManager::from_config(
                config,
                package_resolver,
                pkg_cache,
                Some(data_proxy),
                &CachePackages::from_entries(vec![]),
            ),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        // This fails to load, and we log "failed to load persistent eager
        // package: could not verify cup signature
        // VerificationError(SpecifiedPublicKeyIdMissing)".
        assert!(manager.get_package_dir(&url.clone().into()).is_err());
        assert!(manager.packages[&url].package_directory.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_data_fallback_to_cache() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let (manager, ()) = future::join(
            EagerPackageManager::from_config(
                config,
                package_resolver,
                pkg_cache,
                None,
                &CachePackages::from_entries(vec![TEST_PINNED_URL.parse().unwrap()]),
            ),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        assert!(manager.packages[&url].package_directory.is_some());
        assert!(manager.get_package_dir(&url.clone().into()).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_persistent_fidl_fallback_to_cache() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        write_persistent_fidl(&data_proxy, [] as [(String, CupData); 0]).await;

        let (manager, ()) = future::join(
            EagerPackageManager::from_config(
                config,
                package_resolver,
                pkg_cache,
                Some(data_proxy),
                &CachePackages::from_entries(vec![TEST_PINNED_URL.parse().unwrap()]),
            ),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        assert!(manager.packages[&url].package_directory.is_some());
        assert!(manager.get_package_dir(&url.clone().into()).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let mut manager = EagerPackageManager::from_config(
            config.clone(),
            package_resolver.clone(),
            pkg_cache.clone(),
            Some(Clone::clone(&data_proxy)),
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        manager
            .cup_write(&fpkg::PackageUrl { url: TEST_PINNED_URL.into() }, cup.clone().into())
            .await
            .unwrap();
        assert!(manager.packages[&url].package_directory.is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup.clone()));

        // create a new manager which should load the persisted cup data.
        let (manager2, ()) = future::join(
            EagerPackageManager::from_config(
                config,
                package_resolver,
                pkg_cache,
                Some(data_proxy),
                &CachePackages::from_entries(vec![]),
            ),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        assert!(manager2.packages[&url].package_directory.is_some());
        assert_eq!(manager2.packages[&url].cup, Some(cup));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_persist_fail() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        assert_matches!(
            manager.cup_write(&fpkg::PackageUrl { url: TEST_PINNED_URL.into() }, cup.into()).await,
            Err(CupWriteError::Persist(_))
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_omaha_response_different_url_hash() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        assert_matches!(
            manager
                .cup_write(&fpkg::PackageUrl { url: format!("{url}?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead") }, cup.into())
                .await,
            Err(CupWriteError::CupResponseURLNotFound)
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_omaha_response_different_url_host() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, _) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::open_directory_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let mut manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            Some(data_proxy),
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup_response =
            get_cup_response("fuchsia-pkg://real.host.name/", format!("package?hash={TEST_HASH}"));
        let cup: CupData = make_cup_data(&cup_response);

        manager
            .cup_write(
                &fpkg::PackageUrl {
                    url: format!("fuchsia-pkg://real.host.name/package?hash={TEST_HASH}"),
                },
                cup.clone().into(),
            )
            .await
            .unwrap();
        assert!(manager.packages[&url].package_directory.is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_unknown_url() {
        let url = UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        assert_matches!(
            manager.cup_write(&fpkg::PackageUrl { url: TEST_PINNED_URL.into() }, cup.into()).await,
            Err(CupWriteError::UnknownURL(_))
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        let (version, channel) =
            manager.cup_get_info(&fpkg::PackageUrl { url: TEST_URL.into() }).await.unwrap();
        assert_eq!(version, "1.2.3.4");
        assert_eq!(channel, "stable");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_not_available() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;
        assert_matches!(
            manager.cup_get_info(&fpkg::PackageUrl { url: TEST_URL.into() }).await,
            Err(CupGetInfoError::CupDataNotAvailable)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_unknown_url() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        assert_matches!(
            manager
                .cup_get_info(&fpkg::PackageUrl {
                    url: "fuchsia-pkg://example.com/package2".into()
                })
                .await,
            Err(CupGetInfoError::UnknownURL(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_omaha_response_different_url() {
        let url = UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
            }],
        };
        let package_resolver = get_test_package_resolver();
        let (pkg_cache, _) = get_mock_pkg_cache();

        let mut manager = EagerPackageManager::from_config(
            config,
            package_resolver,
            pkg_cache,
            None,
            &CachePackages::from_entries(vec![]),
        )
        .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        assert_matches!(
            manager
                .cup_get_info(&fpkg::PackageUrl {
                    url: "fuchsia-pkg://example.com/package2".into()
                })
                .await,
            Err(CupGetInfoError::CupResponseURLNotFound)
        );
    }
}
