// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Inspector,
    async_trait::async_trait,
    failure::{bail, format_err, Error},
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
        self.state()
            .map(|state| {
                let state = state.lock();
                state.callbacks.keys().map(|k| k.to_string()).collect::<Vec<String>>()
            })
            .ok_or(format_err!("failed to get tree names"))
    }

    async fn read_tree(&self, name: &str) -> Result<Self, Error> {
        let result = self.state().and_then(|state| {
            let state = state.lock();
            state.callbacks.get(name).map(|cb| cb())
        });
        match result {
            Some(cb_result) => cb_result.await,
            None => bail!("failed to load tree name = {:?}", name),
        }
    }
}
