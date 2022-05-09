// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::backend::{BlockBackend, DeviceAttrs, Request, Sector},
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    futures::future::try_join_all,
    std::cell::RefCell,
    std::convert::TryInto,
};

#[derive(Debug, Clone, PartialEq, Eq)]
enum TargetBackend {
    Backing,
    Copied,
}

/// A `Bitmap` to track which sectors have been been written and are now valid in the `Copied`
/// backend.
///
/// This is just a simple bit-array with a single bit per sector. So sector 0 is bit 0, sector 1 is
/// bit 1, etc.
struct Bitmap(Vec<u8>);

impl Bitmap {
    pub fn with_size(size: Sector) -> Self {
        let size: usize = size.raw().try_into().unwrap();
        Self(vec![0u8; (size + 7) / 8])
    }

    pub fn backend(&self, at: Sector) -> TargetBackend {
        let at: usize = at.raw().try_into().unwrap();
        if self.0[at / 8] & (0x1 << (at % 8)) != 0 {
            TargetBackend::Copied
        } else {
            TargetBackend::Backing
        }
    }

    pub fn set_copied(&mut self, at: Sector) {
        let at: usize = at.raw().try_into().unwrap();
        self.0[at / 8] |= 0x1 << (at % 8);
    }
}

/// `CopyOnWriteBackend` is a backend that can source reads from one backend and route writes to a
/// different backend. This is useful to allow writes to a backend backed by immutable storage by
/// storing written blocks in a different backend.
///
/// This backend is currently unable to persist state across restarts. While both backends may be
/// backed by non-volatile storage, the bitmap that tracks the written sectors is not. This  means
/// that reads will always revert to the `backing` backend when this component restarts.
pub struct CopyOnWriteBackend {
    // The backing backend is the immutable (read-only) backend that will be used to service read
    // requests until those sectors have been written to.
    backing: Box<dyn BlockBackend>,
    // The copied backend will service all write requests, as well as read requests to sectors that
    // have been previously been written to.
    copied: Box<dyn BlockBackend>,
    // The bitmap tracks which sectors are stored in `backing` vs `copied`.
    bitmap: RefCell<Bitmap>,
}

impl CopyOnWriteBackend {
    pub async fn new(
        backing: Box<dyn BlockBackend>,
        copied: Box<dyn BlockBackend>,
    ) -> Result<Self, Error> {
        let (backing_attrs, copied_attrs) =
            futures::try_join!(backing.get_attrs(), copied.get_attrs())?;
        if backing_attrs.capacity < copied_attrs.capacity {
            return Err(anyhow!(
                "Copied backend is not large enough {:?} vs {:?} sectors",
                backing_attrs.capacity,
                copied_attrs.capacity
            ));
        }
        Ok(Self {
            backing,
            copied,
            bitmap: RefCell::new(Bitmap::with_size(backing_attrs.capacity)),
        })
    }
}

/// Implement an iterator over a request that will create sub-requests depending on which backend
/// the region should read from.
struct ByBackend<'a, 'b, F: Fn(Sector) -> TargetBackend> {
    request: Request<'a, 'b>,
    target_resolver: F,
}

impl<'a, 'b, F: Fn(Sector) -> TargetBackend> Iterator for ByBackend<'a, 'b, F> {
    type Item = (TargetBackend, Request<'a, 'b>);

    fn next(&mut self) -> Option<Self::Item> {
        if self.request.ranges.is_empty() {
            return None;
        }
        let mut sector = Sector::from_raw_sector(0);
        let len = Sector::from_bytes_round_down(
            self.request.ranges.iter().fold(0, |a, x| a + x.len() as u64),
        );

        // Find contiguous sectors with the same target.
        let target = (self.target_resolver)(self.request.sector + sector);
        sector += Sector::from_raw_sector(1);
        while sector < len && (self.target_resolver)(self.request.sector + sector) == target {
            sector += Sector::from_raw_sector(1);
        }

        // Split the request at the boundary. We retain the remainder of the request and return
        // the part that is coverd by the next target backend.
        let (r, rest) = self.request.split_at(sector).unwrap();
        self.request = rest;
        Some((target, r))
    }
}

