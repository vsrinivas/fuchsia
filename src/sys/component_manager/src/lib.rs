// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
// This is needed for the pseudo_directory nesting in crate::model::tests
#![recursion_limit = "128"]

pub mod elf_runner;
pub mod fuchsia_boot_resolver;
pub mod fuchsia_pkg_resolver;
pub mod klog;
pub mod model;
pub mod startup;

mod directory_broker;
mod ns_util;
