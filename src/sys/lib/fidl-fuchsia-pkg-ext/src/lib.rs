// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fidl_fuchsia_pkg_ext` contains wrapper types around the auto-generated `fidl_fuchsia_pkg`
//! bindings.

mod types;
pub use crate::types::{BlobId, BlobInfo};

mod repo;
pub use crate::repo::{
    MirrorConfig, MirrorConfigBuilder, RepositoryConfig, RepositoryConfigBuilder,
    RepositoryConfigs, RepositoryKey, RepositoryStorageType, RepositoryUrl,
};

mod errors;
pub use crate::errors::{BlobIdParseError, RepositoryParseError, ResolveError};

mod measure;
pub use crate::measure::Measurable;

pub mod cache;

mod serve_fidl_iterator;
pub use serve_fidl_iterator::serve_fidl_iterator;
