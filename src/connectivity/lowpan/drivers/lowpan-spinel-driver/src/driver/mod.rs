// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod api;
mod inbound;
mod tasks;

#[cfg(test)]
mod tests;

use crate::spinel::*;
use fuchsia_syslog::macros::*;

/// High-level LoWPAN driver implementation for Spinel-based devices.
/// It covers the basic high-level state machine as well as
/// the task definitions for all API commands.
#[derive(Debug)]
pub struct SpinelDriver<DS> {
    /// Handles sending commands and routing responses.
    frame_handler: FrameHandler<DS>,

    /// Frame sink for sending raw Spinel commands, as well
    /// as managing `open`/`close`/`reset` for the Spinel device.
    device_sink: DS,

    did_vend_main_task: std::sync::atomic::AtomicBool,
}

impl<DS: SpinelDeviceClient> From<DS> for SpinelDriver<DS> {
    fn from(device_sink: DS) -> Self {
        SpinelDriver {
            frame_handler: FrameHandler::new(device_sink.clone()),
            device_sink,
            did_vend_main_task: Default::default(),
        }
    }
}

impl From<fidl_fuchsia_lowpan_spinel::DeviceProxy>
    for SpinelDriver<SpinelDeviceSink<fidl_fuchsia_lowpan_spinel::DeviceProxy>>
{
    fn from(device_proxy: fidl_fuchsia_lowpan_spinel::DeviceProxy) -> Self {
        SpinelDriver::from(SpinelDeviceSink::new(device_proxy))
    }
}
