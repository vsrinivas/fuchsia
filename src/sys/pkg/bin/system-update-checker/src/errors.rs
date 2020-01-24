// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Clone, Eq, PartialEq, Debug, Error)]
pub enum Error {
    #[error("error reading /system/meta")]
    ReadSystemMeta,

    #[error("error parsing /system/meta merkle")]
    ParseSystemMeta,

    #[error("error reading update package merkle")]
    ReadUpdateMeta,

    #[error("error parsing update package merkle")]
    ParseUpdateMeta,

    #[error("error connecting to PackageResolver")]
    ConnectPackageResolver,

    #[error("error creating Directory proxy for the update package")]
    CreateUpdatePackageDirectoryProxy,

    #[error("fidl error resolving update package")]
    ResolveUpdatePackageFidl,

    #[error("error resolving update package")]
    ResolveUpdatePackage,

    #[error("error creating File endpoints for the update package's 'packages' file")]
    CreateUpdatePackagePackagesEndpoint,

    #[error("error opening the update package's 'packages' file")]
    OpenUpdatePackagePackages,

    #[error("error opening the update package's 'meta' file")]
    OpenUpdatePackageMeta,

    #[error("error converting 'packages' handle into an fd")]
    CreatePackagesFd,

    #[error("error reading line from 'packages' file")]
    ReadPackages,

    #[error("error parsing latest system image merkle: {}", packages_entry)]
    ParseLatestSystemImageMerkle { packages_entry: String },

    #[error("could not find latest system image merkle in update package's 'packages' list")]
    MissingLatestSystemImageMerkle,

    #[error("error connecting to component Launcher")]
    ConnectToLauncher,

    #[error("error launching system_updater component")]
    LaunchSystemUpdater,

    #[error("error waiting for system_updater component")]
    WaitForSystemUpdater,

    #[error("system_updater component exited with failure")]
    SystemUpdaterFailed,

    #[error("system_updater component exited with success, it should have rebooted the system")]
    SystemUpdaterFinished,

    #[error("error reading /system/data/static_packages")]
    ReadStaticPackages,
}
