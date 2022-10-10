// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{repository::Repository, repository_manager::Stats, TCP_KEEPALIVE_TIMEOUT},
    cobalt_sw_delivery_registry as metrics,
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_metrics::MetricEvent,
    fidl_fuchsia_pkg::LocalMirrorProxy,
    fidl_fuchsia_pkg_ext::{self as pkg, BlobId, BlobInfo, MirrorConfig, RepositoryConfig},
    fuchsia_cobalt_builders::MetricEventExt as _,
    fuchsia_pkg::PackageDirectory,
    fuchsia_trace as ftrace,
    fuchsia_url::AbsolutePackageUrl,
    fuchsia_zircon::Status,
    futures::{lock::Mutex as AsyncMutex, prelude::*, stream::FuturesUnordered},
    http_uri_ext::HttpUriExt as _,
    hyper::StatusCode,
    parking_lot::Mutex,
    std::{
        sync::{
            atomic::{AtomicBool, AtomicU64, Ordering},
            Arc,
        },
        time::Duration,
    },
    tuf::metadata::{MetadataPath, MetadataVersion, TargetPath},
};

pub use fidl_fuchsia_pkg_ext::BasePackageIndex;

mod inspect;
mod resume;
mod retry;

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

pub async fn cache_package<'a>(
    repo: Arc<AsyncMutex<Repository>>,
    config: &'a RepositoryConfig,
    url: &'a AbsolutePackageUrl,
    cache: &'a pkg::cache::Client,
    blob_fetcher: &'a BlobFetcher,
    cobalt_sender: ProtocolSender<MetricEvent>,
    trace_id: ftrace::Id,
) -> Result<(BlobId, PackageDirectory), CacheError> {
    let (merkle, size) =
        merkle_for_url(repo, url, cobalt_sender).await.map_err(CacheError::MerkleFor)?;
    // If a merkle pin was specified, use it, but only after having verified that the name and
    // variant exist in the TUF repo.  Note that this doesn't guarantee that the merkle pinned
    // package ever actually existed in the repo or that the merkle pin refers to the named
    // package.
    let (merkle, size) = if let Some(merkle_pin) = url.hash() {
        (BlobId::from(merkle_pin), None)
    } else {
        (merkle, Some(size))
    };

    let meta_far_blob = BlobInfo { blob_id: merkle, length: 0 };

    let mut get = cache.get(meta_far_blob)?;

    let blob_fetch_res = async {
        let mirrors = config.mirrors().to_vec().into();

        // Fetch the meta.far.
        let () = blob_fetcher
            .push(
                merkle,
                FetchBlobContext {
                    opener: get.make_open_meta_blob(),
                    mirrors: Arc::clone(&mirrors),
                    expected_len: size,
                    use_local_mirror: config.use_local_mirror(),
                    parent_trace_id: trace_id,
                },
            )
            .await
            .expect("processor exists")
            .map_err(|e| CacheError::FetchMetaFar(e, merkle))?;

        let mut fetches = FuturesUnordered::new();
        let mut missing_blobs = get.get_missing_blobs();
        while let Some(chunk) = missing_blobs.try_next().await? {
            let chunk = chunk
                .into_iter()
                .map(|need| {
                    (
                        need.blob_id,
                        FetchBlobContext {
                            opener: get.make_open_blob(need.blob_id),
                            mirrors: Arc::clone(&mirrors),
                            expected_len: None,
                            use_local_mirror: config.use_local_mirror(),
                            parent_trace_id: trace_id,
                        },
                    )
                })
                .collect::<Vec<_>>();
            let () = fetches.extend(blob_fetcher.push_all(chunk.into_iter()));
        }

        let fetches = fetches.map(|res| res.expect("processor exists"));

        // When a blob write fails and tears down the entire Get() operation, the other pending blob
        // writes will fail with an unexpected close of the NeededBlobs/blob write channel.
        //
        // Wait for:
        // * any blob fetch to fail with a non-cancelled error
        // * every blob fetch to either succeed/fail with a cancelled error
        // returning the more interesting error encountered, if any.
        try_collect_useful_error(fetches).await.map_err(|e| CacheError::FetchContentBlob(e, merkle))
    }
    .await;

    match blob_fetch_res {
        Ok(()) => Ok((merkle, get.finish().await?)),
        Err(e) => {
            get.abort().await;
            Err(e)
        }
    }
}

