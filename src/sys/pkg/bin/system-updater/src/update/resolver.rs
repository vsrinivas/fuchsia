// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_pkg::{PackageResolverProxy, UpdatePolicy},
    fuchsia_url::pkg_url::PkgUrl,
    futures::prelude::*,
    thiserror::Error,
    update_package::UpdatePackage,
};

const CONCURRENT_PACKAGE_RESOLVES: usize = 5;

/// Error encountered while resolving a package.
#[derive(Debug, Error)]
pub enum ResolveError {
    #[error("while performing resolve call")]
    Fidl(#[source] fidl::Error),

    #[error("resolve responded with")]
    Status(#[source] fuchsia_zircon::Status),
}

/// Resolves the update package given by `url` through the pkg_resolver.
pub(super) async fn resolve_update_package(
    pkg_resolver: &PackageResolverProxy,
    url: &PkgUrl,
) -> Result<UpdatePackage, Error> {
    let dir =
        resolve_package(pkg_resolver, &url).await.context("while resolving the update package")?;
    Ok(UpdatePackage::new(dir))
}

/// Resolves each package URL through the package resolver with some concurrency, yielding results
/// of the resolved package directories. The output order is not guaranteed to match the input
/// order.
pub(super) fn resolve_packages<'a, I>(
    pkg_resolver: &'a PackageResolverProxy,
    urls: I,
) -> impl Stream<Item = Result<DirectoryProxy, Error>> + 'a
where
    I: 'a + Iterator<Item = &'a PkgUrl>,
{
    stream::iter(urls)
        .map(move |url| resolve_package(pkg_resolver, url))
        .buffer_unordered(CONCURRENT_PACKAGE_RESOLVES)
}

async fn resolve_package(
    pkg_resolver: &PackageResolverProxy,
    url: &PkgUrl,
) -> Result<DirectoryProxy, Error> {
    let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;
    let res = pkg_resolver.resolve(
        &url.to_string(),
        &mut std::iter::empty(),
        &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
        dir_server_end,
    );
    let res =
        res.await.map_err(ResolveError::Fidl).with_context(|| format!("resolving {}", url))?;

    let () = res
        .map_err(|raw| ResolveError::Status(fuchsia_zircon::Status::from_raw(raw)))
        .with_context(|| format!("resolving {}", url))?;

    Ok(dir)
}
