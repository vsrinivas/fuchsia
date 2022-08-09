// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::resolve::get_binary_and_loader_from_pkg_dir;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_process::{ResolverRequest, ResolverRequestStream};
use fuchsia_url::AbsoluteComponentUrl;
use fuchsia_zircon as zx;
use futures::prelude::*;
use tracing::warn;

/// Use /pkgfs/packages to resolve packages from the base set only
pub async fn serve(mut stream: ResolverRequestStream) {
    // Open /pkgfs/packages. This must succeed.
    let packages_dir = fuchsia_fs::open_directory_in_namespace(
        "/pkgfs/packages",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .expect("Could not open /pkgfs/packages");

    while let Some(Ok(request)) = stream.next().await {
        match request {
            ResolverRequest::Resolve { name, responder } => {
                match resolve(&packages_dir, &name).await {
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
    packages_dir: &fio::DirectoryProxy,
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

    // Break it into a package path and binary path
    let pkg_url = url.package_url();

    let pkg_path = match pkg_url.variant() {
        Some(variant) => format!("/{}/{}", pkg_url.name(), variant),

        // TODO(fxbug.dev/4002): Currently this defaults to "0" if not present, but variant
        // will eventually be required in fuchsia-pkg URLs.
        None => format!("/{}/0", pkg_url.name()),
    };

    let bin_path = url.resource();
    let pkg_url = pkg_url.to_string();

    let pkg_dir = fuchsia_fs::directory::open_directory(
        packages_dir,
        &pkg_path,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await
    .map_err(|e| {
        if let fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND) = e {
            zx::Status::NOT_FOUND
        } else {
            warn!("Could not open {} from /pkgfs/packages: {:?}", pkg_path, e);
            zx::Status::IO
        }
    })?;

    get_binary_and_loader_from_pkg_dir(&pkg_dir, &bin_path, &pkg_url).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, pseudo_directory,
        remote::remote_dir,
    };

    pub fn serve_packages_dir() -> fio::DirectoryProxy {
        let real_pkg_dir = fuchsia_fs::open_directory_in_namespace(
            "/pkg",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .unwrap();
        let dir = pseudo_directory! {
            "foo" => pseudo_directory! {
                "0" => remote_dir(real_pkg_dir)
            },
        };
        let (packages_dir, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(server_end.into_channel()),
        );
        packages_dir
    }

    #[fuchsia::test]
    async fn basic_success() {
        let packages_dir = serve_packages_dir();
        let (vmo, _) = resolve(
            &packages_dir,
            "fuchsia-pkg://fuchsia.com/foo#bin/process_resolver_unittests_no_auto_update",
        )
        .await
        .unwrap();

        // We don't know much about this test binary. Make a basic assertion.
        assert!(vmo.get_content_size().unwrap() > 0);

        // We can't make any assumptions about the libraries, especially in variations like ASAN.
    }

    #[fuchsia::test]
    async fn malformed_url() {
        let packages_dir = serve_packages_dir();
        let status = resolve(&packages_dir, "fuchsia-pkg://fuchsia.com/foo").await.unwrap_err();
        assert_eq!(status, zx::Status::INTERNAL);
        let status = resolve(&packages_dir, "abcd").await.unwrap_err();
        assert_eq!(status, zx::Status::INTERNAL);
    }

    #[fuchsia::test]
    async fn package_not_found() {
        let packages_dir = serve_packages_dir();
        let status =
            resolve(&packages_dir, "fuchsia-pkg://fuchsia.com/bar#bin/bar").await.unwrap_err();
        assert_eq!(status, zx::Status::NOT_FOUND);
    }

    #[fuchsia::test]
    async fn binary_not_found() {
        let packages_dir = serve_packages_dir();
        let status = resolve(&packages_dir, "fuchsia-pkg://fuchsia.com/foo#bin/does_not_exist")
            .await
            .unwrap_err();
        assert_eq!(status, zx::Status::NOT_FOUND);
    }
}