async fn try_collect_useful_error(
    mut fetches: impl Stream<Item = Result<(), Arc<FetchError>>> + Unpin,
) -> Result<(), Arc<FetchError>> {
    let mut first_closed_error: Option<Arc<FetchError>> = None;

    while let Some(res) = fetches.next().await {
        match res {
            Ok(()) => {}
            Err(e) if e.is_unexpected_pkg_cache_closed() => {
                first_closed_error.get_or_insert(e);
            }
            Err(e) => return Err(e),
        }
    }

    match first_closed_error {
        Some(e) => Err(e),
        None => Ok(()),
    }
}

/// Provides a Debug impl for an Iterator that produces a list of all elements in the iterator.
struct DebugIter<I>(I);

impl<I> std::fmt::Debug for DebugIter<I>
where
    I: Iterator + Clone,
    I::Item: std::fmt::Debug,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        f.debug_list().entries(self.0.clone()).finish()
    }
}

#[derive(Debug, thiserror::Error)]
pub enum CacheError {
    #[error("fidl error")]
    Fidl(#[from] fidl::Error),

    #[error("while looking up merkle root for package")]
    MerkleFor(#[source] MerkleForError),

    #[error("while listing needed blobs for package")]
    ListNeeds(#[from] pkg::cache::ListMissingBlobsError),

    #[error("while fetching the meta.far: {1}")]
    FetchMetaFar(#[source] Arc<FetchError>, BlobId),

    #[error("while fetching content blob for meta.far {1}")]
    FetchContentBlob(#[source] Arc<FetchError>, BlobId),

    #[error("Get() request failed")]
    Get(#[from] pkg::cache::GetError),
}

pub(crate) trait ToResolveError {
    fn to_resolve_error(&self) -> pkg::ResolveError;
}

impl ToResolveError for Status {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match *self {
            Status::ACCESS_DENIED => pkg::ResolveError::AccessDenied,
            Status::IO => pkg::ResolveError::Io,
            Status::NOT_FOUND => pkg::ResolveError::PackageNotFound,
            Status::NO_SPACE => pkg::ResolveError::NoSpace,
            Status::UNAVAILABLE => pkg::ResolveError::UnavailableBlob,
            Status::INVALID_ARGS => pkg::ResolveError::InvalidUrl,
            Status::INTERNAL => pkg::ResolveError::Internal,
            _ => pkg::ResolveError::Internal,
        }
    }
}

pub(crate) trait ToResolveStatus {
    fn to_resolve_status(&self) -> Status;
}
impl ToResolveStatus for pkg::ResolveError {
    fn to_resolve_status(&self) -> Status {
        use pkg::ResolveError::*;
        match *self {
            AccessDenied => Status::ACCESS_DENIED,
            Io => Status::IO,
            PackageNotFound | RepoNotFound | BlobNotFound => Status::NOT_FOUND,
            NoSpace => Status::NO_SPACE,
            UnavailableBlob | UnavailableRepoMetadata => Status::UNAVAILABLE,
            InvalidUrl | InvalidContext => Status::INVALID_ARGS,
            Internal => Status::INTERNAL,
        }
    }
}

// From resolver.fidl:
// * `ZX_ERR_INTERNAL` if the resolver encountered an otherwise unspecified error
//   while handling the request
// * `ZX_ERR_NOT_FOUND` if the package does not exist.
// * `ZX_ERR_ADDRESS_UNREACHABLE` if the resolver does not know about the repo.
impl ToResolveError for CacheError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match self {
            CacheError::Fidl(_) => pkg::ResolveError::Io,
            CacheError::MerkleFor(err) => err.to_resolve_error(),
            CacheError::ListNeeds(err) => err.to_resolve_error(),
            CacheError::FetchMetaFar(err, ..) => err.to_resolve_error(),
            CacheError::FetchContentBlob(err, _) => err.to_resolve_error(),
            CacheError::Get(err) => err.to_resolve_error(),
        }
    }
}

impl ToResolveStatus for MerkleForError {
    fn to_resolve_status(&self) -> Status {
        match self {
            MerkleForError::MetadataNotFound { .. } => Status::INTERNAL,
            MerkleForError::TargetNotFound(_) => Status::NOT_FOUND,
            MerkleForError::InvalidTargetPath(_) => Status::INTERNAL,
            // FIXME(42326) when tuf::Error gets an HTTP error variant, this should be mapped to Status::UNAVAILABLE
            MerkleForError::FetchTargetDescription(..) => Status::INTERNAL,
            MerkleForError::NoCustomMetadata => Status::INTERNAL,
            MerkleForError::SerdeError(_) => Status::INTERNAL,
        }
    }
}

impl ToResolveError for MerkleForError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match self {
            MerkleForError::MetadataNotFound { .. } => pkg::ResolveError::Internal,
            MerkleForError::TargetNotFound(_) => pkg::ResolveError::PackageNotFound,
            MerkleForError::InvalidTargetPath(_) => pkg::ResolveError::Internal,
            // FIXME(42326) when tuf::Error gets an HTTP error variant, this should be mapped to Status::UNAVAILABLE
            MerkleForError::FetchTargetDescription(..) => pkg::ResolveError::Internal,
            MerkleForError::NoCustomMetadata => pkg::ResolveError::Internal,
            MerkleForError::SerdeError(_) => pkg::ResolveError::Internal,
        }
    }
}

impl ToResolveError for pkg::cache::OpenError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match self {
            pkg::cache::OpenError::NotFound => pkg::ResolveError::PackageNotFound,
            pkg::cache::OpenError::UnexpectedResponse(_) | pkg::cache::OpenError::Fidl(_) => {
                pkg::ResolveError::Internal
            }
        }
    }
}

