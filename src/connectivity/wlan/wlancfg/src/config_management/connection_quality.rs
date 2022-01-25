// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::client::types as client_types, fuchsia_zircon as zx};

/// Connection quality data related to signal
#[derive(Clone, Debug, PartialEq)]
pub struct SignalData {
    pub rssi: f32,
    pub rssi_velocity: f32,
}

/// Data points related to a particular connection
#[derive(Clone, Debug)]
pub struct ConnectionData {
    /// Time at which connect was first attempted
    pub time: zx::Time,
    /// Seconds to connect
    pub time_to_connect: zx::Duration,
    /// Time from connection to disconnect
    pub connection_duration: zx::Duration,
    /// Cause of disconnect or failure to connect
    pub disconnect_reason: client_types::DisconnectReason,
    /// Final signal strength measure before disconnect
    pub signal_data_at_disconnect: SignalData,
    /// Average phy rate over connection duration
    pub average_tx_rate: u16,
}
