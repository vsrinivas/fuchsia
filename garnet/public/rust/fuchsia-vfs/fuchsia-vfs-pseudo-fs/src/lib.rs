// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A helper to create files backed by in process callbacks.  For example to expose component
//! configuration, debug information or statistics.

#![feature(async_await, await_macro, futures_api)]
#![warn(missing_docs)]
#![recursion_limit = "128"]

#[cfg(test)]
#[macro_use]
mod test_utils;

pub mod directory;
pub mod directory_entry;
pub mod file;

mod common;
mod watcher_connection;
