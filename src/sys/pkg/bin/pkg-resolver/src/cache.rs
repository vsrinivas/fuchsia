// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{queue, repository::Repository, repository_manager::Stats, TCP_KEEPALIVE_TIMEOUT},
    anyhow::anyhow,
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{LocalMirrorProxy, PackageCacheProxy},
    fidl_fuchsia_pkg_ext::{BlobId, MirrorConfig, RepositoryConfig},
    fuchsia_cobalt::CobaltSender,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_trace as trace,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon::Status,
    futures::{lock::Mutex as AsyncMutex, prelude::*, stream::FuturesUnordered},
    http_uri_ext::HttpUriExt as _,
    hyper::StatusCode,
    parking_lot::Mutex,
    pkgfs::install::BlobKind,
    std::{
        collections::HashSet,
        hash::Hash,
        sync::{
            atomic::{AtomicBool, AtomicU64, Ordering},
            Arc,
        },
        time::Duration,
    },
    tuf::metadata::TargetPath,
};

mod base_package_index;
pub use base_package_index::BasePackageIndex;

mod inspect;
mod resume;
mod retry;

pub type BlobFetcher = queue::WorkSender<BlobId, FetchBlobContext, Result<(), Arc<FetchError>>>;

/// Root of typesafe builder for BlobFetchParams.
#[derive(Clone, Copy, Debug)]
pub struct BlobFetchParamsBuilderNeedsHeaderNetworkTimeout;

