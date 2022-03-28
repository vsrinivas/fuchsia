// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    scrutiny::{
        engine::manager::{PluginManager, PluginState},
        model::controller::DataController,
        model::model::DataModel,
    },
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::{Arc, Mutex, Weak},
};

pub struct PluginListController {
    manager: Weak<Mutex<PluginManager>>,
}

#[derive(Deserialize, Serialize)]
pub struct PluginListEntry {
    name: String,
    state: PluginState,
}

impl PluginListController {
    pub fn new(manager: Weak<Mutex<PluginManager>>) -> Self {
        Self { manager }
    }
}

impl DataController for PluginListController {
    fn query(&self, _: Arc<DataModel>, _query: Value) -> Result<Value> {
        let manager_arc = match self.manager.upgrade() {
            Some(m) => m,
            None => return Err(format_err!("manager inner value behind Weak reference dropped")),
        };
        let manager = manager_arc.lock().unwrap();

        let plugin_descriptors = manager.plugins();
        let mut plugins = vec![];
        for plugin_desc in plugin_descriptors.iter() {
            let state = manager.state(plugin_desc).unwrap();
            plugins.push(PluginListEntry { name: format!("{}", plugin_desc), state });
        }
        plugins.sort_by(|a, b| a.name.partial_cmp(&b.name).unwrap());
        return Ok(json!(plugins));
    }
    fn description(&self) -> String {
        "Returns a list of all plugins and their state.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.plugin.list - Lists all the plugins")
            .summary("engine.plugin.list")
            .description("Lists all of the available plugins and their state")
            .build()
    }
}
