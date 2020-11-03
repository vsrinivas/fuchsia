// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{reader::ReaderError, Inspector},
    async_trait::async_trait,
    fidl_fuchsia_inspect::{TreeMarker, TreeNameIteratorMarker, TreeProxy},
    fuchsia_zircon as zx,
};

#[async_trait]
pub trait ReadableTree: Sized {
    async fn tree_names(&self) -> Result<Vec<String>, ReaderError>;
    async fn vmo(&self) -> Result<zx::Vmo, ReaderError>;
    async fn read_tree(&self, name: &str) -> Result<Self, ReaderError>;
}

#[async_trait]
impl ReadableTree for Inspector {
    async fn vmo(&self) -> Result<zx::Vmo, ReaderError> {
        self.duplicate_vmo().ok_or(ReaderError::DuplicateVmo)
    }

    async fn tree_names(&self) -> Result<Vec<String>, ReaderError> {
        match self.state() {
            // A no-op inspector.
            None => Ok(vec![]),
            Some(state) => {
                let state = state.lock();
                let names = state.callbacks.keys().map(|k| k.to_string()).collect::<Vec<String>>();
                Ok(names)
            }
        }
    }

    async fn read_tree(&self, name: &str) -> Result<Self, ReaderError> {
        let result = self.state().and_then(|state| {
            let state = state.lock();
            state.callbacks.get(name).map(|cb| cb())
        });
        match result {
            Some(cb_result) => cb_result.await.map_err(ReaderError::LazyCallback),
            None => return Err(ReaderError::FailedToLoadTree(name.to_string())),
        }
    }
}

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
