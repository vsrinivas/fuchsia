// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![deny(missing_docs)]

//! Utilities for Bluetooth development.

/// Lists of Bluetooth SIG assigned numbers and conversion functions
pub mod assigned_numbers;
/// Bluetooth Error type
pub mod error;
/// Fake bt-hci device API
pub mod fake_hci;
/// Bluetooth HCI device utilities.
pub mod hci;
/// Bluetooth host API
pub mod host;
/// Common Bluetooth type extensions
pub mod types;
/// Frequent Used Functions
pub mod util;
