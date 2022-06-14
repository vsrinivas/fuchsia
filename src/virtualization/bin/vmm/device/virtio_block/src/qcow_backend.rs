// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::backend::{BlockBackend, DeviceAttrs, Request, Sector},
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    fuchsia_zircon as zx,
    std::io::{Read, Seek},
};

pub struct QcowBackend {
    file: std::cell::RefCell<std::fs::File>,
    qcow: qcow::TranslationTable,
}

impl QcowBackend {
    pub fn new(channel: zx::Channel) -> Result<Self, Error> {
        Self::from_file(fdio::create_fd(channel.into())?)
    }

    pub fn from_file(mut file: std::fs::File) -> Result<Self, Error> {
        Ok(Self {
            qcow: qcow::TranslationTable::load(&mut file)?,
            file: std::cell::RefCell::new(file),
        })
    }
}

#[async_trait(?Send)]
impl BlockBackend for QcowBackend {
    async fn get_attrs(&self) -> Result<DeviceAttrs, Error> {
        return Ok(DeviceAttrs {
            capacity: Sector::from_bytes_round_down(self.qcow.linear_size()),
            block_size: None,
        });
    }

    async fn read<'a, 'b>(&self, request: Request<'a, 'b>) -> Result<(), Error> {
        let offset = request.sector.to_bytes().unwrap();
        let length = request.ranges.iter().fold(0, |a, x| a + x.len()) as u64;
        let linear_range = std::ops::Range { start: offset, end: offset + length };
        // We should never attempt to read past the end of the device. Assert here as we expect the
        // BlockDevice to handle detecting this situation and reporting it to the driver for us.
        assert!(linear_range.end <= self.qcow.linear_size());

        let mut iter = request.ranges_unbounded();
        self.qcow.translate(linear_range).try_for_each(|mapping| -> Result<(), Error> {
            let mut bytes_to_read = mapping.len() as usize;

            // If we have a mapping, seek to the correct physical position in the file.
            if let qcow::Mapping::Mapped { physical_offset, .. } = mapping {
                self.file.borrow_mut().seek(std::io::SeekFrom::Start(physical_offset))?;
            }

            // Each mapping may span 1 or more DeviceRanges. Iterate over them here and read
            // directly into the DeviceRange.
            while bytes_to_read > 0 {
                // Unwrap here because this should always cover the entire request range, so long
                // as the range is contained fully within the file. We asserted this already so
                // this should not be possible to fail here.
                let (_, range) = iter.next_with_bound(bytes_to_read).unwrap();
                if let qcow::Mapping::Mapped { .. } = mapping {
                    let slice = unsafe {
                        std::slice::from_raw_parts_mut(range.try_mut_ptr().unwrap(), range.len())
                    };
                    self.file.borrow_mut().read_exact(slice)?;
                } else {
                    // If the range is not mapped in the qcow, then the bytes should read as 0.
                    unsafe {
                        libc::memset(range.try_mut_ptr().unwrap(), 0, range.len());
                    }
                }
                bytes_to_read -= range.len();
            }
            Ok(())
        })?;

        Ok(())
    }

    async fn write<'a, 'b>(&self, _request: Request<'a, 'b>) -> Result<(), Error> {
        Err(anyhow!("Not Implemented"))
    }

    async fn flush(&self) -> Result<(), Error> {
        // There's nothing to flush since we do not support writes.
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::backend::Request, crate::backend_test::check_range, crate::wire,
        fuchsia_async as fasync, virtio_device::fake_queue::IdentityDriverMem,
    };

    // We don't use the test cases in `crate::backend_test` directly because we don't currently
    // have the ability to write the test qcow files at runtime (which is something required by
    // those test cases).
    //
    // Instead we adapt some test cases to use a pre-generated disk image. For details of this
    // image see the target //src/virtualization/bin/vmm/device/virtio_block:qcow_test
    fn create_backend() -> QcowBackend {
        let test_image =
            std::fs::File::open("/pkg/data/qcow_test.qcow2").expect("Failed to open qcow file");
        QcowBackend::from_file(test_image).expect("Failed to create backend")
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_per_sector_ranges() {
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        ];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Create the backend and process the request.
        let backend = create_backend();
        backend.read(request).await.expect("Failed to read from the backend");

        // Verify the file data was read into the device ranges.
        check_range(&ranges[0], 0x01);
        check_range(&ranges[1], 0x02);
        check_range(&ranges[2], 0x00);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_multiple_sectors_per_range() {
        // Create a request to read all 3 sectors into a single descriptor.
        let mem = IdentityDriverMem::new();
        let range = mem.new_range(3 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let request = Request::from_ref(std::slice::from_ref(&range), Sector::from_raw_sector(0));

        // Create the backend and process the request.
        let backend = create_backend();
        backend.read(request).await.expect("Failed to read from the backend");

        // Verify the file data was read into the device ranges.
        let (sector0, remain) = range.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let (sector1, sector2) = remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        check_range(&sector0, 0x01);
        check_range(&sector1, 0x02);
        check_range(&sector2, 0x00);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_subsector_range() {
        // Create a request to read only one sector using 4 different descriptors.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 4).unwrap(),
        ];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Create the backend and process the request.
        let backend = create_backend();
        backend.read(request).await.expect("Failed to read from the backend");

        // Verify the correct data is read.
        check_range(&ranges[0], 0x01);
        check_range(&ranges[1], 0x01);
        check_range(&ranges[2], 0x01);
        check_range(&ranges[3], 0x01);
    }
}
