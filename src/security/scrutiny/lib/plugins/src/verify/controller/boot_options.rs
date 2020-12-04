// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Zbi,
    anyhow::{anyhow, Result},
    scrutiny::{model::controller::DataController, model::model::*},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct ZbiCmdlineVerifyController {}

impl DataController for ZbiCmdlineVerifyController {
    fn query(&self, model: Arc<DataModel>, _value: Value) -> Result<Value> {
        if let Ok(zbi) = model.get::<Zbi>() {
            if zbi.cmdline.contains("kernel.enable-debugging-syscalls=true") {
                return Err(anyhow!("debugging syscalls are not disabled"));
            }
            return Ok(json!({"verified":true}));
        }
        Err(anyhow!("ZBI not found"))
    }

    fn description(&self) -> String {
        "verifies the ZBI cmdline options for a _user build".to_string()
    }
}
