// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

use crate::tiles::facade::TilesFacade;
use crate::tiles::types::TilesMethod;

#[async_trait(?Send)]
impl Facade for TilesFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            TilesMethod::Start => self.start_tile(),
            TilesMethod::Stop => self.stop_tile(),
            TilesMethod::List => self.list().await,
            TilesMethod::Remove => self.remove(args).await,
            TilesMethod::AddFromUrl => self.add_from_url(args).await,
        }
    }
}
