// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FxFS is a log-structured filesystem for [Fuchsia](https://fuchsia.dev/).
//!
//! For a high-level overview, please refer to the
//! [RFC](/docs/contribute/governance/rfcs/0136_fxfs.md).
//!
//! Where possible, Fxfs code tries to be target agnostic.
//! Fuchsia specific bindings are primarily found under [server].

pub mod crypt;
pub mod errors;
pub mod lsm_tree;
pub mod mkfs;
pub mod mount;
pub mod object_handle;
pub mod object_store;
pub mod range;
#[cfg(target_os = "fuchsia")]
pub mod remote_crypt;
pub mod round;
pub mod serialized_types;
#[cfg(target_os = "fuchsia")]
pub mod server;
#[cfg(test)]
pub mod testing;
#[macro_use]
mod trace;
#[macro_use]
mod debug_assert_not_too_long;
#[cfg(target_os = "fuchsia")]
mod pager;
