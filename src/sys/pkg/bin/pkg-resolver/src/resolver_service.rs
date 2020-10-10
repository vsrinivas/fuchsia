// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        cache::{
            BasePackageIndex, BlobFetcher, CacheError, MerkleForError, PackageCache,
            ToResolveStatus as _,
        },
        font_package_manager::FontPackageManager,
        queue,
        repository_manager::RepositoryManager,
        repository_manager::{GetPackageError, GetPackageHashError},
        rewrite_manager::RewriteManager,
    },
    anyhow::{anyhow, Error},
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self, DirectoryMarker},
    fidl_fuchsia_pkg::{
        FontResolverRequest, FontResolverRequestStream, PackageResolverRequest,
        PackageResolverRequestStream,
    },
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_cobalt::CobaltSender,
    fuchsia_pkg::PackagePath,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as trace,
    fuchsia_url::pkg_url::{ParseError, PkgUrl},
    fuchsia_zircon::Status,
    futures::{future::Future, stream::TryStreamExt as _},
    parking_lot::RwLock,
    std::sync::Arc,
    std::time::Instant,
    system_image::CachePackages,
};

mod inspect;
pub use inspect::ResolverService as ResolverServiceInspectState;

pub type PackageFetcher = queue::WorkSender<PkgUrl, (), Result<BlobId, Status>>;

pub fn make_package_fetch_queue(
    cache: PackageCache,
    base_package_index: Arc<BasePackageIndex>,
    system_cache_list: Arc<CachePackages>,
    repo_manager: Arc<RwLock<RepositoryManager>>,
    rewriter: Arc<RwLock<RewriteManager>>,
    blob_fetcher: BlobFetcher,
    max_concurrency: usize,
    inspect: Arc<ResolverServiceInspectState>,
) -> (impl Future<Output = ()>, PackageFetcher) {
    let (package_fetch_queue, package_fetcher) =
        queue::work_queue(max_concurrency, move |url: PkgUrl, _: ()| {
            let cache = cache.clone();
            let base_package_index = Arc::clone(&base_package_index);
            let system_cache_list = Arc::clone(&system_cache_list);
            let repo_manager = Arc::clone(&repo_manager);
            let rewriter = Arc::clone(&rewriter);
            let blob_fetcher = blob_fetcher.clone();
            let inspect = Arc::clone(&inspect);
            async move {
                Ok(package_from_base_or_repo_or_cache(
                    &repo_manager,
                    &rewriter,
                    &base_package_index,
                    &system_cache_list,
                    &url,
                    cache,
                    blob_fetcher,
                    &inspect,
                )
                .await?)
            }
        });
    (package_fetch_queue.into_future(), package_fetcher)
}

pub async fn run_resolver_service(
    cache: PackageCache,
    repo_manager: Arc<RwLock<RepositoryManager>>,
    rewriter: Arc<RwLock<RewriteManager>>,
    package_fetcher: Arc<PackageFetcher>,
    base_package_index: Arc<BasePackageIndex>,
    system_cache_list: Arc<CachePackages>,
    stream: PackageResolverRequestStream,
    cobalt_sender: CobaltSender,
    inspect: Arc<ResolverServiceInspectState>,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| {
            async {
                let mut cobalt_sender = cobalt_sender.clone();
                match event {
                    PackageResolverRequest::Resolve {
                        package_url,
                        selectors,
                        update_policy: _,
                        dir,
                        responder,
                    } => {
                        // FIXME: need to implement selectors.
                        if !selectors.is_empty() {
                            fx_log_warn!("resolve does not support selectors yet");
                        }
                        let start_time = Instant::now();
                        let response = resolve(&cache, &package_fetcher, package_url, dir).await;

                        cobalt_sender.log_event_count(
                            metrics::RESOLVE_METRIC_ID,
                            (
                                resolve_result_to_resolve_code(response),
                                metrics::ResolveMetricDimensionResolverType::Regular,
                            ),
                            0,
                            1,
                        );

                        cobalt_sender.log_elapsed_time(
                            metrics::RESOLVE_DURATION_METRIC_ID,
                            (
                                resolve_result_to_resolve_duration_code(&response),
                                metrics::ResolveDurationMetricDimensionResolverType::Regular,
                            ),
                            Instant::now().duration_since(start_time).as_micros() as i64,
                        );
                        responder.send(&mut response.map_err(|status| status.into_raw()))?;
                        Ok(())
                    }

                    PackageResolverRequest::GetHash { package_url, responder } => {
                        match get_hash(
                            &rewriter,
                            &repo_manager,
                            &base_package_index,
                            &system_cache_list,
                            &package_url.url,
                            &inspect,
                        )
                        .await
                        {
                            Ok(blob_id) => {
                                responder.send(&mut Ok(blob_id.into()))?;
                            }
                            Err(status) => {
                                responder.send(&mut Err(status.into_raw()))?;
                            }
                        }
                        Ok(())
                    }
                }
            }
        })
        .await
}

