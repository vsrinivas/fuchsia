// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        cache::{
            BasePackageIndex, BlobFetcher, CacheError::*, MerkleForError, MerkleForError::*,
            ToResolveError as _, ToResolveStatus as _,
        },
        eager_package_manager::EagerPackageManager,
        font_package_manager::FontPackageManager,
        repository_manager::RepositoryManager,
        repository_manager::{GetPackageError, GetPackageError::*, GetPackageHashError},
        rewrite_manager::RewriteManager,
    },
    anyhow::{anyhow, Context as _, Error},
    async_lock::RwLock as AsyncRwLock,
    async_trait::async_trait,
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::ServerEnd,
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_metrics::MetricEvent,
    fidl_fuchsia_pkg::{
        self as fpkg, FontResolverRequest, FontResolverRequestStream, PackageResolverRequest,
        PackageResolverRequestStream, ResolveError,
    },
    fidl_fuchsia_pkg_ext::{self as pkg, BlobId},
    fuchsia_cobalt_builders::MetricEventExt as _,
    fuchsia_pkg::PackageDirectory,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as ftrace,
    fuchsia_url::{AbsolutePackageUrl, ParseError},
    fuchsia_zircon::Status,
    futures::{future::Future, stream::TryStreamExt as _},
    std::{sync::Arc, time::Instant},
    system_image::CachePackages,
};

mod inspect;
pub use inspect::ResolverService as ResolverServiceInspectState;

/// Work-queue based package resolver. When all clones of
/// [`QueuedResolver`] are dropped, the queue will resolve all remaining
/// packages and terminate its output stream.
#[derive(Clone, Debug)]
pub struct QueuedResolver {
    queue: work_queue::WorkSender<
        AbsolutePackageUrl,
        ResolveQueueContext,
        Result<(BlobId, PackageDirectory), Arc<GetPackageError>>,
    >,
    cache: pkg::cache::Client,
    base_package_index: Arc<BasePackageIndex>,
    rewriter: Arc<AsyncRwLock<RewriteManager>>,
    system_cache_list: Arc<CachePackages>,
    inspect: Arc<ResolverServiceInspectState>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct ResolveQueueContext {
    trace_id: ftrace::Id,
}

impl ResolveQueueContext {
    fn new(trace_id: ftrace::Id) -> Self {
        Self { trace_id }
    }
}

impl work_queue::TryMerge for ResolveQueueContext {
    // Merges Contexts with different trace ids. This will not leak trace durations because the
    // active duration associated with this id is started and stopped in QueuedResolver::resolve.
    // If a resolve is merged, then to tell which blob fetches the merged resolve is waiting for you
    // need to find the trace id of the first active resolve with the same package URL.
    fn try_merge(&mut self, _other: Self) -> Result<(), Self> {
        Ok(())
    }
}

/// This trait represents an instance which can be used to resolve package URLs, which typically
/// ensures that packages are available on the local filesystem and/or fetch them from remote
/// repository if necessary and possible.
#[async_trait]
pub trait Resolver: std::fmt::Debug + Sync + Sized {
    /// Resolves the given absolute package URL and returns the package
    /// directory and a resolution context for resolving subpackages.
    async fn resolve(
        &self,
        url: AbsolutePackageUrl,
        eager_package_manager: Option<&AsyncRwLock<EagerPackageManager<Self>>>,
    ) -> Result<(PackageDirectory, pkg::ResolutionContext), pkg::ResolveError>;
}

// How the package directory was resolved.
struct PackageWithSourceAndBlobId {
    package: PackageDirectory,
    source: PackageSource,
    blob_id: BlobId,
}

impl PackageWithSourceAndBlobId {
    fn base(package: PackageDirectory, blob_id: BlobId) -> Self {
        Self { package, source: PackageSource::Base, blob_id }
    }

    fn eager(package: PackageDirectory, blob_id: BlobId) -> Self {
        Self { package, source: PackageSource::Eager, blob_id }
    }

    fn tuf(package: PackageDirectory, blob_id: BlobId) -> Self {
        Self { package, source: PackageSource::Tuf, blob_id }
    }

    fn cache(package: PackageDirectory, blob_id: BlobId) -> Self {
        Self { package, source: PackageSource::Cache, blob_id }
    }
}

enum PackageSource {
    Base,
    Eager,
    Tuf,
    Cache,
}

impl PackageSource {
    fn str_for_trace(&self) -> &'static str {
        use PackageSource::*;
        match self {
            Base => "base pinned",
            Eager => "eager package manager",
            Tuf => "tuf ephemeral resolution",
            Cache => "cache fallback",
        }
    }
}

