// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is for Omaha client binaries written in Rust.

#![recursion_limit = "256"]
#![warn(clippy::all)]
#![allow(clippy::let_unit_value)]

pub mod app_set;
pub mod clock;
pub mod common;
pub mod configuration;
pub mod cup_ecdsa;
pub mod http_request;
pub mod installer;
pub mod metrics;
pub mod policy;
pub mod protocol;
pub mod request_builder;
pub mod state_machine;
pub mod storage;
pub mod time;
pub mod unless;
