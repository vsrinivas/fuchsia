// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![deny(missing_docs)]
#![allow(stable_features)]

//! The fuchsia_bluetooth crate is meant to be used for bluetooth specific functionality.

#[macro_use]
extern crate fdio;
#[macro_use]
extern crate failure;
extern crate rand;

extern crate fuchsia_zircon as zircon;

/// Bluetooth hci API
pub mod hci;
/// Bluetooth host API
pub mod host;
