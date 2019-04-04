// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fidl_fuchsia_pkg_ext` contains wrapper types around the auto-generated `fidl_fuchsia_pkg`
//! bindings.

#[cfg(test)]
#[macro_use]
extern crate proptest;

mod types;
pub use crate::types::BlobId;

mod repo;
pub use crate::repo::{
    MirrorConfig, RepositoryBlobKey, RepositoryConfig, RepositoryConfigBuilder, RepositoryKey,
};

mod errors;
pub use crate::errors::{BlobIdParseError, RepositoryParseError};
