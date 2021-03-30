// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Eq, Error, Clone, Debug, PartialEq)]
pub enum FxfsError {
    #[error("Already exists")]
    AlreadyExists,
    #[error("Filesystem inconsistency")]
    Inconsistent,
    #[error("Internal error")]
    Internal,
    #[error("Expected directory")]
    NotDir,
    #[error("Not found")]
    NotFound,
}
