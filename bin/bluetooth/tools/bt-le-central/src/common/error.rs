// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bt;
use std::fmt;

// TODO(NET-857): Move these to the fuchsia-bluetooth crate.

#[derive(Debug, Fail)]
#[fail(display = "error")]
pub struct BluetoothError;

impl BluetoothError {
    pub fn new() -> BluetoothError {
        BluetoothError {}
    }
}

#[derive(Debug, Fail)]
pub struct BluetoothFidlError(bt::Error);

impl BluetoothFidlError {
    pub fn new(error: bt::Error) -> BluetoothFidlError {
        BluetoothFidlError(error)
    }
}

// Custom Display implementation for Bluetooth FIDL errors. This outputs the error description if
// there is one.
impl fmt::Display for BluetoothFidlError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self.0.description {
            Some(ref msg) => f.write_str(msg),
            None => write!(f, "unknown bluetooth error"),
        }
    }
}
