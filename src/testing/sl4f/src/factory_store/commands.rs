// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

use crate::factory_store::facade::FactoryStoreFacade;
use crate::factory_store::types::FactoryStoreMethod;

#[async_trait(?Send)]
impl Facade for FactoryStoreFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            FactoryStoreMethod::ReadFile => self.read_file(args).await,
            FactoryStoreMethod::ListFiles => self.list_files(args).await,
        }
    }
}
