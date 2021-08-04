// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_syslog::fx_log_warn, fuchsia_zircon as zx};

/// A safe, RAII wrapper around a VMAR mapping.
pub struct MappedVmo {
    address: usize,
    size: usize,
}

impl MappedVmo {
    /// Map |vmo| with |size| into the root vmar. It will be mapped readable and writable.
    /// This is a wrapper around |zx_vmar_map|.
    pub fn new(vmo: &zx::Vmo, size: usize) -> Result<Self, zx::Status> {
        let address = fuchsia_runtime::vmar_root_self().map(
            0,
            vmo,
            0,
            size,
            zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
        )?;

        Ok(MappedVmo { address, size })
    }

    /// Get a slice that represents the VMO.
    pub fn as_slice<'a>(&'a self) -> &'a [u8] {
        let addr = self.address as *const u8;
        // Safe because we mapped this area in new().
        unsafe { std::slice::from_raw_parts(addr, self.size) }
    }
}

impl Drop for MappedVmo {
    fn drop(&mut self) {
        // Safe because we have a mutable reference, which means that all references to the VMAR
        // region (returned by |as_slice()|) will have been dropped.
        unsafe {
            fuchsia_runtime::vmar_root_self().unmap(self.address, self.size).unwrap_or_else(|e| {
                // Not much we can do other than warn here.
                fx_log_warn!("Failed to unmap VMAR: {:?}", e);
            });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_map_vmo() {
        let buffer: [u8; 10] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
        let vmo = zx::Vmo::create(buffer.len() as u64).unwrap();
        vmo.write(&buffer, 0).unwrap();

        let mapping = MappedVmo::new(&vmo, buffer.len()).expect("mapping vmo ok");
        let slice = mapping.as_slice();

        assert_eq!(buffer, slice);
    }
}
