// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_zircon as zx,
    std::{any::Any, sync::Arc},
};

// TODO(fxbug.dev/82100) Possibly re-work vfs::file::File so Filesystem is no longer needed,
// which would remove an extra copy during fuchsia.io/File.Read{At} and stop wasting the memory
// used by the Filesystem backing buffer.

/// vfs::filesystem::Filesystem implementation for package directory files.
/// To be given to package_directory::serve. Only used by
/// vfs::file::connection::io1::FileConnection::handle_read_at [1] to allocate buffers for holding
/// read bytes.
/// [1] https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/vfs/rust/src/file/connection/io1.rs;l=407;drc=39b5cd216c4318fac80b40d298ae86a08cb8cc32
pub struct Filesystem {
    allocator: storage_device::buffer_allocator::BufferAllocator,
}

impl Filesystem {
    /// Create a `Filesystem` with an eagerly allocated backing buffer of size
    /// `backing_buffer_size`.
    /// The buffer needs to be at least large enough to hold the largest possible allocation [1],
    /// which is 12 KiB [2] (if a client calls fuchsia.io/File.Read{At} with the maximum allowed
    /// count of fuchsia.io/MAX_BUF = 8K and the offset is not page-aligned (currently 4 KiB)).
    /// [1] https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/storage_device/src/buffer_allocator.rs;l=180-182;drc=25dae2875d430264728567bd193554d23c092e20
    /// [2] https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/vfs/rust/src/file/connection/io1.rs;l=407-412;drc=39b5cd216c4318fac80b40d298ae86a08cb8cc32
    pub fn new(backing_buffer_size: usize) -> Self {
        Self {
            allocator: storage_device::buffer_allocator::BufferAllocator::new(
                1, /*block_size*/
                Box::new(storage_device::buffer_allocator::MemBufferSource::new(
                    backing_buffer_size,
                )),
            ),
        }
    }
}

#[async_trait]
impl vfs::filesystem::FilesystemRename for Filesystem {
    // Filesystem is only to be used with MetaFile and Meta, which only need it for
    // vfs::file::connection::io1::FileConnection::handle_read_at which does not use rename.
    async fn rename(
        &self,
        _src_dir: Arc<dyn Any + Sync + Send + 'static>,
        _src_name: vfs::path::Path,
        _dst_dir: Arc<dyn Any + Sync + Send + 'static>,
        _dst_name: vfs::path::Path,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }
}

impl vfs::filesystem::Filesystem for Filesystem {
    // MetaFile and Meta have no alignment requirements.
    fn block_size(&self) -> u32 {
        1
    }

    fn allocate_buffer(&self, size: usize) -> storage_device::buffer::Buffer<'_> {
        self.allocator.allocate_buffer(size)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rename() {
        let filesystem = Filesystem::new(1);

        assert_eq!(
            vfs::filesystem::FilesystemRename::rename(
                &filesystem,
                Arc::new("unused"),
                vfs::path::Path::dot(),
                Arc::new("unused"),
                vfs::path::Path::dot()
            )
            .await,
            Err(zx::Status::NOT_SUPPORTED)
        );
    }

    #[test]
    fn block_size() {
        let filesystem = Filesystem::new(1);

        assert_eq!(vfs::filesystem::Filesystem::block_size(&filesystem), 1);
    }

    #[test]
    fn allocate_buffer() {
        let filesystem = Filesystem::new(32);

        let buffer = vfs::filesystem::Filesystem::allocate_buffer(&filesystem, 14);

        assert_eq!(buffer.len(), 14);
    }
}