fn rewrite_url(rewriter: &RwLock<RewriteManager>, url: &PkgUrl) -> Result<PkgUrl, Status> {
    Ok(rewriter.read().rewrite(url))
}

fn missing_cache_package_disk_fallback(
    rewritten_url: &PkgUrl,
    pkg_url: &PkgUrl,
    system_cache_list: &CachePackages,
    inspect: &ResolverServiceInspectState,
) -> Option<BlobId> {
    let possible_fallback = hash_from_cache_packages_manifest(&pkg_url, system_cache_list);
    if possible_fallback.is_some() {
        fx_log_warn!(
            "Did not find {} at URL {}, but did find a matching package name in the \
            built-in cache packages set, so falling back to it. Your package \
            repository may not be configured to serve the package correctly, or may \
            be overriding the domain for the repository which would normally serve \
            this package. This will be an error in a future version of Fuchsia, see \
            fxbug.dev/50748.",
            rewritten_url.name(),
            rewritten_url
        );
        inspect.cache_fallback_due_to_not_found();
    }
    possible_fallback
}

enum HashSource<TufError> {
    Tuf(BlobId),
    SystemImageCachePackages(BlobId, TufError),
}

async fn hash_from_base_or_repo_or_cache(
    repo_manager: &RwLock<RepositoryManager>,
    rewriter: &RwLock<RewriteManager>,
    base_package_index: &BasePackageIndex,
    system_cache_list: &CachePackages,
    pkg_url: &PkgUrl,
    inspect_state: &ResolverServiceInspectState,
) -> Result<BlobId, Status> {
    if let Some(blob) = unpinned_base_package(pkg_url, base_package_index) {
        fx_log_info!("get_hash for {} to {} with base pin", pkg_url, blob);
        return Ok(blob);
    }

    let rewritten_url = rewrite_url(rewriter, &pkg_url)?;
    hash_from_repo_or_cache(repo_manager, system_cache_list, pkg_url, &rewritten_url, inspect_state)
        .await
        .map_err(|e| {
            let status = e.to_resolve_status();
            fx_log_err!("error getting hash {} as {}: {:#}", pkg_url, rewritten_url, anyhow!(e));
            status
        })
        .map(|hash| match hash {
            HashSource::Tuf(blob) => {
                fx_log_info!("get_hash for {} as {} to {} with TUF", pkg_url, rewritten_url, blob);
                blob
            }
            HashSource::SystemImageCachePackages(blob, tuf_err) => {
                fx_log_info!(
                    "get_hash for {} as {} to {} with cache_packages due to {:#}",
                    pkg_url,
                    rewritten_url,
                    blob,
                    anyhow!(tuf_err)
                );
                blob
            }
        })
}
async fn hash_from_repo_or_cache(
    repo_manager: &RwLock<RepositoryManager>,
    system_cache_list: &CachePackages,
    pkg_url: &PkgUrl,
    rewritten_url: &PkgUrl,
    inspect_state: &ResolverServiceInspectState,
) -> Result<HashSource<GetPackageHashError>, GetPackageHashError> {
    // The RwLock created by `.read()` must not exist across the `.await` (to e.g. prevent
    // deadlock). Rust temporaries are kept alive for the duration of the innermost enclosing
    // statement, so the following two lines should not be combined.
    let fut = repo_manager.read().get_package_hash(&rewritten_url);
    match fut.await {
        Ok(b) => Ok(HashSource::Tuf(b)),
        Err(e @ GetPackageHashError::MerkleFor(MerkleForError::NotFound)) => {
            // If we can get metadata but the repo doesn't know about the package,
            // it shouldn't be in the cache, BUT some SDK customers currently rely on this behavior.
            // TODO(fxbug.dev/50764): remove this behavior.
            match missing_cache_package_disk_fallback(
                &rewritten_url,
                pkg_url,
                system_cache_list,
                inspect_state,
            ) {
                Some(blob) => Ok(HashSource::SystemImageCachePackages(blob, e)),
                None => Err(e),
            }
        }
        Err(e) => {
            // If we couldn't get TUF metadata, we might not have networking. Check in
            // system/data/cache_packages (not to be confused with pkg-cache). The cache_packages
            // manifest pkg URLs are for fuchsia.com, so do not use the rewritten URL.
            match hash_from_cache_packages_manifest(&pkg_url, system_cache_list) {
                Some(blob) => Ok(HashSource::SystemImageCachePackages(blob, e)),
                None => Err(e),
            }
        }
    }
}