#[async_trait]
impl Resolver for QueuedResolver {
    async fn resolve(
        &self,
        pkg_url: AbsolutePackageUrl,
        eager_package_manager: Option<&AsyncRwLock<EagerPackageManager<Self>>>,
    ) -> Result<(PackageDirectory, pkg::ResolutionContext), pkg::ResolveError> {
        let trace_id = ftrace::Id::random();
        let guard = ftrace::async_enter!(
            trace_id, "app", "resolve",
            "url" => pkg_url.to_string().as_str(),
            // An async duration cannot have multiple concurrent child async durations
            // so we include the id as metadata to manually determine the
            // relationship.
            "trace_id" => u64::from(trace_id)
        );
        let resolve_res = self.resolve_with_source(pkg_url, eager_package_manager, trace_id).await;
        let error_string;
        let () = guard.end(&[
            ftrace::ArgValue::of(
                "status",
                match resolve_res {
                    Ok(_) => "success",
                    Err(ref e) => {
                        error_string = e.to_string();
                        error_string.as_str()
                    }
                },
            ),
            ftrace::ArgValue::of(
                "source",
                match resolve_res {
                    Ok(ref package_with_source) => package_with_source.source.str_for_trace(),
                    Err(_) => "no source because resolve failed",
                },
            ),
        ]);
        resolve_res.map(|pkg_with_source| {
            (
                pkg_with_source.package,
                pkg::ResolutionContext::new(pkg_with_source.blob_id.as_bytes().to_vec()),
            )
        })
    }
}

impl QueuedResolver {
    /// Creates an unbounded queue that will resolve up to `max_concurrency`
    /// packages at once.
    pub fn new(
        cache_client: pkg::cache::Client,
        base_package_index: Arc<BasePackageIndex>,
        system_cache_list: Arc<CachePackages>,
        repo_manager: Arc<AsyncRwLock<RepositoryManager>>,
        rewriter: Arc<AsyncRwLock<RewriteManager>>,
        blob_fetcher: BlobFetcher,
        max_concurrency: usize,
        inspect: Arc<ResolverServiceInspectState>,
    ) -> (impl Future<Output = ()>, Self) {
        let cache = cache_client.clone();
        let (package_fetch_queue, queue) = work_queue::work_queue(
            max_concurrency,
            move |rewritten_url: AbsolutePackageUrl, context: ResolveQueueContext| {
                let cache = cache_client.clone();
                let repo_manager = Arc::clone(&repo_manager);
                let blob_fetcher = blob_fetcher.clone();
                async move {
                    Ok(package_from_repo(
                        &repo_manager,
                        &rewritten_url,
                        cache,
                        blob_fetcher,
                        context.trace_id,
                    )
                    .await
                    .map_err(Arc::new)?)
                }
            },
        );
        let fetcher =
            Self { queue, inspect, base_package_index, cache, rewriter, system_cache_list };
        (package_fetch_queue.into_future(), fetcher)
    }

    async fn resolve_with_source(
        &self,
        pkg_url: AbsolutePackageUrl,
        eager_package_manager: Option<&AsyncRwLock<EagerPackageManager<Self>>>,
        trace_id: ftrace::Id,
    ) -> Result<PackageWithSourceAndBlobId, pkg::ResolveError> {
        // Base pin.
        let package_inspect = self.inspect.resolve(&pkg_url);
        if let Some(blob) = self.base_package_index.is_unpinned_base_package(&pkg_url) {
            let dir = self.cache.open(blob).await.map_err(|e| {
                let error = e.to_resolve_error();
                fx_log_err!("failed to open base package url {:?}: {:#}", pkg_url, anyhow!(e));
                error
            })?;
            fx_log_info!("resolved {} to {} with base pin", pkg_url, blob);
            return Ok(PackageWithSourceAndBlobId::base(dir, blob.into()));
        }

        // Rewrite the url.
        let rewritten_url =
            rewrite_url(&self.rewriter, &pkg_url).await.map_err(|e| e.to_resolve_error())?;
        let _package_inspect = package_inspect.rewritten_url(&rewritten_url);

        // Attempt to use EagerPackageManager to resolve the package.
        if let Some(eager_package_manager) = eager_package_manager {
            if let Some((dir, hash)) =
                eager_package_manager.read().await.get_package_dir(&rewritten_url).map_err(|e| {
                    fx_log_err!(
                        "failed to resolve eager package at {} as {}: {:#}",
                        pkg_url,
                        rewritten_url,
                        e
                    );
                    pkg::ResolveError::PackageNotFound
                })?
            {
                fx_log_info!(
                    "resolved {} as {} with eager package manager",
                    pkg_url,
                    rewritten_url,
                );
                return Ok(PackageWithSourceAndBlobId::eager(dir, hash.into()));
            }
        }

        // Fetch from TUF.
        let queued_fetch =
            self.queue.push(rewritten_url.clone(), ResolveQueueContext::new(trace_id));
        match queued_fetch.await.expect("expected queue to be open") {
            Ok((hash, dir)) => {
                fx_log_info!("resolved {} as {} to {} with TUF", pkg_url, rewritten_url, hash);
                Ok(PackageWithSourceAndBlobId::tuf(dir, hash))
            }
            Err(tuf_err) => {
                match self.handle_cache_fallbacks(&*tuf_err, &pkg_url, &rewritten_url).await {
                    Ok(Some((hash, pkg))) => {
                        fx_log_info!(
                            "resolved {} as {} to {} with cache_packages due to {:#}",
                            pkg_url,
                            rewritten_url,
                            hash,
                            anyhow!(tuf_err)
                        );
                        Ok(PackageWithSourceAndBlobId::cache(pkg, hash))
                    }
                    Ok(None) => {
                        let fidl_err = tuf_err.to_resolve_error();
                        fx_log_err!(
                            "failed to resolve {} as {} with TUF: {:#}",
                            pkg_url,
                            rewritten_url,
                            anyhow!(tuf_err)
                        );
                        Err(fidl_err)
                    }
                    Err(fallback_err) => {
                        let fidl_err = fallback_err.to_resolve_error();
                        fx_log_err!(
                            "failed to resolve {} as {} with cache packages fallback: {:#}. \
                            fallback was attempted because TUF failed with {:#}",
                            pkg_url,
                            rewritten_url,
                            anyhow!(fallback_err),
                            anyhow!(tuf_err)
                        );
                        Err(fidl_err)
                    }
                }
            }
        }
    }

