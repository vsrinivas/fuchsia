// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A library to create "pseudo" file systems.  These file systems are backed by in process
//! callbacks.  Examples are: component configuration, debug information or statistics.

#![feature(async_await, await_macro)]
#![recursion_limit = "1024"]

pub mod test_utils;

pub mod directory;
pub mod file;

mod common;
mod execution_scope;
mod path;
