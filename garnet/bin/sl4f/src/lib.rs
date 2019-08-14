// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![recursion_limit = "1024"]

#[macro_use]
extern crate failure;

pub mod audio;
pub mod auth;
pub mod basemgr;
pub mod bluetooth;
pub mod file;
pub mod logging;
pub mod netstack;
pub mod scenic;
pub mod server;
pub mod setui;
pub mod test;
pub mod traceutil;
pub mod webdriver;
pub mod wlan;
