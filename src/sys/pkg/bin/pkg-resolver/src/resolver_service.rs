// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        cache::{BlobFetcher, CacheError, MerkleForError, PackageCache, ToResolveStatus as _},
        font_package_manager::FontPackageManager,
        queue,
        repository_manager::GetPackageError,
        repository_manager::RepositoryManager,
        rewrite_manager::RewriteManager,
    },
    anyhow::Error,
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
    system_image::CachePackages,
};

pub type PackageFetcher = queue::WorkSender<PkgUrl, (), Result<BlobId, Status>>;

pub fn make_package_fetch_queue(
    cache: PackageCache,
    system_cache_list: Arc<CachePackages>,
    repo_manager: Arc<RwLock<RepositoryManager>>,
    rewriter: Arc<RwLock<RewriteManager>>,
    blob_fetcher: BlobFetcher,
    max_concurrency: usize,
) -> (impl Future<Output = ()>, PackageFetcher) {
    let (package_fetch_queue, package_fetcher) =
        queue::work_queue(max_concurrency, move |url: PkgUrl, _: ()| {
            let cache = cache.clone();
            let system_cache_list = Arc::clone(&system_cache_list);
            let repo_manager = Arc::clone(&repo_manager);
            let rewriter = Arc::clone(&rewriter);
            let blob_fetcher = blob_fetcher.clone();
            async move {
                Ok(package_from_repo_or_cache(
                    &repo_manager,
                    &rewriter,
                    &system_cache_list,
                    &url,
                    cache,
                    blob_fetcher,
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
    system_cache_list: Arc<CachePackages>,
    stream: PackageResolverRequestStream,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| {
            async {
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
                        let status = resolve(&cache, &package_fetcher, package_url, dir).await;
                        responder.send(Status::from(status).into_raw())?;
                        Ok(())
                    }
                    PackageResolverRequest::GetHash { package_url, responder } => {
                        match get_hash(
                            &rewriter,
                            &repo_manager,
                            &system_cache_list,
                            &package_url.url,
                        )
                        .await
                        {
                            Ok(blob_id) => {
                                fx_log_info!("hash of {} is {}", package_url.url, blob_id);
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
    let rewritten_url = rewriter.read().rewrite(url);
    // While the fuchsia-pkg:// spec allows resource paths, the package resolver should not be
    // given one.
    if rewritten_url.resource().is_some() {
        fx_log_err!("package url should not contain a resource name: {}", rewritten_url);
        return Err(Status::INVALID_ARGS);
    }
    Ok(rewritten_url)
}

async fn hash_from_repo_or_cache(
    repo_manager: &RwLock<RepositoryManager>,
    rewriter: &RwLock<RewriteManager>,
    system_cache_list: &CachePackages,
    pkg_url: &PkgUrl,
) -> Result<BlobId, Status> {
    let rewritten_url = rewrite_url(rewriter, &pkg_url)?;
    let fut = repo_manager.read().get_package_hash(&rewritten_url);
    let res = fut.await;
    let res = match res {
        Ok(b) => Ok(b),
        Err(e @ GetPackageError::Cache(CacheError::MerkleFor(MerkleForError::NotFound))) => {
            // If we can get metadata but the repo doesn't know about the package,
            // it shouldn't be in the cache, and we wouldn't trust it if it was.
            Err(e)
        }
        Err(e) => {
            // If we couldn't get TUF metadata, we might not have networking. Check in
            // system/data/cache_packages (not to be confused with the package cache).
            // The system cache doesn't know about rewrite rules, so use the original url.
            // Return the existing error if not found in the cache.
            lookup_from_system_cache(&pkg_url, system_cache_list).ok_or(e)
        }
    };
    res.map_err(|e| e.to_resolve_status())
}

async fn package_from_repo_or_cache(
    repo_manager: &RwLock<RepositoryManager>,
    rewriter: &RwLock<RewriteManager>,
    system_cache_list: &CachePackages,
    pkg_url: &PkgUrl,
    cache: PackageCache,
    blob_fetcher: BlobFetcher,
) -> Result<BlobId, Status> {
    let rewritten_url = rewrite_url(rewriter, &pkg_url)?;
    // The RwLock created by `.read()` must not exist across the `.await` (to e.g. prevent
    // deadlock). Rust temporaries are kept alive for the duration of the innermost enclosing
    // statement, so the following two lines should not be combined.
    let fut = repo_manager.read().get_package(&rewritten_url, &cache, &blob_fetcher);
    match fut.await {
        Ok(b) => Ok(b),
        Err(e @ GetPackageError::Cache(CacheError::MerkleFor(MerkleForError::NotFound))) => {
            // If we can get metadata but the repo doesn't know about the package,
            // it shouldn't be in the cache, and we wouldn't trust it if it was.
            Err(e)
        }
        Err(e) => {
            // If we couldn't get TUF metadata, we might not have networking. Check in
            // system/data/cache_packages (not to be confused with the package cache).
            // The system cache doesn't know about rewrite rules, so use the original url.
            // Return the existing error if not found in the cache.
            lookup_from_system_cache(&pkg_url, system_cache_list).ok_or(e)
        }
    }
    .map_err(|e| e.to_resolve_status())
    .map(|b| {
        fx_log_info!("resolved {} as {} to {}", pkg_url, rewritten_url, b);
        b
    })
}

fn lookup_from_system_cache<'a>(
    url: &PkgUrl,
    system_cache_list: &'a CachePackages,
) -> Option<BlobId> {
    // If the URL isn't of the form fuchsia-pkg://fuchsia.com/$name[/0], don't bother.
    if url.host() != "fuchsia.com" || url.resource().is_some() {
        return None;
    }
    // Cache packages should only have a variant of 0 or none.
    let variant = url.variant();
    let variant = match variant {
        Some("0") => "0",
        None => "0",
        _ => return None,
    };
    let package_name = url.name();
    let package_name = match package_name {
        Some(n) => n,
        None => return None,
    };

    let package_path = PackagePath::from_name_and_variant(package_name, variant);
    let package_path = match package_path {
        Ok(p) => p,
        Err(e) => {
            fx_log_err!(
                "cache fallback: PackagePath::name_and_variant failed for url {}: {}",
                url,
                e
            );
            return None;
        }
    };
    if let Some(hash) = system_cache_list.hash_for_package(&package_path) {
        fx_log_info!("found package {} using cache fallback", url);
        return Some(hash.into());
    }
    None
}

async fn get_hash(
    rewriter: &RwLock<RewriteManager>,
    repo_manager: &RwLock<RepositoryManager>,
    system_cache_list: &CachePackages,
    url: &str,
) -> Result<BlobId, Status> {
    let pkg_url = match PkgUrl::parse(url) {
        Ok(url) => url,
        Err(err) => {
            return Err(handle_bad_package_url(err, url));
        }
    };
    trace::duration_begin!("app", "get-hash", "url" => pkg_url.to_string().as_str());
    let hash_or_status =
        hash_from_repo_or_cache(&repo_manager, &rewriter, &system_cache_list, &pkg_url).await;

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

    let queued_fetch = package_fetcher.push(pkg_url.clone(), ());
    let merkle_or_status = queued_fetch.await.expect("expected queue to be open");
    trace::duration_end!("app", "resolve", "status" => merkle_or_status.err().unwrap_or(Status::OK).to_string().as_str());
    match merkle_or_status {
        Ok(merkle) => {
            let selectors = vec![];
            cache
                .open(merkle, &selectors, dir_request)
                .await
                .map_err(|err| handle_bad_package_open(err, &url))
        }
        Err(status) => Err(status),
    }
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
            let FontResolverRequest::Resolve {
                package_url,
                update_policy: _,
                directory_request,
                responder,
            } = event;

            let status = resolve_font(
                &font_package_manager,
                &cache,
                &package_fetcher,
                package_url,
                directory_request,
                cobalt_sender.clone(),
            )
            .await;
            responder.send(Status::from(status).into_raw())?;

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
    fx_log_err!("failed to parse package url {:?}: {}", pkg_url, parse_error);
    Status::INVALID_ARGS
}

fn handle_bad_package_open(open_error: crate::cache::PackageOpenError, pkg_url: &str) -> Status {
    fx_log_err!("failed to open package url {:?}: {}", pkg_url, open_error);
    Status::from(open_error)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lookup_from_system_cache() {
        let hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let cache_packages = CachePackages::from_entries(vec![(
            PackagePath::from_name_and_variant("potato", "0").unwrap(),
            hash,
        )]);
        let empty_cache_packages = CachePackages::from_entries(vec![]);

        let fuchsia_url = PkgUrl::parse("fuchsia-pkg://fuchsia.com/potato").unwrap();
        let variant_zero_fuchsia_url = PkgUrl::parse("fuchsia-pkg://fuchsia.com/potato/0").unwrap();
        let resource_url = PkgUrl::parse("fuchsia-pkg://fuchsia.com/potato#resource").unwrap();
        let other_repo_url = PkgUrl::parse("fuchsia-pkg://nope.com/potato/0").unwrap();
        let wrong_variant_fuchsia_url =
            PkgUrl::parse("fuchsia-pkg://fuchsia.com/potato/17").unwrap();

        assert_eq!(lookup_from_system_cache(&fuchsia_url, &cache_packages), Some(hash.into()));
        assert_eq!(
            lookup_from_system_cache(&variant_zero_fuchsia_url, &cache_packages),
            Some(hash.into())
        );
        assert_eq!(lookup_from_system_cache(&other_repo_url, &cache_packages), None);
        assert_eq!(lookup_from_system_cache(&wrong_variant_fuchsia_url, &cache_packages), None);
        assert_eq!(lookup_from_system_cache(&resource_url, &cache_packages), None);
        assert_eq!(lookup_from_system_cache(&fuchsia_url, &empty_cache_packages), None);
    }
}
