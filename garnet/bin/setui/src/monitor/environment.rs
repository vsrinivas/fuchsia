// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defining and handling of the monitor environment.
//!
//! # Summary
//!
//! The monitoring logic in the setting service is meant to be modular. Based on
//! product configuration, a variety of sources (resource monitors) and
//! sinks (data outlets) may participate in the monitoring environment. The
//! components in this mod are meant to aid capturing and passing this setup
//! to the appropriate handling component, and bringing up the environment as
//! directed. The separation of this responsibility from the main
//! resource-watching component promotes code reshare and modularity.

use crate::monitor::base::{Context, GenerateMonitor};

/// `Actor` handles bringing up and controlling environment-specific components
/// surrounding monitoring, such as the resource monitors.
#[derive(Clone)]
pub struct Actor {
    monitors: Vec<GenerateMonitor>,
}

impl Actor {
    /// Starts up environment monitors.
    pub async fn start_monitoring(&self, context: Context) -> Result<(), anyhow::Error> {
        for monitor in &self.monitors {
            monitor(context.clone()).await?
        }

        Ok(())
    }
}

/// `Builder` helps construct a monitoring environment in a step-wise fashion,
/// reutrning an [`Actor`] to control the resulting environment.
pub struct Builder {
    monitors: Vec<GenerateMonitor>,
}

impl Builder {
    /// Returns a builder with no values set.
    pub fn new() -> Self {
        Self { monitors: vec![] }
    }

    /// Appends GenerateMonitors to the set of monitors to participate in
    /// this environment.
    pub fn add_monitors(mut self, mut monitors: Vec<GenerateMonitor>) -> Self {
        self.monitors.append(&mut monitors);
        self
    }

    /// Constructs the configuration.
    pub fn build(self) -> Actor {
        Actor { monitors: self.monitors }
    }
}
