// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire,
    anyhow::{anyhow, Error},
    fuchsia_zircon::{self as zx},
    mapped_vmo,
};

pub struct Resource2D {
    _mapping: mapped_vmo::Mapping,
}

impl Resource2D {
    pub fn allocate_from_request(
        cmd: &wire::VirtioGpuResourceCreate2d,
    ) -> Result<(Self, zx::Vmo), Error> {
        // Eventually we will want to use sysmem to allocate the buffers.
        let width: usize = cmd.width.get().try_into()?;
        let height: usize = cmd.height.get().try_into()?;
        let size = width.checked_mul(height).ok_or_else(|| {
            anyhow!("Overflow computing buffer size for resource {}x{}", width, height)
        })?;
        let (mapping, vmo) = mapped_vmo::Mapping::allocate(size)?;
        Ok((Self { _mapping: mapping }, vmo))
    }
}
