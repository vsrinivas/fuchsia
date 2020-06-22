// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_url::pkg_url::PkgUrl;
use fuchsia_zircon as zx;
use std::io;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("reading /pkgfs/system/meta")]
    ReadSystemMeta(#[source] io::Error),

    #[error("parsing /pkgfs/system/meta merkle")]
    ParseSystemMeta(#[source] fuchsia_merkle::ParseHashError),

    #[error("connecting to PackageResolver")]
    ConnectPackageResolver(#[source] anyhow::Error),

    #[error("system-updater component exited with failure")]
    SystemUpdaterFailed,

    #[error("system-updater component exited with success, it should have rebooted the system")]
    SystemUpdaterFinished,

    #[error("reading /pkgfs/system/data/static_packages")]
    ReadStaticPackages(#[source] io::Error),

    #[error("update package")]
    UpdatePackage(#[from] UpdatePackage),
}

#[derive(Debug, thiserror::Error)]
pub enum UpdatePackage {
    #[error("creating Directory proxy to resolve the update package")]
    CreateDirectoryProxy(#[source] fidl::Error),

    #[error("fidl error resolving update package")]
    ResolveFidl(#[source] fidl::Error),

    #[error("resolving update package")]
    Resolve(#[source] zx::Status),

    #[error("extracting the 'packages' manifest")]
    ExtractPackagesManifest(#[source] update_package::ParsePackageError),

    #[error("could not find system_image/0 in 'packages' manifest")]
    MissingSystemImage,

    #[error("system_image/0 pkg url was not merkle pinned: {0}")]
    UnPinnedSystemImage(PkgUrl),

    #[error("parsing the system_image merkle from the 'packages' manifest")]
    ParseSystemImageMerkle(#[source] fuchsia_merkle::ParseHashError),

    #[error("extracting package hash")]
    Hash(#[source] update_package::HashError),
}