async fn package_from_base_or_repo_or_cache(
    repo_manager: &RwLock<RepositoryManager>,
    rewriter: &RwLock<RewriteManager>,
    base_package_index: &BasePackageIndex,
    system_cache_list: &CachePackages,
    pkg_url: &PkgUrl,
    cache: PackageCache,
    blob_fetcher: BlobFetcher,
    inspect: &ResolverServiceInspectState,
) -> Result<BlobId, Status> {
    let package_inspect = inspect.resolve(pkg_url);
    if let Some(blob) = unpinned_base_package(pkg_url, base_package_index) {
        fx_log_info!("resolved {} to {} with base pin", pkg_url, blob);
        return Ok(blob);
    }

    let rewritten_url = rewrite_url(rewriter, &pkg_url)?;
    let _package_inspect = package_inspect.rewritten_url(&rewritten_url);
    package_from_repo_or_cache(
        repo_manager,
        system_cache_list,
        pkg_url,
        &rewritten_url,
        cache,
        blob_fetcher,
        inspect,
    )
    .await
    .map_err(|e| {
        let status = e.to_resolve_status();
        fx_log_err!("error resolving {} as {}: {:#}", pkg_url, rewritten_url, anyhow!(e));
        status
    })
    .map(|hash| match hash {
        HashSource::Tuf(blob) => {
            fx_log_info!("resolved {} as {} to {} with TUF", pkg_url, rewritten_url, blob);
            blob
        }
        HashSource::SystemImageCachePackages(blob, tuf_err) => {
            fx_log_info!(
                "resolved {} as {} to {} with cache_packages due to {:#}",
                pkg_url,
                rewritten_url,
                blob,
                anyhow!(tuf_err)
            );
            blob
        }
    })
}

async fn package_from_repo_or_cache(
    repo_manager: &RwLock<RepositoryManager>,
    system_cache_list: &CachePackages,
    pkg_url: &PkgUrl,
    rewritten_url: &PkgUrl,
    cache: PackageCache,
    blob_fetcher: BlobFetcher,
    inspect_state: &ResolverServiceInspectState,
) -> Result<HashSource<GetPackageError>, GetPackageError> {
    // The RwLock created by `.read()` must not exist across the `.await` (to e.g. prevent
    // deadlock). Rust temporaries are kept alive for the duration of the innermost enclosing
    // statement, so the following two lines should not be combined.
    let fut = repo_manager.read().get_package(&rewritten_url, &cache, &blob_fetcher);
    match fut.await {
        Ok(b) => Ok(HashSource::Tuf(b)),
        Err(e @ GetPackageError::Cache(CacheError::MerkleFor(MerkleForError::NotFound))) => {
            // If we can get metadata but the repo doesn't know about the package,
            // it shouldn't be in the cache, BUT some SDK customers currently rely on this behavior.
            // TODO(fxbug.dev/50764): remove this behavior.
            match missing_cache_package_disk_fallback(
                &rewritten_url,
                pkg_url,
                system_cache_list,
                inspect_state,
            ) {
                Some(blob) => Ok(HashSource::SystemImageCachePackages(blob, e)),
                None => Err(e),
            }
        }
        Err(e) => {
            // If we couldn't get TUF metadata, we might not have networking. Check in
            // system/data/cache_packages (not to be confused with pkg-cache). The cache_packages
            // manifest pkg URLs are for fuchsia.com, so do not use the rewritten URL.
            match hash_from_cache_packages_manifest(&pkg_url, system_cache_list) {
                Some(blob) => Ok(HashSource::SystemImageCachePackages(blob, e)),
                None => Err(e),
            }
        }
    }
}

