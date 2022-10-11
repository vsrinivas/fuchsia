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
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    fuchsia_url::{AbsolutePackageUrl, Hash, PinnedAbsolutePackageUrl, UnpinnedAbsolutePackageUrl},
    fuchsia_zircon as zx,
    futures::prelude::*,
    omaha_client::{
        cup_ecdsa::{CupVerificationError, Cupv2Verifier, PublicKeys, StandardCupv2Handler},
        protocol::response::{App, Response},
    },
    p256::ecdsa::{signature::Signature, DerSignature},
    std::{
        collections::{BTreeMap, HashMap},
        convert::TryInto,
        str::FromStr,
        sync::Arc,
    },
    system_image::CachePackages,
    version::Version,
};

const EAGER_PACKAGE_PERSISTENT_FIDL_NAME: &str = "eager_packages.pf";

#[derive(Clone, Debug)]
struct EagerPackage {
    #[allow(dead_code)]
    executable: bool,
    package_directory_and_hash: Option<(PackageDirectory, Hash)>,
    cup: Option<CupData>,
    public_keys: PublicKeys,
    minimum_required_version: Version,
}

impl EagerPackage {
    fn load_cup(
        &self,
        url: &UnpinnedAbsolutePackageUrl,
        persistent_cup: &mut HashMap<String, CupData>,
    ) -> Result<CupData, LoadError> {
        let cup = persistent_cup.remove(&url.to_string()).ok_or(LoadError::NotAvailable)?;
        let cup_handler = StandardCupv2Handler::new(&self.public_keys);
        let () = verify_cup_signature(&cup_handler, &cup)?;
        Ok(cup)
    }

    async fn load_cup_and_package_directory(
        &mut self,
        url: &UnpinnedAbsolutePackageUrl,
        persistent_cup: &mut HashMap<String, CupData>,
        pkg_cache: &cache::Client,
        cache_packages: &CachePackages,
    ) -> Result<PackageSource, LoadError> {
        let pinned_url_in_cup;

        let (pinned_url, package_source) = match self.load_cup(url, persistent_cup) {
            Ok(cup) => {
                let response = parse_omaha_response_from_cup(&cup)?;
                let (app, pinned_url) = find_app_with_matching_url(&response, url)
                    .ok_or_else(|| LoadError::CupResponseURLNotFound(url.clone()))?;
                pinned_url_in_cup = pinned_url.clone();

                if app_version_too_old(&app, &self.minimum_required_version)? {
                    (
                        cache_packages
                            .find_unpinned_url(&url)
                            .ok_or(LoadError::RequestedVersionTooLow)?,
                        PackageSource::CachePackages,
                    )
                } else {
                    self.cup = Some(cup);

                    (&pinned_url_in_cup, PackageSource::Cup)
                }
            }
            Err(e) => {
                // The config includes an eager package, but no CUP is persisted, try to load it
                // from cache packages.
                (cache_packages.find_unpinned_url(url).ok_or(e)?, PackageSource::CachePackages)
            }
        };

        let pkg_dir = pkg_cache
            .get_already_cached(BlobInfo { blob_id: pinned_url.hash().into(), length: 0 })
            .await
            .map_err(LoadError::GetAlreadyCached)?;
        self.package_directory_and_hash = Some((pkg_dir, pinned_url.hash()));
        Ok(package_source)
    }
}

// Where the hash of the eager package comes from.
#[derive(Debug)]
enum PackageSource {
    Cup,
    CachePackages,
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

fn find_app_with_matching_url<'a>(
    response: &'a Response,
    url: &'a UnpinnedAbsolutePackageUrl,
) -> Option<(&'a App, PinnedAbsolutePackageUrl)> {
    for app in &response.apps {
        if let Some(uc) = &app.update_check {
            for u in uc.get_all_full_urls() {
                if let Ok(u) = PinnedAbsolutePackageUrl::parse(&u) {
                    if u.path() == url.path() {
                        return Some((&app, u));
                    }
                }
            }
        }
    }
    None
}

