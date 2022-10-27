// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::result_large_err)]
#![allow(clippy::let_unit_value)]

pub mod manager;
pub mod range;
pub mod repo_builder;
pub mod repo_client;
pub mod repo_keys;
pub mod repository;
pub mod resolve;
pub mod resource;
pub mod server;
pub mod test_utils;

mod async_spooled;
mod util;
