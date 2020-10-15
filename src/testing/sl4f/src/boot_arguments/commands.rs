// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{facade::BootArgumentsFacade, types::Method};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use serde_json::{from_value, to_value, Value};

#[async_trait(?Send)]
impl Facade for BootArgumentsFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            Method::GetString => {
                let args: GetStringRequest = from_value(args)?;
                let result = self.get_string(&args.key).await?;
                Ok(to_value(result)?)
            }
        }
    }
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub(super) struct GetStringRequest {
    key: String,
}
