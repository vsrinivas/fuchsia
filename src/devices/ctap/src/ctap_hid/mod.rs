// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod connection;
mod device;
mod message;

use rand::rngs::OsRng;

/// A CTAP device backed by a connection over FIDL to a CTAPHID device.
pub type CtapHidDevice = device::Device<connection::FidlConnection, OsRng>;
