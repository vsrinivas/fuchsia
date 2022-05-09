// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::backend_test::BackendController;
use {
    crate::backend::{BlockBackend, DeviceAttrs, Request, Sector},
    crate::wire,
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    std::cell::RefCell,
    std::convert::TryInto,
    std::rc::Rc,
};

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

#[cfg(test)]
impl BackendController for Controller {
    fn write_sector(&mut self, sector: Sector, data: &[u8]) -> Result<(), Error> {
        self.0.borrow_mut()[sector.as_byte_range()].copy_from_slice(data);
        Ok(())
    }

    fn read_sector(&mut self, sector: Sector, data: &mut [u8]) -> Result<(), Error> {
        data.copy_from_slice(&self.0.borrow()[sector.as_byte_range()]);
        Ok(())
    }
}

/// `MemoryBackend` is a `BlockBackend` that fulfills block requests with sector data held in-
/// memory.
///
/// This is intended for testing purposes only.
pub struct MemoryBackend(Rc<RefCell<Vec<u8>>>);

impl MemoryBackend {
    /// Creates a `MemoryBackend` with a 64KiB capacity.
    #[cfg(test)]
    pub fn new() -> (Self, Controller) {
        const DEFAULT_CAPACITY: usize = 64 * 1024;

        Self::with_size(DEFAULT_CAPACITY)
    }

    /// Creates a `MemoryBackend` with the requestsed size in bytes.
    ///
    /// `size` must be aligned to `wire::VIRTIO_BLOCK_SECTOR_SIZE`.
    pub fn with_size(size: usize) -> (Self, Controller) {
        let size = Sector::from_bytes_round_down(size as u64).to_bytes().unwrap() as usize;
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

    async fn read<'a, 'b>(&self, request: Request<'a, 'b>) -> Result<(), Error> {
        let mut offset = request.sector.to_bytes().unwrap() as usize;
        for range in request.ranges.into_iter() {
            let top = offset.checked_add(range.len()).unwrap();
            if top > self.0.borrow().len() {
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

    async fn write<'a, 'b>(&self, request: Request<'a, 'b>) -> Result<(), Error> {
        let mut offset = request.sector.to_bytes().unwrap() as usize;
        for range in request.ranges.into_iter() {
            let top = offset.checked_add(range.len()).unwrap();
            if top > self.0.borrow().len() {
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

#[cfg(test)]
mod tests {
    use {super::*, crate::backend_test::BackendTest, anyhow::Error};

    struct MemoryBackendTest;

    #[async_trait(?Send)]
    impl BackendTest for MemoryBackendTest {
        type Backend = MemoryBackend;
        type Controller = super::Controller;

        async fn create_with_size(size: u64) -> Result<(MemoryBackend, Controller), Error> {
            Ok(MemoryBackend::with_size(size as usize))
        }
    }

    crate::backend_test::instantiate_backend_test_suite!(MemoryBackendTest);
}