#[derive(Debug, thiserror::Error)]
enum VersionTooOldError {
    #[error("the manifest version is absent")]
    ManifestVersionAbsent,
    #[error("the manifest version could not be parsed")]
    ManifestVersionParseError(#[source] anyhow::Error),
}

fn app_version_too_old(
    app: &App,
    minimum_required_version: &Version,
) -> Result<bool, VersionTooOldError> {
    let manifest_version: Version = Version::from_str(
        app.get_manifest_version().ok_or(VersionTooOldError::ManifestVersionAbsent)?.as_str(),
    )
    .map_err(|e| VersionTooOldError::ManifestVersionParseError(e))?;
    Ok(manifest_version < *minimum_required_version)
}

#[derive(Debug)]
pub struct EagerPackageManager<T: Resolver> {
    packages: BTreeMap<UnpinnedAbsolutePackageUrl, EagerPackage>,
    package_resolver: T,
    data_proxy: Option<fio::DirectoryProxy>,
}

impl<T: Resolver> EagerPackageManager<T> {
    pub async fn from_namespace(
        package_resolver: T,
        pkg_cache: cache::Client,
        data_proxy: Option<fio::DirectoryProxy>,
        cache_packages: &CachePackages,
        cobalt_sender: ProtocolSender<MetricEvent>,
    ) -> Result<Self, Error> {
        let config =
            EagerPackageConfigs::from_namespace().await.context("loading eager package config")?;
        Ok(Self::from_config(
            config,
            package_resolver,
            pkg_cache,
            data_proxy,
            cache_packages,
            cobalt_sender,
        )
        .await)
    }

    async fn from_config(
        config: EagerPackageConfigs,
        package_resolver: T,
        pkg_cache: cache::Client,
        data_proxy: Option<fio::DirectoryProxy>,
        cache_packages: &CachePackages,
        mut cobalt_sender: ProtocolSender<MetricEvent>,
    ) -> Self {
        let (mut persistent_cup, storage_error) = match &data_proxy {
            Some(data_proxy) => {
                match Self::load_persistent_eager_packages_fidl(&data_proxy).await {
                    Ok(persistent_cup) => (persistent_cup, false),
                    Err(e) => {
                        fx_log_err!(
                            "failed to load persistent eager packages fidl: {:#}",
                            anyhow!(e)
                        );
                        (HashMap::new(), true)
                    }
                }
            }
            None => (HashMap::new(), true),
        };
        let mut packages = BTreeMap::new();
        for (i, EagerPackageConfig { url, executable, public_keys, minimum_required_version }) in
            config.packages.into_iter().enumerate()
        {
            let mut package = EagerPackage {
                executable,
                package_directory_and_hash: None,
                cup: None,
                public_keys,
                minimum_required_version,
            };
            let result = package
                .load_cup_and_package_directory(
                    &url,
                    &mut persistent_cup,
                    &pkg_cache,
                    cache_packages,
                )
                .await;

            cobalt_sender.send(
                MetricEvent::builder(metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID)
                    .with_event_codes((load_result_to_event_code(&result, storage_error), i as u32))
                    .as_occurrence(1),
            );

            if let Err(e) = result {
                fx_log_warn!("failed to load package directory: {:#}", anyhow!(e));
            }

            packages.insert(url, package);
        }

        if !persistent_cup.is_empty() {
            fx_log_warn!(
                "{EAGER_PACKAGE_PERSISTENT_FIDL_NAME} contains unknown eager package: {:?}",
                persistent_cup
            );
        }

        Self { packages, package_resolver, data_proxy }
    }

