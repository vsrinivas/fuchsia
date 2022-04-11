// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::backend::{BlockBackend, DeviceAttrs, Request, Sector},
    anyhow::{anyhow, Error},
    async_lock::Semaphore,
    async_trait::async_trait,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{FileMarker, FileProxy, MAX_BUF},
    fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
    futures::future::try_join_all,
    virtio_device::mem::DeviceRange,
};

const MAX_INFLIGHT_REQUESTS: usize = 64;

pub struct FileBackend {
    file: FileProxy,
    // fxbug.dev/12536: The guest can cause an unbounded number of requests to the backing file.
    // Due to the current lack of channel back-pressure this could result in a policy violation
    // and termination of the virtio-block process.
    //
    // Work around this by grabbing a ticket from the semaphore before sending any requests to the
    // file and holding it for the duration of the operation.
    semaphore: Semaphore,
}

impl FileBackend {
    pub fn new(channel: zx::Channel) -> Result<Self, Error> {
        Ok(Self {
            file: ClientEnd::<FileMarker>::new(channel).into_proxy()?,
            semaphore: Semaphore::new(MAX_INFLIGHT_REQUESTS),
        })
    }

    /// Reads a single DeviceRange from the file.
    ///
    /// The caller must ensure that `DeviceRange` is no longer that `fidl_fuchsia_io::MAX_BUF`.
    async fn read_range<'a>(&self, offset: u64, range: DeviceRange<'a>) -> Result<(), Error> {
        assert!(range.len() <= MAX_BUF as usize);
        let bytes = {
            let _ticket = self.semaphore.acquire().await;
            self.file
                .read_at(range.len() as u64, offset)
                .await?
                .map_err(zx_status::Status::from_raw)?
        };
        if bytes.len() != range.len() {
            return Err(anyhow!(
                "Incorrect number of bytes read from file. Wanted {} got {}",
                range.len(),
                bytes.len()
            ));
        }
        // SAFETY: the range comes from the virtio chain and alignment is verified by
        // `try_mut_ptr`.
        let ptr = range.try_mut_ptr().unwrap();
        unsafe { libc::memmove(ptr, bytes.as_ptr() as *const libc::c_void, bytes.len()) };
        Ok(())
    }

    /// Writes a single DeviceRange to the file.
    ///
    /// The caller must ensure that `DeviceRange` is no longer that `fidl_fuchsia_io::MAX_BUF`.
    async fn write_range<'a>(&self, offset: u64, range: DeviceRange<'a>) -> Result<(), Error> {
        assert!(range.len() <= MAX_BUF as usize);
        // SAFETY: the range comes from the virtio chain and alignment is verified by `try_ptr`.
        let slice = unsafe { std::slice::from_raw_parts(range.try_ptr().unwrap(), range.len()) };
        let bytes_written = {
            let _ticket = self.semaphore.acquire().await;
            self.file.write_at(slice, offset).await?.map_err(zx_status::Status::from_raw)?
        };
        if bytes_written < range.len() as u64 {
            return Err(anyhow!(
                "Incorrect number of bytes read written to file. Wanted {} got {}",
                range.len(),
                bytes_written
            ));
        }
        Ok(())
    }
}

#[async_trait(?Send)]
impl BlockBackend for FileBackend {
    async fn get_attrs(&self) -> Result<DeviceAttrs, Error> {
        let (status, attrs) = self.file.get_attr().await?;
        zx_status::Status::ok(status)?;
        return Ok(DeviceAttrs {
            capacity: Sector::from_bytes_round_down(attrs.content_size),
            block_size: None,
        });
    }