    // On success returns the package directory and the package's hash for easier logging (the
    // package's hash could be obtained from the package directory but that would require reading
    // the package's meta file).
    async fn handle_cache_fallbacks(
        &self,
        tuf_error: &GetPackageError,
        pkg_url: &AbsolutePackageUrl,
        rewritten_url: &AbsolutePackageUrl,
    ) -> Result<Option<(BlobId, PackageDirectory)>, fidl_fuchsia_pkg_ext::cache::OpenError> {
        match tuf_error {
            Cache(MerkleFor(TargetNotFound(_))) => {
                // If we can get metadata but the repo doesn't know about the package,
                // it shouldn't be in the cache, BUT some SDK customers currently rely on this
                // behavior.
                // TODO(fxbug.dev/50764): remove this behavior.
                match missing_cache_package_disk_fallback(
                    &rewritten_url,
                    pkg_url,
                    &self.system_cache_list,
                    &self.inspect,
                ) {
                    Some(hash) => self.cache.open(hash).await.map(|pkg| Some((hash, pkg))),
                    None => Ok(None),
                }
            }
            RepoNotFound(..)
            | OpenRepo(..)
            | Cache(Fidl(..))
            | Cache(ListNeeds(..))
            | Cache(MerkleFor(MetadataNotFound { .. }))
            | Cache(MerkleFor(FetchTargetDescription(..)))
            | Cache(MerkleFor(InvalidTargetPath(..)))
            | Cache(MerkleFor(NoCustomMetadata))
            | Cache(MerkleFor(SerdeError(..))) => {
                // If we couldn't get TUF metadata, we might not have networking. Check the
                // cache packages manifest obtained from pkg-cache.
                // The manifest pkg URLs are for fuchsia.com, so do not use the rewritten URL.
                match hash_from_cache_packages_manifest(&pkg_url, &self.system_cache_list) {
                    Some(hash) => self.cache.open(hash).await.map(|pkg| Some((hash, pkg))),
                    None => Ok(None),
                }
            }
            OpenPackage(..)
            | Cache(FetchContentBlob(..))
            | Cache(FetchMetaFar(..))
            | Cache(Get(_)) => {
                // We could talk to TUF and we know there's a new version of this package,
                // but we couldn't retrieve its blobs for some reason. Refuse to fall back to
                // cache_packages and instead return an error for the resolve, which is consistent
                // with the path for packages which are not in cache_packages.
                //
                // We don't use cache_packages in production, and when developers resolve a package
                // on a bench they expect the newest version, or a failure. cache_packages are great
                // for running packages before networking is up, but for these error conditions,
                // we know we have networking because we could talk to TUF.
                Ok(None)
            }
        }
    }
}

#[derive(Clone, Debug)]
#[cfg(test)]
/// Creates a mocked PackageResolver that resolves any url using the given callback.
pub struct MockResolver {
    queue: work_queue::WorkSender<
        AbsolutePackageUrl,
        (),
        Result<(PackageDirectory, pkg::ResolutionContext), Arc<GetPackageError>>,
    >,
}

#[cfg(test)]
impl MockResolver {
    pub fn new<W, F>(callback: W) -> Self
    where
        W: Fn(AbsolutePackageUrl) -> F + Send + 'static,
        F: Future<
                Output = Result<(PackageDirectory, pkg::ResolutionContext), Arc<GetPackageError>>,
            > + Send,
    {
        let (package_fetch_queue, queue) =
            work_queue::work_queue(1, move |url, _: ()| callback(url));
        fuchsia_async::Task::spawn(package_fetch_queue.into_future()).detach();
        Self { queue }
    }
}

