// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use failure::Error;

/// Common trait implemented by the different transport mechanisms for CTAP devices.
/// Note: the ?Send is necessary to allow implementations to make FIDL calls becasuse the auto
/// generated FIDL bindings don't require threadsafe inputs.
#[async_trait(?Send)]
pub trait CtapDevice: Sized {
    /// Returns all known CTAP devices on this transport mechanism.
    async fn devices() -> Result<Vec<Self>, Error>;

    /// Returns the path this device was created from.
    fn path(&self) -> &str;
}
