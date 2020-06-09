// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
use crate::factory_reset::{facade::FactoryResetFacade, types::FactoryResetMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for FactoryResetFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        match method.parse()? {
            FactoryResetMethod::FactoryReset => {
                let result = self.factory_reset().await?;
                Ok(to_value(result)?)
            }
        }
    }
}
