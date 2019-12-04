// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_mem as mem,
    fuchsia_zircon::{self as zx, HandleBased},
};

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct AssetId(pub u32);

impl From<AssetId> for u32 {
    fn from(id: AssetId) -> u32 {
        id.0
    }
}

#[derive(Debug)]
pub struct Asset {
    pub id: AssetId,
    pub buffer: mem::Buffer,
}

impl Asset {
    /// Creates a new [`Asset`] instance with the same `id` and cloned of `buffer`.
    /// Returns [`Error`] if the `buffer` clone fails.
    pub fn try_clone(&self) -> Result<Asset, Error> {
        Ok(Asset { id: self.id, buffer: self.clone_buffer()? })
    }

    fn clone_buffer(&self) -> Result<mem::Buffer, Error> {
        let vmo_rights = zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP;
        let vmo = self
            .buffer
            .vmo
            .duplicate_handle(vmo_rights)
            .context("Failed to duplicate VMO handle.")?;
        Ok(mem::Buffer { vmo, size: self.buffer.size })
    }
}
