// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Crate-wide types.

use fidl_fuchsia_auth_account::Lifetime;
use std::path::PathBuf;

/// Enum describing whether the account is ephemeral or persistent. If persistent, contains the
/// persistent storage location.
#[derive(Clone, PartialEq)]
pub enum AccountLifetime {
    /// The account lives on disk (in account_dir) and in memory.
    Persistent { account_dir: PathBuf },
    /// The account lives only in memory.
    Ephemeral,
}

impl From<&AccountLifetime> for Lifetime {
    fn from(lifetime: &AccountLifetime) -> Lifetime {
        match lifetime {
            AccountLifetime::Persistent { .. } => Lifetime::Persistent,
            AccountLifetime::Ephemeral => Lifetime::Ephemeral,
        }
    }
}
