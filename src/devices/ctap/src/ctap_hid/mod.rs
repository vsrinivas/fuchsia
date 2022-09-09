// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod command;
mod connection;
mod device;
mod message;
mod packet;
mod util;

use rand::rngs::OsRng;

/// A CTAP device backed by a connection over FIDL to a HID device.
pub type HidCtapDevice = device::Device<connection::FidlConnection, OsRng>;