    async fn load_persistent_eager_packages_fidl(
        data_proxy: &fio::DirectoryProxy,
    ) -> Result<HashMap<String, CupData>, Error> {
        let file_proxy = match fuchsia_fs::directory::open_file(
            data_proxy,
            EAGER_PACKAGE_PERSISTENT_FIDL_NAME,
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        {
            Ok(proxy) => proxy,
            Err(fuchsia_fs::node::OpenError::OpenError(s)) if s == zx::Status::NOT_FOUND => {
                return Ok(HashMap::new())
            }
            Err(e) => Err(e).context("while opening eager_packages.pf")?,
        };

        let persistent_packages =
            fuchsia_fs::read_file_fidl::<PersistentEagerPackages>(&file_proxy)
                .await
                .context("while reading eager_packages.pf")?;

        Ok(persistent_packages
            .packages
            .ok_or_else(|| anyhow!("PersistentEagerPackages does not contain `packages` field"))?
            .into_iter()
            .filter_map(|PersistentEagerPackage { url, cup, .. }| {
                (|| {
                    let url = url.ok_or_else(|| {
                        anyhow!("PersistentEagerPackage does not contain `url` field")
                    })?;
                    let cup: CupData = cup
                        .ok_or_else(|| {
                            anyhow!("PersistentEagerPackage does not contain `cup` field")
                        })?
                        .try_into()?;
                    Ok((url.url, cup))
                })()
                .map_err(|e: Error| fx_log_err!("failed to load persistent eager package: {:#}", e))
                .ok()
            })
            .collect())
    }

    // Returns eager package directory.
    // Returns Ok(None) if that's not an eager package, or the package is pinned.
    pub fn get_package_dir(
        &self,
        url: &AbsolutePackageUrl,
    ) -> Result<Option<(PackageDirectory, Hash)>, Error> {
        let url = match url {
            AbsolutePackageUrl::Unpinned(unpinned) => unpinned,
            AbsolutePackageUrl::Pinned(_) => return Ok(None),
        };
        if let Some(eager_package) = self.packages.get(url) {
            if eager_package.package_directory_and_hash.is_some() {
                Ok(eager_package.package_directory_and_hash.clone())
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
        let app = response
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

        if app_version_too_old(&app, &package.minimum_required_version)? {
            return Err(CupWriteError::RequestedVersionTooLow);
        }

        let hash = pinned_url.hash();
        let (pkg_dir, _resolution_context) =
            Self::resolve_pinned(&self.package_resolver, pinned_url).await?;
        package.package_directory_and_hash = Some((pkg_dir, hash));

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
        let package = self
            .packages
            .get(&pkg_url)
            .ok_or_else(|| CupGetInfoError::UnknownURL(pkg_url.clone()))?;

        let (version, channel) = match package.cup.as_ref() {
            Some(cup) => {
                let response = parse_omaha_response_from_cup(&cup)?;
                let (app, _) = find_app_with_matching_url(&response, &pkg_url)
                    .ok_or(CupGetInfoError::CupResponseURLNotFound)?;
                (
                    app.get_manifest_version().ok_or(CupGetInfoError::CupDataMissingVersion)?,
                    app.cohort.name.clone().ok_or(CupGetInfoError::CupDataMissingChannel)?,
                )
            }
            None => {
                if package.package_directory_and_hash.is_none() {
                    return Err(CupGetInfoError::CupDataNotAvailable);
                }
                // If we're using the fallback version of a package (i.e. did not
                // recover from CUP but there is still a package directory), we're
                // using the minimum required version of a package rather than the
                // version present in the CUP response manifest.
                (package.minimum_required_version.to_string(), "".to_string())
            }
        };
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
enum LoadError {
    #[error("while parsing CUP omaha response")]
    ParseCupResponse(#[from] ParseCupResponseError),
    #[error("URL not found in CUP omaha response: {0}")]
    CupResponseURLNotFound(UnpinnedAbsolutePackageUrl),
    #[error("while getting an already cached package")]
    GetAlreadyCached(#[source] cache::GetAlreadyCachedError),
    #[error("the persisted eager package is not available")]
    NotAvailable,
    #[error("while checking minimum required version")]
    VersionTooOldError(#[from] VersionTooOldError),
    #[error("the version to be installed is lower than the minimum required version")]
    RequestedVersionTooLow,
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
    #[error("while checking minimum required version")]
    VersionTooOldError(#[from] VersionTooOldError),
    #[error("the version to be written is lower than the minimum required version")]
    RequestedVersionTooLow,
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
            CupWriteError::VersionTooOldError(_) => WriteError::Verification,
            CupWriteError::RequestedVersionTooLow => WriteError::Verification,
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

fn load_result_to_event_code(
    result: &Result<PackageSource, LoadError>,
    storage_error: bool,
) -> metrics::LoadPersistentEagerPackageMetricDimensionResult {
    use metrics::LoadPersistentEagerPackageMetricDimensionResult as EventCodes;
    match result {
        Ok(PackageSource::Cup) => EventCodes::Success,
        Ok(PackageSource::CachePackages) => EventCodes::SuccessFallback,
        Err(LoadError::NotAvailable) => {
            if storage_error {
                EventCodes::Storage
            } else {
                EventCodes::NotAvailable
            }
        }
        Err(LoadError::ParseCupResponse(_)) => EventCodes::Verification,
        Err(LoadError::CupResponseURLNotFound(_)) => EventCodes::Verification,
        Err(LoadError::VersionTooOldError(_)) => EventCodes::Verification,
        Err(LoadError::RequestedVersionTooLow) => EventCodes::Verification,
        Err(LoadError::GetAlreadyCached(_)) => EventCodes::Resolve,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            resolver_service::MockResolver,
            test_util::{get_mock_cobalt_sender, verify_cobalt_emits_event},
        },
        assert_matches::assert_matches,
        fidl_fuchsia_pkg::{
            BlobInfoIteratorRequest, NeededBlobsRequest, PackageCacheMarker, PackageCacheRequest,
            PackageCacheRequestStream,
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
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
            async move { Ok((pkg_dir, vec![].into())) }
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
        let proxy = fuchsia_fs::directory::open_in_namespace(
            dir.path().to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .unwrap();
        let pkg_dir = PackageDirectory::from_proxy(proxy);
        let package_resolver = MockResolver::new(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok((pkg_dir, vec![].into())) }
        });
        (package_resolver, dir)
    }
    fn get_default_cup_response() -> Vec<u8> {
        get_cup_response(
            "fuchsia-pkg://example.com/",
            format!("package?hash={TEST_HASH}"),
            "1.2.3.4",
        )
    }
    fn get_cup_response(
        url_codebase: impl AsRef<str>,
        package_name: impl AsRef<str>,
        manifest_version: impl AsRef<str>,
    ) -> Vec<u8> {
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
                "version": manifest_version.as_ref(),
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

    fn get_test_eager_package_config() -> EagerPackageConfigs {
        EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
                minimum_required_version: [1, 2, 3, 4].into(),
            }],
        }
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

    #[derive(Debug, derive_builder::Builder)]
    #[builder(pattern = "owned", build_fn(name = "build_inner"))]
    struct TestEagerPackageManager {
        #[builder(default = "get_test_eager_package_config()")]
        config: EagerPackageConfigs,
        #[builder(default = "get_test_package_resolver()")]
        package_resolver: MockResolver,
        #[builder(default = "get_mock_pkg_cache().0")]
        pkg_cache: cache::Client,
        #[builder(setter(strip_option), default)]
        data_proxy: Option<fio::DirectoryProxy>,
        #[builder(default = "CachePackages::from_entries(vec![])")]
        cache_packages: CachePackages,
        #[builder(default = "get_mock_cobalt_sender().0")]
        cobalt_sender: ProtocolSender<MetricEvent>,
    }

    impl TestEagerPackageManagerBuilder {
        async fn build(self) -> EagerPackageManager<MockResolver> {
            let TestEagerPackageManager {
                config,
                package_resolver,
                pkg_cache,
                data_proxy,
                cache_packages,
                cobalt_sender,
            } = self.build_inner().unwrap();
            EagerPackageManager::from_config(
                config,
                package_resolver,
                pkg_cache,
                data_proxy,
                &cache_packages,
                cobalt_sender,
            )
            .await
        }
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
                    },
                    "minimum_required_version": "1.2.3.4"
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
                    },
                    "minimum_required_version": "1.2.3.4"
                },
                {
                    "url": "fuchsia-pkg://example.com/package3",
                    "public_keys": {
                        "latest": {
                            "id": 123,
                            "key": RAW_PUBLIC_KEY_FOR_TEST,
                        },
                        "historical": []
                    },
                    "minimum_required_version": "1.2.3.4"
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
                        minimum_required_version: [1, 2, 3, 4].into(),
                    },
                    EagerPackageConfig {
                        url: "fuchsia-pkg://example.com/package2".parse().unwrap(),
                        executable: false,
                        public_keys: public_keys.clone(),
                        minimum_required_version: [1, 2, 3, 4].into(),
                    },
                    EagerPackageConfig {
                        url: "fuchsia-pkg://example.com/package3".parse().unwrap(),
                        executable: false,
                        public_keys,
                        minimum_required_version: [1, 2, 3, 4].into(),
                    },
                ]
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_eager_package_with_empty_config() {
        let config = EagerPackageConfigs { packages: Vec::new() };
        let manager = TestEagerPackageManagerBuilder::default().config(config).build().await;

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
                    minimum_required_version: [1, 2, 3, 4].into(),
                },
                EagerPackageConfig {
                    url: url2.clone(),
                    executable: false,
                    public_keys: make_default_public_keys_for_test(),
                    minimum_required_version: [1, 2, 3, 4].into(),
                },
            ],
        };
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();
        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::directory::open_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();

        let cup1_response: Vec<u8> = get_default_cup_response();
        let cup1: CupData = make_cup_data(&cup1_response);
        let cup2_response = get_cup_response(
            "fuchsia-pkg://example.com/",
            format!("package2?hash={}", "1".repeat(64)),
            "1.2.3.4",
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
            TestEagerPackageManagerBuilder::default()
                .config(config)
                .pkg_cache(pkg_cache)
                .data_proxy(data_proxy)
                .cobalt_sender(cobalt_sender)
                .build(),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
            (metrics::LoadPersistentEagerPackageMetricDimensionResult::Success, 0),
        );
        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
            (metrics::LoadPersistentEagerPackageMetricDimensionResult::Resolve, 1),
        );
        assert_matches!(
            manager
                .get_package_dir(&"fuchsia-pkg://example.com/non-eager-package".parse().unwrap()),
            Ok(None)
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_some());
        assert!(manager.get_package_dir(&url.clone().into()).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup1));
        assert!(manager.packages[&url2].package_directory_and_hash.is_none());
        assert_matches!(manager.get_package_dir(&url2.clone().into()), Err(_));
        // cup is still loaded even if resolve fails
        assert_eq!(manager.packages[&url2].cup, Some(cup2));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_persistent_eager_packages_with_different_host_name_in_cup() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::directory::open_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();

        let cup_response = get_cup_response(
            "fuchsia-pkg://real.host.name/",
            format!("package?hash={TEST_HASH}"),
            "1.2.3.4",
        );
        let cup: CupData = make_cup_data(&cup_response);
        write_persistent_fidl(&data_proxy, [(url.clone(), cup.clone())]).await;
        let (manager, ()) = future::join(
            TestEagerPackageManagerBuilder::default()
                .pkg_cache(pkg_cache)
                .data_proxy(data_proxy)
                .build(),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        assert!(manager.packages[&url].package_directory_and_hash.is_some());
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
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys,
                minimum_required_version: [1, 2, 3, 4].into(),
            }],
        };
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();
        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::directory::open_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();