#[cfg(test)]
#[async_trait]
impl Resolver for MockResolver {
    async fn resolve(
        &self,
        url: AbsolutePackageUrl,
        _eager_package_manager: Option<&AsyncRwLock<EagerPackageManager<Self>>>,
    ) -> Result<(PackageDirectory, pkg::ResolutionContext), pkg::ResolveError> {
        let queued_fetch = self.queue.push(url, ());
        queued_fetch.await.expect("expected queue to be open").map_err(|e| e.to_resolve_error())
    }
}

pub async fn run_resolver_service(
    repo_manager: Arc<AsyncRwLock<RepositoryManager>>,
    rewriter: Arc<AsyncRwLock<RewriteManager>>,
    package_resolver: QueuedResolver,
    base_package_index: Arc<BasePackageIndex>,
    system_cache_list: Arc<CachePackages>,
    stream: PackageResolverRequestStream,
    cobalt_sender: ProtocolSender<MetricEvent>,
    inspect: Arc<ResolverServiceInspectState>,
    eager_package_manager: Arc<Option<AsyncRwLock<EagerPackageManager<QueuedResolver>>>>,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| async {
            let mut cobalt_sender = cobalt_sender.clone();
            match event {
                PackageResolverRequest::Resolve { package_url, dir, responder } => {
                    let start_time = Instant::now();
                    let response = resolve_and_reopen(
                        &package_resolver,
                        package_url.clone(),
                        dir,
                        eager_package_manager.as_ref().as_ref(),
                    )
                    .await;

                    cobalt_sender.send(
                        MetricEvent::builder(metrics::RESOLVE_STATUS_MIGRATED_METRIC_ID)
                            .with_event_codes(resolve_result_to_resolve_status_code(&response))
                            .as_occurrence(1),
                    );

                    cobalt_sender.send(
                        MetricEvent::builder(metrics::RESOLVE_DURATION_MIGRATED_METRIC_ID)
                            .with_event_codes((
                                resolve_result_to_resolve_duration_code(&response),
                                metrics::ResolveDurationMigratedMetricDimensionResolverType::Regular,
                            ))
                            .as_integer(
                                Instant::now().duration_since(start_time).as_micros() as i64
                            ),
                    );

                    responder.send(&mut response.map_err(|status| status.into())).with_context(
                        || {
                            format!(
                                "sending fuchsia.pkg/PackageResolver.Resolve response for {:?}",
                                package_url
                            )
                        },
                    )?;
                    Ok(())
                }

                PackageResolverRequest::ResolveWithContext {
                    package_url,
                    context,
                    dir: _,
                    responder,
                } => {
                    fx_log_err!(
                        "ResolveWithContext is not currently implemented by the resolver_service.
                         Could not resolve {} with context {:?}",
                        package_url,
                        context,
                    );
                    responder.send(&mut Err(ResolveError::Internal)).with_context(|| {
                        format!(
                            "sending fuchsia.pkg/PackageResolver.ResolveWithContext response
                                 for {} with context {:?}",
                            package_url, context
                        )
                    })?;
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
                        eager_package_manager.as_ref().as_ref(),
                    )
                    .await
                    {
                        Ok(blob_id) => {
                            responder.send(&mut Ok(blob_id.into())).with_context(|| {
                                format!(
                                    "sending fuchsia.pkg/PackageResolver.GetHash success \
                                         response for {:?}",
                                    package_url.url
                                )
                            })?;
                        }
                        Err(status) => {
                            responder.send(&mut Err(status.into_raw())).with_context(|| {
                                format!(
                                    "sending fuchsia.pkg/PackageResolver.GetHash failure \
                                         response for {:?}",
                                    package_url.url
                                )
                            })?;
                        }
                    }
                    Ok(())
                }
            }
        })
        .await
}

async fn rewrite_url(
    rewriter: &AsyncRwLock<RewriteManager>,
    url: &AbsolutePackageUrl,
) -> Result<AbsolutePackageUrl, Status> {
    Ok(rewriter.read().await.rewrite(url))
}

