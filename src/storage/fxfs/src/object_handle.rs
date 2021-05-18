// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{transaction::Transaction, Timestamp},
    anyhow::{bail, Error},
    async_trait::async_trait,
    std::ops::Range,
    storage_device::buffer::{Buffer, BufferRef, MutableBufferRef},
};

// Some places use Default and assume that zero is an invalid object ID, so this cannot be changed
// easily.
pub const INVALID_OBJECT_ID: u64 = 0;

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
}

#[async_trait]
pub trait ObjectHandle: Send + Sync + 'static {
    /// Returns the object identifier for this object which will be unique for the store that the
    /// object is contained in, but not necessarily unique within the entire system.
    fn object_id(&self) -> u64;

    fn block_size(&self) -> u32;

    /// Allocates a buffer for doing I/O (read and write) for the object.
    fn allocate_buffer(&self, size: usize) -> Buffer<'_>;

    /// Fills |buf| with up to |buf.len()| bytes read from |offset| on the underlying device.
    /// |offset| and |buf| must both be block-aligned.
    async fn read(&self, offset: u64, buf: MutableBufferRef<'_>) -> Result<usize, Error>;

    /// Writes |buf| to the device at |offset|.
    /// The alignment of |offset| and |buf.range().start| must be equal (but they do not need to be
    /// block-aligned).
    async fn txn_write<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        offset: u64,
        buf: BufferRef<'_>,
    ) -> Result<(), Error>;

    // Returns the size of the object.
    fn get_size(&self) -> u64;

    /// Sets the size of the object to |size|.  If this extends the object, a hole is created.  If
    /// this shrinks the object, space will be deallocated (if there are no more references to the
    /// data).
    async fn truncate<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        size: u64,
    ) -> Result<(), Error>;

    /// Preallocates the given file range.  Data will not be initialised so this should be a
    /// privileged operation for now.  The data can be later written to using an overwrite mode.
    /// Returns the device ranges allocated.  Existing allocated ranges will be left untouched and
    /// the ranges returned will include those.
    async fn preallocate_range<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        range: Range<u64>,
    ) -> Result<Vec<Range<u64>>, Error>;

    /// Updates the timestamps for the object.  If either argument is None, that timestamp is not
    /// modified.
    /// If |transaction| is unset, the updates can be deferred until a later write.
    /// |get_properties| must immediately reflect the new values, though (i.e. they must be
    /// buffered).
    async fn update_timestamps<'a>(
        &'a self,
        transaction: Option<&mut Transaction<'a>>,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error>;

    /// Gets the object's properties.
    async fn get_properties(&self) -> Result<ObjectProperties, Error>;

    /// Returns a new transaction including a lock for this handle.
    async fn new_transaction<'a>(&self) -> Result<Transaction<'a>, Error>;

    /// Sets tracing for this object.
    fn set_trace(&self, _v: bool) {}
}

#[async_trait]
pub trait ObjectHandleExt: ObjectHandle {
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

    /// Performs a write within a transaction.
    async fn write(&self, offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
        let mut transaction = self.new_transaction().await?;
        self.txn_write(&mut transaction, offset, buf).await?;
        transaction.commit().await;
        Ok(())
    }
}

#[async_trait]
impl<T: ObjectHandle + ?Sized> ObjectHandleExt for T {}
