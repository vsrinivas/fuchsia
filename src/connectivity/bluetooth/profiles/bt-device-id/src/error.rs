// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

/// Errors that occur during the operation of the Device Identification component.
#[derive(Error, Debug)]
pub enum Error {
    #[error("Fidl Error: {}", .0)]
    Fidl(#[from] fidl::Error),
}
