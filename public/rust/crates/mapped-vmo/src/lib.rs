// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A convenience crate for Zircon vmo objects mapped into vmars.

#![deny(warnings)]
#![deny(missing_docs)]

extern crate fuchsia_zircon as zx;

use zx::VmarFlags;

/// An object representing a mapping into an address space.
#[derive(Debug)]
pub struct Mapping {
    addr: usize,
    size: usize,
}

impl Mapping {
    /// Create a Mapping and map it in the root address space.
    pub fn create(size: usize, flags: VmarFlags) -> Result<Self, zx::Status> {
        let vmo = zx::Vmo::create(size as u64)?;
        let addr = zx::Vmar::root_self().map(0, &vmo, 0, size, flags)?;
        Ok(Mapping {
            addr,
            size,
        })
    }

    /// Create a Mapping from an existing Vmo and map it in the root
    /// address space.
    pub fn create_from_vmo(vmo: &zx::Vmo, size: usize, flags: VmarFlags) ->
        Result<Self, zx::Status> {
        let addr = zx::Vmar::root_self().map(0, vmo, 0, size, flags)?;
        Ok(Mapping {
            addr,
            size,
        })
    }

    /// Return the size of the mapping.
    pub fn size(&self) -> usize {
        self.size
    }

    /// Return a raw pointer to the mapping.
    pub fn as_ptr(&self) -> *const u8 {
        self.addr as *const u8
    }

    /// Return a raw mutable pointer to the mapping.
    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.addr as *mut u8
    }

    // Internal helper to release the current mapping.
    //
    // Should be used with caution, as "addr" and "size"
    // will effectively have no meaning after this function
    // completes.
    unsafe fn release(&mut self) {
        zx::Vmar::root_self().unmap(self.addr, self.size as usize).ok();
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        unsafe {
            self.release()
        }
    }
}

/// An object representing a Vmo mapped into an address space.
#[derive(Debug)]
pub struct MappedVmo {
    vmo: zx::Vmo,
    mapping: Mapping,
}

impl MappedVmo {
    /// Create a MappedVmo and map it in the root address space.
    pub fn create(size: usize, flags: VmarFlags) -> Result<Self, zx::Status> {
        let vmo = zx::Vmo::create(size as u64)?;
        let mapping = Mapping::create_from_vmo(&vmo, size, flags)?;
        Ok(MappedVmo {
            vmo,
            mapping,
        })
    }

    /// Create a MappedVmo from an existing Vmo and map it in the root
    /// address space.
    pub fn create_from_vmo(vmo: zx::Vmo, size: usize, flags: VmarFlags) ->
        Result<Self, zx::Status> {
        let mapping = Mapping::create_from_vmo(&vmo, size, flags)?;
        Ok(MappedVmo {
            vmo,
            mapping,
        })
    }

    /// Resizes the Vmo and initializes a new mapping.
    ///
    /// This invalidates all raw pointers previously returned
    /// from the MappedVmo.
    pub fn resize(&mut self, size: usize, flags: VmarFlags) -> Result<(), zx::Status> {
        self.vmo.set_size(size as u64)?;
        let addr = zx::Vmar::root_self().map(0, &self.vmo, 0, size, flags)?;
        unsafe {
            self.mapping.release();
        }
        self.mapping.addr = addr;
        self.mapping.size = size;
        Ok(())
    }

    /// Return the size of the mapping.
    pub fn size(&self) -> usize {
        self.mapping.size
    }

    /// Return an immutable reference to the underlying Vmo.
    pub fn vmo(&self) -> &zx::Vmo {
        &self.vmo
    }

    /// Return a raw pointer to the mapping.
    pub fn as_ptr(&self) -> *const u8 {
        self.mapping.as_ptr()
    }

    /// Return a raw mutable pointer to the mapping.
    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.mapping.as_mut_ptr()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::slice;

    const PAGE_SIZE: usize = 4096;

    #[test]
    fn test_create() {
        let size = PAGE_SIZE;
        let flags = VmarFlags::PERM_READ | VmarFlags::PERM_WRITE;
        let mapping = Mapping::create(size, flags).unwrap();
        assert_eq!(size, mapping.size());
    }

    #[test]
    fn test_create_from_vmo() {
        let size = PAGE_SIZE;
        let flags = VmarFlags::PERM_READ | VmarFlags::PERM_WRITE;
        let vmo = zx::Vmo::create(size as u64).unwrap();
        let mapping = Mapping::create_from_vmo(&vmo, size, flags).unwrap();
        assert_eq!(size, mapping.size());
    }

    #[test]
    fn test_mapping_read_write() {
        let size = PAGE_SIZE;
        let flags = VmarFlags::PERM_READ | VmarFlags::PERM_WRITE;
        let vmo = zx::Vmo::create(size as u64).unwrap();
        let mut mapping = Mapping::create_from_vmo(&vmo, size, flags).unwrap();

        // We can write to the Vmo, and see the results in the mapping.
        let s = String::from("Hello world");
        vmo.write(s.as_bytes(), 0).unwrap();
        let output = unsafe { slice::from_raw_parts(mapping.as_ptr(), s.len()) };
        assert_eq!(s.as_bytes(), output);

        // We can write to the mapping, and see the results in the Vmo.
        let s = String::from("Goodbye world");
        unsafe { mapping.as_mut_ptr().copy_from(s.as_ptr(), s.len()) };
        let mut output = vec![0; s.len()];
        vmo.read(output.as_mut_slice(), 0).unwrap();
        assert_eq!(s.as_bytes(), output.as_slice());
    }

    #[test]
    fn test_mapped_vmo() {
        let size = PAGE_SIZE;
        let flags = VmarFlags::PERM_READ | VmarFlags::PERM_WRITE;
        let vmo = zx::Vmo::create(size as u64).unwrap();
        let mut mvmo = MappedVmo::create_from_vmo(vmo, size, flags).unwrap();

        // We can write to the Vmo, and see the results in the mapping.
        let s = String::from("Hello world");
        mvmo.vmo().write(s.as_bytes(), 0).unwrap();
        let output = unsafe { slice::from_raw_parts(mvmo.as_ptr(), s.len()) };
        assert_eq!(s.as_bytes(), output);

        // We should still be able to read from the mapping after resizing.
        mvmo.resize(size * 2, flags).unwrap();
        let output = unsafe { slice::from_raw_parts(mvmo.as_ptr(), s.len()) };
        assert_eq!(s.as_bytes(), output);

        // We can write to the mapping, and see the results in the Vmo.
        let s = String::from("Goodbye world");
        unsafe { mvmo.as_mut_ptr().copy_from(s.as_ptr(), s.len()) };
        let mut output = vec![0; s.len()];
        mvmo.vmo().read(output.as_mut_slice(), 0).unwrap();
        assert_eq!(s.as_bytes(), output.as_slice());

        // We should still be able to read from the Vmo after resizing.
        mvmo.resize(size, flags).unwrap();
        mvmo.vmo().read(output.as_mut_slice(), 0).unwrap();
        assert_eq!(s.as_bytes(), output.as_slice());
    }
}
