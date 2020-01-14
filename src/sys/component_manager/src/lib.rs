// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]
// This is needed for the pseudo_directory nesting in crate::model::tests
#![recursion_limit = "256"]

pub mod builtin_environment;
pub mod capability;
pub mod klog;
pub mod model;
pub mod startup;

pub(crate) mod elf_runner;
pub(crate) mod framework;
pub(crate) mod fuchsia_base_pkg_resolver;
pub(crate) mod fuchsia_boot_resolver;
pub(crate) mod fuchsia_pkg_resolver;
pub(crate) mod root_realm_stop_notifier;
pub(crate) mod system_controller;
pub(crate) mod work_scheduler;

mod builtin_capability;
mod constants;
mod process_launcher;
mod root_job;
mod root_resource;
mod runner;
mod vmex;
