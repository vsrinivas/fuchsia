// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    async_trait::async_trait,
};

#[async_trait]
pub trait ObjectHandle: Send + Sync {
    /// Returns the object identifier for this object which will be unique for the store that the
    /// object is contained in, but not necessarily unique within the entire system.
    fn object_id(&self) -> u64;

    async fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize, Error>;

    async fn write(&self, offset: u64, buf: &[u8]) -> Result<(), Error>;

    // Returns the size of the object.
    fn get_size(&self) -> u64;

    /// Sets the size of the object to |size|.  If this extends the object, a hole is created.  If
    /// this shrinks the object, space will be deallocated (if there are no more references to the
    /// data).
    async fn truncate(&self, size: u64) -> Result<(), Error>;
}

#[async_trait]
pub trait ObjectHandleExt {
    // Returns the contents of the object. The object must be < |limit| bytes in size.
    async fn contents(&self, limit: usize) -> Result<Box<[u8]>, Error>;
}

#[async_trait]
impl<T: ObjectHandle> ObjectHandleExt for T {
    async fn contents(&self, limit: usize) -> Result<Box<[u8]>, Error> {
        let size = self.get_size();
        if size > limit as u64 {
            bail!("Object too big ({} > {})", size, limit);
        }
        let mut buf = vec![0; size as usize];
        self.read(0, &mut buf[..]).await?;
        Ok(buf.into())
    }
}
