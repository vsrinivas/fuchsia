// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::i2c::{facade::I2cFacade, types::Device2Method};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for I2cFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            Device2Method::Transfer => {
                let result = self.transfer(args).await?;
                Ok(to_value(result)?)
            }
        }
    }
}
