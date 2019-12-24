// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::amber_connector::AmberConnect,
    crate::cache::{BlobFetcher, PackageCache},
    crate::font_package_manager::FontPackageManager,
    crate::repository_manager::RepositoryManager,
    crate::rewrite_manager::RewriteManager,
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self, DirectoryMarker},
    fidl_fuchsia_pkg::{
        FontResolverRequest, FontResolverRequestStream, PackageResolverRequest,
        PackageResolverRequestStream, UpdatePolicy,
    },
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as trace,
    fuchsia_url::pkg_url::{ParseError, PkgUrl},
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

pub async fn run_resolver_service<A>(
    rewrites: Arc<RwLock<RewriteManager>>,
    repo_manager: Arc<RwLock<RepositoryManager<A>>>,
    cache: PackageCache,
    blob_fetcher: BlobFetcher,
    mut stream: PackageResolverRequestStream,
) -> Result<(), Error>
where
    A: AmberConnect,
{
    while let Some(event) = stream.try_next().await? {
        let PackageResolverRequest::Resolve {
            package_url,
            selectors,
            update_policy,
            dir,
            responder,
        } = event;

        trace::duration_begin!("app", "resolve", "url" => package_url.as_str());
        let status = resolve(
            &rewrites,
            &repo_manager,
            &cache,
            &blob_fetcher,
            package_url,
            selectors,
            update_policy,
            dir,
        )
        .await;
        trace::duration_end!("app", "resolve", "status" => Status::from(status).to_string().as_str());

        responder.send(Status::from(status).into_raw())?;
    }

    Ok(())
}

/// Resolve the package.
///
/// FIXME: at the moment, we are proxying to Amber to resolve a package name and variant to a
/// merkleroot. Because of this, we cant' implement the update policy, so we just ignore it.
async fn resolve<'a, A>(
    rewrites: &'a Arc<RwLock<RewriteManager>>,
    repo_manager: &'a Arc<RwLock<RepositoryManager<A>>>,
    cache: &'a PackageCache,
    blob_fetcher: &'a BlobFetcher,
    pkg_url: String,
    selectors: Vec<String>,
    _update_policy: UpdatePolicy,
    dir_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status>
where
    A: AmberConnect,
{
    let url = PkgUrl::parse(&pkg_url).map_err(|err| handle_bad_package_url(err, &pkg_url))?;
    let url = rewrites.read().rewrite(url);

    // While the fuchsia-pkg:// spec allows resource paths, the package resolver should not be
    // given one.
    if url.resource().is_some() {
        fx_log_err!("package url should not contain a resource name: {}", url);
        return Err(Status::INVALID_ARGS);
    }

    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    let fut = repo_manager.read().get_package(&url, cache, blob_fetcher);
    let merkle = fut.await?;

    fx_log_info!(
        "resolved {} as {} with the selectors {:?} to {}",
        pkg_url,
        url,
        selectors,
        merkle
    );

    cache.open(merkle, &selectors, dir_request).await?;

    Ok(())
}

/// Run a service that only resolves registered font packages.
pub async fn run_font_resolver_service<A>(
    font_package_manager: Arc<FontPackageManager>,
    rewrites: Arc<RwLock<RewriteManager>>,
    repo_manager: Arc<RwLock<RepositoryManager<A>>>,
    cache: PackageCache,
    blob_fetcher: BlobFetcher,
    mut stream: FontResolverRequestStream,
) -> Result<(), Error>
where
    A: AmberConnect,
{
    while let Some(event) = stream.try_next().await? {
        let FontResolverRequest::Resolve {
            package_url,
            update_policy,
            directory_request,
            responder,
        } = event;

        let result = resolve_font(
            &font_package_manager,
            &rewrites,
            &repo_manager,
            &cache,
            &blob_fetcher,
            package_url,
            update_policy,
            directory_request,
        )
        .await;

        responder.send(Status::from(result).into_raw())?;
    }
    Ok(())
}

/// Resolve a single font package.
async fn resolve_font<'a, A>(
    font_package_manager: &'a Arc<FontPackageManager>,
    rewrites: &'a Arc<RwLock<RewriteManager>>,
    repo_manager: &'a Arc<RwLock<RepositoryManager<A>>>,
    cache: &'a PackageCache,
    blob_fetcher: &'a BlobFetcher,
    package_url: String,
    update_policy: UpdatePolicy,
    directory_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status>
where
    A: AmberConnect,
{
    match PkgUrl::parse(&package_url) {
        Err(err) => handle_bad_package_url(err, &package_url),
        Ok(parsed_package_url) => {
            if !font_package_manager.is_font_package(&parsed_package_url) {
                fx_log_err!("tried to resolve unknown font package: {}", package_url);
                Err(Status::NOT_FOUND)
            } else {
                resolve(
                    &rewrites,
                    &repo_manager,
                    &cache,
                    &blob_fetcher,
                    package_url,
                    vec![],
                    update_policy,
                    directory_request,
                )
                .await
            }
        }
    }
}

fn handle_bad_package_url(parse_error: ParseError, pkg_url: &str) -> Result<(), Status> {
    fx_log_err!("failed to parse package url {:?}: {}", pkg_url, parse_error);
    Err(Status::INVALID_ARGS)
}
