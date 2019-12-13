// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

#[macro_use]
extern crate failure;

pub mod audio;
pub mod basemgr;
pub mod bluetooth;
pub mod camera_factory;
pub mod common_utils;
pub mod factory_store;
pub mod file;
pub mod logging;
pub mod netstack;
pub mod paver;
pub mod scenic;
pub mod server;
pub mod setui;
pub mod test;
pub mod traceutil;
pub mod webdriver;
pub mod wlan;
