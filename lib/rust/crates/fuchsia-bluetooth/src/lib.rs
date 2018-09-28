// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![deny(missing_docs)]
#![allow(stable_features)]

//! The fuchsia_bluetooth crate is meant to be used for bluetooth specific functionality.

/// Lists of Bluetooth SIG assigned numbers and conversion functions
pub mod assigned_numbers;
/// Bluetooth Error type
pub mod error;
/// Bluetooth hci API
pub mod hci;
/// Bluetooth host API
pub mod host;
/// Common Bluetooth type extensions
pub mod types;
/// Frequent Used Functions
pub mod util;