fn unpinned_base_package(
    pkg_url: &PkgUrl,
    base_package_index: &BasePackageIndex,
) -> Option<BlobId> {
    // Always send Merkle-pinned requests through the resolver.
    // TODO: consider checking the Merkle pin to see if it matches the base hash.
    if pkg_url.package_hash().is_some() {
        return None;
    }
    // Make sure to strip off a "/0" variant before checking the base index.
    let stripped_url;
    let base_url = match pkg_url.variant() {
        Some("0") => {
            stripped_url = pkg_url.strip_variant();
            &stripped_url
        }
        _ => pkg_url,
    };
    match base_package_index.get(&base_url) {
        Some(base_merkle) => Some(base_merkle.clone()),
        None => None,
    }
}

// Attempts to lookup the hash of a package from `system_cache_list`, which is populated from the
// cache_packages manifest of the system_image package. The cache_packages manifest only includes
// the package path and assumes all hosts are "fuchsia.com", so this fn can only succeed on
// `PkgUrl`s with a host of "fuchsia.com".
fn hash_from_cache_packages_manifest<'a>(
    url: &PkgUrl,
    system_cache_list: &'a CachePackages,
) -> Option<BlobId> {
    if url.host() != "fuchsia.com" {
        return None;
    }

    let variant = match url.variant() {
        Some("0") => "0",
        None => "0",
        _ => return None,
    };
    let package_path = PackagePath::from_name_and_variant(url.name(), variant)
        .map_err(|e| {
            fx_log_err!(
                "cache fallback: PackagePath::name_and_variant failed for url {}: {:#}",
                url,
                anyhow!(e)
            )
        })
        .ok()?;
    let cache_hash = system_cache_list.hash_for_package(&package_path).map(Into::into);
    match (cache_hash, url.package_hash()) {
        // This arm is less useful than (Some, None) b/c generally metadata lookup for pinned URLs
        // succeeds (because generally the package from the pinned URL exists in the repo), and if
        // the blob fetching failed, then even if this fn returns success, the resolve will still
        // end up failing when we try to open the package from pkg-cache. The arm is still useful
        // if initial creation of the TUF client for fuchsia.com fails (e.g. because networking is
        // down/not yet available).
        (Some(cache), Some(url)) if cache == BlobId::from(*url) => Some(cache),
        (Some(cache), None) => Some(cache),
        _ => None,
    }
}

async fn get_hash(
    rewriter: &RwLock<RewriteManager>,
    repo_manager: &RwLock<RepositoryManager>,
    base_package_index: &BasePackageIndex,
    system_cache_list: &CachePackages,
    url: &str,
    inspect_state: &ResolverServiceInspectState,
) -> Result<BlobId, Status> {
    let pkg_url = match PkgUrl::parse(url) {
        Ok(url) => url,
        Err(err) => {
            return Err(handle_bad_package_url(err, url));
        }
    };
    // While the fuchsia-pkg:// spec allows resource paths, the package resolver should not be
    // given one.
    if pkg_url.resource().is_some() {
        fx_log_err!("package url should not contain a resource name: {}", pkg_url);
        return Err(Status::INVALID_ARGS);
    }
    trace::duration_begin!("app", "get-hash", "url" => pkg_url.to_string().as_str());
    let hash_or_status = hash_from_base_or_repo_or_cache(
        &repo_manager,
        &rewriter,
        &base_package_index,
        &system_cache_list,
        &pkg_url,
        inspect_state,
    )
    .await;
    trace::duration_end!("app", "get-hash", "status" => hash_or_status.err().unwrap_or(Status::OK).to_string().as_str());
    hash_or_status
}

