// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::kernel::facade::KernelFacade;
use crate::kernel::types::KernelMethod;
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};
use std::convert::TryFrom;

#[async_trait(?Send)]
impl Facade for KernelFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match KernelMethod::try_from((method.as_str(), args))? {
            KernelMethod::GetMemoryStats => {
                let result = self.get_memory_stats().await?;
                Ok(to_value(result)?)
            }
            KernelMethod::GetCpuStats => {
                let result = self.get_cpu_stats().await?;
                Ok(to_value(result)?)
            }
            KernelMethod::GetAllStats => {
                let result = self.get_all_stats().await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid Kernel Facade FIDL method: {:?}", method),
        }
    }
}
