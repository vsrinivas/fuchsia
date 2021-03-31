// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Eq, Error, Debug, PartialEq)]
pub enum FxfsError {
    #[error("Not found")]
    NotFound,
    #[error("Filesystem inconsistency")]
    Inconsistent,
    #[error("Expected directory")]
    NotDir,
}