/// Resolve the package.
///
/// FIXME: at the moment, we are proxying to Amber to resolve a package name and variant to a
/// merkleroot. Because of this, we cant' implement the update policy, so we just ignore it.
async fn resolve(
    cache: &PackageCache,
    package_fetcher: &PackageFetcher,
    url: String,
    dir_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status> {
    trace::duration_begin!("app", "resolve", "url" => url.as_str());
    let pkg_url = match PkgUrl::parse(&url) {
        Ok(url) => url,
        Err(err) => {
            return Err(handle_bad_package_url(err, &url));
        }
    };
    // While the fuchsia-pkg:// spec allows resource paths, the package resolver should not be
    // given one.
    if pkg_url.resource().is_some() {
        fx_log_err!("package url should not contain a resource name: {}", pkg_url);
        return Err(Status::INVALID_ARGS);
    }

    let queued_fetch = package_fetcher.push(pkg_url.clone(), ());
    let merkle_or_status = queued_fetch.await.expect("expected queue to be open");
    trace::duration_end!("app", "resolve", "status" => merkle_or_status.err().unwrap_or(Status::OK).to_string().as_str());
    let merkle = merkle_or_status?;
    let selectors = vec![];
    cache
        .open(merkle, &selectors, dir_request)
        .await
        .map_err(|err| handle_bad_package_open(err, &url))
}

/// Run a service that only resolves registered font packages.
pub async fn run_font_resolver_service(
    font_package_manager: Arc<FontPackageManager>,
    cache: PackageCache,
    package_fetcher: Arc<PackageFetcher>,
    stream: FontResolverRequestStream,
    cobalt_sender: CobaltSender,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| async {
            let mut cobalt_sender = cobalt_sender.clone();
            let FontResolverRequest::Resolve {
                package_url,
                update_policy: _,
                directory_request,
                responder,
            } = event;
            let start_time = Instant::now();
            let response = resolve_font(
                &font_package_manager,
                &cache,
                &package_fetcher,
                package_url,
                directory_request,
                cobalt_sender.clone(),
            )
            .await;

            cobalt_sender.log_event_count(
                metrics::RESOLVE_METRIC_ID,
                (
                    resolve_result_to_resolve_code(response),
                    metrics::ResolveMetricDimensionResolverType::Font,
                ),
                0,
                1,
            );

            cobalt_sender.log_elapsed_time(
                metrics::RESOLVE_DURATION_METRIC_ID,
                (
                    resolve_result_to_resolve_duration_code(&response),
                    metrics::ResolveDurationMetricDimensionResolverType::Font,
                ),
                Instant::now().duration_since(start_time).as_micros() as i64,
            );
            responder.send(&mut response.map_err(|s| s.into_raw()))?;
            Ok(())
        })
        .await
}

/// Resolve a single font package.
async fn resolve_font<'a>(
    font_package_manager: &'a Arc<FontPackageManager>,
    cache: &'a PackageCache,
    package_fetcher: &'a Arc<PackageFetcher>,
    package_url: String,
    directory_request: ServerEnd<DirectoryMarker>,
    mut cobalt_sender: CobaltSender,
) -> Result<(), Status>
where
{
    match PkgUrl::parse(&package_url) {
        Err(err) => Err(handle_bad_package_url(err, &package_url)),
        Ok(parsed_package_url) => {
            let is_font_package = font_package_manager.is_font_package(&parsed_package_url);
            cobalt_sender.log_event_count(
                metrics::IS_FONT_PACKAGE_CHECK_METRIC_ID,
                if is_font_package {
                    metrics::IsFontPackageCheckMetricDimensionResult::Font
                } else {
                    metrics::IsFontPackageCheckMetricDimensionResult::NotFont
                },
                0,
                1,
            );
            if is_font_package {
                resolve(&cache, &package_fetcher, package_url, directory_request).await
            } else {
                fx_log_err!("font resolver asked to resolve non-font package: {}", package_url);
                Err(Status::NOT_FOUND)
            }
        }
    }
}