fn missing_cache_package_disk_fallback(
    rewritten_url: &AbsolutePackageUrl,
    pkg_url: &AbsolutePackageUrl,
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
    repo_manager: &AsyncRwLock<RepositoryManager>,
    rewriter: &AsyncRwLock<RewriteManager>,
    base_package_index: &BasePackageIndex,
    system_cache_list: &CachePackages,
    pkg_url: &AbsolutePackageUrl,
    inspect_state: &ResolverServiceInspectState,
    eager_package_manager: Option<&AsyncRwLock<EagerPackageManager<QueuedResolver>>>,
) -> Result<BlobId, Status> {
    if let Some(blob) = base_package_index.is_unpinned_base_package(pkg_url) {
        fx_log_info!("get_hash for {} to {} with base pin", pkg_url, blob);
        return Ok(blob);
    }

    let rewritten_url = rewrite_url(rewriter, pkg_url).await?;

    // Attempt to use EagerPackageManager to resolve the package.
    if let Some(eager_package_manager) = eager_package_manager {
        if let Some((_, hash)) =
            eager_package_manager.read().await.get_package_dir(&rewritten_url).map_err(|err| {
                fx_log_err!(
                    "retrieval error eager package url {} as {}: {:#}",
                    pkg_url,
                    rewritten_url,
                    anyhow!(err)
                );
                Status::NOT_FOUND
            })?
        {
            fx_log_info!(
                "get_hash for {} as {} to {} with eager package manager",
                pkg_url,
                rewritten_url,
                hash
            );
            return Ok(hash.into());
        }
    }

    hash_from_repo_or_cache(repo_manager, system_cache_list, pkg_url, &rewritten_url, inspect_state)
        .await
        .map_err(|e| {
            let status = e.to_resolve_status();
            fx_log_warn!("error getting hash {} as {}: {:#}", pkg_url, rewritten_url, anyhow!(e));
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
    repo_manager: &AsyncRwLock<RepositoryManager>,
    system_cache_list: &CachePackages,
    pkg_url: &AbsolutePackageUrl,
    rewritten_url: &AbsolutePackageUrl,
    inspect_state: &ResolverServiceInspectState,
) -> Result<HashSource<GetPackageHashError>, GetPackageHashError> {
    // The RwLock created by `.read()` must not exist across the `.await` (to e.g. prevent
    // deadlock). Rust temporaries are kept alive for the duration of the innermost enclosing
    // statement, so the following two lines should not be combined.
    let fut = repo_manager.read().await.get_package_hash(&rewritten_url);
    match fut.await {
        Ok(b) => Ok(HashSource::Tuf(b)),
        Err(e @ GetPackageHashError::MerkleFor(MerkleForError::TargetNotFound(_))) => {
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
            // the cache packages manifest (not to be confused with pkg-cache). The
            // cache packages manifest pkg URLs are for fuchsia.com, so do not use the
            // rewritten URL.
            match hash_from_cache_packages_manifest(&pkg_url, system_cache_list) {
                Some(blob) => Ok(HashSource::SystemImageCachePackages(blob, e)),
                None => Err(e),
            }
        }
    }
}

// On success returns the resolved package directory and the package's hash for convenient
// logging (the package hash could be obtained from the package directory but that would
// require reading the package's meta file).
async fn package_from_repo(
    repo_manager: &AsyncRwLock<RepositoryManager>,
    rewritten_url: &AbsolutePackageUrl,
    cache: pkg::cache::Client,
    blob_fetcher: BlobFetcher,
    trace_id: ftrace::Id,
) -> Result<(BlobId, PackageDirectory), GetPackageError> {
    // Rust temporaries are kept alive for the duration of the innermost enclosing statement, and
    // we don't want to hold the repo_manager lock while we fetch the package, so the following two
    // lines should not be combined.
    let fut =
        repo_manager.read().await.get_package(&rewritten_url, &cache, &blob_fetcher, trace_id);
    fut.await
}

// Attempts to lookup the hash of a package from `system_cache_list`, which is populated from the
// cache_packages manifest of the system_image package.
fn hash_from_cache_packages_manifest<'a>(
    url: &AbsolutePackageUrl,
    system_cache_list: &'a CachePackages,
) -> Option<BlobId> {
    // We are in the process of removing the concept of package variant
    // (generalizing fuchsia-pkg URL paths to be `(first-segment)(/more-segments)*`
    // instead of requiring that paths are `(name)/(variant)`. Towards this goal,
    // the URLs the pkg-resolver gets from the pkg-cache from `PackageCache.CachePackageIndex`
    // do not have variants. However, they are intended to match only URLs with variant of "0".
    // Additionally, pkg-resolver allows clients to not specify a variant, in which case a
    // variant of "0" will be assumed. This means that if the URL we are resolving has a
    // variant that is not "0", it should never match anything in the cache packages manifest,
    // and if the URL has a variant of "0", we should remove it before checking the cache manifest.
    let mut no_variant;
    let url = match url.variant() {
        None => url,
        Some(variant) if !variant.is_zero() => {
            return None;
        }
        Some(_) => {
            no_variant = url.clone();
            no_variant.clear_variant();
            &no_variant
        }
    };

    system_cache_list.hash_for_package(url).map(Into::into)
}

async fn get_hash(
    rewriter: &AsyncRwLock<RewriteManager>,
    repo_manager: &AsyncRwLock<RepositoryManager>,
    base_package_index: &BasePackageIndex,
    system_cache_list: &CachePackages,
    url: &str,
    inspect_state: &ResolverServiceInspectState,
    eager_package_manager: Option<&AsyncRwLock<EagerPackageManager<QueuedResolver>>>,
) -> Result<BlobId, Status> {
    let pkg_url = AbsolutePackageUrl::parse(url).map_err(|e| handle_bad_package_url(e, url))?;

    ftrace::duration_begin!("app", "get-hash", "url" => pkg_url.to_string().as_str());
    let hash_or_status = hash_from_base_or_repo_or_cache(
        &repo_manager,
        &rewriter,
        &base_package_index,
        &system_cache_list,
        &pkg_url,
        inspect_state,
        eager_package_manager,
    )
    .await;
    ftrace::duration_end!("app", "get-hash",
        "status" => hash_or_status.err().unwrap_or(Status::OK).to_string().as_str());
    hash_or_status
}

async fn resolve_and_reopen(
    package_resolver: &QueuedResolver,
    url: String,
    dir_request: ServerEnd<fio::DirectoryMarker>,
    eager_package_manager: Option<&AsyncRwLock<EagerPackageManager<QueuedResolver>>>,
) -> Result<fpkg::ResolutionContext, pkg::ResolveError> {
    let pkg_url =
        AbsolutePackageUrl::parse(&url).map_err(|e| handle_bad_package_url_error(e, &url))?;
    let (pkg, resolution_context) =
        package_resolver.resolve(pkg_url, eager_package_manager).await?;
    let () = pkg.reopen(dir_request).map_err(|e| {
        fx_log_err!("failed to re-open directory for package url {}: {:#}", url, anyhow!(e));
        pkg::ResolveError::Internal
    })?;
    Ok(resolution_context.into())
}

/// Run a service that only resolves registered font packages.
pub async fn run_font_resolver_service(
    font_package_manager: Arc<FontPackageManager>,
    package_resolver: QueuedResolver,
    stream: FontResolverRequestStream,
    cobalt_sender: ProtocolSender<MetricEvent>,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| async {
            let mut cobalt_sender = cobalt_sender.clone();
            let FontResolverRequest::Resolve { package_url, directory_request, responder } = event;
            let start_time = Instant::now();
            let response = resolve_font(
                &font_package_manager,
                &package_resolver,
                package_url,
                directory_request,
                cobalt_sender.clone(),
            )
            .await;

            let response_legacy =
                response.clone().map(|_resolution_context| ()).map_err(|s| s.to_resolve_status());
            cobalt_sender.send(
                MetricEvent::builder(metrics::RESOLVE_MIGRATED_METRIC_ID)
                    .with_event_codes((
                        resolve_result_to_resolve_code(response_legacy),
                        metrics::ResolveMigratedMetricDimensionResolverType::Font,
                    ))
                    .as_occurrence(1),
            );

            cobalt_sender.send(
                MetricEvent::builder(metrics::RESOLVE_DURATION_MIGRATED_METRIC_ID)
                    .with_event_codes((
                        resolve_result_to_resolve_duration_code(&response),
                        metrics::ResolveDurationMigratedMetricDimensionResolverType::Font,
                    ))
                    .as_integer(Instant::now().duration_since(start_time).as_micros() as i64),
            );

            responder.send(&mut response_legacy.map_err(|s| s.into_raw()))?;
            Ok(())
        })
        .await
}