impl ToResolveError for pkg::cache::GetError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match self {
            pkg::cache::GetError::UnexpectedResponse(_) => pkg::ResolveError::Internal,
            pkg::cache::GetError::Fidl(_) => pkg::ResolveError::Internal,
        }
    }
}

impl ToResolveError for pkg::cache::OpenBlobError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match self {
            pkg::cache::OpenBlobError::OutOfSpace => pkg::ResolveError::NoSpace,
            pkg::cache::OpenBlobError::ConcurrentWrite => pkg::ResolveError::Internal,
            pkg::cache::OpenBlobError::UnspecifiedIo => pkg::ResolveError::Io,
            pkg::cache::OpenBlobError::Internal => pkg::ResolveError::Internal,
            pkg::cache::OpenBlobError::Fidl(_) => pkg::ResolveError::Internal,
        }
    }
}

impl ToResolveError for pkg::cache::ListMissingBlobsError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        pkg::ResolveError::Internal
    }
}

impl ToResolveError for pkg::cache::TruncateBlobError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match self {
            pkg::cache::TruncateBlobError::NoSpace => pkg::ResolveError::NoSpace,
            pkg::cache::TruncateBlobError::UnexpectedResponse(_) => pkg::ResolveError::Io,
            pkg::cache::TruncateBlobError::Fidl(_) => pkg::ResolveError::Io,
        }
    }
}

impl ToResolveError for pkg::cache::WriteBlobError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        match self {
            pkg::cache::WriteBlobError::Overwrite => pkg::ResolveError::Io,
            pkg::cache::WriteBlobError::Corrupt => pkg::ResolveError::Io,
            pkg::cache::WriteBlobError::NoSpace => pkg::ResolveError::NoSpace,
            pkg::cache::WriteBlobError::UnexpectedResponse(_) => pkg::ResolveError::Io,
            pkg::cache::WriteBlobError::Fidl(_) => pkg::ResolveError::Io,
        }
    }
}

