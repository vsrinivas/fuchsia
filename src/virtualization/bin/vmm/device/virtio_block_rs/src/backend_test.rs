// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*, crate::wire, anyhow::Error, async_trait::async_trait, fidl_fuchsia_io::MAX_BUF,
    futures::future::try_join_all, std::slice, virtio_device::fake_queue::IdentityDriverMem,
    virtio_device::mem::DeviceRange,
};

/// A `BackendController` allows tests to interface directly with the underlying storage of a
/// `BlockBackend` to read or write bytes directly, bypassing the `BlockBackend`.
///
/// For example, with a `FileBackend`, the `BackendController` could use another
/// [File](std::fs::File) to access the contents of the underlying file that the `FileBackend`
/// is servicing requests from.
pub trait BackendController {
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
#[async_trait(?Send)]
pub trait BackendTest {
    /// The `BlockBackend` to be tested.
    type Backend: BlockBackend;

    /// The `BackendController` type that can be used validate the operation of the backend.
    type Controller: BackendController;

    /// Create a new instance of the backend under test with the capacity of `size` bytes.
    async fn create_with_size(size: u64) -> Result<(Self::Backend, Self::Controller), Error>;

    /// Create a new instance of the backend under test with the capacity of `size` sectors.
    async fn create_with_sectors(size: Sector) -> Result<(Self::Backend, Self::Controller), Error> {
        Self::create_with_size(
            size.to_bytes().ok_or(anyhow!("Requested sector capacity {:?} is too large", size))?,
        )
        .await
    }
}

pub fn check_range<'a>(range: &DeviceRange<'a>, color: u8) {
    let actual_data: &[u8] =
        unsafe { std::slice::from_raw_parts(range.try_ptr().unwrap(), range.len()) };
    actual_data.iter().for_each(|c| assert_eq!(*c, color));
}

fn color_range<'a>(range: &DeviceRange<'a>, color: u8) {
    unsafe {
        libc::memset(range.try_mut_ptr().unwrap(), color as i32, range.len());
    }
}

/// Test that a backend properly reports capacity in different configurations.
pub async fn test_get_attrs<T: BackendTest>() -> Result<(), Error> {
    let expected_byte_sector_sizes = [
        (4096, Sector::from_raw_sector(8)),
        (4097, Sector::from_raw_sector(8)),
        (4607, Sector::from_raw_sector(8)),
        // 400MiB -> 819200 sectors
        (400 * 1024 * 1024, Sector::from_raw_sector(819200)),
    ];
    for (size, sectors) in expected_byte_sector_sizes {
        let (backend, _controller) = T::create_with_size(size).await?;
        // Comparison is a little loose here because some backends have minimum granularity and may
        // round up sizes. We just verify we get at least the capacity we requested otherwise other
        // tests may fail as they depend on this precondition.
        assert!(backend.get_attrs().await?.capacity >= sectors);
    }

    Ok(())
}

/// Test that a backend can read into a set of sector-aligned DeviceRanges.
pub async fn test_read_per_sector_ranges<T: BackendTest>() -> Result<(), Error> {
    // Create a file and color 3 different sectors.
    let (backend, mut controller) = T::create_with_sectors(Sector::from_raw_sector(3)).await?;
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
    let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

    // Create the backend and process the request.
    backend.read(request).await?;

    // Verify the file data was read into the device ranges.
    check_range(&ranges[0], 0xaa);
    check_range(&ranges[1], 0xbb);
    check_range(&ranges[2], 0xcc);
    Ok(())
}

