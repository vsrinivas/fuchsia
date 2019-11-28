// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{self, Backtrace, Context, Fail};
use std::fmt::{self, Display};

#[derive(Debug)]
pub struct Error {
    inner: Context<ErrorKind>,
}

#[derive(Clone, Eq, PartialEq, Debug, Fail)]
pub enum ErrorKind {
    #[fail(display = "error reading /system/meta")]
    ReadSystemMeta,

    #[fail(display = "error parsing /system/meta merkle")]
    ParseSystemMeta,

    #[fail(display = "error reading update package merkle")]
    ReadUpdateMeta,

    #[fail(display = "error parsing update package merkle")]
    ParseUpdateMeta,

    #[fail(display = "error connecting to PackageResolver")]
    ConnectPackageResolver,

    #[fail(display = "error creating Directory proxy for the update package")]
    CreateUpdatePackageDirectoryProxy,

    #[fail(display = "fidl error resolving update package")]
    ResolveUpdatePackageFidl,

    #[fail(display = "error resolving update package")]
    ResolveUpdatePackage,

    #[fail(display = "error creating File endpoints for the update package's 'packages' file")]
    CreateUpdatePackagePackagesEndpoint,

    #[fail(display = "error opening the update package's 'packages' file")]
    OpenUpdatePackagePackages,

    #[fail(display = "error opening the update package's 'meta' file")]
    OpenUpdatePackageMeta,

    #[fail(display = "error converting 'packages' handle into an fd")]
    CreatePackagesFd,

    #[fail(display = "error reading line from 'packages' file")]
    ReadPackages,

    #[fail(display = "error parsing latest system image merkle: {}", packages_entry)]
    ParseLatestSystemImageMerkle { packages_entry: String },

    #[fail(
        display = "could not find latest system image merkle in update package's 'packages' list"
    )]
    MissingLatestSystemImageMerkle,

    #[fail(display = "error connecting to component Launcher")]
    ConnectToLauncher,

    #[fail(display = "error launching system_updater component")]
    LaunchSystemUpdater,

    #[fail(display = "error waiting for system_updater component")]
    WaitForSystemUpdater,

    #[fail(display = "system_updater component exited with failure")]
    SystemUpdaterFailed,

    #[fail(
        display = "system_updater component exited with success, it should have rebooted the system"
    )]
    SystemUpdaterFinished,

    #[fail(display = "failed to garbage collect pkgfs")]
    PkgfsGc,

    #[fail(display = "error reading /system/data/static_packages")]
    ReadStaticPackages,
}

impl Fail for Error {
    fn name(&self) -> Option<&str> {
        self.inner.name()
    }

    fn cause(&self) -> Option<&dyn Fail> {
        self.inner.cause()
    }

    fn backtrace(&self) -> Option<&Backtrace> {
        self.inner.backtrace()
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        Display::fmt(&self.inner, f)
    }
}

impl From<ErrorKind> for Error {
    fn from(kind: ErrorKind) -> Error {
        Error { inner: Context::new(kind) }
    }
}

impl From<Context<ErrorKind>> for Error {
    fn from(inner: Context<ErrorKind>) -> Error {
        Error { inner: inner }
    }
}

#[cfg(test)]
impl Error {
    pub fn kind(&self) -> ErrorKind {
        self.inner.get_context().clone()
    }
}
