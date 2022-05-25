// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_power::Identifier;
use fuchsia_bluetooth::types::PeerId;
use parking_lot::Mutex;
use std::{collections::HashMap, convert::TryFrom};

use crate::error::Error;

/// The current state of discovered/connected Bluetooth peripherals.
#[derive(Default)]
pub struct PeripheralState {
    inner: Mutex<PeripheralStateInner>,
}

#[derive(Default, Debug)]
struct PeripheralStateInner {
    peripherals: HashMap<PeerId, PeripheralData>,
}

/// A snapshot of properties associated with a Bluetooth peripheral.
#[derive(Debug)]
struct PeripheralData {
    _id: PeerId,
    /// Information about battery health & status.
    battery: Option<BatteryInfo>,
}

impl PeripheralData {
    fn new(_id: PeerId) -> Self {
        Self { _id, battery: None }
    }
}

impl PeripheralState {
    pub fn record_power_update(&self, id: PeerId, battery: BatteryInfo) {
        let mut inner = self.inner.lock();
        let entry = inner.peripherals.entry(id).or_insert(PeripheralData::new(id));
        entry.battery = Some(battery);
        // TODO(fxbug.dev/86556): After updating the local cache, notify any listeners of the
        // `Watcher` protocol.
    }

    #[cfg(test)]
    pub fn contains_entry(&self, id: &PeerId) -> bool {
        self.inner.lock().peripherals.contains_key(id)
    }
}

/// Battery information about a peripheral.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BatteryInfo {
    level_percent: f32,
    level_status: Option<fidl_fuchsia_power_battery::LevelStatus>,
}

impl TryFrom<fidl_fuchsia_power_battery::BatteryInfo> for BatteryInfo {
    type Error = Error;

    fn try_from(src: fidl_fuchsia_power_battery::BatteryInfo) -> Result<BatteryInfo, Self::Error> {
        // The `level_percent` must be specified per the `fidl_fuchsia_bluetooth_power` docs.
        let level_percent = src.level_percent.ok_or(Error::battery("missing level percent"))?;
        Ok(BatteryInfo { level_percent, level_status: src.level_status })
    }
}

/// Returns the Bluetooth PeerId from the `identifier`, or Error otherwise.
pub fn peer_id_from_identifier(identifier: &Identifier) -> Result<PeerId, Error> {
    match identifier {
        Identifier::PeerId(id) => Ok((*id).into()),
        id => Err(id.into()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl_fuchsia_bluetooth_power::LocalDevice;

    #[test]
    fn invalid_identifier() {
        let invalid = Identifier::unknown(10, vec![]);
        assert_matches!(peer_id_from_identifier(&invalid), Err(Error::Identifier { .. }));

        let unsupported = Identifier::LocalDevice(LocalDevice);
        assert_matches!(peer_id_from_identifier(&unsupported), Err(Error::Identifier { .. }));
    }

    #[test]
    fn valid_identifier() {
        let id = Identifier::PeerId(PeerId(123).into());
        assert_matches!(peer_id_from_identifier(&id), Ok(_));
    }

    #[test]
    fn invalid_battery_info() {
        let empty = fidl_fuchsia_power_battery::BatteryInfo::EMPTY;
        let local = BatteryInfo::try_from(empty);
        assert_matches!(local, Err(Error::BatteryInfo { .. }));

        let missing_percent = fidl_fuchsia_power_battery::BatteryInfo {
            level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
            ..fidl_fuchsia_power_battery::BatteryInfo::EMPTY
        };
        let local = BatteryInfo::try_from(missing_percent);
        assert_matches!(local, Err(Error::BatteryInfo { .. }));
    }

    #[test]
    fn battery_info() {
        // Extra fields are Ok - ignored.
        let valid = fidl_fuchsia_power_battery::BatteryInfo {
            level_percent: Some(1.0f32),
            level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
            charge_source: Some(fidl_fuchsia_power_battery::ChargeSource::Usb),
            ..fidl_fuchsia_power_battery::BatteryInfo::EMPTY
        };
        let local = BatteryInfo::try_from(valid).expect("valid conversion");
        let expected = BatteryInfo {
            level_percent: 1.0f32,
            level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
        };
        assert_eq!(local, expected);
    }
}
