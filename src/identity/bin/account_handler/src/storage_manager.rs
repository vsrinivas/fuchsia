// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::AccountManagerError;
use async_trait::async_trait;
use fidl_fuchsia_io::DirectoryProxy;

/// An enumeration of the possible key configurations for securing the
/// data contained in a `StorageManager`.
#[allow(dead_code)]
pub enum Key<'a> {
    /// No caller specified key.
    NoCustomKey,
    /// Caller specified key provided.
    CustomKey(&'a [u8]),
}

/// An implementation of `StorageManager` provides access to a directory,
/// optionally protected with a key specified by the caller.  A `StorageManager`
/// transitions between three internal states - uninitialized, locked, and
/// available.  Handles to the directory may only be retrieved while the
/// `StorageManager` is in the available state.  Similarly, handles to the
/// directory provided by a `StorageManager` and any subdirectories should be
/// closed prior to locking the `StorageManager`.
#[allow(dead_code)]
#[async_trait]
pub trait StorageManager: Sized {
    /// Provisions a new storage instance.  The same `Key` must be supplied
    /// during a call to `unlock`. Moves the `StorageManager` from the
    /// uninitialized to the unlocked state.
    async fn provision(&self, key: Key) -> Result<(), AccountManagerError>;

    /// Unlocks a locked storage resource.  Moves the `StorageManager` from
    /// the locked or uninitialized state to the available state. Fails if
    /// the key does not match the key originally given through `provision`.
    async fn unlock(&self, key: Key) -> Result<(), AccountManagerError>;

    /// Locks the storage resource.  All connections to the directory should be
    /// closed prior to calling this method.  Moves the `StorageManager` from
    /// the available state to the locked state.
    async fn lock(&self) -> Result<(), AccountManagerError>;

    /// Removes the underlying storage resources, permanently destroying the
    /// directory managed by the `StorageManager`.  All connections to the
    /// directory should be closed prior to calling this method.  Moves the
    /// `StorageManager` to the uninitialized state.
    async fn destroy(&self) -> Result<(), AccountManagerError>;

    /// Returns a proxy to the root directory managed by the `StorageManager`.
    /// The `StorageManager` must be in the available state.
    async fn get_root_dir(&self) -> Result<DirectoryProxy, AccountManagerError>;
}