impl ToResolveError for FetchError {
    fn to_resolve_error(&self) -> pkg::ResolveError {
        use FetchError::*;
        match self {
            CreateBlob(e) => e.to_resolve_error(),
            BadHttpStatus { code: hyper::StatusCode::UNAUTHORIZED, .. } => {
                pkg::ResolveError::AccessDenied
            }
            BadHttpStatus { code: hyper::StatusCode::FORBIDDEN, .. } => {
                pkg::ResolveError::AccessDenied
            }
            BadHttpStatus { .. } => pkg::ResolveError::UnavailableBlob,
            ContentLengthMismatch { .. } => pkg::ResolveError::UnavailableBlob,
            UnknownLength { .. } => pkg::ResolveError::UnavailableBlob,
            BlobTooSmall { .. } => pkg::ResolveError::UnavailableBlob,
            BlobTooLarge { .. } => pkg::ResolveError::UnavailableBlob,
            Hyper { .. } => pkg::ResolveError::UnavailableBlob,
            Http { .. } => pkg::ResolveError::UnavailableBlob,
            Truncate(e) => e.to_resolve_error(),
            Write(e) => e.to_resolve_error(),
            NoMirrors => pkg::ResolveError::Internal,
            BlobUrl(_) => pkg::ResolveError::Internal,
            FidlError(_) => pkg::ResolveError::Internal,
            IoError(_) => pkg::ResolveError::Io,
            LocalMirror(_) => pkg::ResolveError::Internal,
            NoBlobSource { .. } => pkg::ResolveError::Internal,
            ConflictingBlobSources => pkg::ResolveError::Internal,
            BlobHeaderTimeout { .. } => pkg::ResolveError::UnavailableBlob,
            BlobBodyTimeout { .. } => pkg::ResolveError::UnavailableBlob,
            ExpectedHttpStatus206 { .. } => pkg::ResolveError::UnavailableBlob,
            MissingContentRangeHeader { .. } => pkg::ResolveError::UnavailableBlob,
            MalformedContentRangeHeader { .. } => pkg::ResolveError::UnavailableBlob,
            InvalidContentRangeHeader { .. } => pkg::ResolveError::UnavailableBlob,
            ExceededResumptionAttemptLimit { .. } => pkg::ResolveError::UnavailableBlob,
            ContentLengthContentRangeMismatch { .. } => pkg::ResolveError::UnavailableBlob,
        }
    }
}

impl From<&MerkleForError> for metrics::MerkleForUrlMigratedMetricDimensionResult {
    fn from(e: &MerkleForError) -> metrics::MerkleForUrlMigratedMetricDimensionResult {
        use metrics::MerkleForUrlMigratedMetricDimensionResult as EventCodes;
        match e {
            MerkleForError::MetadataNotFound { .. } => EventCodes::TufError,
            MerkleForError::TargetNotFound(_) => EventCodes::NotFound,
            MerkleForError::FetchTargetDescription(..) => EventCodes::TufError,
            MerkleForError::InvalidTargetPath(_) => EventCodes::InvalidTargetPath,
            MerkleForError::NoCustomMetadata => EventCodes::NoCustomMetadata,
            MerkleForError::SerdeError(_) => EventCodes::SerdeError,
        }
    }
}

