// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod binding;
pub mod error;
pub mod hooks;
pub mod hub;
pub mod model;
pub mod moniker;
pub mod realm;
// TODO: This would be #[cfg(test)], but it cannot be because some external crates depend on
// fuctionality in this module. Factor out the externally-depended code into its own module.
pub mod testing;

pub(crate) mod actions;
pub(crate) mod breakpoints;
pub(crate) mod resolver;
pub(crate) mod routing;
pub(crate) mod routing_facade;
pub(crate) mod runner;
pub(crate) mod shutdown;

mod addable_directory;
mod dir_tree;
mod exposed_dir;
mod namespace;
mod rights;
mod storage;
#[cfg(test)]
mod tests;
