// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test tools for building Fuchsia packages and TUF repositories.

#![feature(async_await)]
#![deny(missing_docs)]

mod package;
pub use crate::package::{Package, PackageBuilder, PackageDir};

mod repo;
pub use crate::repo::{
    BlobEncryptionKey, PackageEntry, Repository, RepositoryBuilder, ServedRepository,
};

mod blobfs;

pub mod pkgfs;

fn as_dir(dir: fidl::endpoints::ClientEnd<fidl_fuchsia_io::DirectoryMarker>) -> openat::Dir {
    use {
        openat::Dir,
        std::os::unix::io::{FromRawFd, IntoRawFd},
    };

    let f = fdio::create_fd(dir.into()).expect("into file");
    unsafe { Dir::from_raw_fd(f.into_raw_fd()) }
}
