// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::types as client_types,
    fuchsia_zircon as zx,
    log::error,
    wlan_common::{
        energy::DecibelMilliWatt, ewma_signal::EwmaSignalStrength,
        signal_velocity::calculate_dbm_linear_velocity,
    },
};

// Number of previous RSSI measurements to exponentially weigh into average.
// TODO(fxbug.dev/84870): Tune smoothing factor.
pub const EWMA_SMOOTHING_FACTOR: usize = 10;

/// Connection quality data related to signal
#[derive(Clone, Debug, PartialEq)]
pub struct SignalData {
    pub ewma_rssi: EwmaSignalStrength,
    pub rssi_velocity: DecibelMilliWatt,
}

impl SignalData {
    pub fn update_with_new_measurement(&mut self, rssi: DecibelMilliWatt) {
        let prev_rssi = self.ewma_rssi.dbm();
        self.ewma_rssi.update_average(rssi);
        self.rssi_velocity =
            match calculate_dbm_linear_velocity(vec![prev_rssi, self.ewma_rssi.dbm()]) {
                Ok(velocity) => velocity,
                Err(e) => {
                    error!("Failed to update SignalData velocity: {:?}", e);
                    self.rssi_velocity
                }
            };
    }
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
