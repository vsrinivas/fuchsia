// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A convenience crate for Zircon VMO objects mapped into memory.

#![deny(missing_docs)]

use {
    fuchsia_runtime::vmar_root_self,
    fuchsia_zircon::{self as zx, AsHandleRef},
    shared_buffer::SharedBuffer,
    std::ffi::CString,
    std::ops::{Deref, DerefMut},
};

/// A safe wrapper around a mapped region of memory.
///
/// Note: this type implements `Deref`/`DerefMut` to the `SharedBuffer`
/// type, which allows reading/writing from the underlying memory.
/// Aside from creation and the `Drop` impl, all of the interesting
/// functionality of this type is offered via `SharedBuffer`.
#[derive(Debug)]
pub struct Mapping {
    buffer: SharedBuffer,
}

impl Deref for Mapping {
    type Target = SharedBuffer;
    fn deref(&self) -> &Self::Target {
        &self.buffer
    }
}

impl DerefMut for Mapping {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.buffer
    }
}

impl Mapping {
    /// Create a `Mapping` and map it in the root address space.
    /// Returns the VMO that was mapped.
    ///
    /// The resulting VMO will not be resizeable.
    pub fn allocate(size: usize) -> Result<(Self, zx::Vmo), zx::Status> {
        let vmo = zx::Vmo::create(size as u64)?;
        let flags = zx::VmarFlags::PERM_READ
            | zx::VmarFlags::PERM_WRITE
            | zx::VmarFlags::MAP_RANGE
            | zx::VmarFlags::REQUIRE_NON_RESIZABLE;
        let mapping = Self::create_from_vmo(&vmo, size, flags)?;
        Ok((mapping, vmo))
    }

    /// Create a `Mapping` and map it in the root address space.
    /// Returns the VMO that was mapped.
    ///
    /// The resulting VMO will not be resizeable.
    pub fn allocate_with_name(size: usize, name: &str) -> Result<(Self, zx::Vmo), zx::Status> {
        let cname = CString::new(name).map_err(|_e| Err(zx::Status::INVALID_ARGS))?;
        let vmo = zx::Vmo::create(size as u64)?;
        vmo.set_name(&cname)?;
        let flags = zx::VmarFlags::PERM_READ
            | zx::VmarFlags::PERM_WRITE
            | zx::VmarFlags::MAP_RANGE
            | zx::VmarFlags::REQUIRE_NON_RESIZABLE;
        let mapping = Self::create_from_vmo(&vmo, size, flags)?;
        Ok((mapping, vmo))
    }

    /// Create a `Mapping` from an existing VMO.
    ///
    /// Requires that the VMO was not created with the `RESIZABLE`
    /// option, and returns `ZX_ERR_NOT_SUPPORTED` otherwise.
    pub fn create_from_vmo(
        vmo: &zx::Vmo,
        size: usize,
        flags: zx::VmarFlags,
    ) -> Result<Self, zx::Status> {
        let flags = flags | zx::VmarFlags::REQUIRE_NON_RESIZABLE;
        let addr = vmar_root_self().map(0, &vmo, 0, size, flags)?;

        // Safety:
        //
        // The memory behind this `SharedBuffer` is only accessible via
        // methods on `SharedBuffer`.
        //
        // The underlying memory is accessible during any accesses to `SharedBuffer`:
        // - It is only unmapped on `drop`
        // - `SharedBuffer` is never exposed in a way that would allow it to live longer than
        //   the `Mapping` itself
        // - The underlying VMO is non-resizeable.
        let buffer = unsafe { SharedBuffer::new(addr as *mut u8, size) };
        Ok(Mapping { buffer })
    }

    /// Return the size of the mapping.
    pub fn len(&self) -> usize {
        self.buffer.len()
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        let (addr, size): (*mut u8, usize) = self.buffer.as_ptr_len();
        let addr = addr as usize;

        // Safety:
        //
        // The memory behind this `SharedBuffer` is only accessible
        // via references to the internal `SharedBuffer`, which must
        // have all been invalidated at this point. The memory is
        // therefore safe to unmap.
        unsafe {
            let _ = vmar_root_self().unmap(addr, size);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon as zx;

    const PAGE_SIZE: usize = 4096;

    #[test]
    fn test_create() {
        let size = PAGE_SIZE;
        let (mapping, _vmo) = Mapping::allocate(size).unwrap();
        assert_eq!(size, mapping.len());
    }

    #[test]
    fn test_create_from_vmo() {
        let size = PAGE_SIZE;
        let flags = zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE;
        {
            // Mapping::create_from_vmo requires a non-resizable vmo
            let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, size as u64).unwrap();
            let status = Mapping::create_from_vmo(&vmo, size, flags).unwrap_err();
            assert_eq!(status, zx::Status::NOT_SUPPORTED);
        }
        {
            let vmo = zx::Vmo::create(size as u64).unwrap();
            let mapping = Mapping::create_from_vmo(&vmo, size, flags).unwrap();
            assert_eq!(size, mapping.len());
        }
    }

    #[test]
    fn test_create_with_name() {
        let size = PAGE_SIZE;
        let (mapping, vmo) = Mapping::allocate_with_name(size, "TestName").unwrap();
        assert_eq!(size, mapping.len());
        assert_eq!(CString::new("TestName").unwrap(), vmo.get_name().expect("Has name"));
        let res = Mapping::allocate_with_name(size, "Invalid\0TestName");
        assert_eq!(zx::Status::INVALID_ARGS, res.unwrap_err());
    }

    #[test]
    fn test_mapping_read_write() {
        let size = PAGE_SIZE;
        let (mapping, vmo) = Mapping::allocate(size).unwrap();

        let mut buf = [0; 128];

        // We can write to the Vmo, and see the results in the mapping.
        let s = b"Hello world";
        vmo.write(s, 0).unwrap();
        let slice = &mut buf[0..s.len()];
        mapping.read(slice);
        assert_eq!(s, slice);

        // We can write to the mapping, and see the results in the Vmo.
        let s = b"Goodbye world";
        mapping.write(s);
        let slice = &mut buf[0..s.len()];
        vmo.read(slice, 0).unwrap();
        assert_eq!(s, slice);
    }
}
