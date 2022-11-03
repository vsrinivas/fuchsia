// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::expect_fun_call)]
#![allow(clippy::let_unit_value)]
#![allow(clippy::type_complexity)]

mod directory;
pub mod minfs;
mod volume;

pub use volume::EncryptedVolumeStorageManager;

use account_common::AccountManagerError;
use anyhow::{bail, Error};
use async_trait::async_trait;
use fidl_fuchsia_io as fio;
use serde::{Deserialize, Serialize};

/// An enumeration of the possible key configurations for securing the
/// data contained in a `StorageManager`.
#[derive(PartialEq, Serialize, Deserialize)]
pub enum Key {
    /// No caller specified key.
    NoSpecifiedKey,

    // Caller specified key with 256 bits. Prefer using this over
    // CustomLength(..) if the length of the key is known upfront.
    Key256Bit([u8; 32]),
}

impl TryFrom<&Key> for [u8; 32] {
    type Error = Error;

    fn try_from(key: &Key) -> Result<Self, Self::Error> {
        match key {
            Key::Key256Bit(bits) => Ok(*bits),
            Key::NoSpecifiedKey => bail!("No custom key."),
        }
    }
}

/// An implementation of `StorageManager` provides access to a directory,
/// optionally protected with a key specified by the caller.  A `StorageManager`
/// transitions between three internal states - uninitialized, locked, and
/// available.  Handles to the directory may only be retrieved while the
/// `StorageManager` is in the available state.  Similarly, handles to the
/// directory provided by a `StorageManager` and any subdirectories should be
/// closed prior to locking the `StorageManager`.
///
///       unlock_
///       storage
///  ┌───────────────┐
///  │               ▼
///  │             ┌─────────────────────────────────────────┐
///  │    ┌──────▶ │                AVAILABLE                │
///  │    │        └─────────────────────────────────────────┘
///  │    │          │                │           ▲
///  │    │ unlock_  │ lock_          │           │ provision
///  │    │ storage  │ storage        │           │
///  │    │          ▼                │           │
///  │    │        ┌───────────────┐  │           │
///  │    └─────── │    LOCKED     │  │           │
///  │             └───────────────┘  │           │
///  │               │                │ destroy   │
///  │               │ destroy        │           │
///  │               ▼                │           │
///  │             ┌───────────────┐  │           │
///  └──────────── │ UNINITIALIZED │ ◀┘           │
///                └───────────────┘              │
///                  │                            │
///                  └────────────────────────────┘
///
#[async_trait]
pub trait StorageManager: Sized {
    type Key;

    /// Provisions a new storage instance.  The same `Key` must be supplied
    /// during a call to `unlock`. Moves the `StorageManager` from the
    /// uninitialized to the unlocked state.
    async fn provision(&self, key: &Self::Key) -> Result<(), AccountManagerError>;

    /// Unlocks a locked storage resource.  Moves the `StorageManager` from
    /// the locked or uninitialized state to the available state. Fails if
    /// the key does not match the key originally given through `provision`.
    async fn unlock_storage(&self, key: &Self::Key) -> Result<(), AccountManagerError>;

    /// Locks the storage resource.  All connections to the directory should be
    /// closed prior to calling this method.  Moves the `StorageManager` from
    /// the available state to the locked state.
    async fn lock_storage(&self) -> Result<(), AccountManagerError>;

    /// Removes the underlying storage resources, permanently destroying the
    /// directory managed by the `StorageManager`.  All connections to the
    /// directory should be closed prior to calling this method.  Moves the
    /// `StorageManager` to the uninitialized state.
    async fn destroy(&self) -> Result<(), AccountManagerError>;

    /// Returns a proxy to the root directory managed by the `StorageManager`.
    /// The `StorageManager` must be in the available state.
    async fn get_root_dir(&self) -> Result<fio::DirectoryProxy, AccountManagerError>;
}
