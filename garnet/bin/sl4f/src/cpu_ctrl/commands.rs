// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cpu_ctrl::facade::CpuCtrlFacade;
use crate::cpu_ctrl::types::{
    CpuCtrlMethod, GetLogicalCoreIdRequest, GetNumLogicalCoresRequest,
    GetPerformanceStateInfoRequest,
};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};
use std::convert::TryFrom;

fn get_state(args: Value) -> Result<u32, Error> {
    // Serde json does not support as_u32 so you need to take care of the cast.
    let state = match args.get("state") {
        Some(value) => match value.as_u64() {
            Some(v) => v as u32,
            None => bail!("Expected u64 type for state."),
        },
        None => bail!("Expected a serde_json Value state."),
    };
    Ok(state)
}

#[async_trait(?Send)]
impl Facade for CpuCtrlFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match CpuCtrlMethod::try_from((method.as_str(), args.clone()))? {
            CpuCtrlMethod::GetPerformanceStateInfo(GetPerformanceStateInfoRequest {
                device_number,
            }) => {
                let state = get_state(args)?;
                Ok(to_value(self.get_performance_state_info(device_number, state).await?)?)
            }

            CpuCtrlMethod::GetNumLogicalCores(GetNumLogicalCoresRequest { device_number }) => {
                Ok(to_value(self.get_num_logical_cores(device_number).await?)?)
            }

            CpuCtrlMethod::GetLogicalCoreId(GetLogicalCoreIdRequest { device_number }) => {
                let index = match args.get("index") {
                    Some(value) => value.as_u64(),
                    None => bail!("Expected a serde_json Value index."),
                };
                Ok(to_value(self.get_logical_core_id(device_number, index.unwrap()).await?)?)
            }
            _ => bail!("Invalid cpu-ctrl Facade FIDL method: {:?}", method),
        }
    }
}
