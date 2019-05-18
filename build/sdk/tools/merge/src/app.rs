// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;

/// The various types of errors raised by this tool.
#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "could not find file in archive: {}", name)]
    ArchiveFileNotFound {
        name: String,
    },
    #[fail(display = "path already maps to a file: {}", path)]
    PathAlreadyExists  {
        path: String,
    },
}

/// Common result types for methods in this crate.
pub type Result<T> = std::result::Result<T, failure::Error>;
