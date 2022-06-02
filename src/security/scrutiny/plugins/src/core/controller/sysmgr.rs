// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Sysmgr,
    anyhow::Result,
    scrutiny::{model::controller::DataController, model::model::DataModel},
    scrutiny_utils::usage::UsageBuilder,
    serde_json::{self, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct SysmgrServicesController {}

impl DataController for SysmgrServicesController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        let mut services = vec![];
        for (name, url) in model.get::<Sysmgr>()?.iter() {
            services.push((name.to_string(), url.to_string()));
        }
        services.sort();
        Ok(serde_json::to_value(services)?)
    }
    fn description(&self) -> String {
        "Returns every sysmgr service in the /sys realm.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("sysmgr.services - Dumps every sysgmr service.")
            .summary("sysmgr.services")
            .description("Dumps all the sysmgr service mappings")
            .build()
    }
}