        let cup: CupData = make_cup_data(&get_default_cup_response());
        write_persistent_fidl(&data_proxy, [(url.clone(), cup.clone())]).await;
        let (manager, ()) = future::join(
            TestEagerPackageManagerBuilder::default()
                .config(config)
                .pkg_cache(pkg_cache)
                .data_proxy(data_proxy)
                .cobalt_sender(cobalt_sender)
                .build(),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        // This fails to load, and we log "failed to load persistent eager
        // package: could not verify cup signature
        // VerificationError(SpecifiedPublicKeyIdMissing)".
        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
            (metrics::LoadPersistentEagerPackageMetricDimensionResult::Verification, 0),
        );
        assert!(manager.get_package_dir(&url.clone().into()).is_err());
        assert!(manager.packages[&url].package_directory_and_hash.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_persistent_eager_packages_not_available() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::directory::open_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let manager = TestEagerPackageManagerBuilder::default()
            .data_proxy(data_proxy)
            .cobalt_sender(cobalt_sender)
            .build()
            .await;
        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
            (metrics::LoadPersistentEagerPackageMetricDimensionResult::NotAvailable, 0),
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_none());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_persistent_eager_packages_storage_error() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

        let manager =
            TestEagerPackageManagerBuilder::default().cobalt_sender(cobalt_sender).build().await;
        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
            (metrics::LoadPersistentEagerPackageMetricDimensionResult::Storage, 0),
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_none());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_data_fallback_to_cache() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();
        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

        let (manager, ()) = future::join(
            TestEagerPackageManagerBuilder::default()
                .pkg_cache(pkg_cache)
                .cache_packages(CachePackages::from_entries(vec![TEST_PINNED_URL.parse().unwrap()]))
                .cobalt_sender(cobalt_sender)
                .build(),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
            (metrics::LoadPersistentEagerPackageMetricDimensionResult::SuccessFallback, 0),
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_some());
        assert!(manager.get_package_dir(&url.clone().into()).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_persistent_fidl_fallback_to_cache() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();
        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::directory::open_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        write_persistent_fidl(&data_proxy, [] as [(String, CupData); 0]).await;

        let (manager, ()) = future::join(
            TestEagerPackageManagerBuilder::default()
                .pkg_cache(pkg_cache)
                .data_proxy(data_proxy)
                .cache_packages(CachePackages::from_entries(vec![TEST_PINNED_URL.parse().unwrap()]))
                .cobalt_sender(cobalt_sender)
                .build(),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
            (metrics::LoadPersistentEagerPackageMetricDimensionResult::SuccessFallback, 0),
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_some());
        assert!(manager.get_package_dir(&url.clone().into()).unwrap().is_some());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::directory::open_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let mut manager = TestEagerPackageManagerBuilder::default()
            .package_resolver(package_resolver.clone())
            .pkg_cache(pkg_cache.clone())
            .data_proxy(Clone::clone(&data_proxy))
            .build()
            .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        manager
            .cup_write(&fpkg::PackageUrl { url: TEST_PINNED_URL.into() }, cup.clone().into())
            .await
            .unwrap();
        assert!(manager.packages[&url].package_directory_and_hash.is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup.clone()));

        // create a new manager which should load the persisted cup data.
        let (manager2, ()) = future::join(
            TestEagerPackageManagerBuilder::default()
                .package_resolver(package_resolver)
                .pkg_cache(pkg_cache)
                .data_proxy(data_proxy)
                .build(),
            handle_pkg_cache(pkg_cache_stream),
        )
        .await;
        assert!(manager2.packages[&url].package_directory_and_hash.is_some());
        assert_eq!(manager2.packages[&url].cup, Some(cup));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_persist_fail() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);

        let mut manager = TestEagerPackageManagerBuilder::default()
            .package_resolver(package_resolver)
            .build()
            .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        assert_matches!(
            manager.cup_write(&fpkg::PackageUrl { url: TEST_PINNED_URL.into() }, cup.into()).await,
            Err(CupWriteError::Persist(_))
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_none());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_omaha_response_different_url_hash() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);

        let mut manager = TestEagerPackageManagerBuilder::default()
            .package_resolver(package_resolver)
            .build()
            .await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        assert_matches!(
            manager
                .cup_write(&fpkg::PackageUrl { url: format!("{url}?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead") }, cup.into())
                .await,
            Err(CupWriteError::CupResponseURLNotFound)
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_omaha_response_different_url_host() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let (package_resolver, _test_dir) = get_test_package_resolver_with_hash(TEST_HASH);

        let data_dir = tempfile::tempdir().unwrap();
        let data_proxy = fuchsia_fs::directory::open_in_namespace(
            data_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let mut manager = TestEagerPackageManagerBuilder::default()
            .package_resolver(package_resolver)
            .data_proxy(data_proxy)
            .build()
            .await;
        let cup_response = get_cup_response(
            "fuchsia-pkg://real.host.name/",
            format!("package?hash={TEST_HASH}"),
            "1.2.3.4",
        );
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
        assert!(manager.packages[&url].package_directory_and_hash.is_some());
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
                minimum_required_version: [1, 2, 3, 4].into(),
            }],
        };
        let mut manager = TestEagerPackageManagerBuilder::default().config(config).build().await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        assert_matches!(
            manager.cup_write(&fpkg::PackageUrl { url: TEST_PINNED_URL.into() }, cup.into()).await,
            Err(CupWriteError::UnknownURL(_))
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_too_old() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();

        let mut manager = TestEagerPackageManagerBuilder::default().build().await;
        let cup: CupData = make_cup_data(&get_cup_response(
            "fuchsia-pkg://example.com/",
            format!("package?hash={TEST_HASH}"),
            "0.0.0.0",
        ));
        assert_matches!(
            manager
                .cup_write(&fpkg::PackageUrl { url: TEST_PINNED_URL.into() }, cup.clone().into())
                .await,
            Err(CupWriteError::RequestedVersionTooLow)
        );
        assert!(manager.packages[&url].package_directory_and_hash.is_none());
        assert_eq!(manager.packages[&url].cup, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let mut manager = TestEagerPackageManagerBuilder::default().build().await;
        let cup: CupData = make_cup_data(&get_default_cup_response());
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        let (version, channel) =
            manager.cup_get_info(&fpkg::PackageUrl { url: TEST_URL.into() }).await.unwrap();
        assert_eq!(version, "1.2.3.4");
        assert_eq!(channel, "stable");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_not_available() {
        let manager = TestEagerPackageManagerBuilder::default().build().await;
        assert_matches!(
            manager.cup_get_info(&fpkg::PackageUrl { url: TEST_URL.into() }).await,
            Err(CupGetInfoError::CupDataNotAvailable)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_unknown_url() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let mut manager = TestEagerPackageManagerBuilder::default().build().await;
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
    async fn test_cup_get_info_url_different_hostname() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let mut manager = TestEagerPackageManagerBuilder::default().build().await;
        let cup: CupData = make_cup_data(&get_cup_response(
            "fuchsia-pkg://real.host.name/",
            format!("package?hash={TEST_HASH}"),
            "1.2.3.4",
        ));
        manager.packages.get_mut(&url).unwrap().cup = Some(cup);
        let (version, channel) =
            manager.cup_get_info(&fpkg::PackageUrl { url: TEST_URL.into() }).await.unwrap();
        assert_eq!(version, "1.2.3.4");
        assert_eq!(channel, "stable");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_omaha_response_different_url() {
        let url = UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: url.clone(),
                executable: true,
                public_keys: make_default_public_keys_for_test(),
                minimum_required_version: [1, 2, 3, 4].into(),
            }],
        };
        let mut manager = TestEagerPackageManagerBuilder::default().config(config).build().await;
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

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_fallback() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();

        let cache_packages = CachePackages::from_entries(vec![TEST_PINNED_URL.parse().unwrap()]);
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let _handle = fasync::Task::spawn(handle_pkg_cache(pkg_cache_stream)).detach();

        let manager = TestEagerPackageManagerBuilder::default()
            .config(EagerPackageConfigs {
                packages: vec![EagerPackageConfig {
                    url,
                    executable: true,
                    public_keys: make_default_public_keys_for_test(),
                    minimum_required_version: [2, 0, 0, 0].into(),
                }],
            })
            .pkg_cache(pkg_cache)
            .cache_packages(cache_packages)
            .build()
            .await;

        let (version, channel) =
            manager.cup_get_info(&fpkg::PackageUrl { url: TEST_URL.into() }).await.unwrap();

        assert_eq!(version, "2.0.0.0");
        assert_eq!(channel, "");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_cup_and_package_directory() {
        let url = UnpinnedAbsolutePackageUrl::parse(TEST_URL).unwrap();
        let cache_packages = CachePackages::from_entries(vec![TEST_PINNED_URL.parse().unwrap()]);
        let empty_cache_packages = CachePackages::from_entries(vec![]);
        let (pkg_cache, pkg_cache_stream) = get_mock_pkg_cache();

        let _handle = fasync::Task::spawn(handle_pkg_cache(pkg_cache_stream)).detach();

        let mut ep = EagerPackage {
            executable: true,
            package_directory_and_hash: None,
            cup: None,
            public_keys: make_default_public_keys_for_test(),
            minimum_required_version: [1, 2, 3, 4].into(),
        };

        {
            // If version in the response is malformed, expect a parse error.
            let mut persistent_cup = HashMap::from([(
                TEST_URL.to_string(),
                make_cup_data(&get_cup_response(
                    "fuchsia-pkg://example.com/",
                    format!("package?hash={TEST_HASH}"),
                    /*manifest_version=*/ "",
                )),
            )]);

            assert_matches!(
                ep.load_cup_and_package_directory(
                    &url,
                    &mut persistent_cup,
                    &pkg_cache,
                    &cache_packages,
                )
                .await,
                Err(LoadError::VersionTooOldError(VersionTooOldError::ManifestVersionParseError(
                    _
                )))
            );
        }

        {
            // If the version in the response is too low, and we do not have the
            // package in cached_packages, report the initial error.
            let mut persistent_cup = HashMap::from([(
                TEST_URL.to_string(),
                make_cup_data(&get_cup_response(
                    "fuchsia-pkg://example.com/",
                    format!("package?hash={TEST_HASH}"),
                    /*manifest_version=*/ "1.2.3.3",
                )),
            )]);

            assert_matches!(
                ep.load_cup_and_package_directory(
                    &url,
                    &mut persistent_cup,
                    &pkg_cache,
                    &empty_cache_packages,
                )
                .await,
                Err(LoadError::RequestedVersionTooLow)
            );
        }

        {
            // If the version in the response is too low, and we do have the
            // package in cached_packages, return that.
            let mut persistent_cup = HashMap::from([(
                TEST_URL.to_string(),
                make_cup_data(&get_cup_response(
                    "fuchsia-pkg://example.com/",
                    format!("package?hash={TEST_HASH}"),
                    /*manifest_version=*/ "1.2.3.3",
                )),
            )]);

            assert_matches!(
                ep.load_cup_and_package_directory(
                    &url,
                    &mut persistent_cup,
                    &pkg_cache,
                    &cache_packages,
                )
                .await,
                Ok(PackageSource::CachePackages)
            );
        }

        {
            // If version in the response is the same as the minimum required
            // version (as set by the manifest_version), expect to load CUP
            // successfully.
            let mut persistent_cup = HashMap::from([(
                TEST_URL.to_string(),
                make_cup_data(&get_cup_response(
                    "fuchsia-pkg://example.com/",
                    format!("package?hash={TEST_HASH}"),
                    /*manifest_version=*/ "1.2.3.4",
                )),
            )]);

            assert_matches!(
                ep.load_cup_and_package_directory(
                    &url,
                    &mut persistent_cup,
                    &pkg_cache,
                    &cache_packages,
                )
                .await,
                Ok(PackageSource::Cup)
            );
        }

        {
            // If version in the response is greater than the minimum required
            // version (as set by the manifest_version), expect to load CUP
            // successfully.
            let mut persistent_cup = HashMap::from([(
                TEST_URL.to_string(),
                make_cup_data(&get_cup_response(
                    "fuchsia-pkg://example.com/",
                    format!("package?hash={TEST_HASH}"),
                    /*manifest_version=*/ "1.2.3.5",
                )),
            )]);

            assert_matches!(
                ep.load_cup_and_package_directory(
                    &url,
                    &mut persistent_cup,
                    &pkg_cache,
                    &cache_packages,
                )
                .await,
                Ok(PackageSource::Cup)
            );
        }
    }
}
