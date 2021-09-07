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

use crate::message::MessageHubUtil;
use crate::monitor::base::monitor as base_monitor;
#[cfg(test)]
use crate::monitor::base::Error;
use crate::service;

/// `Actor` handles bringing up and controlling environment-specific components
/// surrounding monitoring, such as the resource monitors.
#[derive(Clone)]
pub struct Actor {
    delegate: service::message::Delegate,
    monitors: Vec<base_monitor::Generate>,
}

impl Actor {
    /// Starts up environment monitors and returns a TargetedMessenger that
    /// broadcasts to all monitors.
    #[cfg(test)]
    pub(crate) async fn start_monitoring(
        &self,
    ) -> Result<service::message::TargetedMessenger, Error> {
        use crate::message::base::MessengerType;
        // Create unbound, broadcasting messenger to send messages to the monitors.
        let monitor_messenger = service::message::TargetedMessenger::new(
            self.delegate
                .create(MessengerType::Unbound)
                .await
                .map_err(|_| {
                    Error::MessageSetupFailure("could not create monitor messenger".into())
                })?
                .0,
            service::message::Audience::Broadcast,
        );

        // Bring up each monitor.
        for monitor in &self.monitors {
            let (_, monitor_receptor) =
                self.delegate.create(MessengerType::Unbound).await.map_err(|_| {
                    Error::MessageSetupFailure("could not create monitor receptor".into())
                })?;

            monitor(base_monitor::Context { receptor: monitor_receptor })
                .await
                .map_err(|_| Error::MonitorSetupFailure("could not create monitor".into()))?
        }

        Ok(monitor_messenger)
    }
}

/// `Builder` helps construct a monitoring environment in a step-wise fashion,
/// returning an [`Actor`] to control the resulting environment.
#[derive(Default)]
pub struct Builder {
    monitors: Vec<base_monitor::Generate>,
}

impl Builder {
    /// Returns a builder with no values set.
    pub(crate) fn new() -> Self {
        Self { monitors: vec![] }
    }

    /// Appends [`base_monitor::Generate`] to the set of monitors to participate in
    /// this environment.
    pub(crate) fn add_monitors(mut self, mut monitors: Vec<base_monitor::Generate>) -> Self {
        self.monitors.append(&mut monitors);
        self
    }

    /// Constructs the configuration.
    pub(crate) fn build(self) -> Actor {
        let monitor_delegate = service::MessageHub::create_hub();
        Actor { delegate: monitor_delegate, monitors: self.monitors }
    }
}
