// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer::{Buffer, BufferRef, MutableBufferRef},
        buffer_allocator::{BufferAllocator, MemBufferSource},
        Device,
    },
    anyhow::{ensure, Error},
    async_trait::async_trait,
    // Provides read_exact_at and write_all_at.
    // TODO(jfsulliv): Do we need to support non-UNIX systems?
    std::os::unix::fs::FileExt,
};

// TODO(csuter): Consider using an async file interface.

/// FileBackedDevice is an implementation of Device backed by a std::fs::File. It is intended to be
/// used for host tooling (to create or verify fxfs images), although it could also be used on
/// Fuchsia builds if we wanted to do that for whatever reason.
pub struct FileBackedDevice {
    allocator: BufferAllocator,
    file: std::fs::File,
    block_count: u64,
    block_size: u32,
}

const TRANSFER_HEAP_SIZE: usize = 32 * 1024 * 1024;

impl FileBackedDevice {
    /// Creates a new FileBackedDevice over |file|. The size of the file will be used as the size of
    /// the Device.
    pub fn new(file: std::fs::File, block_size: u32) -> Self {
        let size = file.metadata().unwrap().len();
        let block_count = size / block_size as u64;
        // TODO(jfsulliv): If file is S_ISBLK, we should use its block size. Rust does not appear to
        // expose this information in a portable way, so we may need to dip into non-portable code
        // to do so.
        let allocator = BufferAllocator::new(
            block_size as usize,
            Box::new(MemBufferSource::new(TRANSFER_HEAP_SIZE)),
        );
        Self { allocator, file, block_count, block_size }
    }
}

#[async_trait]
impl Device for FileBackedDevice {
    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.allocator.allocate_buffer(size)
    }

    fn block_size(&self) -> u32 {
        self.block_size
    }

    fn block_count(&self) -> u64 {
        self.block_count
    }

    async fn read(&self, offset: u64, mut buffer: MutableBufferRef<'_>) -> Result<(), Error> {
        assert_eq!(offset % self.block_size() as u64, 0);
        assert_eq!(buffer.range().start % self.block_size() as usize, 0);
        assert_eq!(buffer.len() % self.block_size() as usize, 0);
        ensure!(offset + buffer.len() as u64 <= self.size(), "Reading past end of file");
        // This isn't actually async, but that probably doesn't matter for host usage.
        self.file.read_exact_at(buffer.as_mut_slice(), offset)?;
        Ok(())
    }

    async fn write(&self, offset: u64, buffer: BufferRef<'_>) -> Result<(), Error> {
        assert_eq!(offset % self.block_size() as u64, 0);
        assert_eq!(buffer.range().start % self.block_size() as usize, 0);
        assert_eq!(buffer.len() % self.block_size() as usize, 0);
        ensure!(offset + buffer.len() as u64 <= self.size(), "Writing past end of file");
        // This isn't actually async, but that probably doesn't matter for host usage.
        self.file.write_all_at(buffer.as_slice(), offset)?;
        Ok(())
    }

    async fn close(&self) -> Result<(), Error> {
        // This isn't actually async, but that probably doesn't matter for host usage.
        self.file.sync_all()?;
        Ok(())
    }

    async fn flush(&self) -> Result<(), Error> {
        self.file.sync_data().map_err(Into::into)
    }

    fn is_read_only(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{file_backed_device::FileBackedDevice, Device},
        std::fs::{File, OpenOptions},
        std::path::PathBuf,
    };

    fn create_file() -> (PathBuf, File) {
        let mut temp_path = std::env::temp_dir();
        temp_path.push(format!("file_{:x}", rand::random::<u64>()));
        let (pathbuf, file) = (
            temp_path.clone(),
            OpenOptions::new()
                .read(true)
                .write(true)
                .create_new(true)
                .open(temp_path.as_path())
                .expect(&format!("create {:?} failed", temp_path.as_path())),
        );
        file.set_len(1024 * 1024).expect("Failed to truncate file");
        (pathbuf, file)
    }

    #[fuchsia::test]
    async fn test_lifecycle() {
        let (_path, file) = create_file();
        let device = FileBackedDevice::new(file, 512);

        {
            let _buf = device.allocate_buffer(8192);
        }

        device.close().await.expect("Close failed");
    }

    #[fuchsia::test]
    async fn test_read_write() {
        let (_path, file) = create_file();
        let device = FileBackedDevice::new(file, 512);

        {
            let mut buf1 = device.allocate_buffer(8192);
            let mut buf2 = device.allocate_buffer(8192);
            buf1.as_mut_slice().fill(0xaa as u8);
            buf2.as_mut_slice().fill(0xbb as u8);
            device.write(65536, buf1.as_ref()).await.expect("Write failed");
            device.write(65536 + 8192, buf2.as_ref()).await.expect("Write failed");
        }
        {
            let mut buf = device.allocate_buffer(16384);
            device.read(65536, buf.as_mut()).await.expect("Read failed");
            assert_eq!(buf.as_slice()[..8192], vec![0xaa as u8; 8192]);
            assert_eq!(buf.as_slice()[8192..], vec![0xbb as u8; 8192]);
        }

        device.close().await.expect("Close failed");
    }

    #[fuchsia::test]
    async fn test_read_write_past_end_of_file_fails() {
        let (_path, file) = create_file();
        let device = FileBackedDevice::new(file, 512);

        {
            let mut buf = device.allocate_buffer(8192);
            let offset = (device.size() as usize - buf.len() + device.block_size() as usize) as u64;
            buf.as_mut_slice().fill(0xaa as u8);
            device.write(offset, buf.as_ref()).await.expect_err("Write should have failed");
            device.read(offset, buf.as_mut()).await.expect_err("Read should have failed");
        }

        device.close().await.expect("Close failed");
    }

    #[fuchsia::test]
    async fn test_writes_persist() {
        let (path, file) = create_file();
        let device = FileBackedDevice::new(file, 512);

        {
            let mut buf1 = device.allocate_buffer(8192);
            let mut buf2 = device.allocate_buffer(8192);
            buf1.as_mut_slice().fill(0xaa as u8);
            buf2.as_mut_slice().fill(0xbb as u8);
            device.write(65536, buf1.as_ref()).await.expect("Write failed");
            device.write(65536 + 8192, buf2.as_ref()).await.expect("Write failed");
        }
        device.close().await.expect("Close failed");

        let file = File::open(path.as_path()).expect("Open failed");
        let device = FileBackedDevice::new(file, 512);

        {
            let mut buf = device.allocate_buffer(16384);
            device.read(65536, buf.as_mut()).await.expect("Read failed");
            assert_eq!(buf.as_slice()[..8192], vec![0xaa as u8; 8192]);
            assert_eq!(buf.as_slice()[8192..], vec![0xbb as u8; 8192]);
        }
        device.close().await.expect("Close failed");
    }
}
