// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::collector::DataCollector, crate::model::model::DataModel, anyhow::Result,
    log::info, std::sync::Arc, uuid::Uuid,
};

struct CollectorInstance {
    pub instance_id: Uuid,
    pub collector: Arc<dyn DataCollector>,
}

impl CollectorInstance {
    pub fn new(instance_id: Uuid, collector: Arc<dyn DataCollector>) -> Self {
        Self { instance_id: instance_id, collector: collector }
    }
}

/// The `CollectorPool` contains all of the `DataCollectors` registered by `Plugins`.
/// It provides a simple way to collectively run the data collectors.
pub struct CollectorPool {
    model: Arc<DataModel>,
    collectors: Vec<CollectorInstance>,
}

impl CollectorPool {
    pub fn new(model: Arc<DataModel>) -> Self {
        Self { model: model, collectors: Vec::new() }
    }

    /// Adds a collector associated with a particular `instance_id` to the collector
    /// pool.
    pub fn add(&mut self, instance_id: Uuid, collector: Arc<dyn DataCollector>) {
        self.collectors.push(CollectorInstance::new(instance_id, collector));
    }

    /// Removes all `CollectorInstance` objects with a matching instance-id.
    /// This effectively unhooks all the plugins collectors.
    pub fn remove(&mut self, instance_id: Uuid) {
        self.collectors.retain(|v| v.instance_id != instance_id);
    }

    /// Runs all of the tasks.
    pub fn schedule(&self) -> Result<()> {
        info!("Collector Pool: Scheduling {} Tasks", self.collectors.len());
        for instance in self.collectors.iter() {
            instance.collector.collect(Arc::clone(&self.model)).unwrap();
        }
        Ok(())
    }
}
