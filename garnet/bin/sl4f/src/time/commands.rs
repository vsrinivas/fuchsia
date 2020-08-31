// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{bail, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

use crate::time::facade::TimeFacade;

#[async_trait(?Send)]
impl Facade for TimeFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        match method.as_str() {
            "SystemTimeMillis" => {
                let system_time = Self::system_time_millis()?;
                Ok(to_value(system_time)?)
            }
            "KernelTimeMillis" => {
                let kernel_time = Self::kernel_time_millis()?;
                Ok(to_value(kernel_time)?)
            }
            "IsSynchronized" => {
                let result = Self::is_synchronized().await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Unrecognized time facade method: {}", method),
        }
    }
}
