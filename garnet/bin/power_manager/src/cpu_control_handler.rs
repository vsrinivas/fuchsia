// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::Watts;
use async_trait::async_trait;
use failure::{format_err, Error};
use std::rc::Rc;

/// Node: CpuControlHandler
///
/// Summary: WIP
///
/// Handles Messages: WIP
///
/// Sends Messages: WIP
///
/// FIDL dependencies: WIP

pub struct CpuControlHandler;

impl CpuControlHandler {
    pub fn new() -> Rc<Self> {
        Rc::new(Self)
    }

    fn handle_set_max_power_consumption(&self, _max_power: &Watts) -> Result<MessageReturn, Error> {
        // TODO(fxb/41453): Using the CPU load (from CpuStatsHandler node) and the list of
        // available P-States (from the CPU driver), determine the highest P-State we can operate
        // in without exceeding `max_power`. We may decide to filter the CPU load using a low-pass
        // filter, in which case the time constant should be supplied as a constructor parameter. We
        // should expect the ThermalPolicy node to call this function on each of its iterations so
        // that our load measurements stay current.
        Ok(MessageReturn::SetMaxPowerConsumption)
    }
}

#[async_trait(?Send)]
impl Node for CpuControlHandler {
    fn name(&self) -> &'static str {
        "CpuControlHandler"
    }

    async fn handle_message(&self, msg: &Message<'_>) -> Result<MessageReturn, Error> {
        match msg {
            Message::SetMaxPowerConsumption(p) => self.handle_set_max_power_consumption(p),
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    pub fn setup_test_node() -> Rc<CpuControlHandler> {
        CpuControlHandler::new()
    }
}
