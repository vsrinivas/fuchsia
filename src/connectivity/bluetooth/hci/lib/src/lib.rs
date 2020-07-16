// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]
#![deny(missing_docs)]

//! HCI Transport Lib contains all the functionality needed to write implementations of
//! bt-transport. It is safe, modular, and well-tested.
//!
//! See the README for high level documentation.

#[macro_use]
mod log;
mod control_plane;
mod error;
mod ffi;
mod snoop;
#[cfg(test)]
mod test_utils;
mod transport;
mod worker;
