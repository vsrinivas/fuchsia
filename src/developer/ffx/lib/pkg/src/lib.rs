// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]

pub mod config;
pub mod manager;
pub mod range;
pub mod repository;
pub mod resolve;
pub mod resource;
pub mod server;
pub mod test_utils;
