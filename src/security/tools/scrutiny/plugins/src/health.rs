// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny::{
        plugin, collectors, controllers,
        engine::hook::PluginHooks,
        engine::plugin::{Plugin, PluginDescriptor},
        model::collector::DataCollector,
        model::controller::DataController,
        model::model::DataModel,
    },
    anyhow::Result,
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct HealthController {}

/// The `HealthPlugin` simply returns a ping. This is used to determine if
/// the service is alive and operating.
impl DataController for HealthController {
    fn query(&self, _: Arc<DataModel>, _: Value) -> Result<Value> {
        Ok(json!({"status" : "ok"}))
    }
}

plugin!(
    HealthPlugin,
    PluginHooks::new(
        collectors! {},
        controllers! {
            "/health/status" => HealthController::default(),
        }
    ),
    vec![]
);
