// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta vmo objects.

use {HandleBase, Handle, HandleRef, Status};
use {sys, into_result};

/// An object representing a Magenta
/// [virtual memory object](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/vm_object.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct Vmo(Handle);

impl HandleBase for Vmo {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Vmo(handle)
    }
}

impl Vmo {
    /// Create a virtual memory object.
    ///
    /// Wraps the
    /// `mx_vmo_create`
    /// syscall. See the
    /// [Shared Memory: Virtual Memory Objects (VMOs)](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md#Shared-Memory_Virtual-Memory-Objects-VMOs)
    /// for more information.
    pub fn create(size: u64, options: VmoOpts) -> Result<Vmo, Status> {
        let mut handle = 0;
        let status = unsafe { sys::mx_vmo_create(size, options as u32, &mut handle) };
        into_result(status, ||
            Vmo::from_handle(Handle(handle)))
    }

    /// Read from a virtual memory object.
    ///
    /// Wraps the `mx_vmo_read` syscall.
    pub fn read(&self, data: &mut [u8], offset: u64) -> Result<usize, Status> {
        unsafe {
            let mut actual = 0;
            let status = sys::mx_vmo_read(self.raw_handle(), data.as_mut_ptr(),
                offset, data.len(), &mut actual);
            into_result(status, || actual)
        }
    }

    /// Write to a virtual memory object.
    ///
    /// Wraps the `mx_vmo_write` syscall.
    pub fn write(&self, data: &[u8], offset: u64) -> Result<usize, Status> {
        unsafe {
            let mut actual = 0;
            let status = sys::mx_vmo_write(self.raw_handle(), data.as_ptr(),
                offset, data.len(), &mut actual);
            into_result(status, || actual)
        }
    }

    /// Get the size of a virtual memory object.
    ///
    /// Wraps the `mx_vmo_get_size` syscall.
    pub fn get_size(&self) -> Result<u64, Status> {
        let mut size = 0;
        let status = unsafe { sys::mx_vmo_get_size(self.raw_handle(), &mut size) };
        into_result(status, || size)
    }

    /// Attempt to change the size of a virtual memory object.
    ///
    /// Wraps the `mx_vmo_set_size` syscall.
    pub fn set_size(&self, size: u64) -> Result<(), Status> {
        let status = unsafe { sys::mx_vmo_set_size(self.raw_handle(), size) };
        into_result(status, || ())
    }
}

/// Options for creating virtual memory objects. None supported yet.
#[repr(u32)]
pub enum VmoOpts {
    /// Default options.
    Default = 0,
}

impl Default for VmoOpts {
    fn default() -> Self {
        VmoOpts::Default
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vmo_get_size() {
        let size = 16 * 1024 * 1024;
        let vmo = Vmo::create(size, VmoOpts::Default).unwrap();
        assert_eq!(size, vmo.get_size().unwrap());
    }

    #[test]
    fn vmo_set_size() {
        let start_size = 12;
        let vmo = Vmo::create(start_size, VmoOpts::Default).unwrap();
        assert_eq!(start_size, vmo.get_size().unwrap());

        // Change the size and make sure the new size is reported
        let new_size = 23;
        assert!(vmo.set_size(new_size).is_ok());
        assert_eq!(new_size, vmo.get_size().unwrap());
    }

    #[test]
    fn vmo_read_write() {
        let mut vec1 = vec![0; 16];
        let vmo = Vmo::create(vec1.len() as u64, VmoOpts::Default).unwrap();
        vmo.write(b"abcdef", 0).unwrap();
        assert_eq!(16, vmo.read(&mut vec1, 0).unwrap());
        assert_eq!(b"abcdef", &vec1[0..6]);
        vmo.write(b"123", 2).unwrap();
        assert_eq!(16, vmo.read(&mut vec1, 0).unwrap());
        assert_eq!(b"ab123f", &vec1[0..6]);
        assert_eq!(15, vmo.read(&mut vec1, 1).unwrap());
        assert_eq!(b"b123f", &vec1[0..5]);
    }
}
