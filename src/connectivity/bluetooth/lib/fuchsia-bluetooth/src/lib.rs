// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Bluetooth development.

pub mod constants;

/// Lists of Bluetooth SIG assigned numbers and conversion functions
pub mod assigned_numbers;
/// The DetachableMap type
pub mod detachable_map;
/// Bluetooth Error type
pub mod error;
/// Tools for writing asynchronous expectations in tests
pub mod expectation;
/// Utility for interacting with the bt-hci-emulator driver
pub mod hci_emulator;
/// Bluetooth host API
pub mod host;
/// Extension traits and functions for interfacing with the Inspect API
pub mod inspect;
/// Common Bluetooth type extensions
pub mod types;
/// Frequently Used Functions
pub mod util;

/// Convenience wrappers around VFS watcher.
pub mod device_watcher;
