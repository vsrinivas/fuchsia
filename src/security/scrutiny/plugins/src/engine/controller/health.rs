// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{model::controller::DataController, model::model::DataModel},
    scrutiny_utils::usage::UsageBuilder,
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct HealthController {}

/// The `HealthController` simply returns a ping. This is used to determine if
/// the service is alive and operating.
impl DataController for HealthController {
    fn query(&self, _: Arc<DataModel>, _: Value) -> Result<Value> {
        Ok(json!({"status" : "ok"}))
    }
    fn description(&self) -> String {
        "Health endpoint that always returns ok.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.health.status - Health endpoint")
            .summary("engine.health.status")
            .description(
                "Health endpoint that always returns ok. \
            This is used mostly by remote endpoints to determine if the \
            connection and the api is available",
            )
            .build()
    }
}
