// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

use crate::repository_manager::facade::RepositoryManagerFacade;
use crate::repository_manager::types::RepositoryManagerMethod;

#[async_trait(?Send)]
impl Facade for RepositoryManagerFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            RepositoryManagerMethod::Add => self.add(args).await,
            RepositoryManagerMethod::List => self.list_repo().await,
        }
    }
}
