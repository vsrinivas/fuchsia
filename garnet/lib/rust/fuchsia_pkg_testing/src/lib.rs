// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test tools for building Fuchsia packages and TUF repositories.

#![feature(async_await, await_macro)]
#![deny(missing_docs)]

mod package;
pub use crate::package::{Package, PackageBuilder, PackageDir, PackageSigningKey};

mod repo;
pub use crate::repo::{BlobEncryptionKey, PackageEntry, Repository, RepositoryBuilder};
