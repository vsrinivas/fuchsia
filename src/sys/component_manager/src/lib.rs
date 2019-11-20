// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is needed for the pseudo_directory nesting in crate::model::tests
#![recursion_limit = "256"]

pub mod capability;
pub mod elf_runner;
pub mod framework;
pub mod fuchsia_base_pkg_resolver;
pub mod fuchsia_boot_resolver;
pub mod fuchsia_pkg_resolver;
pub mod klog;
pub mod model;
pub mod root_realm_stop_notifier;
pub mod startup;
pub mod system_controller;
pub mod work_scheduler;

mod constants;
mod process_launcher;
mod vmex;

#[macro_use]
extern crate maplit;
