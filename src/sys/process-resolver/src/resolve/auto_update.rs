// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::resolve::get_binary_and_loader_from_pkg_dir;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_pkg as fpkg;
use fidl_fuchsia_process::{ResolverRequest, ResolverRequestStream};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_url::AbsoluteComponentUrl;
use fuchsia_zircon as zx;
use futures::prelude::*;
use tracing::warn;

/// Use fuchsia.pkg.PackageResolver to resolve any package from the universe
pub async fn serve(mut stream: ResolverRequestStream) {
    // Connect to fuchsia.pkg.PackageResolver. This must succeed.
    let pkg_resolver = connect_to_protocol::<fpkg::PackageResolverMarker>()
        .expect("Could not connect to fuchsia.pkg.PackageResolver");

    while let Some(Ok(request)) = stream.next().await {
        match request {
            ResolverRequest::Resolve { name, responder } => {
                match resolve(&pkg_resolver, &name).await {
                    Ok((vmo, ldsvc)) => {
                        let _ = responder.send(zx::Status::OK.into_raw(), Some(vmo), ldsvc);
                    }
                    Err(s) => {
                        let _ = responder.send(s.into_raw(), None, None);
                    }
                }
            }
        }
    }
}

async fn resolve(
    pkg_resolver: &fpkg::PackageResolverProxy,
    url: &str,
) -> Result<
    (fidl::Vmo, Option<fidl::endpoints::ClientEnd<fidl_fuchsia_ldsvc::LoaderMarker>>),
    zx::Status,
> {
    // Parse the URL
    let url = AbsoluteComponentUrl::parse(url).map_err(|_| {
        // There is a CTS test that attempts to resolve `fuchsia-pkg://example.com/mustnotexist`.
        // and expects to see INTERNAL, instead of the more suitable INVALID_ARGS.
        zx::Status::INTERNAL
    })?;

    // Break it into a package URL and binary path
    let pkg_url = url.package_url();
    let pkg_url = pkg_url.to_string();
    let bin_path = url.resource();

    // Resolve the package URL
    let (pkg_dir, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _subpackage_context =
        pkg_resolver.resolve(&pkg_url, server).await.map_err(|_| zx::Status::INTERNAL)?.map_err(
            |e| {
                if e == fpkg::ResolveError::PackageNotFound {
                    zx::Status::NOT_FOUND
                } else {
                    warn!("Could not resolve {}: {:?}", pkg_url, e);
                    zx::Status::INTERNAL
                }
            },
        )?;

    get_binary_and_loader_from_pkg_dir(&pkg_dir, bin_path, &pkg_url).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    fn serve_pkg_resolver_success() -> fpkg::PackageResolverProxy {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fpkg::PackageResolverMarker>().unwrap();
        fasync::Task::spawn(async move {
            let mut stream = server_end.into_stream().unwrap();
            let (package_url, dir, responder) =
                if let fpkg::PackageResolverRequest::Resolve { package_url, dir, responder } =
                    stream.next().await.unwrap().unwrap()
                {
                    (package_url, dir, responder)
                } else {
                    panic!("Unexpected call to PackageResolver");
                };
            assert_eq!(package_url, "fuchsia-pkg://fuchsia.com/foo");

            // Return the test's own pkg directory. This is guaranteed to support
            // the readable + executable rights needed by this test.
            fuchsia_fs::directory::open_channel_in_namespace(
                "/pkg",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
                dir,
            )
            .unwrap();

            responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
        })
        .detach();
        proxy
    }

    fn serve_pkg_resolver_fail(error: fpkg::ResolveError) -> fpkg::PackageResolverProxy {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fpkg::PackageResolverMarker>().unwrap();
        fasync::Task::spawn(async move {
            let mut stream = server_end.into_stream().unwrap();
            let (package_url, responder) =
                if let fpkg::PackageResolverRequest::Resolve { package_url, responder, .. } =
                    stream.next().await.unwrap().unwrap()
                {
                    (package_url, responder)
                } else {
                    panic!("Unexpected call to PackageResolver");
                };
            assert_eq!(package_url, "fuchsia-pkg://fuchsia.com/foo");
            responder.send(&mut Err(error)).unwrap();
        })
        .detach();
        proxy
    }

    #[fuchsia::test]
    async fn success_resolve() {
        let pkg_resolver = serve_pkg_resolver_success();
        let (vmo, _) = resolve(
            &pkg_resolver,
            "fuchsia-pkg://fuchsia.com/foo#bin/process_resolver_unittests_auto_update",
        )
        .await
        .unwrap();

        // We don't know much about this test binary. Make a basic assertion.
        assert!(vmo.get_content_size().unwrap() > 0);

        // We can't make any assumptions about the libraries, especially in variations like ASAN.
    }

    #[fuchsia::test]
    async fn malformed_url() {
        let pkg_resolver = serve_pkg_resolver_success();
        let status = resolve(&pkg_resolver, "fuchsia-pkg://fuchsia.com/foo").await.unwrap_err();
        assert_eq!(status, zx::Status::INTERNAL);
        let status = resolve(&pkg_resolver, "abcd").await.unwrap_err();
        assert_eq!(status, zx::Status::INTERNAL);
    }

    #[fuchsia::test]
    async fn package_resolver_not_found() {
        let pkg_resolver = serve_pkg_resolver_fail(fpkg::ResolveError::PackageNotFound);
        let status =
            resolve(&pkg_resolver, "fuchsia-pkg://fuchsia.com/foo#bin/bar").await.unwrap_err();
        assert_eq!(status, zx::Status::NOT_FOUND);
    }

    #[fuchsia::test]
    async fn package_resolver_internal() {
        let pkg_resolver = serve_pkg_resolver_fail(fpkg::ResolveError::NoSpace);
        let status =
            resolve(&pkg_resolver, "fuchsia-pkg://fuchsia.com/foo#bin/bar").await.unwrap_err();
        assert_eq!(status, zx::Status::INTERNAL);
    }

    #[fuchsia::test]
    async fn binary_not_found() {
        let pkg_resolver = serve_pkg_resolver_success();
        let status = resolve(&pkg_resolver, "fuchsia-pkg://fuchsia.com/foo#bin/does_not_exist")
            .await
            .unwrap_err();
        assert_eq!(status, zx::Status::NOT_FOUND);
    }
}
