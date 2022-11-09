// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{
        engine::dispatcher::ControllerDispatcher, model::controller::DataController,
        model::model::DataModel,
    },
    scrutiny_utils::usage::UsageBuilder,
    serde_json::{json, value::Value},
    std::sync::{Arc, RwLock},
};

/// Lists all of the controllers.
pub struct ControllerListController {
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
}

impl ControllerListController {
    pub fn new(dispatcher: Arc<RwLock<ControllerDispatcher>>) -> Self {
        Self { dispatcher }
    }
}

impl DataController for ControllerListController {
    fn query(&self, _model: Arc<DataModel>, _query: Value) -> Result<Value> {
        Ok(json!(self.dispatcher.read().unwrap().controllers_all()))
    }
    fn description(&self) -> String {
        "Lists all loaded data collectors.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.collector.list- List all of the DataCollectors")
            .summary("engine.collector.list")
            .description(
                "Lists all of the loaded Data Collectors. This can be\
            useful for debugging plugins.",
            )
            .build()
    }
}
