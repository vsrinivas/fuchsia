// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::Timestamp,
    anyhow::{bail, Error},
    async_trait::async_trait,
    storage_device::buffer::{Buffer, BufferRef, MutableBufferRef},
};

// Some places use Default and assume that zero is an invalid object ID, so this cannot be changed
// easily.
pub const INVALID_OBJECT_ID: u64 = 0;

/// A handle for a generic object.  For objects with a data payload, use the ReadObjectHandle or
/// WriteObjectHandle traits.
pub trait ObjectHandle: Send + Sync + 'static {
    /// Returns the object identifier for this object which will be unique for the store that the
    /// object is contained in, but not necessarily unique within the entire system.
    fn object_id(&self) -> u64;

    // Returns the size of the object.
    fn get_size(&self) -> u64;

    /// Returns the filesystem block size, which should be at least as big as the device block size,
    /// but not necessarily the same.
    fn block_size(&self) -> u32;

    /// Allocates a buffer for doing I/O (read and write) for the object.
    fn allocate_buffer(&self, size: usize) -> Buffer<'_>;

    /// Sets tracing for this object.
    fn set_trace(&self, _v: bool) {}
}

#[derive(Clone, Debug, PartialEq)]
pub struct ObjectProperties {
    /// The number of references to this object.
    pub refs: u64,
    /// The number of bytes allocated to all extents across all attributes for this object.
    pub allocated_size: u64,
    /// The logical content size for the default data attribute of this object, i.e. the size of a
    /// file.  (Objects with no data attribute have size 0.)
    pub data_attribute_size: u64,
    /// The timestamp at which the object was created (i.e. crtime).
    pub creation_time: Timestamp,
    /// The timestamp at which the objects's data was last modified (i.e. mtime).
    pub modification_time: Timestamp,
    /// The number of sub-directories.
    pub sub_dirs: u64,
}

#[async_trait]
pub trait GetProperties {
    /// Gets the object's properties.
    async fn get_properties(&self) -> Result<ObjectProperties, Error>;
}

#[async_trait]
pub trait ReadObjectHandle: ObjectHandle {
    /// Fills |buf| with up to |buf.len()| bytes read from |offset| on the underlying device.
    /// |offset| and |buf| must both be block-aligned.
    async fn read(&self, offset: u64, buf: MutableBufferRef<'_>) -> Result<usize, Error>;
}

#[async_trait]
pub trait WriteObjectHandle: ObjectHandle {
    /// Writes |buf.len())| bytes at |offset| (or the end of the file), returning the object size
    /// after writing.
    /// The writes may be cached, in which case a later call to |flush| is necessary to persist the
    /// writes.
    async fn write_or_append(&self, offset: Option<u64>, buf: BufferRef<'_>) -> Result<u64, Error>;

    /// Truncates the object to |size| bytes.
    /// The truncate may be cached, in which case a later call to |flush| is necessary to persist
    /// the truncate.
    async fn truncate(&self, size: u64) -> Result<(), Error>;

    /// Updates the timestamps for the object.
    /// The truncate may be cached, in which case a later call to |flush| is necessary to persist
    /// the truncate.
    async fn write_timestamps<'a>(
        &'a self,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error>;

    /// Flushes all pending data and metadata updates for the object.
    async fn flush(&self) -> Result<(), Error>;
}

#[async_trait]
pub trait ObjectHandleExt: ReadObjectHandle {
    // Returns the contents of the object. The object must be < |limit| bytes in size.
    async fn contents(&self, limit: usize) -> Result<Box<[u8]>, Error> {
        let size = self.get_size();
        if size > limit as u64 {
            bail!("Object too big ({} > {})", size, limit);
        }
        let mut buf = self.allocate_buffer(size as usize);
        self.read(0u64, buf.as_mut()).await?;
        let mut vec = vec![0; size as usize];
        vec.copy_from_slice(&buf.as_slice());
        Ok(vec.into())
    }
}

#[async_trait]
impl<T: ReadObjectHandle + ?Sized> ObjectHandleExt for T {}

#[async_trait]
pub trait WriteBytes {
    fn handle(&self) -> &dyn WriteObjectHandle;

    async fn write_bytes(&mut self, buf: &[u8]) -> Result<(), Error>;
    async fn complete(&mut self) -> Result<(), Error>;

    fn skip(&mut self, amount: u64);
}

const BUFFER_SIZE: usize = 131_072;

pub struct Writer<'a> {
    handle: &'a dyn WriteObjectHandle,
    buffer: Buffer<'a>,
    offset: u64,
}

impl<'a> Writer<'a> {
    pub fn new(handle: &'a dyn WriteObjectHandle) -> Self {
        Self { handle, buffer: handle.allocate_buffer(BUFFER_SIZE), offset: 0 }
    }
}

#[async_trait]
impl WriteBytes for Writer<'_> {
    fn handle(&self) -> &dyn WriteObjectHandle {
        self.handle
    }

    async fn write_bytes(&mut self, mut buf: &[u8]) -> Result<(), Error> {
        while buf.len() > 0 {
            let to_do = std::cmp::min(buf.len(), BUFFER_SIZE);
            self.buffer.subslice_mut(..to_do).as_mut_slice().copy_from_slice(&buf[..to_do]);
            self.handle.write_or_append(Some(self.offset), self.buffer.subslice(..to_do)).await?;
            self.offset += to_do as u64;
            buf = &buf[to_do..];
        }
        Ok(())
    }

    async fn complete(&mut self) -> Result<(), Error> {
        self.handle.flush().await
    }

    fn skip(&mut self, amount: u64) {
        self.offset += amount;
    }
}
