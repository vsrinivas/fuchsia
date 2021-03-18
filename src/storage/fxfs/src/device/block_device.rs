// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::{
        buffer::Buffer,
        buffer_allocator::{BufferAllocator, BufferSource},
        Device,
    },
    anyhow::Error,
    async_trait::async_trait,
    fuchsia_runtime::vmar_root_self,
    fuchsia_zircon::{self as zx, AsHandleRef},
    remote_block_device::{BlockClient, BufferSlice, MutableBufferSlice, VmoId},
    std::any::Any,
    std::cell::UnsafeCell,
    std::ffi::CString,
    std::ops::Range,
};

#[derive(Debug)]
struct VmoBufferSource {
    vmoid: VmoId,
    // This needs to be 'static because the BufferSource trait requires 'static.
    slice: UnsafeCell<&'static mut [u8]>,
    vmo: zx::Vmo,
}

// Safe because none of the fields in VmoBufferSource are modified, except the contents of |slice|,
// but that is managed by the BufferAllocator.
unsafe impl Sync for VmoBufferSource {}

impl VmoBufferSource {
    fn new(remote: &dyn BlockClient, size: usize) -> Self {
        let vmo = zx::Vmo::create(size as u64).unwrap();
        let cname = CString::new("transfer-buf").unwrap();
        vmo.set_name(&cname).unwrap();
        let flags = zx::VmarFlags::PERM_READ
            | zx::VmarFlags::PERM_WRITE
            | zx::VmarFlags::MAP_RANGE
            | zx::VmarFlags::REQUIRE_NON_RESIZABLE;
        let addr = vmar_root_self().map(0, &vmo, 0, size, flags).unwrap();
        let slice =
            unsafe { UnsafeCell::new(std::slice::from_raw_parts_mut(addr as *mut u8, size)) };
        let vmoid = remote.attach_vmo(&vmo).unwrap();
        Self { vmoid, slice, vmo }
    }

    fn vmoid(&self) -> &VmoId {
        &self.vmoid
    }

    fn take_vmoid(self) -> VmoId {
        self.vmoid
    }
}

impl BufferSource for VmoBufferSource {
    fn size(&self) -> usize {
        // Safe because the reference goes out of scope as soon as we use it.
        unsafe { (&*self.slice.get()).len() }
    }

    unsafe fn sub_slice(&self, range: &Range<usize>) -> &mut [u8] {
        assert!(range.start < self.size() && range.end <= self.size());
        assert!(range.start % zx::system_get_page_size() as usize == 0);
        std::slice::from_raw_parts_mut(
            ((&mut *self.slice.get()).as_mut_ptr() as usize + range.start) as *mut u8,
            range.end - range.start,
        )
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn into_any(self: Box<Self>) -> Box<dyn Any> {
        self
    }
}

/// BlockDevice is an implementation of Device backed by a real block device behind a FIFO.
pub struct BlockDevice {
    allocator: BufferAllocator,
    remote: Box<dyn BlockClient>,
}

const TRANSFER_VMO_SIZE: usize = 128 * 1024 * 1024;

impl BlockDevice {
    /// Creates a new BlockDevice over |remote|.
    pub fn new(remote: Box<dyn BlockClient>) -> Self {
        let allocator = BufferAllocator::new(
            remote.block_size() as usize,
            Box::new(VmoBufferSource::new(remote.as_ref(), TRANSFER_VMO_SIZE)),
        );
        Self { allocator, remote }
    }

    fn buffer_source(&self) -> &VmoBufferSource {
        self.allocator.buffer_source().as_any().downcast_ref::<VmoBufferSource>().unwrap()
    }
}

#[async_trait]
impl Device for BlockDevice {
    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.allocator.allocate_buffer(size)
    }

    fn block_size(&self) -> u32 {
        self.remote.block_size()
    }

    async fn read(&self, offset: u64, buffer: &mut Buffer<'_>) -> Result<(), Error> {
        self.remote
            .read_at(
                MutableBufferSlice::new_with_vmo_id(
                    self.buffer_source().vmoid(),
                    buffer.range().start as u64,
                    buffer.size() as u64,
                ),
                offset,
            )
            .await
    }

    async fn write(&self, offset: u64, buffer: &Buffer<'_>) -> Result<(), Error> {
        self.remote
            .write_at(
                BufferSlice::new_with_vmo_id(
                    self.buffer_source().vmoid(),
                    buffer.range().start as u64,
                    buffer.size() as u64,
                ),
                offset,
            )
            .await
    }

    async fn close(self) -> Result<(), Error> {
        let source = self
            .allocator
            .take_buffer_source()
            .into_any()
            .downcast::<VmoBufferSource>()
            .unwrap();
        self.remote.detach_vmo(source.take_vmoid()).await
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::device::{block_device::BlockDevice, Device},
        fuchsia_async as fasync,
        remote_block_device::testing::FakeBlockClient,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_lifecycle() {
        let device = BlockDevice::new(Box::new(FakeBlockClient::new(1024, 1024)));

        {
            let _buf = device.allocate_buffer(8192);
        }

        device.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "Did you forget to detach?")]
    async fn test_panics_if_device_not_closed() {
        let _device = BlockDevice::new(Box::new(FakeBlockClient::new(1024, 1024)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_block_aligned_buffers() {
        let device = BlockDevice::new(Box::new(FakeBlockClient::new(1024, 1024)));

        {
            let buf = device.allocate_buffer(2000);
            assert_eq!(buf.size(), 2048);
        }

        device.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_write_buffer() {
        let device = BlockDevice::new(Box::new(FakeBlockClient::new(1024, 1024)));

        {
            let mut buf1 = device.allocate_buffer(8192);
            let mut buf2 = device.allocate_buffer(8192);
            buf1.as_mut_slice().fill(0xaa as u8);
            buf2.as_mut_slice().fill(0xbb as u8);
            device.write(65536, &buf1).await.expect("Write failed");
            device.write(65536 + 8192, &buf2).await.expect("Write failed");
        }
        {
            let mut buf = device.allocate_buffer(16384);
            device.read(65536, &mut buf).await.expect("Read failed");
            assert_eq!(buf.as_slice()[..8192], vec![0xaa as u8; 8192]);
            assert_eq!(buf.as_slice()[8192..], vec![0xbb as u8; 8192]);
        }

        device.close().await.expect("Close failed");
    }
}
