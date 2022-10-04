// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fidl_fuchsia_pkg_ext` contains wrapper types around the auto-generated `fidl_fuchsia_pkg`
//! bindings.

mod types;
pub use crate::types::{BlobId, BlobInfo, CupData, ResolutionContext};

mod repo;
pub use crate::repo::{
    MirrorConfig, MirrorConfigBuilder, RepositoryConfig, RepositoryConfigBuilder,
    RepositoryConfigs, RepositoryKey, RepositoryStorageType, RepositoryUrl,
};

mod errors;
pub use crate::errors::{
    BlobIdParseError, CupMissingField, MirrorConfigError, RepositoryParseError, ResolveError,
};

mod measure;
pub use crate::measure::Measurable;

pub mod base_package_index;
pub use crate::base_package_index::BasePackageIndex;

pub mod cache;

mod serve_fidl_iterator;
pub use serve_fidl_iterator::{serve_fidl_iterator_from_slice, serve_fidl_iterator_from_stream};

mod fidl_iterator_to_stream;
pub use fidl_iterator_to_stream::fidl_iterator_to_stream;