fn handle_bad_package_url(parse_error: ParseError, pkg_url: &str) -> Status {
    fx_log_err!("failed to parse package url {:?}: {:#}", pkg_url, anyhow!(parse_error));
    Status::INVALID_ARGS
}

fn handle_bad_package_open(open_error: crate::cache::PackageOpenError, pkg_url: &str) -> Status {
    let status = (&open_error).into();
    fx_log_err!("failed to open package url {:?}: {:#}", pkg_url, anyhow!(open_error));
    status
}

fn resolve_result_to_resolve_duration_code(
    res: &Result<(), Status>,
) -> metrics::ResolveDurationMetricDimensionResult {
    match res {
        Ok(_) => metrics::ResolveDurationMetricDimensionResult::Success,
        Err(_) => metrics::ResolveDurationMetricDimensionResult::Failure,
    }
}

fn resolve_result_to_resolve_code(
    result: Result<(), Status>,
) -> metrics::ResolveMetricDimensionResult {
    match result {
        Ok(()) => metrics::ResolveMetricDimensionResult::ZxOk,
        Err(Status::INTERNAL) => metrics::ResolveMetricDimensionResult::ZxErrInternal,
        Err(Status::NOT_SUPPORTED) => metrics::ResolveMetricDimensionResult::ZxErrNotSupported,
        Err(Status::NO_RESOURCES) => metrics::ResolveMetricDimensionResult::ZxErrNoResources,
        Err(Status::NO_MEMORY) => metrics::ResolveMetricDimensionResult::ZxErrNoMemory,
        Err(Status::INTERRUPTED_RETRY) => {
            metrics::ResolveMetricDimensionResult::ZxErrInternalIntrRetry
        }
        Err(Status::INVALID_ARGS) => metrics::ResolveMetricDimensionResult::ZxErrInvalidArgs,
        Err(Status::BAD_HANDLE) => metrics::ResolveMetricDimensionResult::ZxErrBadHandle,
        Err(Status::WRONG_TYPE) => metrics::ResolveMetricDimensionResult::ZxErrWrongType,
        Err(Status::BAD_SYSCALL) => metrics::ResolveMetricDimensionResult::ZxErrBadSyscall,
        Err(Status::OUT_OF_RANGE) => metrics::ResolveMetricDimensionResult::ZxErrOutOfRange,
        Err(Status::BUFFER_TOO_SMALL) => metrics::ResolveMetricDimensionResult::ZxErrBufferTooSmall,
        Err(Status::BAD_STATE) => metrics::ResolveMetricDimensionResult::ZxErrBadState,
        Err(Status::TIMED_OUT) => metrics::ResolveMetricDimensionResult::ZxErrTimedOut,
        Err(Status::SHOULD_WAIT) => metrics::ResolveMetricDimensionResult::ZxErrShouldWait,
        Err(Status::CANCELED) => metrics::ResolveMetricDimensionResult::ZxErrCanceled,
        Err(Status::PEER_CLOSED) => metrics::ResolveMetricDimensionResult::ZxErrPeerClosed,
        Err(Status::NOT_FOUND) => metrics::ResolveMetricDimensionResult::ZxErrNotFound,
        Err(Status::ALREADY_EXISTS) => metrics::ResolveMetricDimensionResult::ZxErrAlreadyExists,
        Err(Status::ALREADY_BOUND) => metrics::ResolveMetricDimensionResult::ZxErrAlreadyBound,
        Err(Status::UNAVAILABLE) => metrics::ResolveMetricDimensionResult::ZxErrUnavailable,
        Err(Status::ACCESS_DENIED) => metrics::ResolveMetricDimensionResult::ZxErrAccessDenied,
        Err(Status::IO) => metrics::ResolveMetricDimensionResult::ZxErrIo,
        Err(Status::IO_REFUSED) => metrics::ResolveMetricDimensionResult::ZxErrIoRefused,
        Err(Status::IO_DATA_INTEGRITY) => {
            metrics::ResolveMetricDimensionResult::ZxErrIoDataIntegrity
        }
        Err(Status::IO_DATA_LOSS) => metrics::ResolveMetricDimensionResult::ZxErrIoDataLoss,
        Err(Status::IO_NOT_PRESENT) => metrics::ResolveMetricDimensionResult::ZxErrIoNotPresent,
        Err(Status::IO_OVERRUN) => metrics::ResolveMetricDimensionResult::ZxErrIoOverrun,
        Err(Status::IO_MISSED_DEADLINE) => {
            metrics::ResolveMetricDimensionResult::ZxErrIoMissedDeadline
        }
        Err(Status::IO_INVALID) => metrics::ResolveMetricDimensionResult::ZxErrIoInvalid,
        Err(Status::BAD_PATH) => metrics::ResolveMetricDimensionResult::ZxErrBadPath,
        Err(Status::NOT_DIR) => metrics::ResolveMetricDimensionResult::ZxErrNotDir,
        Err(Status::NOT_FILE) => metrics::ResolveMetricDimensionResult::ZxErrNotFile,
        Err(Status::FILE_BIG) => metrics::ResolveMetricDimensionResult::ZxErrFileBig,
        Err(Status::NO_SPACE) => metrics::ResolveMetricDimensionResult::ZxErrNoSpace,
        Err(Status::NOT_EMPTY) => metrics::ResolveMetricDimensionResult::ZxErrNotEmpty,
        Err(Status::STOP) => metrics::ResolveMetricDimensionResult::ZxErrStop,
        Err(Status::NEXT) => metrics::ResolveMetricDimensionResult::ZxErrNext,
        Err(Status::ASYNC) => metrics::ResolveMetricDimensionResult::ZxErrAsync,
        Err(Status::PROTOCOL_NOT_SUPPORTED) => {
            metrics::ResolveMetricDimensionResult::ZxErrProtocolNotSupported
        }
        Err(Status::ADDRESS_UNREACHABLE) => {
            metrics::ResolveMetricDimensionResult::ZxErrAddressUnreachable
        }
        Err(Status::ADDRESS_IN_USE) => metrics::ResolveMetricDimensionResult::ZxErrAddressInUse,
        Err(Status::NOT_CONNECTED) => metrics::ResolveMetricDimensionResult::ZxErrNotConnected,
        Err(Status::CONNECTION_REFUSED) => {
            metrics::ResolveMetricDimensionResult::ZxErrConnectionRefused
        }
        Err(Status::CONNECTION_RESET) => {
            metrics::ResolveMetricDimensionResult::ZxErrConnectionReset
        }
        Err(Status::CONNECTION_ABORTED) => {
            metrics::ResolveMetricDimensionResult::ZxErrConnectionAborted
        }
        Err(_) => metrics::ResolveMetricDimensionResult::UnexpectedZxStatusValue,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hash_from_cache_packages_manifest() {
        let hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let cache_packages = CachePackages::from_entries(vec![(
            PackagePath::from_name_and_variant("potato", "0").unwrap(),
            hash,
        )]);
        let empty_cache_packages = CachePackages::from_entries(vec![]);

        let fuchsia_url = PkgUrl::parse("fuchsia-pkg://fuchsia.com/potato").unwrap();
        let variant_zero_fuchsia_url = PkgUrl::parse("fuchsia-pkg://fuchsia.com/potato/0").unwrap();
        let other_repo_url = PkgUrl::parse("fuchsia-pkg://nope.com/potato/0").unwrap();
        let wrong_variant_fuchsia_url =
            PkgUrl::parse("fuchsia-pkg://fuchsia.com/potato/17").unwrap();

        assert_eq!(
            hash_from_cache_packages_manifest(&fuchsia_url, &cache_packages),
            Some(hash.into())
        );
        assert_eq!(
            hash_from_cache_packages_manifest(&variant_zero_fuchsia_url, &cache_packages),
            Some(hash.into())
        );
        assert_eq!(hash_from_cache_packages_manifest(&other_repo_url, &cache_packages), None);
        assert_eq!(
            hash_from_cache_packages_manifest(&wrong_variant_fuchsia_url, &cache_packages),
            None
        );
        assert_eq!(hash_from_cache_packages_manifest(&fuchsia_url, &empty_cache_packages), None);
    }
}