/// Test that a backend can read multiple sectors into a single DeviceRange.
pub async fn test_read_multiple_sectors_per_range<T: BackendTest>() -> Result<(), Error> {
    // Create a file and color 3 different sectors.
    let (backend, mut controller) = T::create_with_sectors(Sector::from_raw_sector(3)).await?;
    controller.color_sector(Sector::from_raw_sector(0), 0xaa)?;
    controller.color_sector(Sector::from_raw_sector(1), 0xbb)?;
    controller.color_sector(Sector::from_raw_sector(2), 0xcc)?;

    // Create a request to read all 3 sectors into a single descriptor.
    let mem = IdentityDriverMem::new();
    let range = mem.new_range(3 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
    let request = Request::from_ref(slice::from_ref(&range), Sector::from_raw_sector(0));

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

/// Test that a backend can read into a set of device ranges that are smaller than a sector.
pub async fn test_read_subsector_range<T: BackendTest>() -> Result<(), Error> {
    // Create a file and color 2 sectors.
    let (backend, mut controller) = T::create_with_sectors(Sector::from_raw_sector(2)).await?;
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
    let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

    // Process the request.
    backend.read(request).await?;

    // Verify the correct data is read.
    check_range(&ranges[0], 0xaa);
    check_range(&ranges[1], 0xaa);
    check_range(&ranges[2], 0xaa);
    check_range(&ranges[3], 0xaa);
    Ok(())
}

/// Test that a backend can handle 'large' read requests.
pub async fn test_read_large<T: BackendTest>() -> Result<(), Error> {
    const BYTE_SIZE: u64 = 64 * 1024;
    const SECTOR_SIZE: u64 = BYTE_SIZE / wire::VIRTIO_BLOCK_SECTOR_SIZE;

    // We want to make sure we're testing that we can process a request that is larger than
    // what the File protocol can handle in a single request.
    assert!(BYTE_SIZE > MAX_BUF);

    // Create a file and color enough sectors to cover the entire `BYTE_SIZE` range.
    let (backend, mut controller) =
        T::create_with_sectors(Sector::from_raw_sector(SECTOR_SIZE)).await?;
    (0..SECTOR_SIZE)
        .try_for_each(|sector| controller.color_sector(Sector::from_raw_sector(sector), 0xaa))?;

    // Create a request to read all the sectors colored above.
    let mem = IdentityDriverMem::new();
    let range = mem.new_range(BYTE_SIZE as usize).unwrap();
    let request = Request::from_ref(slice::from_ref(&range), Sector::from_raw_sector(0));

    // Process the request.
    backend.read(request).await?;

    // Verify the file data was read into the descriptor.
    check_range(&range, 0xaa);
    Ok(())
}

/// Test that a backend can handle multiple read operations concurrently.
pub async fn test_read_concurrent<T: BackendTest>() -> Result<(), Error> {
    const CONCURRENCY_COUNT: u64 = 1024;

    // Create a file and color a sector for each request we will run concurrently
    let (backend, mut controller) =
        T::create_with_sectors(Sector::from_raw_sector(CONCURRENCY_COUNT)).await?;
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
            backend.read(Request::from_ref(
                slice::from_ref(&ranges[sector as usize]),
                Sector::from_raw_sector(sector),
            ))
        })
        .collect();

    // Now wait for all the reads to complete and verify each has read the correct data.
    try_join_all(futures).await?;
    for sector in 0..CONCURRENCY_COUNT {
        check_range(&ranges[sector as usize], sector as u8);
    }

    Ok(())
}

/// Test that a backend can write a set of sector-sized DeviceRanges.
pub async fn test_write_per_sector_ranges<T: BackendTest>() -> Result<(), Error> {
    // Create a request with 3 device ranges.
    let mem = IdentityDriverMem::new();
    let ranges = vec![
        mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
    ];
    let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

    // Fill each range with a different byte value.
    color_range(&ranges[0], 0xaa);
    color_range(&ranges[1], 0xbb);
    color_range(&ranges[2], 0xcc);

    // Create the backend and write the ranges.
    let (backend, mut controller) = T::create_with_sectors(Sector::from_raw_sector(3)).await?;
    backend.write(request).await?;

    // Verify the data was written into the file.
    controller.check_sector(Sector::from_raw_sector(0), 0xaa)?;
    controller.check_sector(Sector::from_raw_sector(1), 0xbb)?;
    controller.check_sector(Sector::from_raw_sector(2), 0xcc)?;
    Ok(())
}