/// Resolve a single font package.
async fn resolve_font<'a>(
    font_package_manager: &'a Arc<FontPackageManager>,
    package_resolver: &'a QueuedResolver,
    package_url: String,
    directory_request: ServerEnd<fio::DirectoryMarker>,
    mut cobalt_sender: ProtocolSender<MetricEvent>,
) -> Result<fpkg::ResolutionContext, pkg::ResolveError> {
    let parsed_package_url = AbsolutePackageUrl::parse(&package_url)
        .map_err(|e| handle_bad_package_url_error(e, &package_url))?;
    let is_font_package = match &parsed_package_url {
        AbsolutePackageUrl::Unpinned(unpinned) => font_package_manager.is_font_package(unpinned),
        AbsolutePackageUrl::Pinned(_) => false,
    };
    cobalt_sender.send(
        MetricEvent::builder(metrics::IS_FONT_PACKAGE_CHECK_MIGRATED_METRIC_ID)
            .with_event_codes(if is_font_package {
                metrics::IsFontPackageCheckMigratedMetricDimensionResult::Font
            } else {
                metrics::IsFontPackageCheckMigratedMetricDimensionResult::NotFont
            })
            .as_occurrence(1),
    );
    if is_font_package {
        let _resolution_context =
            resolve_and_reopen(&package_resolver, package_url, directory_request, None).await?;
        Ok(fpkg::ResolutionContext { bytes: vec![] })
    } else {
        fx_log_err!("font resolver asked to resolve non-font package: {}", package_url);
        Err(pkg::ResolveError::PackageNotFound)
    }
}

fn handle_bad_package_url_error(parse_error: ParseError, pkg_url: &str) -> pkg::ResolveError {
    fx_log_err!("failed to parse package url {:?}: {:#}", pkg_url, anyhow!(parse_error));
    pkg::ResolveError::InvalidUrl
}

