// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::fmt::Debug, thiserror::Error};

/// Errors encountered running test_list_tool.
#[derive(Debug, Error)]
pub enum TestListToolError {
    #[error("Facet '{0}' defined but is null")]
    NullFacet(String),

    #[error("Invalid facet: {0}, value: {1:?}")]
    InvalidFacetValue(String, String),

    #[error("Invalid package URL: {0}")]
    InvalidPackageURL(String),

    #[error("meta/ blob missing in package manifest {0}")]
    MissingMetaBlob(String),
}