    async fn read<'a>(&self, request: Request<'a>) -> Result<(), Error> {
        try_join_all(
            request
                .for_each_range_bounded(MAX_BUF as usize)
                .map(|(offset, range)| self.read_range(offset, range)),
        )
        .await?;
        Ok(())
    }

    async fn write<'a>(&self, request: Request<'a>) -> Result<(), Error> {
        try_join_all(
            request
                .for_each_range_bounded(MAX_BUF as usize)
                .map(|(offset, range)| self.write_range(offset, range)),
        )
        .await?;
        Ok(())
    }

    async fn flush(&self) -> Result<(), Error> {
        let _ticket = self.semaphore.acquire().await;
        self.file.sync().await?.map_err(zx_status::Status::from_raw)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::wire,
        anyhow::Error,
        fuchsia_async as fasync,
        futures::future::try_join_all,
        std::io::{Read, Seek, SeekFrom, Write},
        std::slice,
        tempfile::tempfile,
        virtio_device::fake_queue::IdentityDriverMem,
        virtio_device::mem::DeviceRange,
    };

    /// A `BackendController` allows tests to interface directly with the underlying storage of a
    /// `BlockBackend` to read or write bytes directly, bypassing the `BlockBackend`.
    ///
    /// For example, with a `FileBackend`, the `BackendController` could use another
    /// [File](std::fs::File) to access the contents of the underlying file that the `FileBackend`
    /// is servicing requests from.
    trait BackendController {
        /// Reads a full sector from the underlying storage for a `BlockBackend`.
        ///
        /// `data` must be exactly
        /// [VIRTIO_BLOCK_SECTOR_SIZE](crate::wire::VIRTIO_BLOCK_SECTOR_SIZE) in length. `sector`
        /// must be valid such that entirerty of the read sector must be within the capacity of the
        /// `BlockBackend`.
        fn read_sector(&mut self, sector: Sector, data: &mut [u8]) -> Result<(), Error>;

        /// Writes a full sector to the underlying storage for a `BlockBackend`.
        ///
        /// See [read_sector](Self::read_sector) for more details.
        fn write_sector(&mut self, sector: Sector, data: &[u8]) -> Result<(), Error>;

        /// Writes a full sector of `color` bytes at `sector`.
        fn color_sector(&mut self, sector: Sector, color: u8) -> Result<(), Error> {
            let data = [color; wire::VIRTIO_BLOCK_SECTOR_SIZE as usize];
            self.write_sector(sector, &data)
        }

        /// Validates that the contents of `sector` is an entire sector of `color` bytes.
        fn check_sector(&mut self, sector: Sector, color: u8) -> Result<(), Error> {
            let mut data = [0u8; wire::VIRTIO_BLOCK_SECTOR_SIZE as usize];
            self.read_sector(sector, &mut data)?;
            data.iter().for_each(|c| assert_eq!(*c, color));
            Ok(())
        }
    }

    /// A `BackendTest` allows for multiple backends to share test suites by implementing this
    /// trait.
    trait BackendTest {
        /// The `BlockBackend` to be tested.
        type Backend: BlockBackend;

        /// The `BackendController` type that can be used validate the operation of the backend.
        type Controller: BackendController;

        /// Create a new instance of the backend under test with the capacity of `size` bytes.
        fn create_with_size(size: u64) -> Result<(Self::Backend, Self::Controller), Error>;

        /// Create a new instance of the backend under test with the capacity of `size` sectors.
        fn create_with_sectors(size: Sector) -> Result<(Self::Backend, Self::Controller), Error> {
            Self::create_with_size(
                size.to_bytes()
                    .ok_or(anyhow!("Requested sector capacity {:?} is too large", size))?,
            )
        }
    }

    struct FileBackendController(std::fs::File);

    impl BackendController for FileBackendController {
        fn write_sector(&mut self, sector: Sector, data: &[u8]) -> Result<(), Error> {
            self.0.seek(SeekFrom::Start(sector.to_bytes().unwrap()))?;
            self.0.write_all(data)?;
            // Sync because we'll read this from the remote file so we want to make sure there's no
            // buffering in the way.
            self.0.sync_all()?;
            Ok(())
        }

        fn read_sector(&mut self, sector: Sector, data: &mut [u8]) -> Result<(), Error> {
            self.0.seek(SeekFrom::Start(sector.to_bytes().unwrap()))?;
            self.0.read_exact(data)?;
            Ok(())
        }
    }

    struct FileBackendTest;

    impl BackendTest for FileBackendTest {
        type Backend = FileBackend;
        type Controller = FileBackendController;

        fn create_with_size(size: u64) -> Result<(FileBackend, FileBackendController), Error> {
            let file = tempfile()?;
            file.set_len(size)?;
            Ok((FileBackend::new(fdio::clone_channel(&file)?.into())?, FileBackendController(file)))
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_attrs() -> Result<(), Error> {
        let (backend, _) = FileBackendTest::create_with_size(0)?;
        assert_eq!(
            backend.get_attrs().await?,
            DeviceAttrs { capacity: Sector::from_raw_sector(0), block_size: None }
        );

        let expected_byte_sector_sizes = [
            // anything less than 512 bytes rounds down to 0 sectors.
            (1, Sector::from_raw_sector(0)),
            (511, Sector::from_raw_sector(0)),
            (4096, Sector::from_raw_sector(8)),
            // 400MiB -> 819200 sectors
            (400 * 1024 * 1024, Sector::from_raw_sector(819200)),
        ];
        for (file_size, sectors) in expected_byte_sector_sizes {
            let (backend, _) = FileBackendTest::create_with_size(file_size)?;
            assert_eq!(
                backend.get_attrs().await?,
                DeviceAttrs { capacity: sectors, block_size: None }
            );
        }

        Ok(())
    }

    fn check_range<'a>(range: &DeviceRange<'a>, color: u8) {
        let actual_data: &[u8] =
            unsafe { std::slice::from_raw_parts(range.try_ptr().unwrap(), range.len()) };
        actual_data.iter().for_each(|c| assert_eq!(*c, color));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_per_sector_ranges() -> Result<(), Error> {
        // Create a file and color 3 different sectors.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(3))?;
        controller.color_sector(Sector::from_raw_sector(0), 0xaa)?;
        controller.color_sector(Sector::from_raw_sector(1), 0xbb)?;
        controller.color_sector(Sector::from_raw_sector(2), 0xcc)?;

        // Create a request to read all 3 sectors with a DeviceRange for each.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        ];
        let request = Request { ranges: ranges.as_slice(), sector: Sector::from_raw_sector(0) };

        // Create the backend and process the request.
        backend.read(request).await?;

        // Verify the file data was read into the device ranges.
        check_range(&ranges[0], 0xaa);
        check_range(&ranges[1], 0xbb);
        check_range(&ranges[2], 0xcc);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_multiple_sectors_per_range() -> Result<(), Error> {
        // Create a file and color 3 different sectors.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(3))?;
        controller.color_sector(Sector::from_raw_sector(0), 0xaa)?;
        controller.color_sector(Sector::from_raw_sector(1), 0xbb)?;
        controller.color_sector(Sector::from_raw_sector(2), 0xcc)?;

        // Create a request to read all 3 sectors into a single descriptor.
        let mem = IdentityDriverMem::new();
        let range = mem.new_range(3 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let request =
            Request { ranges: slice::from_ref(&range), sector: Sector::from_raw_sector(0) };

        // Process the request.
        backend.read(request).await?;

        // Verify the file data was read into the device ranges.
        let (sector0, remain) = range.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let (sector1, sector2) = remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        check_range(&sector0, 0xaa);
        check_range(&sector1, 0xbb);
        check_range(&sector2, 0xcc);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_subsector_range() -> Result<(), Error> {
        // Create a file and color 2 sectors.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(2))?;
        controller.color_sector(Sector::from_raw_sector(0), 0xaa)?;
        controller.color_sector(Sector::from_raw_sector(1), 0xbb)?;

        // Create a request to read only one sector using 4 different descriptors.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
        ];
        let request = Request { ranges: ranges.as_slice(), sector: Sector::from_raw_sector(0) };

        // Process the request.
        backend.read(request).await?;

        // Verify the correct data is read.
        check_range(&ranges[0], 0xaa);
        check_range(&ranges[1], 0xaa);
        check_range(&ranges[2], 0xaa);
        check_range(&ranges[3], 0xaa);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_large() -> Result<(), Error> {
        const BYTE_SIZE: u64 = 64 * 1024;
        const SECTOR_SIZE: u64 = BYTE_SIZE / wire::VIRTIO_BLOCK_SECTOR_SIZE;

        // We want to make sure we're testing that we can process a request that is larger than
        // what the File protocol can handle in a single request.
        assert!(BYTE_SIZE > MAX_BUF);

        // Create a file and color enough sectors to cover the entire `BYTE_SIZE` range.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(SECTOR_SIZE))?;
        (0..SECTOR_SIZE).try_for_each(|sector| {
            controller.color_sector(Sector::from_raw_sector(sector), 0xaa)
        })?;

        // Create a request to read all the sectors colored above.
        let mem = IdentityDriverMem::new();
        let range = mem.new_range(BYTE_SIZE as usize).unwrap();
        let request =
            Request { ranges: slice::from_ref(&range), sector: Sector::from_raw_sector(0) };

        // Process the request.
        backend.read(request).await?;

        // Verify the file data was read into the descriptor.
        check_range(&range, 0xaa);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_concurrent() -> Result<(), Error> {
        const CONCURRENCY_COUNT: u64 = 1024;

        // Create a file and color a sector for each request we will run concurrently
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(CONCURRENCY_COUNT))?;
        for sector in 0..CONCURRENCY_COUNT {
            controller.color_sector(Sector::from_raw_sector(sector), sector as u8)?;
        }
        let mem = IdentityDriverMem::new();

        // Create a range for each request and then dispatch all the read operations.
        let ranges: Vec<DeviceRange<'_>> = (0..CONCURRENCY_COUNT)
            .map(|_| mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap())
            .collect();
        let futures: Vec<_> = (0..CONCURRENCY_COUNT)
            .map(|sector| {
                backend.read(Request {
                    ranges: slice::from_ref(&ranges[sector as usize]),
                    sector: Sector::from_raw_sector(sector),
                })
            })
            .collect();

        // Now wait for all the reads to complete and verify each has read the correct data.
        try_join_all(futures).await?;
        for sector in 0..CONCURRENCY_COUNT {
            check_range(&ranges[sector as usize], sector as u8);
        }

        Ok(())
    }

    fn color_range<'a>(range: &DeviceRange<'a>, color: u8) {
        unsafe {
            libc::memset(range.try_mut_ptr().unwrap(), color as i32, range.len());
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_per_sector_ranges() -> Result<(), Error> {
        // Create a request with 3 device ranges.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        ];
        let request = Request { ranges: ranges.as_slice(), sector: Sector::from_raw_sector(0) };

        // Fill each range with a different byte value.
        color_range(&ranges[0], 0xaa);
        color_range(&ranges[1], 0xbb);
        color_range(&ranges[2], 0xcc);

        // Create the backend and write the ranges.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(3))?;
        backend.write(request).await?;

        // Verify the data was written into the file.
        controller.check_sector(Sector::from_raw_sector(0), 0xaa)?;
        controller.check_sector(Sector::from_raw_sector(1), 0xbb)?;
        controller.check_sector(Sector::from_raw_sector(2), 0xcc)?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_multiple_sectors_per_range() -> Result<(), Error> {
        // Create a request to write 3 sectors with a single descriptor.
        let mem = IdentityDriverMem::new();
        let ranges = vec![mem.new_range(3 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap()];
        {
            // Split the range to populate each sector with different data. We'll still use the
            // single, large range for performing the write.
            let (sector0, remain) =
                ranges[0].split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
            let (sector1, sector2) =
                remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
            color_range(&sector0, 0xaa);
            color_range(&sector1, 0xbb);
            color_range(&sector2, 0xcc);
        }
        let request = Request { ranges: ranges.as_slice(), sector: Sector::from_raw_sector(0) };

        // Execute the write.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(3))?;
        backend.write(request).await?;

        // Verify the data was written to the file.
        controller.check_sector(Sector::from_raw_sector(0), 0xaa)?;
        controller.check_sector(Sector::from_raw_sector(1), 0xbb)?;
        controller.check_sector(Sector::from_raw_sector(2), 0xcc)?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_subsector_range() -> Result<(), Error> {
        // Create a request to write one sector using 4 descriptors.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
        ];
        let request = Request { ranges: ranges.as_slice(), sector: Sector::from_raw_sector(0) };

        // Color them all the same.
        color_range(&ranges[0], 0xaa);
        color_range(&ranges[1], 0xaa);
        color_range(&ranges[2], 0xaa);
        color_range(&ranges[3], 0xaa);

        // Execute the write.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(1))?;
        backend.write(request).await?;

        // Verify the full sector was written correctly.
        controller.check_sector(Sector::from_raw_sector(0), 0xaa)?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_large() -> Result<(), Error> {
        const BYTE_SIZE: u64 = 64 * 1024;
        const SECTOR_SIZE: u64 = BYTE_SIZE / wire::VIRTIO_BLOCK_SECTOR_SIZE;

        // We want to make sure we're testing that we can process a request that is larger than
        // what the File protocol can handle in a single request.
        assert!(BYTE_SIZE > MAX_BUF);

        // Create a request with a single large descriptor.
        let mem = IdentityDriverMem::new();
        let range = mem.new_range(BYTE_SIZE as usize).unwrap();
        let request =
            Request { ranges: slice::from_ref(&range), sector: Sector::from_raw_sector(0) };

        // Fill the descrioptor memory with a specific value.
        color_range(&range, 0xcd);

        // Create the backend and process the request.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(SECTOR_SIZE))?;
        backend.write(request).await?;

        // Verify the data was written to the file.
        (0..SECTOR_SIZE).try_for_each(|sector| {
            controller.check_sector(Sector::from_raw_sector(sector), 0xcd)
        })?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_concurrent() -> Result<(), Error> {
        const CONCURRENCY_COUNT: u64 = 1024;

        // Create a range for each concurrent write. These are each a single sector and we'll write
        // the sector index to each sector (ex: Sector 0 will be full of 0's, Sector 1 will be full
        // of 1's, etc.
        let mem = IdentityDriverMem::new();
        let ranges: Vec<DeviceRange<'_>> = (0..CONCURRENCY_COUNT)
            .map(|sector| {
                let range = mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
                color_range(&range, sector as u8);
                range
            })
            .collect();

        // Execute the requests concurrently.
        let (backend, mut controller) =
            FileBackendTest::create_with_sectors(Sector::from_raw_sector(CONCURRENCY_COUNT))?;
        let futures: Vec<_> = (0..CONCURRENCY_COUNT)
            .map(|sector| {
                backend.write(Request {
                    ranges: slice::from_ref(&ranges[sector as usize]),
                    sector: Sector::from_raw_sector(sector),
                })
            })
            .collect();

        // Join all the writes and then verify the data was written to the file as expected.
        try_join_all(futures).await?;
        for sector in 0..CONCURRENCY_COUNT {
            controller.check_sector(Sector::from_raw_sector(sector), sector as u8)?;
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_write_loop() -> Result<(), Error> {
        const ITERATIONS: u64 = 100;

        let (backend, _) = FileBackendTest::create_with_sectors(Sector::from_raw_sector(1))?;

        // Iteratively write a value to a sector and read it back.
        let mem = IdentityDriverMem::new();
        for i in 0..ITERATIONS {
            {
                let write_range = mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
                color_range(&write_range, i as u8);
                backend
                    .write(Request {
                        ranges: slice::from_ref(&write_range),
                        sector: Sector::from_raw_sector(0),
                    })
                    .await?;
            }
            {
                let read_range = mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
                backend
                    .read(Request {
                        ranges: slice::from_ref(&read_range),
                        sector: Sector::from_raw_sector(0),
                    })
                    .await?;
                check_range(&read_range, i as u8);
            }
        }

        Ok(())
    }
}