fn handle_bad_package_url(parse_error: ParseError, pkg_url: &str) -> Status {
    fx_log_err!("failed to parse package url {:?}: {:#}", pkg_url, anyhow!(parse_error));
    Status::INVALID_ARGS
}

fn resolve_result_to_resolve_duration_code<T>(
    res: &Result<fpkg::ResolutionContext, T>,
) -> metrics::ResolveDurationMigratedMetricDimensionResult {
    use metrics::ResolveDurationMigratedMetricDimensionResult as EventCodes;
    match res {
        Ok(_) => EventCodes::Success,
        Err(_) => EventCodes::Failure,
    }
}

fn resolve_result_to_resolve_status_code(
    result: &Result<fpkg::ResolutionContext, pkg::ResolveError>,
) -> metrics::ResolveStatusMigratedMetricDimensionResult {
    use metrics::ResolveStatusMigratedMetricDimensionResult as EventCodes;
    match *result {
        Ok(_) => EventCodes::Success,
        Err(pkg::ResolveError::Internal) => EventCodes::Internal,
        Err(pkg::ResolveError::AccessDenied) => EventCodes::AccessDenied,
        Err(pkg::ResolveError::Io) => EventCodes::Io,
        Err(pkg::ResolveError::BlobNotFound) => EventCodes::BlobNotFound,
        Err(pkg::ResolveError::PackageNotFound) => EventCodes::PackageNotFound,
        Err(pkg::ResolveError::RepoNotFound) => EventCodes::RepoNotFound,
        Err(pkg::ResolveError::NoSpace) => EventCodes::NoSpace,
        Err(pkg::ResolveError::UnavailableBlob) => EventCodes::UnavailableBlob,
        Err(pkg::ResolveError::UnavailableRepoMetadata) => EventCodes::UnavailableRepoMetadata,
        Err(pkg::ResolveError::InvalidUrl) => EventCodes::InvalidUrl,
        Err(pkg::ResolveError::InvalidContext) => EventCodes::InvalidContext,
    }
}

