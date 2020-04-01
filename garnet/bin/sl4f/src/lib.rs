// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

#[macro_use]
extern crate anyhow;

pub mod audio;
pub mod basemgr;
pub mod battery_simulator;
pub mod bluetooth;
pub mod camera_factory;
pub mod common_utils;
pub mod device;
pub mod factory_store;
pub mod file;
pub mod hardware_power_statecontrol;
pub mod hwinfo;
pub mod input;
pub mod launch;
pub mod location;
pub mod logging;
pub mod netstack;
pub mod paver;
pub mod scenic;
pub mod server;
pub mod setui;
pub mod test;
pub mod traceutil;
pub mod tracing;
pub mod update;
pub mod weave;
pub mod webdriver;
pub mod wlan;
pub mod wlan_policy;
