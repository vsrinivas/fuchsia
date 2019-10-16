// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test tools for building Fuchsia packages and TUF repositories.

#![deny(missing_docs)]

mod package;
pub use crate::package::{Package, PackageBuilder, PackageDir, VerificationError};

mod repo;
pub use crate::repo::{
    BlobEncryptionKey, PackageEntry, Repository, RepositoryBuilder, ServedRepository,
};
pub mod serve;

pub mod blobfs;

pub mod pkgfs;

fn as_dir(dir: fidl::endpoints::ClientEnd<fidl_fuchsia_io::DirectoryMarker>) -> openat::Dir {
    use {
        openat::Dir,
        std::os::unix::io::{FromRawFd, IntoRawFd},
    };

    let f = fdio::create_fd(dir.into()).expect("into file");
    unsafe { Dir::from_raw_fd(f.into_raw_fd()) }
}

fn as_file(dir: fidl::endpoints::ClientEnd<fidl_fuchsia_io::DirectoryMarker>) -> std::fs::File {
    fdio::create_fd(dir.into_channel().into()).expect("into file")
}

struct ProcessKillGuard {
    process: fuchsia_zircon::Process,
}

impl Drop for ProcessKillGuard {
    fn drop(&mut self) {
        use fuchsia_zircon::Task;
        let _ = self.process.kill();
    }
}

impl std::ops::Deref for ProcessKillGuard {
    type Target = fuchsia_zircon::Process;
    fn deref(&self) -> &Self::Target {
        &self.process
    }
}

impl From<fuchsia_zircon::Process> for ProcessKillGuard {
    fn from(process: fuchsia_zircon::Process) -> Self {
        Self { process }
    }
}
