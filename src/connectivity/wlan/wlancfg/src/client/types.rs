// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_policy as fidl_policy;

pub type NetworkIdentifier = fidl_policy::NetworkIdentifier;
#[cfg(test)] // TODO: remove
pub type SecurityType = fidl_policy::SecurityType;
pub type ConnectionState = fidl_policy::ConnectionState;
pub type DisconnectStatus = fidl_policy::DisconnectStatus;
