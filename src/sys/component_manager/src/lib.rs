// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]
// This is needed for the pseudo_directory nesting in crate::model::tests
#![recursion_limit = "256"]

pub mod bootfs;
pub mod builtin_environment;
pub mod capability;
pub mod elf_runner;
pub mod model;
pub mod startup;

pub(crate) mod binder;
pub(crate) mod collection;
pub(crate) mod directory_ready_notifier;
pub(crate) mod framework;
pub(crate) mod fuchsia_pkg_resolver;
pub(crate) mod root_stop_notifier;

mod builtin;
mod constants;
mod diagnostics;