impl CopyOnWriteBackend {
    /// Takes a request and returns an iterator over contiguous sub-requests that hit the
    /// corresponding backend. For example, if the `request` only addresses sectors that are stored
    /// in one backend, then the iterator will produce a single item. Conversely, if the request
    /// starts with sectors stored in the `Backing` backend and then moves to a region stored in
    /// the `Copied` backend, then the iterator would yield 2 regions.
    ///
    /// The returned iterator will always address the entire set of sectors covered by `request`.
    fn by_backend<'a, 'b>(
        &'b self,
        request: Request<'a, 'b>,
    ) -> ByBackend<'a, 'b, impl Fn(Sector) -> TargetBackend + 'b> {
        ByBackend { request, target_resolver: move |sector| self.bitmap.borrow().backend(sector) }
    }
}

#[async_trait(?Send)]
impl BlockBackend for CopyOnWriteBackend {
    async fn get_attrs(&self) -> Result<DeviceAttrs, Error> {
        self.backing.get_attrs().await
    }

    async fn read<'a, 'b>(&self, request: Request<'a, 'b>) -> Result<(), Error> {
        try_join_all(self.by_backend(request).map(|(target, request)| match target {
            TargetBackend::Backing => self.backing.read(request),
            TargetBackend::Copied => self.copied.read(request),
        }))
        .await?;
        Ok(())
    }

    async fn write<'a, 'b>(&self, request: Request<'a, 'b>) -> Result<(), Error> {
        // Compute the extents of the request. We'll need this to update the bitmap.
        let start = request.sector;
        let len =
            Sector::from_bytes_round_down(request.ranges.iter().fold(0, |a, x| a + x.len() as u64));
        let end = start + len;

        // Since we currently track changes in 512 byte chunks, we will never write a partial
        // sector.
        //
        // TODO(fxbug.dev/99759): as an improvement we can allow for flexibility in the size of
        // blocks tracked in the dirty bitmap. In that situation we would have to support reading
        // some (512-byte) sectors from the backing backend that are not explicitly in the input
        // request.
        self.copied.write(request).await?;

        // Now that we've written the sectors to the backend, we update the bitmap. We do this
        // second such that if the driver does try to race a read with this write it should still
        // only see potentially old data from the backing backend instead of possibly invalid data
        // read from the copied backend before the data has been written.
        let mut sector = start;
        while sector < end {
            self.bitmap.borrow_mut().set_copied(sector);
            sector += Sector::from_raw_sector(1);
        }

        Ok(())
    }

    async fn flush(&self) -> Result<(), Error> {
        // There's no need to flush the backing backend since we only read from it.
        self.copied.flush().await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::backend_test::{BackendController, BackendTest},
        crate::file_backend::tests::{FileBackendController, FileBackendTest},
        crate::memory_backend::{self, MemoryBackend},
        anyhow::Error,
    };

    struct CopyOnWriteBackendController {
        backing: FileBackendController,
        copied: memory_backend::Controller,
    }

    impl BackendController for CopyOnWriteBackendController {
        // When the test writes to a sector, we write that to the backing store (as if those
        // sectors were pre-populated).
        fn write_sector(&mut self, sector: Sector, data: &[u8]) -> Result<(), Error> {
            self.backing.write_sector(sector, data)
        }

        // When the test reads from a sector, it is to validate that a write has occurred. In this
        // situation we want to read from the copied backend.
        fn read_sector(&mut self, sector: Sector, data: &mut [u8]) -> Result<(), Error> {
            self.copied.read_sector(sector, data)
        }
    }

    struct CopyOnWriteBackendTest;

    #[async_trait(?Send)]
    impl BackendTest for CopyOnWriteBackendTest {
        type Backend = CopyOnWriteBackend;
        type Controller = CopyOnWriteBackendController;

        async fn create_with_size(
            size: u64,
        ) -> Result<(CopyOnWriteBackend, CopyOnWriteBackendController), Error> {
            let (file_backend, file_controller) = FileBackendTest::create_with_size(size).await?;
            let (memory_backend, memory_controller) = MemoryBackend::with_size(size as usize);

            let cow_backend =
                CopyOnWriteBackend::new(Box::new(file_backend), Box::new(memory_backend)).await?;
            let cow_controller = CopyOnWriteBackendController {
                backing: file_controller,
                copied: memory_controller,
            };
            Ok((cow_backend, cow_controller))
        }
    }

    crate::backend_test::instantiate_backend_test_suite!(CopyOnWriteBackendTest);
}
