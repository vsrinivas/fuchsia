// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use async_trait::async_trait;
use failure::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use futures::future::join;
use futures::prelude::*;
use std::rc::Rc;

/// Node: ThermalPolicy
///
/// Summary: implements the closed loop thermal control policy for the system
///
/// Message Inputs: N/A
///
/// Message Outputs:
///     - ReadTemperature
///     - GetCpuIdlePct
///
/// FIDL dependencies: N/A

#[allow(dead_code)]
pub struct ThermalPolicy {
    config: ThermalConfig,
    temperature_node: Rc<dyn Node>,
    cpu_stats_node: Rc<dyn Node>,
    cpu_control_node: Rc<dyn Node>,
}

pub struct ThermalConfig {
    pub temperature_driver: String,
    pub poll_interval_ms: i32,
}

impl ThermalPolicy {
    pub fn new(
        config: ThermalConfig,
        temperature_node: Rc<dyn Node>,
        cpu_stats_node: Rc<dyn Node>,
        cpu_control_node: Rc<dyn Node>,
    ) -> Result<Rc<Self>, Error> {
        let node = Rc::new(Self { config, temperature_node, cpu_stats_node, cpu_control_node });
        node.clone().setup_poll_timer()?;
        Ok(node)
    }

    fn setup_poll_timer(self: Rc<Self>) -> Result<(), Error> {
        let mut periodic_timer =
            fasync::Interval::new(zx::Duration::from_millis(self.config.poll_interval_ms.into()));

        let s = self.clone();

        fasync::spawn_local(async move {
            while let Some(()) = (periodic_timer.next()).await {
                if let Err(e) = s.tick().await {
                    fx_log_err!("{}", e);
                }
            }
        });
        Ok(())
    }

    async fn tick(&self) -> Result<(), Error> {
        let get_temperature_message = Message::ReadTemperature(&self.config.temperature_driver);
        let get_temperature_future =
            self.send_message(&self.temperature_node, &get_temperature_message);

        let get_cpu_load_message = Message::GetTotalCpuLoad;
        let get_cpu_load_future = self.send_message(&self.cpu_stats_node, &get_cpu_load_message);

        let (temperature, load) = join(get_temperature_future, get_cpu_load_future).await;

        let temperature = match temperature {
            Ok(MessageReturn::ReadTemperature(t)) => t,
            Ok(r) => Err(format_err!("ReadTemperature had unexpected return value: {:?}", r))?,
            Err(e) => Err(format_err!("ReadTemperature failed: {:?}", e))?,
        };

        let load = match load {
            Ok(MessageReturn::GetTotalCpuLoad(l)) => l,
            Ok(r) => Err(format_err!("GetTotalCpuLoad had unexpected return value: {:?}", r))?,
            Err(e) => Err(format_err!("GetTotalCpuLoad failed: {:?}", e))?,
        };

        fx_log_info!("temperature={}; load={}", temperature.0, load);

        Ok(())
    }
}

#[async_trait(?Send)]
impl Node for ThermalPolicy {
    fn name(&self) -> &'static str {
        "ThermalPolicy"
    }

    async fn handle_message(&self, msg: &Message<'_>) -> Result<MessageReturn, Error> {
        match msg {
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}
