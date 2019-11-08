// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! KeyManager manages sets of random keys that are synchronized across the
//! devices that have access to a Fuchsia account.

mod key_manager;

pub use key_manager::KeyManager;

/// A definition of the context within which the KeyManager is handling
/// requests.  The context defines the subset of key data available to the
/// client making requests.
pub struct KeyManagerContext {
    _application_url: String,
}

impl KeyManagerContext {
    /// Creates a new `KeyManagerContext` scoped to the provided
    /// `application_url`.
    pub fn new(application_url: String) -> Self {
        KeyManagerContext { _application_url: application_url }
    }
}