/// Test that a backend can write multiple sctors from a single DeviceRange.
pub async fn test_write_multiple_sectors_per_range<T: BackendTest>() -> Result<(), Error> {
    // Create a request to write 3 sectors with a single descriptor.
    let mem = IdentityDriverMem::new();
    let ranges = vec![mem.new_range(3 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap()];
    {
        // Split the range to populate each sector with different data. We'll still use the
        // single, large range for performing the write.
        let (sector0, remain) =
            ranges[0].split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let (sector1, sector2) = remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        color_range(&sector0, 0xaa);
        color_range(&sector1, 0xbb);
        color_range(&sector2, 0xcc);
    }
    let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

    // Execute the write.
    let (backend, mut controller) = T::create_with_sectors(Sector::from_raw_sector(3)).await?;
    backend.write(request).await?;

    // Verify the data was written to the file.
    controller.check_sector(Sector::from_raw_sector(0), 0xaa)?;
    controller.check_sector(Sector::from_raw_sector(1), 0xbb)?;
    controller.check_sector(Sector::from_raw_sector(2), 0xcc)?;
    Ok(())
}

/// Test that a backend can write a single sector from across multiple DeviceRanges.
pub async fn test_write_subsector_range<T: BackendTest>() -> Result<(), Error> {
    // Create a request to write one sector using 4 descriptors.
    let mem = IdentityDriverMem::new();
    let ranges = vec![
        mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
        mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
        mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
        mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
    ];
    let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

    // Color them all the same.
    color_range(&ranges[0], 0xaa);
    color_range(&ranges[1], 0xaa);
    color_range(&ranges[2], 0xaa);
    color_range(&ranges[3], 0xaa);

    // Execute the write.
    let (backend, mut controller) = T::create_with_sectors(Sector::from_raw_sector(1)).await?;
    backend.write(request).await?;

    // Verify the full sector was written correctly.
    controller.check_sector(Sector::from_raw_sector(0), 0xaa)?;
    Ok(())
}

/// Test that a backend can write from a single 'large' DeviceRange.
pub async fn test_write_large<T: BackendTest>() -> Result<(), Error> {
    const BYTE_SIZE: u64 = 64 * 1024;
    const SECTOR_SIZE: u64 = BYTE_SIZE / wire::VIRTIO_BLOCK_SECTOR_SIZE;

    // We want to make sure we're testing that we can process a request that is larger than
    // what the File protocol can handle in a single request.
    assert!(BYTE_SIZE > MAX_BUF);

    // Create a request with a single large descriptor.
    let mem = IdentityDriverMem::new();
    let range = mem.new_range(BYTE_SIZE as usize).unwrap();
    let request = Request::from_ref(slice::from_ref(&range), Sector::from_raw_sector(0));

    // Fill the descrioptor memory with a specific value.
    color_range(&range, 0xcd);

    // Create the backend and process the request.
    let (backend, mut controller) =
        T::create_with_sectors(Sector::from_raw_sector(SECTOR_SIZE)).await?;
    backend.write(request).await?;

    // Verify the data was written to the file.
    (0..SECTOR_SIZE)
        .try_for_each(|sector| controller.check_sector(Sector::from_raw_sector(sector), 0xcd))?;
    Ok(())
}

/// Test that a backend handle a high number of concurrent write requests.
pub async fn test_write_concurrent<T: BackendTest>() -> Result<(), Error> {
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
        T::create_with_sectors(Sector::from_raw_sector(CONCURRENCY_COUNT)).await?;
    let futures: Vec<_> = (0..CONCURRENCY_COUNT)
        .map(|sector| {
            backend.write(Request::from_ref(
                slice::from_ref(&ranges[sector as usize]),
                Sector::from_raw_sector(sector),
            ))
        })
        .collect();

    // Join all the writes and then verify the data was written to the file as expected.
    try_join_all(futures).await?;
    for sector in 0..CONCURRENCY_COUNT {
        controller.check_sector(Sector::from_raw_sector(sector), sector as u8)?;
    }
    Ok(())
}

/// Tests that a backend is able to consistently write a sector and then read the data back.
pub async fn test_read_write_loop<T: BackendTest>() -> Result<(), Error> {
    const ITERATIONS: u64 = 100;

    let (backend, _controller) = T::create_with_sectors(Sector::from_raw_sector(1)).await?;

    // Iteratively write a value to a sector and read it back.
    let mem = IdentityDriverMem::new();
    for i in 0..ITERATIONS {
        {
            let write_range = mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
            color_range(&write_range, i as u8);
            backend
                .write(Request::from_ref(slice::from_ref(&write_range), Sector::from_raw_sector(0)))
                .await?;
        }
        {
            let read_range = mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
            backend
                .read(Request::from_ref(slice::from_ref(&read_range), Sector::from_raw_sector(0)))
                .await?;
            check_range(&read_range, i as u8);
        }
    }

    Ok(())
}

/// Expand the list test cases into test cases in a module.
macro_rules! instantiate_backend_test_suite {
    ($test_type:ty, $name:ident, $($names:ident),+) => {
        crate::backend_test::instantiate_backend_test_suite!($test_type, $name);
        crate::backend_test::instantiate_backend_test_suite!($test_type, $($names),+);
    };
    ($test_type:ty, $name:ident) => {
        #[fuchsia_async::run_singlethreaded(test)]
        async fn $name() -> Result<(), Error> {
            crate::backend_test::$name::<$test_type>().await
        }
    };
    ($test_type:ty) => {
        crate::backend_test::instantiate_backend_test_suite!($test_type,
                                         test_get_attrs,
                                         test_read_per_sector_ranges,
                                         test_read_multiple_sectors_per_range,
                                         test_read_subsector_range,
                                         test_read_large,
                                         test_read_concurrent,
                                         test_write_per_sector_ranges,
                                         test_write_multiple_sectors_per_range,
                                         test_write_subsector_range,
                                         test_write_large,
                                         test_write_concurrent,
                                         test_read_write_loop);
    };
}

pub(crate) use instantiate_backend_test_suite;