fn resolve_result_to_resolve_code(
    result: Result<(), Status>,
) -> metrics::ResolveMigratedMetricDimensionResult {
    use metrics::ResolveMigratedMetricDimensionResult as EventCodes;
    match result {
        Ok(()) => EventCodes::ZxOk,
        Err(Status::INTERNAL) => EventCodes::ZxErrInternal,
        Err(Status::NOT_SUPPORTED) => EventCodes::ZxErrNotSupported,
        Err(Status::NO_RESOURCES) => EventCodes::ZxErrNoResources,
        Err(Status::NO_MEMORY) => EventCodes::ZxErrNoMemory,
        Err(Status::INTERRUPTED_RETRY) => EventCodes::ZxErrInternalIntrRetry,
        Err(Status::INVALID_ARGS) => EventCodes::ZxErrInvalidArgs,
        Err(Status::BAD_HANDLE) => EventCodes::ZxErrBadHandle,
        Err(Status::WRONG_TYPE) => EventCodes::ZxErrWrongType,
        Err(Status::BAD_SYSCALL) => EventCodes::ZxErrBadSyscall,
        Err(Status::OUT_OF_RANGE) => EventCodes::ZxErrOutOfRange,
        Err(Status::BUFFER_TOO_SMALL) => EventCodes::ZxErrBufferTooSmall,
        Err(Status::BAD_STATE) => EventCodes::ZxErrBadState,
        Err(Status::TIMED_OUT) => EventCodes::ZxErrTimedOut,
        Err(Status::SHOULD_WAIT) => EventCodes::ZxErrShouldWait,
        Err(Status::CANCELED) => EventCodes::ZxErrCanceled,
        Err(Status::PEER_CLOSED) => EventCodes::ZxErrPeerClosed,
        Err(Status::NOT_FOUND) => EventCodes::ZxErrNotFound,
        Err(Status::ALREADY_EXISTS) => EventCodes::ZxErrAlreadyExists,
        Err(Status::ALREADY_BOUND) => EventCodes::ZxErrAlreadyBound,
        Err(Status::UNAVAILABLE) => EventCodes::ZxErrUnavailable,
        Err(Status::ACCESS_DENIED) => EventCodes::ZxErrAccessDenied,
        Err(Status::IO) => EventCodes::ZxErrIo,
        Err(Status::IO_REFUSED) => EventCodes::ZxErrIoRefused,
        Err(Status::IO_DATA_INTEGRITY) => EventCodes::ZxErrIoDataIntegrity,
        Err(Status::IO_DATA_LOSS) => EventCodes::ZxErrIoDataLoss,
        Err(Status::IO_NOT_PRESENT) => EventCodes::ZxErrIoNotPresent,
        Err(Status::IO_OVERRUN) => EventCodes::ZxErrIoOverrun,
        Err(Status::IO_MISSED_DEADLINE) => EventCodes::ZxErrIoMissedDeadline,
        Err(Status::IO_INVALID) => EventCodes::ZxErrIoInvalid,
        Err(Status::BAD_PATH) => EventCodes::ZxErrBadPath,
        Err(Status::NOT_DIR) => EventCodes::ZxErrNotDir,
        Err(Status::NOT_FILE) => EventCodes::ZxErrNotFile,
        Err(Status::FILE_BIG) => EventCodes::ZxErrFileBig,
        Err(Status::NO_SPACE) => EventCodes::ZxErrNoSpace,
        Err(Status::NOT_EMPTY) => EventCodes::ZxErrNotEmpty,
        Err(Status::STOP) => EventCodes::ZxErrStop,
        Err(Status::NEXT) => EventCodes::ZxErrNext,
        Err(Status::ASYNC) => EventCodes::ZxErrAsync,
        Err(Status::PROTOCOL_NOT_SUPPORTED) => EventCodes::ZxErrProtocolNotSupported,
        Err(Status::ADDRESS_UNREACHABLE) => EventCodes::ZxErrAddressUnreachable,
        Err(Status::ADDRESS_IN_USE) => EventCodes::ZxErrAddressInUse,
        Err(Status::NOT_CONNECTED) => EventCodes::ZxErrNotConnected,
        Err(Status::CONNECTION_REFUSED) => EventCodes::ZxErrConnectionRefused,
        Err(Status::CONNECTION_RESET) => EventCodes::ZxErrConnectionReset,
        Err(Status::CONNECTION_ABORTED) => EventCodes::ZxErrConnectionAborted,
        Err(_) => EventCodes::UnexpectedZxStatusValue,
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_url::PinnedAbsolutePackageUrl};

    #[test]
    fn test_hash_from_cache_packages_manifest() {
        let hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let cache_packages = CachePackages::from_entries(vec![
            PinnedAbsolutePackageUrl::new_with_path(
                "fuchsia-pkg://fuchsia.com".parse().unwrap(),
                "/potato",
                hash,
            )
            .unwrap(),
            PinnedAbsolutePackageUrl::new_with_path(
                "fuchsia-pkg://other.com".parse().unwrap(),
                "/potato",
                hash,
            )
            .unwrap(),
        ]);
        let empty_cache_packages = CachePackages::from_entries(vec![]);

        let fuchsia_url = AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/potato").unwrap();
        let variant_nonzero_fuchsia_url =
            AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/potato/1").unwrap();
        let variant_zero_fuchsia_url =
            AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/potato/0").unwrap();
        let other_repo_url = AbsolutePackageUrl::parse("fuchsia-pkg://other.com/potato").unwrap();
        assert_eq!(
            hash_from_cache_packages_manifest(&fuchsia_url, &cache_packages),
            Some(hash.into())
        );
        assert_eq!(
            hash_from_cache_packages_manifest(&variant_zero_fuchsia_url, &cache_packages),
            Some(hash.into())
        );
        assert_eq!(
            hash_from_cache_packages_manifest(&variant_nonzero_fuchsia_url, &cache_packages),
            None
        );
        assert_eq!(
            hash_from_cache_packages_manifest(&other_repo_url, &cache_packages),
            Some(hash.into())
        );
        assert_eq!(hash_from_cache_packages_manifest(&fuchsia_url, &empty_cache_packages), None);
    }

    #[test]
    fn test_hash_from_cache_packages_manifest_with_zero_variant() {
        let hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let cache_packages = CachePackages::from_entries(vec![PinnedAbsolutePackageUrl::new(
            "fuchsia-pkg://fuchsia.com".parse().unwrap(),
            "potato".parse().unwrap(),
            Some(fuchsia_url::PackageVariant::zero()),
            hash,
        )]);
        let empty_cache_packages = CachePackages::from_entries(vec![]);

        let fuchsia_url = AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/potato").unwrap();
        let variant_nonzero_fuchsia_url =
            AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/potato/1").unwrap();
        let variant_zero_fuchsia_url =
            AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/potato/0").unwrap();
        let other_repo_url = AbsolutePackageUrl::parse("fuchsia-pkg://nope.com/potato/0").unwrap();
        // hash_from_cache_packages_manifest removes variant from URL provided, and
        // since CachePackages is initialized with a variant and will only resolve url to a hash
        // if the /0 variant is provided.
        assert_eq!(hash_from_cache_packages_manifest(&fuchsia_url, &cache_packages), None);
        assert_eq!(
            hash_from_cache_packages_manifest(&variant_zero_fuchsia_url, &cache_packages),
            None
        );
        assert_eq!(
            hash_from_cache_packages_manifest(&variant_nonzero_fuchsia_url, &cache_packages),
            None
        );
        assert_eq!(hash_from_cache_packages_manifest(&other_repo_url, &cache_packages), None);
        assert_eq!(hash_from_cache_packages_manifest(&fuchsia_url, &empty_cache_packages), None);
    }
}
