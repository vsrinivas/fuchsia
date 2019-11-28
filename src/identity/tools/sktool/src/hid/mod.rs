// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod connection;
mod device;

use self::connection::{fidl::FidlConnection, Connection};
use self::device::Device;

/// A CTAP device backed by a connection over FIDL to a HID device.
pub type HidCtapDevice = Device<FidlConnection>;
