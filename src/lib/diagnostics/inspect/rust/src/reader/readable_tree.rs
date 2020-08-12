// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Inspector,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_inspect::{TreeMarker, TreeNameIteratorMarker, TreeProxy},
    fuchsia_zircon as zx,
};

#[async_trait]
pub trait ReadableTree: Sized {
    async fn tree_names(&self) -> Result<Vec<String>, Error>;
    async fn vmo(&self) -> Result<zx::Vmo, Error>;
    async fn read_tree(&self, name: &str) -> Result<Self, Error>;
}

#[async_trait]
impl ReadableTree for Inspector {
    async fn vmo(&self) -> Result<zx::Vmo, Error> {
        self.duplicate_vmo().ok_or(format_err!("failed to get vmo"))
    }

    async fn tree_names(&self) -> Result<Vec<String>, Error> {
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

    async fn read_tree(&self, name: &str) -> Result<Self, Error> {
        let result = self.state().and_then(|state| {
            let state = state.lock();
            state.callbacks.get(name).map(|cb| cb())
        });
        match result {
            Some(cb_result) => cb_result.await,
            None => return Err(format_err!("failed to load tree name = {:?}", name)),
        }
    }
}

#[async_trait]
impl ReadableTree for TreeProxy {
    async fn vmo(&self) -> Result<zx::Vmo, Error> {
        let tree_content = self.get_content().await?;
        tree_content.buffer.map(|b| b.vmo).ok_or(format_err!("failed to fetch vmo"))
    }

    async fn tree_names(&self) -> Result<Vec<String>, Error> {
        let (name_iterator, server_end) =
            fidl::endpoints::create_proxy::<TreeNameIteratorMarker>()?;
        self.list_child_names(server_end)?;
        let mut names = vec![];
        loop {
            let subset_names = name_iterator.get_next().await?;
            if subset_names.is_empty() {
                return Ok(names);
            }
            names.extend(subset_names.into_iter());
        }
    }

    async fn read_tree(&self, name: &str) -> Result<Self, Error> {
        let (child_tree, server_end) = fidl::endpoints::create_proxy::<TreeMarker>()?;
        self.open_child(name, server_end)?;
        Ok(child_tree)
    }
}
