// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Temporary, until the consuming code lands.
#![allow(dead_code)]

use {
    crate::backend::{BlockBackend, DeviceAttrs, Request, Sector},
    crate::wire,
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    std::cell::RefCell,
    std::convert::TryInto,
    std::rc::Rc,
};

const DEFAULT_CAPACITY: usize = 64 * 1024;

trait AsByteRange {
    fn as_byte_range(&self) -> std::ops::Range<usize>;
}

impl AsByteRange for Sector {
    fn as_byte_range(&self) -> std::ops::Range<usize> {
        let offset: usize = self.to_bytes().unwrap().try_into().unwrap();
        let top = offset.checked_add(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        return offset..top;
    }
}

/// The `Controller` provides a way for tests to interact with the state of the memory buffer
/// without retaining a reference to the backend.
pub struct Controller(Rc<RefCell<Vec<u8>>>);

impl Controller {
    /// Writes a full sector of `color` to the requested sector in the backing buffer.
    pub fn color_sector(&self, sector: Sector, color: u8) {
        self.0.borrow_mut()[sector.as_byte_range()].fill(color);
    }

    /// Asserts that `sector` contains a full sector of bytes that equal `color`.
    pub fn check_sector(&self, sector: Sector, color: u8) {
        for byte in sector.as_byte_range() {
            if self.0.borrow()[byte] != color {
                panic!(
                    "Mismatch in sector {} at byte {}, expected {} got {}",
                    sector.to_raw(),
                    byte,
                    color,
                    self.0.borrow()[byte]
                );
            }
        }
    }
}

/// `MemoryBackend` is a `BlockBackend` that fulfills block requests with sector data held in-
/// memory.
///
/// This is intended for testing purposes only.
pub struct MemoryBackend(Rc<RefCell<Vec<u8>>>);

impl MemoryBackend {
    /// Creates a `MemoryBackend` with a 64KiB capacity.
    pub fn new() -> (Self, Controller) {
        Self::with_size(DEFAULT_CAPACITY)
    }

    /// Creates a `MemoryBackend` with the requestsed size in bytes.
    ///
    /// `size` must be aligned to `wire::VIRTIO_BLOCK_SECTOR_SIZE`.
    pub fn with_size(size: usize) -> (Self, Controller) {
        assert!(size % wire::VIRTIO_BLOCK_SECTOR_SIZE as usize == 0);
        let buffer = Rc::new(RefCell::new(vec![0; size]));
        (Self(buffer.clone()), Controller(buffer))
    }
}

#[async_trait(?Send)]
impl BlockBackend for MemoryBackend {
    async fn get_attrs(&self) -> Result<DeviceAttrs, Error> {
        return Ok(DeviceAttrs {
            capacity: Sector::from_bytes_round_down(self.0.borrow().len() as u64),
            block_size: None,
        });
    }

    async fn read<'a>(&self, request: Request<'a>) -> Result<(), Error> {
        let mut offset = request.sector.to_bytes().unwrap() as usize;
        for range in request.ranges {
            let top = offset.checked_add(range.len()).unwrap();
            if top >= self.0.borrow().len() {
                return Err(anyhow!(
                    "read is out of range for this backend; size: {} but requested: {}",
                    self.0.borrow().len(),
                    top
                ));
            }
            let bytes = &self.0.borrow()[offset..top];
            unsafe {
                libc::memmove(
                    range.try_mut_ptr().unwrap(),
                    bytes.as_ptr() as *const libc::c_void,
                    bytes.len(),
                )
            };
            offset = top;
        }
        Ok(())
    }

    async fn write<'a>(&self, request: Request<'a>) -> Result<(), Error> {
        let mut offset = request.sector.to_bytes().unwrap() as usize;
        for range in request.ranges {
            let top = offset.checked_add(range.len()).unwrap();
            if top >= self.0.borrow().len() {
                return Err(anyhow!(
                    "write is out of range for this backend; size: {} but requested: {}",
                    self.0.borrow().len(),
                    top
                ));
            }
            let slice =
                unsafe { std::slice::from_raw_parts(range.try_ptr().unwrap(), range.len()) };
            self.0.borrow_mut()[offset..top].clone_from_slice(slice);
            offset = top;
        }
        Ok(())
    }

    async fn flush(&self) -> Result<(), Error> {
        Ok(())
    }
}