pub async fn merkle_for_url<'a>(
    repo: Arc<AsyncMutex<Repository>>,
    url: &'a AbsolutePackageUrl,
    mut cobalt_sender: ProtocolSender<MetricEvent>,
) -> Result<(BlobId, u64), MerkleForError> {
    let target_path = TargetPath::new(format!(
        "{}/{}",
        url.name(),
        url.variant().unwrap_or(&fuchsia_url::PackageVariant::zero())
    ))
    .map_err(MerkleForError::InvalidTargetPath)?;
    let mut repo = repo.lock().await;
    let res = repo.get_merkle_at_path(&target_path).await;
    cobalt_sender.send(
        MetricEvent::builder(metrics::MERKLE_FOR_URL_MIGRATED_METRIC_ID)
            .with_event_codes(match &res {
                Ok(_) => metrics::MerkleForUrlMigratedMetricDimensionResult::Success,
                Err(res) => res.into(),
            })
            .as_occurrence(1),
    );
    res.map(|custom| (custom.merkle(), custom.size()))
}

#[derive(Debug, thiserror::Error)]
pub enum MerkleForError {
    #[error("the repository metadata {path} at version {version} was not found in the repository")]
    MetadataNotFound { path: MetadataPath, version: MetadataVersion },

    #[error("the package {0} was not found in the repository")]
    TargetNotFound(TargetPath),