impl BlobFetchParamsBuilderNeedsHeaderNetworkTimeout {
    pub fn header_network_timeout(
        self,
        header_network_timeout: Duration,
    ) -> BlobFetchParamsBuilderNeedsBodyNetworkTimeout {
        BlobFetchParamsBuilderNeedsBodyNetworkTimeout { header_network_timeout }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct BlobFetchParamsBuilderNeedsBodyNetworkTimeout {
    header_network_timeout: Duration,
}

impl BlobFetchParamsBuilderNeedsBodyNetworkTimeout {
    pub fn body_network_timeout(
        self,
        body_network_timeout: Duration,
    ) -> BlobFetchParamsBuilderNeedsDownloadResumptionAttemptsLimit {
        BlobFetchParamsBuilderNeedsDownloadResumptionAttemptsLimit {
            header_network_timeout: self.header_network_timeout,
            body_network_timeout,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct BlobFetchParamsBuilderNeedsDownloadResumptionAttemptsLimit {
    header_network_timeout: Duration,
    body_network_timeout: Duration,
}

impl BlobFetchParamsBuilderNeedsDownloadResumptionAttemptsLimit {
    pub fn download_resumption_attempts_limit(
        self,
        download_resumption_attempts_limit: u64,
    ) -> BlobFetchParams {
        BlobFetchParams {
            header_network_timeout: self.header_network_timeout,
            body_network_timeout: self.body_network_timeout,
            download_resumption_attempts_limit: download_resumption_attempts_limit,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct BlobFetchParams {
    header_network_timeout: Duration,
    body_network_timeout: Duration,
    download_resumption_attempts_limit: u64,
}

impl BlobFetchParams {
    pub fn builder() -> BlobFetchParamsBuilderNeedsHeaderNetworkTimeout {
        BlobFetchParamsBuilderNeedsHeaderNetworkTimeout
    }

    pub fn header_network_timeout(&self) -> Duration {
        self.header_network_timeout
    }

    pub fn body_network_timeout(&self) -> Duration {
        self.body_network_timeout
    }

    pub fn download_resumption_attempts_limit(&self) -> u64 {
        self.download_resumption_attempts_limit
    }
}

/// Provides access to the package cache components.
#[derive(Clone)]
pub struct PackageCache {
    cache: PackageCacheProxy,
    pkgfs_install: pkgfs::install::Client,
    pkgfs_needs: pkgfs::needs::Client,
}

impl PackageCache {
    /// Constructs a new [`PackageCache`].
    pub fn new(
        cache: PackageCacheProxy,
        pkgfs_install: pkgfs::install::Client,
        pkgfs_needs: pkgfs::needs::Client,
    ) -> Self {
        Self { cache, pkgfs_install, pkgfs_needs }
    }

    /// Open the requested package by merkle root using the given selectors, serving the package
    /// directory on the given directory request on success.
    pub async fn open(
        &self,
        merkle: BlobId,
        selectors: &[String],
        dir_request: ServerEnd<DirectoryMarker>,
    ) -> Result<(), PackageOpenError> {
        let fut = self.cache.open(
            &mut merkle.into(),
            &mut selectors.iter().map(|s| s.as_str()),
            dir_request,
        );
        match fut.await?.map_err(Status::from_raw) {
            Ok(()) => Ok(()),
            Err(Status::NOT_FOUND) => Err(PackageOpenError::NotFound),
            Err(status) => Err(PackageOpenError::UnexpectedStatus(status)),
        }
    }

    /// Check to see if a package with the given merkle root exists and is readable.
    pub async fn package_exists(&self, merkle: BlobId) -> Result<bool, PackageOpenError> {
        let (_dir, server_end) = fidl::endpoints::create_proxy()?;
        let selectors = vec![];
        match self.open(merkle, &selectors, server_end).await {
            Ok(()) => Ok(true),
            Err(PackageOpenError::NotFound) => Ok(false),
            Err(e) => Err(e),
        }
    }

    /// Loads the base package index from pkg-cache.
    pub async fn base_package_index(&self) -> Result<BasePackageIndex, anyhow::Error> {
        BasePackageIndex::from_proxy(self.cache.clone()).await
    }

    /// Create a new blob with the given install intent.
    ///
    /// Returns None if the blob already exists and is readable.
    async fn create_blob(
        &self,
        merkle: BlobId,
        blob_kind: BlobKind,
    ) -> Result<
        Option<(pkgfs::install::Blob<pkgfs::install::NeedsTruncate>, pkgfs::install::BlobCloser)>,
        pkgfs::install::BlobCreateError,
    > {
        match self.pkgfs_install.create_blob(merkle.into(), blob_kind).await {
            Ok((file, closer)) => Ok(Some((file, closer))),
            Err(pkgfs::install::BlobCreateError::AlreadyExists) => Ok(None),
            Err(e) => Err(e),
        }
    }

    /// Returns a stream of chunks of blobs that are needed to resolve the package specified by
    /// `pkg_merkle` provided that the `pkg_merkle` blob has previously been written to
    /// /pkgfs/install/pkg/. The package should be available in /pkgfs/versions when this stream
    /// terminates without error.
    fn list_needs(
        &self,
        pkg_merkle: BlobId,
    ) -> impl Stream<Item = Result<HashSet<BlobId>, pkgfs::needs::ListNeedsError>> + '_ {
        self.pkgfs_needs
            .list_needs(pkg_merkle.into())
            .map(|item| item.map(|needs| needs.into_iter().map(Into::into).collect()))
    }
}

#[derive(Debug, thiserror::Error)]
pub enum PackageOpenError {
    #[error("fidl error")]
    Fidl(#[from] fidl::Error),

    #[error("package not found")]
    NotFound,

    #[error("package cache returned unexpected status: {0}")]
    UnexpectedStatus(Status),
}

impl From<&PackageOpenError> for Status {
    fn from(x: &PackageOpenError) -> Self {
        match x {
            PackageOpenError::NotFound => Status::NOT_FOUND,
            _ => Status::INTERNAL,
        }
    }
}

pub async fn cache_package<'a>(
    repo: Arc<AsyncMutex<Repository>>,
    config: &'a RepositoryConfig,
    url: &'a PkgUrl,
    cache: &'a PackageCache,
    blob_fetcher: &'a BlobFetcher,
    cobalt_sender: CobaltSender,
) -> Result<BlobId, CacheError> {
    let (merkle, size) =
        merkle_for_url(repo, url, cobalt_sender).await.map_err(CacheError::MerkleFor)?;
    // If a merkle pin was specified, use it, but only after having verified that the name and
    // variant exist in the TUF repo.  Note that this doesn't guarantee that the merkle pinned
    // package ever actually existed in the repo or that the merkle pin refers to the named
    // package.
    let (merkle, size) = if let Some(merkle_pin) = url.package_hash() {
        (BlobId::from(*merkle_pin), None)
    } else {
        (merkle, Some(size))
    };

    // If the package already exists, we are done.
    if cache.package_exists(merkle).await.unwrap_or_else(|e| {
        fx_log_err!(
            "unable to check if {} is already cached, assuming it isn't: {:#}",
            url,
            anyhow!(e)
        );
        false
    }) {
        return Ok(merkle);
    }

    let mirrors = config.mirrors().to_vec().into();

    // Fetch the meta.far.
    blob_fetcher
        .push(
            merkle,
            FetchBlobContext {
                blob_kind: BlobKind::Package,
                mirrors: Arc::clone(&mirrors),
                expected_len: size,
                use_local_mirror: config.use_local_mirror(),
            },
        )
        .await
        .expect("processor exists")
        .map_err(|e| CacheError::FetchMetaFar(e, merkle))?;

    cache
        .list_needs(merkle)
        .err_into::<CacheError>()
        .try_for_each(|needs| {
            // Fetch the blobs with some amount of concurrency.
            fx_log_info!("Fetching blobs for {}: {:#?}", url, needs);
            blob_fetcher
                .push_all(needs.into_iter().map(|need| {
                    (
                        need,
                        FetchBlobContext {
                            blob_kind: BlobKind::Data,
                            mirrors: Arc::clone(&mirrors),
                            expected_len: None,
                            use_local_mirror: config.use_local_mirror(),
                        },
                    )
                }))
                .collect::<FuturesUnordered<_>>()
                .map(|res| res.expect("processor exists"))
                .try_collect::<()>()
                .map_err(|e| CacheError::FetchContentBlob(e, merkle))
        })
        .await?;

    Ok(merkle)
}

#[derive(Debug, thiserror::Error)]
pub enum CacheError {
    #[error("fidl error")]
    Fidl(#[from] fidl::Error),

    #[error("while looking up merkle root for package")]
    MerkleFor(#[source] MerkleForError),

    #[error("while listing needed blobs for package")]
    ListNeeds(#[from] pkgfs::needs::ListNeedsError),

    #[error("while fetching the meta.far: {1}")]
    FetchMetaFar(#[source] Arc<FetchError>, BlobId),

    #[error("while fetching content blob for meta.far {1}")]
    FetchContentBlob(#[source] Arc<FetchError>, BlobId),
}

pub(crate) trait ToResolveStatus {
    fn to_resolve_status(&self) -> Status;
}

// From resolver.fidl:
// * `ZX_ERR_ACCESS_DENIED` if the resolver does not have permission to fetch a package blob.
// * `ZX_ERR_IO` if there is some other unspecified error during I/O.
// * `ZX_ERR_NOT_FOUND` if the package or a package blob does not exist.
// * `ZX_ERR_NO_SPACE` if there is no space available to store the package.
// * `ZX_ERR_UNAVAILABLE` if the resolver is currently unable to fetch a package blob.
impl ToResolveStatus for CacheError {
    fn to_resolve_status(&self) -> Status {
        match self {
            CacheError::Fidl(_) => Status::IO,
            CacheError::MerkleFor(err) => err.to_resolve_status(),
            CacheError::ListNeeds(err) => err.to_resolve_status(),
            CacheError::FetchMetaFar(err, ..) => err.to_resolve_status(),
            CacheError::FetchContentBlob(err, _) => err.to_resolve_status(),
        }
    }
}
impl ToResolveStatus for MerkleForError {
    fn to_resolve_status(&self) -> Status {
        match self {
            MerkleForError::NotFound => Status::NOT_FOUND,
            MerkleForError::InvalidTargetPath(_) => Status::INTERNAL,
            // FIXME(42326) when tuf::Error gets an HTTP error variant, this should be mapped to Status::UNAVAILABLE
            MerkleForError::FetchTargetDescription(..) => Status::INTERNAL,
            MerkleForError::NoCustomMetadata => Status::INTERNAL,
            MerkleForError::SerdeError(_) => Status::INTERNAL,
        }
    }
}
impl ToResolveStatus for pkgfs::needs::ListNeedsError {
    fn to_resolve_status(&self) -> Status {
        match self {
            pkgfs::needs::ListNeedsError::OpenDir(_) => Status::IO,
            pkgfs::needs::ListNeedsError::ReadDir(_) => Status::IO,
            pkgfs::needs::ListNeedsError::ParseError(_) => Status::INTERNAL,
        }
    }
}
impl ToResolveStatus for pkgfs::install::BlobTruncateError {
    fn to_resolve_status(&self) -> Status {
        match self {
            pkgfs::install::BlobTruncateError::Fidl(_) => Status::IO,
            pkgfs::install::BlobTruncateError::NoSpace => Status::NO_SPACE,
            pkgfs::install::BlobTruncateError::UnexpectedResponse(_) => Status::IO,
        }
    }
}
impl ToResolveStatus for pkgfs::install::BlobWriteError {
    fn to_resolve_status(&self) -> Status {
        match self {
            pkgfs::install::BlobWriteError::Fidl(_) => Status::IO,
            pkgfs::install::BlobWriteError::Overwrite => Status::IO,
            pkgfs::install::BlobWriteError::Corrupt => Status::IO,
            pkgfs::install::BlobWriteError::NoSpace => Status::NO_SPACE,
            pkgfs::install::BlobWriteError::UnexpectedResponse(_) => Status::IO,
        }
    }
}
impl ToResolveStatus for FetchError {
    fn to_resolve_status(&self) -> Status {
        use FetchError::*;
        match self {
            CreateBlob(_) => Status::IO,
            BadHttpStatus { code: hyper::StatusCode::UNAUTHORIZED, .. } => Status::ACCESS_DENIED,
            BadHttpStatus { code: hyper::StatusCode::FORBIDDEN, .. } => Status::ACCESS_DENIED,
            BadHttpStatus { .. } => Status::UNAVAILABLE,
            ContentLengthMismatch { .. } => Status::UNAVAILABLE,
            UnknownLength { .. } => Status::UNAVAILABLE,
            BlobTooSmall { .. } => Status::UNAVAILABLE,
            BlobTooLarge { .. } => Status::UNAVAILABLE,
            Hyper { .. } => Status::UNAVAILABLE,
            Http { .. } => Status::UNAVAILABLE,
            Truncate(e) => e.to_resolve_status(),
            Write(e) => e.to_resolve_status(),
            NoMirrors => Status::INTERNAL,
            BlobUrl(_) => Status::INTERNAL,
            FidlError(_) => Status::INTERNAL,
            IoError(_) => Status::IO,
            LocalMirror(_) => Status::INTERNAL,
            NoBlobSource { .. } => Status::INTERNAL,
            ConflictingBlobSources => Status::INTERNAL,
            BlobHeaderTimeout { .. } => Status::UNAVAILABLE,
            BlobBodyTimeout { .. } => Status::UNAVAILABLE,
            ExpectedHttpStatus206 { .. } => Status::UNAVAILABLE,
            MissingContentRangeHeader { .. } => Status::UNAVAILABLE,
            MalformedContentRangeHeader { .. } => Status::UNAVAILABLE,
            InvalidContentRangeHeader { .. } => Status::UNAVAILABLE,
            ExceededResumptionAttemptLimit { .. } => Status::UNAVAILABLE,
            ContentLengthContentRangeMismatch { .. } => Status::UNAVAILABLE,
        }
    }
}

impl From<&MerkleForError> for metrics::MerkleForUrlMetricDimensionResult {
    fn from(e: &MerkleForError) -> metrics::MerkleForUrlMetricDimensionResult {
        match e {
            MerkleForError::NotFound => metrics::MerkleForUrlMetricDimensionResult::NotFound,
            MerkleForError::FetchTargetDescription(..) => {
                metrics::MerkleForUrlMetricDimensionResult::TufError
            }
            MerkleForError::InvalidTargetPath(_) => {
                metrics::MerkleForUrlMetricDimensionResult::InvalidTargetPath
            }
            MerkleForError::NoCustomMetadata => {
                metrics::MerkleForUrlMetricDimensionResult::NoCustomMetadata
            }
            MerkleForError::SerdeError(_) => metrics::MerkleForUrlMetricDimensionResult::SerdeError,
        }
    }
}

pub async fn merkle_for_url<'a>(
    repo: Arc<AsyncMutex<Repository>>,
    url: &'a PkgUrl,
    mut cobalt_sender: CobaltSender,
) -> Result<(BlobId, u64), MerkleForError> {
    let target_path = TargetPath::new(format!("{}/{}", url.name(), url.variant().unwrap_or("0")))
        .map_err(MerkleForError::InvalidTargetPath)?;
    let mut repo = repo.lock().await;
    let res = repo.get_merkle_at_path(&target_path).await;
    cobalt_sender.log_event_count(
        metrics::MERKLE_FOR_URL_METRIC_ID,
        match &res {
            Ok(_) => metrics::MerkleForUrlMetricDimensionResult::Success,
            Err(res) => res.into(),
        },
        0,
        1,
    );
    res.map(|custom| (custom.merkle(), custom.size()))
}

#[derive(Debug, thiserror::Error)]
pub enum MerkleForError {
    #[error("the package was not found in the repository")]
    NotFound,

    #[error("unexpected tuf error when fetching target description for {0:?}")]
    FetchTargetDescription(String, #[source] tuf::error::Error),

    #[error("the target path is not safe")]
    InvalidTargetPath(#[source] tuf::error::Error),

    #[error("the target description does not have custom metadata")]
    NoCustomMetadata,

    #[error("serde value could not be converted")]
    SerdeError(#[source] serde_json::Error),
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct FetchBlobContext {
    blob_kind: BlobKind,
    mirrors: Arc<[MirrorConfig]>,
    expected_len: Option<u64>,
    use_local_mirror: bool,
}

impl queue::TryMerge for FetchBlobContext {
    fn try_merge(&mut self, other: Self) -> Result<(), Self> {
        // Unmergeable if both contain different expected lengths. One of these instances will
        // fail, but we can't know which one here.
        let expected_len = match (self.expected_len, other.expected_len) {
            (Some(x), None) | (None, Some(x)) => Some(x),
            (None, None) => None,
            (Some(x), Some(y)) if x == y => Some(x),
            _ => return Err(other),
        };

        // Installing a blob as a package will fulfill any pending needs of that blob as a data
        // blob as well, so upgrade Data to Package.
        let blob_kind =
            if self.blob_kind == BlobKind::Package || other.blob_kind == BlobKind::Package {
                BlobKind::Package
            } else {
                BlobKind::Data
            };

        // For now, don't attempt to merge mirrors, but do merge these contexts if the mirrors are
        // equivalent.
        if self.mirrors != other.mirrors {
            return Err(other);
        }

        // Contexts are mergeable, apply the merged state.
        self.expected_len = expected_len;
        self.blob_kind = blob_kind;
        Ok(())
    }
}

pub fn make_blob_fetch_queue(
    node: fuchsia_inspect::Node,
    cache: PackageCache,
    max_concurrency: usize,
    stats: Arc<Mutex<Stats>>,
    cobalt_sender: CobaltSender,
    local_mirror_proxy: Option<LocalMirrorProxy>,
    blob_fetch_params: BlobFetchParams,
) -> (impl Future<Output = ()>, BlobFetcher) {
    let http_client = Arc::new(fuchsia_hyper::new_https_client_from_tcp_options(
        fuchsia_hyper::TcpOptions::keepalive_timeout(TCP_KEEPALIVE_TIMEOUT),
    ));
    let inspect = inspect::BlobFetcher::from_node_and_params(node, &blob_fetch_params);

    let (blob_fetch_queue, blob_fetcher) =
        queue::work_queue(max_concurrency, move |merkle: BlobId, context: FetchBlobContext| {
            let inspect = inspect.fetch(&merkle);
            let http_client = Arc::clone(&http_client);
            let cache = cache.clone();
            let stats = Arc::clone(&stats);
            let cobalt_sender = cobalt_sender.clone();
            let local_mirror_proxy = local_mirror_proxy.clone();

            async move {
                let res = fetch_blob(
                    inspect,
                    &http_client,
                    cache,
                    stats,
                    cobalt_sender,
                    merkle,
                    context,
                    local_mirror_proxy.as_ref(),
                    blob_fetch_params,
                )
                .map_err(Arc::new)
                .await;
                res
            }
        });

    (blob_fetch_queue.into_future(), blob_fetcher)
}

async fn fetch_blob(
    inspect: inspect::NeedsRemoteType,
    http_client: &fuchsia_hyper::HttpsClient,
    cache: PackageCache,
    stats: Arc<Mutex<Stats>>,
    cobalt_sender: CobaltSender,
    merkle: BlobId,
    context: FetchBlobContext,
    local_mirror_proxy: Option<&LocalMirrorProxy>,
    blob_fetch_params: BlobFetchParams,
) -> Result<(), FetchError> {
    let use_remote_mirror = context.mirrors.len() != 0;
    let use_local_mirror = context.use_local_mirror;

    match (use_remote_mirror, use_local_mirror, local_mirror_proxy) {
        (true, true, _) => Err(FetchError::ConflictingBlobSources),
        (false, true, Some(local_mirror)) => {
            trace::duration_begin!("app", "fetch_blob_local", "merkle" => merkle.to_string().as_str());
            let res = fetch_blob_local(
                inspect.local_mirror(),
                local_mirror,
                merkle,
                context.blob_kind,
                context.expected_len,
                &cache,
            )
            .await;
            trace::duration_end!("app", "fetch_blob_local", "result" => format!("{:?}", res).as_str());
            res
        }
        (true, false, _) => {
            trace::duration_begin!("app", "fetch_blob_http", "merkle" => merkle.to_string().as_str());
            let res = fetch_blob_http(
                inspect.http(),
                http_client,
                &context.mirrors,
                merkle,
                context.blob_kind,
                context.expected_len,
                blob_fetch_params,
                &cache,
                stats,
                cobalt_sender,
            )
            .await;
            trace::duration_end!("app", "fetch_blob_http", "result" => format!("{:?}", res).as_str());
            res
        }
        (use_remote_mirror, use_local_mirror, local_mirror) => Err(FetchError::NoBlobSource {
            use_remote_mirror,
            use_local_mirror,
            allow_local_mirror: local_mirror.is_some(),
        }),
    }
}

#[derive(Default)]
struct FetchStats {
    // How many times the blob download was resumed, e.g. with Http Range requests
    resumptions: AtomicU64,
}

impl FetchStats {
    fn resume(&self) {
        self.resumptions.fetch_add(1, Ordering::SeqCst);
    }

    fn resumptions(&self) -> u64 {
        self.resumptions.load(Ordering::SeqCst)
    }
}

async fn fetch_blob_http(
    inspect: inspect::NeedsMirror,
    client: &fuchsia_hyper::HttpsClient,
    mirrors: &[MirrorConfig],
    merkle: BlobId,
    blob_kind: BlobKind,
    expected_len: Option<u64>,
    blob_fetch_params: BlobFetchParams,
    cache: &PackageCache,
    stats: Arc<Mutex<Stats>>,
    cobalt_sender: CobaltSender,
) -> Result<(), FetchError> {
    // TODO try the other mirrors depending on the errors encountered trying this one.
    let blob_mirror_url = if let Some(mirror) = mirrors.get(0) {
        mirror.blob_mirror_url().to_owned()
    } else {
        return Err(FetchError::NoMirrors);
    };
    let mirror_stats = stats.lock().for_mirror(blob_mirror_url.to_string());
    let blob_url = make_blob_url(blob_mirror_url, &merkle).map_err(|e| FetchError::BlobUrl(e))?;
    let inspect = inspect.mirror(&blob_url.to_string());
    let flaked = Arc::new(AtomicBool::new(false));

    fuchsia_backoff::retry_or_first_error(retry::blob_fetch(), || {
        let flaked = Arc::clone(&flaked);
        let mirror_stats = &mirror_stats;
        let mut cobalt_sender = cobalt_sender.clone();
        // TODO(fxbug.dev/71333) don't use .inspect so this doesn't need to be in an Arc.
        let fetch_stats = Arc::new(FetchStats::default());
        let fetch_stats_clone = Arc::clone(&fetch_stats);
        let inspect = &inspect;
        let blob_url = &blob_url;

        async move {
            let inspect = inspect.attempt();
            inspect.state(inspect::Http::CreateBlob);
            if let Some((blob, blob_closer)) =
                cache.create_blob(merkle, blob_kind).await.map_err(FetchError::CreateBlob)?
            {
                inspect.state(inspect::Http::DownloadBlob);
                let res = download_blob(
                    &inspect,
                    client,
                    &blob_url,
                    expected_len,
                    blob,
                    blob_fetch_params,
                    fetch_stats_clone,
                )
                .await;
                inspect.state(inspect::Http::CloseBlob);
                blob_closer.close().await;
                res?;
            }

            Ok(())
        }
        .inspect(move |res| match res.as_ref().map_err(FetchError::kind) {
            Err(FetchErrorKind::NetworkRateLimit) => {
                mirror_stats.network_rate_limits().increment();
            }
            Err(FetchErrorKind::Network) => {
                flaked.store(true, Ordering::SeqCst);
            }
            Err(FetchErrorKind::Other) => {}
            Ok(()) => {
                if flaked.load(Ordering::SeqCst) {
                    mirror_stats.network_blips().increment();
                }
            }
        })
        .inspect(move |res| {
            let result_event_code = match res {
                Ok(()) => metrics::FetchBlobMetricDimensionResult::Success,
                Err(e) => e.into(),
            };
            let resumed_event_code = if fetch_stats.resumptions() != 0 {
                metrics::FetchBlobMetricDimensionResumed::True
            } else {
                metrics::FetchBlobMetricDimensionResumed::False
            };
            cobalt_sender.log_event_count(
                metrics::FETCH_BLOB_METRIC_ID,
                (result_event_code, resumed_event_code),
                0,
                1,
            );
        })
    })
    .await
}

async fn fetch_blob_local(
    inspect: inspect::TriggerAttempt<inspect::LocalMirror>,
    local_mirror: &LocalMirrorProxy,
    merkle: BlobId,
    blob_kind: BlobKind,
    expected_len: Option<u64>,
    cache: &PackageCache,
) -> Result<(), FetchError> {
    let inspect = inspect.attempt();
    inspect.state(inspect::LocalMirror::CreateBlob);
    if let Some((blob, blob_closer)) =
        cache.create_blob(merkle, blob_kind).await.map_err(FetchError::CreateBlob)?
    {
        let res = read_local_blob(&inspect, local_mirror, merkle, expected_len, blob).await;
        inspect.state(inspect::LocalMirror::CloseBlob);
        blob_closer.close().await;
        res?;
    }
    Ok(())
}

async fn read_local_blob(
    inspect: &inspect::Attempt<inspect::LocalMirror>,
    proxy: &LocalMirrorProxy,
    merkle: BlobId,
    expected_len: Option<u64>,
    dest: pkgfs::install::Blob<pkgfs::install::NeedsTruncate>,
) -> Result<(), FetchError> {
    let (local_file, remote) = fidl::endpoints::create_proxy::<fidl_fuchsia_io::FileMarker>()
        .map_err(FetchError::FidlError)?;

    inspect.state(inspect::LocalMirror::GetBlob);
    proxy
        .get_blob(&mut merkle.into(), remote)
        .await
        .map_err(FetchError::FidlError)?
        .map_err(FetchError::LocalMirror)?;

    let (status, info) = local_file.get_attr().await.map_err(FetchError::FidlError)?;
    Status::ok(status).map_err(FetchError::IoError)?;

    if let Some(ref val) = expected_len {
        if val > &info.content_size {
            return Err(FetchError::BlobTooSmall { uri: merkle.to_string() });
        } else if val < &info.content_size {
            return Err(FetchError::BlobTooLarge { uri: merkle.to_string() });
        }
    }

    inspect.state(inspect::LocalMirror::TruncateBlob);
    let mut dest = dest.truncate(info.content_size).await.map_err(FetchError::Truncate)?;

    loop {
        inspect.state(inspect::LocalMirror::ReadBlob);
        let (status, data) =
            local_file.read(fidl_fuchsia_io::MAX_BUF).await.map_err(FetchError::FidlError)?;
        Status::ok(status).map_err(FetchError::IoError)?;
        if data.len() == 0 {
            return Err(FetchError::BlobTooSmall { uri: merkle.to_string() });
        }
        inspect.state(inspect::LocalMirror::WriteBlob);
        dest = match dest.write(&data).await.map_err(FetchError::Write)? {
            pkgfs::install::BlobWriteSuccess::MoreToWrite(blob) => blob,
            pkgfs::install::BlobWriteSuccess::Done => break,
        };
        inspect.write_bytes(data.len());
    }
    Ok(())
}

fn make_blob_url(
    blob_mirror_url: http::Uri,
    merkle: &BlobId,
) -> Result<hyper::Uri, http_uri_ext::Error> {
    blob_mirror_url.extend_dir_with_path(&merkle.to_string())
}

async fn download_blob(
    inspect: &inspect::Attempt<inspect::Http>,
    client: &fuchsia_hyper::HttpsClient,
    uri: &http::Uri,
    expected_len: Option<u64>,
    dest: pkgfs::install::Blob<pkgfs::install::NeedsTruncate>,
    blob_fetch_params: BlobFetchParams,
    fetch_stats: Arc<FetchStats>,
) -> Result<(), FetchError> {
    inspect.state(inspect::Http::HttpGet);
    let (expected_len, content) =
        resume::resuming_get(client, uri, expected_len, blob_fetch_params, fetch_stats).await?;
    inspect.expected_size_bytes(expected_len);

    inspect.state(inspect::Http::TruncateBlob);
    let mut dest = dest.truncate(expected_len).await.map_err(FetchError::Truncate)?;

    inspect.state(inspect::Http::ReadHttpBody);
    let mut written = 0u64;

    futures::pin_mut!(content);
    while let Some(chunk) = content.try_next().await? {
        if written + chunk.len() as u64 > expected_len {
            return Err(FetchError::BlobTooLarge { uri: uri.to_string() });
        }

        inspect.state(inspect::Http::WriteBlob);
        dest = match dest.write(&chunk).await.map_err(FetchError::Write)? {
            pkgfs::install::BlobWriteSuccess::MoreToWrite(blob) => {
                written += chunk.len() as u64;
                blob
            }
            pkgfs::install::BlobWriteSuccess::Done => {
                written += chunk.len() as u64;
                break;
            }
        };
        inspect.state(inspect::Http::ReadHttpBody);
        inspect.write_bytes(chunk.len());
    }
    inspect.state(inspect::Http::WriteComplete);

    if expected_len != written {
        return Err(FetchError::BlobTooSmall { uri: uri.to_string() });
    }

    Ok(())
}

#[derive(Debug, thiserror::Error)]
pub enum FetchError {
    #[error("could not create blob")]
    CreateBlob(#[source] pkgfs::install::BlobCreateError),

    #[error("Blob fetch of {uri}: http request expected 200, got {code}")]
    BadHttpStatus { code: hyper::StatusCode, uri: String },

    #[error("repository has no configured mirrors")]
    NoMirrors,

    #[error("Blob fetch of {uri}: expected blob length of {expected}, got {actual}")]
    ContentLengthMismatch { expected: u64, actual: u64, uri: String },

    #[error("Blob fetch of {uri}: blob length not known or provided by server")]
    UnknownLength { uri: String },

    #[error("Blob fetch of {uri}: downloaded blob was too small")]
    BlobTooSmall { uri: String },

    #[error("Blob fetch of {uri}: downloaded blob was too large")]
    BlobTooLarge { uri: String },

    #[error("failed to truncate blob")]
    Truncate(#[source] pkgfs::install::BlobTruncateError),

    #[error("failed to write blob data")]
    Write(#[source] pkgfs::install::BlobWriteError),

    #[error("hyper error while fetching {uri}")]
    Hyper {
        #[source]
        e: hyper::Error,
        uri: String,
    },

    #[error("http error while fetching {uri}")]
    Http {
        #[source]
        e: hyper::http::Error,
        uri: String,
    },

    #[error("blob url error")]
    BlobUrl(#[source] http_uri_ext::Error),

    #[error("FIDL error while fetching blob")]
    FidlError(#[source] fidl::Error),

    #[error("IO error while reading blob")]
    IoError(#[source] Status),

    #[error("LocalMirror error while fetching {0:?}")]
    LocalMirror(
        // The FIDL error type doesn't derive Error, so we can't use #[source].
        fidl_fuchsia_pkg::GetBlobError,
    ),

    #[error(
        "No valid source could be found for the requested blob. \
        use_remote_mirror={use_remote_mirror}, use_local_mirror={use_local_mirror}, \
        allow_local_mirror={allow_local_mirror}"
    )]
    NoBlobSource { use_remote_mirror: bool, use_local_mirror: bool, allow_local_mirror: bool },

    #[error("Tried to request a blob with HTTP and local mirrors")]
    ConflictingBlobSources,

    #[error("timed out waiting for http response header while downloading blob: {uri}")]
    BlobHeaderTimeout { uri: String },

    #[error(
        "timed out waiting for bytes from the http response body while downloading blob: {uri}"
    )]
    BlobBodyTimeout { uri: String },

    #[error("Blob fetch of {uri}: http request expected 206, got {code}")]
    ExpectedHttpStatus206 { code: hyper::StatusCode, uri: String },

    #[error("Blob fetch of {uri}: http request expected Content-Range header")]
    MissingContentRangeHeader { uri: String },

    #[error("Blob fetch of {uri}: http request for range {first_byte_pos}-{last_byte_pos} returned malformed Content-Range header: {header:?}")]
    MalformedContentRangeHeader {
        #[source]
        e: resume::ContentRangeParseError,
        uri: String,
        first_byte_pos: u64,
        last_byte_pos: u64,
        header: http::header::HeaderValue,
    },

    #[error("Blob fetch of {uri}: http request returned Content-Range: {content_range:?} but expected: {expected:?}")]
    InvalidContentRangeHeader {
        uri: String,
        content_range: resume::HttpContentRange,
        expected: resume::HttpContentRange,
    },

    #[error("Blob fetch of {uri}: exceeded resumption attempt limit of: {limit}")]
    ExceededResumptionAttemptLimit { uri: String, limit: u64 },

    #[error("Blob fetch of {uri}: Content-Length: {content_length} and Content-Range: {content_range:?} are inconsistent")]
    ContentLengthContentRangeMismatch {
        uri: String,
        content_length: u64,
        content_range: resume::HttpContentRange,
    },
}

impl From<&FetchError> for metrics::FetchBlobMetricDimensionResult {
    fn from(error: &FetchError) -> Self {
        use {metrics::FetchBlobMetricDimensionResult as EventCodes, FetchError::*};
        match error {
            CreateBlob { .. } => EventCodes::CreateBlob,
            BadHttpStatus { code, .. } => match *code {
                StatusCode::BAD_REQUEST => EventCodes::HttpBadRequest,
                StatusCode::UNAUTHORIZED => EventCodes::HttpUnauthorized,
                StatusCode::FORBIDDEN => EventCodes::HttpForbidden,
                StatusCode::NOT_FOUND => EventCodes::HttpNotFound,
                StatusCode::METHOD_NOT_ALLOWED => EventCodes::HttpMethodNotAllowed,
                StatusCode::REQUEST_TIMEOUT => EventCodes::HttpRequestTimeout,
                StatusCode::PRECONDITION_FAILED => EventCodes::HttpPreconditionFailed,
                StatusCode::RANGE_NOT_SATISFIABLE => EventCodes::HttpRangeNotSatisfiable,
                StatusCode::TOO_MANY_REQUESTS => EventCodes::HttpTooManyRequests,
                StatusCode::INTERNAL_SERVER_ERROR => EventCodes::HttpInternalServerError,
                StatusCode::BAD_GATEWAY => EventCodes::HttpBadGateway,
                StatusCode::SERVICE_UNAVAILABLE => EventCodes::HttpServiceUnavailable,
                StatusCode::GATEWAY_TIMEOUT => EventCodes::HttpGatewayTimeout,
                _ => match code.as_u16() {
                    100..=199 => EventCodes::Http1xx,
                    200..=299 => EventCodes::Http2xx,
                    300..=399 => EventCodes::Http3xx,
                    400..=499 => EventCodes::Http4xx,
                    500..=599 => EventCodes::Http5xx,
                    _ => EventCodes::BadHttpStatus,
                },
            },
            NoMirrors => EventCodes::NoMirrors,
            ContentLengthMismatch { .. } => EventCodes::ContentLengthMismatch,
            UnknownLength { .. } => EventCodes::UnknownLength,
            BlobTooSmall { .. } => EventCodes::BlobTooSmall,
            BlobTooLarge { .. } => EventCodes::BlobTooLarge,
            Truncate { .. } => EventCodes::Truncate,
            Write { .. } => EventCodes::Write,
            Hyper { .. } => EventCodes::Hyper,
            Http { .. } => EventCodes::Http,
            BlobUrl { .. } => EventCodes::BlobUrl,
            FidlError { .. } => EventCodes::FidlError,
            IoError { .. } => EventCodes::IoError,
            LocalMirror { .. } => EventCodes::LocalMirror,
            NoBlobSource { .. } => EventCodes::NoBlobSource,
            ConflictingBlobSources => EventCodes::ConflictingBlobSources,
            BlobHeaderTimeout { .. } => EventCodes::BlobHeaderDeadlineExceeded,
            BlobBodyTimeout { .. } => EventCodes::BlobBodyDeadlineExceeded,
            ExpectedHttpStatus206 { .. } => EventCodes::ExpectedHttpStatus206,
            MissingContentRangeHeader { .. } => EventCodes::MissingContentRangeHeader,
            MalformedContentRangeHeader { .. } => EventCodes::MalformedContentRangeHeader,
            InvalidContentRangeHeader { .. } => EventCodes::InvalidContentRangeHeader,
            ExceededResumptionAttemptLimit { .. } => EventCodes::ExceededResumptionAttemptLimit,
            ContentLengthContentRangeMismatch { .. } => {
                EventCodes::ContentLengthContentRangeMismatch
            }
        }
    }
}

impl FetchError {
    fn kind(&self) -> FetchErrorKind {
        use FetchError::*;
        match self {
            BadHttpStatus { code: StatusCode::TOO_MANY_REQUESTS, uri: _ } => {
                FetchErrorKind::NetworkRateLimit
            }
            Hyper { .. }
            | Http { .. }
            | BadHttpStatus { .. }
            | BlobHeaderTimeout { .. }
            | BlobBodyTimeout { .. }
            | ExpectedHttpStatus206 { .. }
            | MissingContentRangeHeader { .. }
            | MalformedContentRangeHeader { .. }
            | InvalidContentRangeHeader { .. }
            | ExceededResumptionAttemptLimit { .. }
            | ContentLengthContentRangeMismatch { .. } => FetchErrorKind::Network,
            CreateBlob { .. }
            | NoMirrors
            | ContentLengthMismatch { .. }
            | UnknownLength { .. }
            | BlobTooSmall { .. }
            | BlobTooLarge { .. }
            | Truncate { .. }
            | Write { .. }
            | BlobUrl { .. }
            | FidlError { .. }
            | IoError { .. }
            | LocalMirror { .. }
            | NoBlobSource { .. }
            | ConflictingBlobSources => FetchErrorKind::Other,
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum FetchErrorKind {
    NetworkRateLimit,
    Network,
    Other,
}

#[cfg(test)]
mod tests {
    use {super::*, http::Uri};

    #[test]
    fn test_make_blob_url() {
        let merkle = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100"
            .parse::<BlobId>()
            .unwrap();

        assert_eq!(
            make_blob_url("http://example.com".parse::<Uri>().unwrap(), &merkle).unwrap(),
            format!("http://example.com/{}", merkle).parse::<Uri>().unwrap()
        );

        assert_eq!(
            make_blob_url("http://example.com/noslash".parse::<Uri>().unwrap(), &merkle).unwrap(),
            format!("http://example.com/noslash/{}", merkle).parse::<Uri>().unwrap()
        );

        assert_eq!(
            make_blob_url("http://example.com/slash/".parse::<Uri>().unwrap(), &merkle).unwrap(),
            format!("http://example.com/slash/{}", merkle).parse::<Uri>().unwrap()
        );

        assert_eq!(
            make_blob_url("http://example.com/twoslashes//".parse::<Uri>().unwrap(), &merkle)
                .unwrap(),
            format!("http://example.com/twoslashes//{}", merkle).parse::<Uri>().unwrap()
        );

        // IPv6 zone id
        assert_eq!(
            make_blob_url(
                "http://[fe80::e022:d4ff:fe13:8ec3%252]:8083/blobs/".parse::<Uri>().unwrap(),
                &merkle
            )
            .unwrap(),
            format!("http://[fe80::e022:d4ff:fe13:8ec3%252]:8083/blobs/{}", merkle)
                .parse::<Uri>()
                .unwrap()
        );
    }
}
