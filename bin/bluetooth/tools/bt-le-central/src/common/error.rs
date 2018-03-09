// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bt;

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
#[fail(display = "unknown bluetooth error")]
pub struct BluetoothFidlError(bt::Error);

impl BluetoothFidlError {
    pub fn new(error: bt::Error) -> BluetoothFidlError {
        BluetoothFidlError(error)
    }
}
