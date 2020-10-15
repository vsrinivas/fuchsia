// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::{facade::DeviceFacade, types::DeviceMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for DeviceFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            DeviceMethod::GetDeviceName => {
                let result = self.get_device_name().await?;
                Ok(to_value(result)?)
            }
            DeviceMethod::Rebind => Ok(to_value(self.rebind(args).await?)?),
        }
    }
}
