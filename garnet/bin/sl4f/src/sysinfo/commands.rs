// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::sysinfo::facade::SysInfoFacade;
use crate::sysinfo::types::SysInfoMethod;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};
use std::convert::TryFrom;

#[async_trait(?Send)]
impl Facade for SysInfoFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match SysInfoMethod::try_from((method.as_str(), args))? {
            SysInfoMethod::GetBoardName => {
                let result = self.get_board_name().await?;
                Ok(to_value(result)?)
            }

            SysInfoMethod::GetBoardRevision => {
                let result = self.get_board_revision().await?;
                Ok(to_value(result)?)
            }

            SysInfoMethod::GetBootloaderVendor => {
                let result = self.get_bootloader_vendor().await?;
                Ok(to_value(result)?)
            }

            _ => bail!("Invalid SysInfo Facade FIDL method: {:?}", method),
        }
    }
}
