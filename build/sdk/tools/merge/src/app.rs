// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

/// The various types of errors raised by this tool.
#[derive(Debug, Error)]
pub enum Error {
    #[error("could not find file in archive: {}", name)]
    ArchiveFileNotFound { name: String },
    #[error("path already maps to a file: {}", path)]
    PathAlreadyExists { path: String },
    #[error("could not merge: {}", error)]
    CannotMerge { error: String },
    #[error("meta files differ")]
    MetaFilesDiffer,
}

/// Common result types for methods in this crate.
pub type Result<T> = std::result::Result<T, anyhow::Error>;