    #[error("unexpected tuf error when fetching target description for {0:?}")]
    FetchTargetDescription(String, #[source] tuf::error::Error),

    #[error("the target path is not safe")]
    InvalidTargetPath(#[source] tuf::error::Error),

    #[error("the target description does not have custom metadata")]
    NoCustomMetadata,

    #[error("serde value could not be converted")]
    SerdeError(#[source] serde_json::Error),
}

#[derive(Debug, PartialEq, Eq)]
pub struct FetchBlobContext {
    opener: pkg::cache::DeferredOpenBlob,
    mirrors: Arc<[MirrorConfig]>,
    expected_len: Option<u64>,
    use_local_mirror: bool,
    parent_trace_id: ftrace::Id,
}

impl work_queue::TryMerge for FetchBlobContext {
    fn try_merge(&mut self, other: Self) -> Result<(), Self> {
        // The NeededBlobs protocol requires pkg-resolver to attempt to open each blob associated
        // with a Get() request, and attempting to open a blob being written by another Get()
        // operation would fail. So, this queue is needed to enforce a concurrency limit and ensure
        // a blob is not written by more than one fetch at a time.

        // Only requests that are the same request on the same channel can be merged, and the
        // packageresolver should never enqueue such duplicate requests, so, realistically,
        // try_merge always returns Err(other).
        if self.opener != other.opener {
            return Err(other);
        }

        // Unmergeable if both contain different expected lengths. One of these instances will
        // fail, but we can't know which one here.
        let expected_len = match (self.expected_len, other.expected_len) {
            (Some(x), None) | (None, Some(x)) => Some(x),
            (None, None) => None,
            (Some(x), Some(y)) if x == y => Some(x),
            _ => return Err(other),
        };

        // Don't attempt to merge mirrors, but do merge these contexts if the mirrors are
        // equivalent.
        if self.mirrors != other.mirrors || self.use_local_mirror != other.use_local_mirror {
            return Err(other);
        }

        // Contexts are mergeable, apply the merged state.
        self.expected_len = expected_len;
        Ok(())
    }
}

/// A clonable handle to the blob fetch queue.  When all clones of
/// [`BlobFetcher`] are dropped, the queue will fetch all remaining blobs in
/// the queue and terminate its output stream.
#[derive(Clone)]
pub struct BlobFetcher(
    work_queue::WorkSender<BlobId, FetchBlobContext, Result<(), Arc<FetchError>>>,
);

impl BlobFetcher {
    /// Creates an unbounded queue that will fetch up to  `max_concurrency`
    /// blobs at once.
    pub fn new(
        node: fuchsia_inspect::Node,
        max_concurrency: usize,
        stats: Arc<Mutex<Stats>>,
        cobalt_sender: ProtocolSender<MetricEvent>,
        local_mirror_proxy: Option<LocalMirrorProxy>,
        blob_fetch_params: BlobFetchParams,
    ) -> (impl Future<Output = ()>, BlobFetcher) {
        let http_client = Arc::new(fuchsia_hyper::new_https_client_from_tcp_options(
            fuchsia_hyper::TcpOptions::keepalive_timeout(TCP_KEEPALIVE_TIMEOUT),
        ));
        let inspect = inspect::BlobFetcher::from_node_and_params(node, &blob_fetch_params);

        let (blob_fetch_queue, blob_fetcher) = work_queue::work_queue(
            max_concurrency,
            move |merkle: BlobId, context: FetchBlobContext| {
                let inspect = inspect.fetch(&merkle);
                let http_client = Arc::clone(&http_client);
                let stats = Arc::clone(&stats);
                let cobalt_sender = cobalt_sender.clone();
                let local_mirror_proxy = local_mirror_proxy.clone();

                async move {
                    let res = fetch_blob(
                        inspect,
                        &http_client,
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
            },
        );

        (blob_fetch_queue.into_future(), BlobFetcher(blob_fetcher))
    }

    /// Enqueue the given blob to be fetched, or attach to an existing request to
    /// fetch the blob.
    pub fn push(
        &self,
        blob_id: BlobId,
        context: FetchBlobContext,
    ) -> impl Future<Output = Result<Result<(), Arc<FetchError>>, work_queue::Closed>> {
        self.0.push(blob_id, context)
    }

    /// Enqueue all the given blobs to be fetched, merging them with existing
    /// known tasks if possible, returning an iterator of the futures that will
    /// resolve to the results.
    ///
    /// This method is similar to, but more efficient than, mapping an iterator
    /// to `BlobFetcher::push`.
    pub fn push_all(
        &self,
        entries: impl Iterator<Item = (BlobId, FetchBlobContext)>,
    ) -> impl Iterator<
        Item = impl Future<Output = Result<Result<(), Arc<FetchError>>, work_queue::Closed>>,
    > {
        self.0.push_all(entries)
    }
}

async fn fetch_blob(
    inspect: inspect::NeedsRemoteType,
    http_client: &fuchsia_hyper::HttpsClient,
    stats: Arc<Mutex<Stats>>,
    cobalt_sender: ProtocolSender<MetricEvent>,
    merkle: BlobId,
    context: FetchBlobContext,
    local_mirror_proxy: Option<&LocalMirrorProxy>,
    blob_fetch_params: BlobFetchParams,
) -> Result<(), FetchError> {
    let use_remote_mirror = context.mirrors.len() != 0;
    let use_local_mirror = context.use_local_mirror;
    let trace_id = ftrace::Id::random();

    match (use_remote_mirror, use_local_mirror, local_mirror_proxy) {
        (true, true, _) => Err(FetchError::ConflictingBlobSources),
        (false, true, Some(local_mirror)) => {
            let guard = ftrace::async_enter!(
                trace_id,
                "app",
                "fetch_blob_local",
                // Async tracing does not support multiple concurrent child durations, so we create
                // a new top-level duration and attach the parent duration as metadata.
                "parent_trace_id" => u64::from(context.parent_trace_id),
                "hash" => merkle.to_string().as_str()
            );
            let res = fetch_blob_local(
                inspect.local_mirror(),
                local_mirror,
                merkle,
                context.opener,
                context.expected_len,
            )
            .await;
            guard.end(&[ftrace::ArgValue::of("result", format!("{:?}", res).as_str())]);
            res
        }
        (true, false, _) => {
            let guard = ftrace::async_enter!(
                trace_id.into(),
                "app",
                "fetch_blob_http",
                // Async tracing does not support multiple concurrent child durations, so we create
                // a new top-level duration and attach the parent duration as metadata.
                "parent_trace_id" => u64::from(context.parent_trace_id),
                "hash" => merkle.to_string().as_str()
            );
            let res = fetch_blob_http(
                inspect.http(),
                http_client,
                &context.mirrors,
                merkle,
                context.opener,
                context.expected_len,
                blob_fetch_params,
                stats,
                cobalt_sender,
                trace_id,
            )
            .await;
            guard.end(&[ftrace::ArgValue::of("result", format!("{:?}", res).as_str())]);
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
    opener: pkg::cache::DeferredOpenBlob,
    expected_len: Option<u64>,
    blob_fetch_params: BlobFetchParams,
    stats: Arc<Mutex<Stats>>,
    cobalt_sender: ProtocolSender<MetricEvent>,
    trace_id: ftrace::Id,
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
        let opener = &opener;
        let inspect = &inspect;
        let blob_url = &blob_url;

        async move {
            let fetch_stats = FetchStats::default();
            let res = async {
                let inspect = inspect.attempt();
                inspect.state(inspect::Http::CreateBlob);
                if let Some(pkg::cache::NeededBlob { blob, closer: blob_closer }) =
                    opener.open().await.map_err(FetchError::CreateBlob)?
                {
                    inspect.state(inspect::Http::DownloadBlob);
                    let guard = ftrace::async_enter!(
                        trace_id.into(),
                        "app",
                        "download_blob",
                        "hash" => merkle.to_string().as_str()
                    );
                    let res = download_blob(
                        &inspect,
                        client,
                        &blob_url,
                        expected_len,
                        blob,
                        blob_fetch_params,
                        &fetch_stats,
                        trace_id,
                    )
                    .await;
                    let (size_str, status_str) = match &res {
                        Ok(size) => (size.to_string(), "success".to_string()),
                        Err(e) => ("no size because download failed".to_string(), e.to_string()),
                    };
                    guard.end(&[
                        ftrace::ArgValue::of("size", size_str.as_str()),
                        ftrace::ArgValue::of("status", status_str.as_str()),
                    ]);
                    inspect.state(inspect::Http::CloseBlob);
                    blob_closer.close().await;
                    res?;
                }
                Ok(())
            }
            .await;

            match res.as_ref().map_err(FetchError::kind) {
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
            }

            let result_event_code = match &res {
                Ok(()) => metrics::FetchBlobMigratedMetricDimensionResult::Success,
                Err(e) => e.into(),
            };
            let resumed_event_code = if fetch_stats.resumptions() != 0 {
                metrics::FetchBlobMigratedMetricDimensionResumed::True
            } else {
                metrics::FetchBlobMigratedMetricDimensionResumed::False
            };
            cobalt_sender.send(
                MetricEvent::builder(metrics::FETCH_BLOB_MIGRATED_METRIC_ID)
                    .with_event_codes((result_event_code, resumed_event_code))
                    .as_occurrence(1),
            );

            res
        }
    })
    .await
}

async fn fetch_blob_local(
    inspect: inspect::TriggerAttempt<inspect::LocalMirror>,
    local_mirror: &LocalMirrorProxy,
    merkle: BlobId,
    opener: pkg::cache::DeferredOpenBlob,
    expected_len: Option<u64>,
) -> Result<(), FetchError> {
    let inspect = inspect.attempt();
    inspect.state(inspect::LocalMirror::CreateBlob);
    if let Some(pkg::cache::NeededBlob { blob, closer: blob_closer }) =
        opener.open().await.map_err(FetchError::CreateBlob)?
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
    dest: pkg::cache::Blob<pkg::cache::NeedsTruncate>,
) -> Result<(), FetchError> {
    let (local_file, remote) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().map_err(FetchError::FidlError)?;

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
        let data = local_file
            .read(fio::MAX_BUF)
            .await
            .map_err(FetchError::FidlError)?
            .map_err(Status::from_raw)
            .map_err(FetchError::IoError)?;
        if data.len() == 0 {
            return Err(FetchError::BlobTooSmall { uri: merkle.to_string() });
        }
        inspect.state(inspect::LocalMirror::WriteBlob);
        dest = match dest.write(&data).await.map_err(FetchError::Write)? {
            pkg::cache::BlobWriteSuccess::MoreToWrite(blob) => blob,
            pkg::cache::BlobWriteSuccess::Done => break,
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

// On success, returns the size of the downloaded blob in bytes (useful for tracing).
async fn download_blob(
    inspect: &inspect::Attempt<inspect::Http>,
    client: &fuchsia_hyper::HttpsClient,
    uri: &http::Uri,
    expected_len: Option<u64>,
    dest: pkg::cache::Blob<pkg::cache::NeedsTruncate>,
    blob_fetch_params: BlobFetchParams,
    fetch_stats: &FetchStats,
    trace_id: ftrace::Id,
) -> Result<u64, FetchError> {
    inspect.state(inspect::Http::HttpGet);
    let (expected_len, content) =
        resume::resuming_get(client, uri, expected_len, blob_fetch_params, fetch_stats).await?;
    ftrace::async_instant!(trace_id.into(), "app", "header_received");

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
        ftrace::async_instant!(
            trace_id.into(),
            "app",
            "read_chunk_from_hyper",
            "size" => chunk.len() as u64
        );

        inspect.state(inspect::Http::WriteBlob);
        dest = match dest
            .write_with_trace_callbacks(
                &chunk,
                |size: u64| {
                    ftrace::async_begin(
                        trace_id.into(),
                        ftrace::cstr!("app"),
                        ftrace::cstr!("waiting_for_pkg_cache_to_ack_write"),
                        &[ftrace::ArgValue::of("size", size)],
                    )
                },
                || {
                    ftrace::async_end(
                        trace_id.into(),
                        ftrace::cstr!("app"),
                        ftrace::cstr!("waiting_for_pkg_cache_to_ack_write"),
                        &[],
                    )
                },
            )
            .await
            .map_err(FetchError::Write)?
        {
            pkg::cache::BlobWriteSuccess::MoreToWrite(blob) => {
                written += chunk.len() as u64;
                blob
            }
            pkg::cache::BlobWriteSuccess::Done => {
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

    Ok(expected_len)
}

#[derive(Debug, thiserror::Error)]
pub enum FetchError {
    #[error("could not create blob")]
    CreateBlob(#[source] pkg::cache::OpenBlobError),

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
    Truncate(#[source] pkg::cache::TruncateBlobError),

    #[error("failed to write blob data")]
    Write(#[source] pkg::cache::WriteBlobError),

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

impl From<&FetchError> for metrics::FetchBlobMigratedMetricDimensionResult {
    fn from(error: &FetchError) -> Self {
        use {metrics::FetchBlobMigratedMetricDimensionResult as EventCodes, FetchError::*};
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

    fn to_pkg_cache_fidl_error(&self) -> Option<&fidl::Error> {
        use FetchError::*;
        match self {
            CreateBlob(pkg::cache::OpenBlobError::Fidl(e))
            | Truncate(pkg::cache::TruncateBlobError::Fidl(e))
            | Write(pkg::cache::WriteBlobError::Fidl(e)) => Some(e),
            CreateBlob(_)
            | Truncate(_)
            | Write(_)
            | FidlError { .. }
            | Hyper { .. }
            | Http { .. }
            | BadHttpStatus { .. }
            | BlobHeaderTimeout { .. }
            | BlobBodyTimeout { .. }
            | ExpectedHttpStatus206 { .. }
            | MissingContentRangeHeader { .. }
            | MalformedContentRangeHeader { .. }
            | InvalidContentRangeHeader { .. }
            | ExceededResumptionAttemptLimit { .. }
            | ContentLengthContentRangeMismatch { .. }
            | NoMirrors
            | ContentLengthMismatch { .. }
            | UnknownLength { .. }
            | BlobTooSmall { .. }
            | BlobTooLarge { .. }
            | BlobUrl { .. }
            | IoError { .. }
            | LocalMirror { .. }
            | NoBlobSource { .. }
            | ConflictingBlobSources => None,
        }
    }

    fn is_unexpected_pkg_cache_closed(&self) -> bool {
        match self.to_pkg_cache_fidl_error() {
            Some(fidl::Error::ClientChannelClosed { .. }) => true,
            _ => false,
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
