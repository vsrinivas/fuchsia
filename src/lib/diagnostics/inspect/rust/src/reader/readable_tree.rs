// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides implementations for common structs that can be read in its entirety. These are structs
//! that can be interpreted using the `fuchsia.inspect.Tree` protocol.

use {
    crate::{reader::ReaderError, Inspector},
    async_trait::async_trait,
};

#[cfg(target_os = "fuchsia")]
use {
    fidl_fuchsia_inspect::{TreeMarker, TreeNameIteratorMarker, TreeProxy},
    fuchsia_zircon as zx,
};

/// Provides functions for reading a snapshot.
pub trait ReadSnapshot {
    /// Copy bytes from self to dest.
    fn read_bytes(&self, dest: &mut [u8], offset: u64) -> Result<(), ReaderError>;

    /// Returns the size of ths snapshot source or an error if it's not possible to get it.
    fn size(&self) -> Result<u64, ReaderError>;
}

#[cfg(target_os = "fuchsia")]
mod target {
    use super::ReadSnapshot;
    use crate::reader::ReaderError;
    use fuchsia_zircon as zx;

    /// A type alias representing a data source that can be snapshotted.
    pub type SnapshotSource = zx::Vmo;

    impl ReadSnapshot for SnapshotSource {
        fn read_bytes(&self, dest: &mut [u8], offset: u64) -> Result<(), ReaderError> {
            self.read(dest, offset).map_err(ReaderError::Vmo)
        }

        fn size(&self) -> Result<u64, ReaderError> {
            self.get_size().map_err(ReaderError::Vmo)
        }
    }
}

#[cfg(not(target_os = "fuchsia"))]
mod target {
    use super::ReadSnapshot;
    use crate::reader::ReaderError;
    use inspect_format::ReadableBlockContainer;
    use std::sync::Arc;
    use std::sync::Mutex;
    use std::vec::Vec;

    /// A type alias representing a data source that can be snapshotted.
    pub type SnapshotSource = Arc<Mutex<Vec<u8>>>;

    impl ReadSnapshot for SnapshotSource {
        fn read_bytes(&self, dest: &mut [u8], offset: u64) -> Result<(), ReaderError> {
            let offset = offset as usize;
            if offset >= ReadableBlockContainer::size(self) {
                return Err(ReaderError::OffsetOutOfBounds);
            }

            let _ = ReadableBlockContainer::read_bytes(self, offset, dest);

            Ok(())
        }

        fn size(&self) -> Result<u64, ReaderError> {
            Ok(ReadableBlockContainer::size(self) as u64)
        }
    }
}

pub use target::*;

/// Trait implemented by structs that can provide inspect data and their lazy links.
#[async_trait]
pub trait ReadableTree: Sized {
    /// Returns the lazy links names.
    async fn tree_names(&self) -> Result<Vec<String>, ReaderError>;

    /// Returns the vmo of the current root node.
    async fn vmo(&self) -> Result<SnapshotSource, ReaderError>;

    /// Loads the lazy link of the given `name`.
    async fn read_tree(&self, name: &str) -> Result<Self, ReaderError>;
}

#[async_trait]
impl ReadableTree for Inspector {
    async fn vmo(&self) -> Result<SnapshotSource, ReaderError> {
        #[cfg(target_os = "fuchsia")]
        return self.duplicate_vmo().ok_or(ReaderError::DuplicateVmo);

        #[cfg(not(target_os = "fuchsia"))]
        return self.clone_heap_container().ok_or(ReaderError::NoOpInspector);
    }

    async fn tree_names(&self) -> Result<Vec<String>, ReaderError> {
        match self.state() {
            // A no-op inspector.
            None => Ok(vec![]),
            Some(state) => {
                let state = state.try_lock().map_err(ReaderError::FailedToLockState)?;
                let names =
                    state.callbacks().keys().map(|k| k.to_string()).collect::<Vec<String>>();
                Ok(names)
            }
        }
    }

    async fn read_tree(&self, name: &str) -> Result<Self, ReaderError> {
        let result = self.state().and_then(|state| match state.try_lock() {
            Err(_) => None,
            Ok(state) => state.callbacks().get(name).map(|cb| cb()),
        });
        match result {
            Some(cb_result) => cb_result.await.map_err(ReaderError::LazyCallback),
            None => return Err(ReaderError::FailedToLoadTree(name.to_string())),
        }
    }
}

#[cfg(target_os = "fuchsia")]
#[async_trait]
impl ReadableTree for TreeProxy {
    async fn vmo(&self) -> Result<zx::Vmo, ReaderError> {
        let tree_content = self.get_content().await.map_err(|e| ReaderError::Fidl(e.into()))?;
        tree_content.buffer.map(|b| b.vmo).ok_or(ReaderError::FetchVmo)
    }

    async fn tree_names(&self) -> Result<Vec<String>, ReaderError> {
        let (name_iterator, server_end) = fidl::endpoints::create_proxy::<TreeNameIteratorMarker>()
            .map_err(|e| ReaderError::Fidl(e.into()))?;
        self.list_child_names(server_end).map_err(|e| ReaderError::Fidl(e.into()))?;
        let mut names = vec![];
        loop {
            let subset_names =
                name_iterator.get_next().await.map_err(|e| ReaderError::Fidl(e.into()))?;
            if subset_names.is_empty() {
                return Ok(names);
            }
            names.extend(subset_names.into_iter());
        }
    }

    async fn read_tree(&self, name: &str) -> Result<Self, ReaderError> {
        let (child_tree, server_end) = fidl::endpoints::create_proxy::<TreeMarker>()
            .map_err(|e| ReaderError::Fidl(e.into()))?;
        self.open_child(name, server_end).map_err(|e| ReaderError::Fidl(e.into()))?;
        Ok(child_tree)
    }
}
