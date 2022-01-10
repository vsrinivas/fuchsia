// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{
        engine::scheduler::{CollectorScheduler, CollectorState},
        model::controller::DataController,
        model::model::DataModel,
    },
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::{Arc, Mutex},
};

/// Displays a list of all the collectors.
pub struct CollectorListController {
    scheduler: Arc<Mutex<CollectorScheduler>>,
}

impl CollectorListController {
    pub fn new(scheduler: Arc<Mutex<CollectorScheduler>>) -> Self {
        Self { scheduler }
    }
}

#[derive(Serialize, Deserialize)]
pub struct CollectorListEntry {
    pub name: String,
    pub state: CollectorState,
}

impl DataController for CollectorListController {
    fn query(&self, _model: Arc<DataModel>, _query: Value) -> Result<Value> {
        let scheduler = self.scheduler.lock().unwrap();
        let mut collectors = vec![];
        for (handle, name) in scheduler.collectors_all() {
            let state = scheduler.state(&handle).unwrap();
            collectors.push(CollectorListEntry { name, state });
        }
        collectors.sort_by(|a, b| a.name.partial_cmp(&b.name).unwrap());
        Ok(json!(collectors))
    }
    fn description(&self) -> String {
        "Returns a list of all loaded data collectors.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.controller.list - Lists all of the data controllers")
            .summary("engine.controller.list")
            .description(
                "Lists all of the controllers that are currently loaded \
            in Scrutiny. This command provides internal inspection of the Scrutiny \
            engine and is helpful for debugging plugin issues.",
            )
            .build()
    }
}

/// Runs all of the collectors when called.
pub struct CollectorSchedulerController {
    scheduler: Arc<Mutex<CollectorScheduler>>,
}

impl CollectorSchedulerController {
    pub fn new(scheduler: Arc<Mutex<CollectorScheduler>>) -> Self {
        Self { scheduler }
    }
}

impl DataController for CollectorSchedulerController {
    fn query(&self, _model: Arc<DataModel>, _query: Value) -> Result<Value> {
        if self.scheduler.lock().unwrap().schedule().is_ok() {
            Ok(json!({"status": "ok"}))
        } else {
            Ok(json!({"status": "failed"}))
        }
    }
    fn description(&self) -> String {
        "Schedules all data collectors to run.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.collector.schedule - Runs all of the DataCollectors")
            .summary("engine.collector.schedule")
            .description(
                "Schedules all the loaded data collectors to run. This \
            repopulates the DataModel with all of the data provided by the collectors. \
            This is useful if you want to force the model to be refreshed with the \
            latest system data.",
            )
            .build()
    }
}
