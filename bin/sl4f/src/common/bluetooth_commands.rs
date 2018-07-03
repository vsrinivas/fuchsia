// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use app;
use async;
use bt::error::Error as BTError;
use common::bluetooth_facade::BluetoothFacade;
use failure::{Error, ResultExt};
use fidl_ble::{AdvertisingData, PeripheralMarker, PeripheralProxy};
use futures::FutureExt;
use parking_lot::{RwLock, RwLockWriteGuard};
use serde_json::Value;
use std::sync::Arc;

// Takes a serde_json::Value and converts it to arguments required for
// a FIDL ble_advertise command
fn ble_advertise_to_fidl(
    adv_args_raw: Value,
) -> Result<(Option<AdvertisingData>, Option<u32>), Error> {
    let adv_data_raw = match adv_args_raw.get("advertising_data") {
        Some(adr) => Some(adr).unwrap().clone(),
        None => return Err(BTError::new("Advertising data missing.").into()),
    };

    let interval_raw = match adv_args_raw.get("interval_ms") {
        Some(ir) => Some(ir).unwrap().clone(),
        None => return Err(BTError::new("Interval_ms missing.").into()),
    };

    // Unpack the name for advertising data, as well as interval of advertising
    let name: Option<String> = adv_data_raw["name"].as_str().map(String::from);
    let interval: Option<u32> = interval_raw.as_u64().map(|i| i as u32);

    // TODO(aniramakri): Is there a better way to unpack the args into an AdvData
    // struct? Unfortunately, can't derive deserialize for AdvData
    let ad = Some(AdvertisingData {
        name: name,
        tx_power_level: None,
        appearance: None,
        service_uuids: None,
        service_data: None,
        manufacturer_specific_data: None,
        solicited_service_uuids: None,
        uris: None,
    });

    Ok((ad, interval))
}

// TODO(aniramakri): Implement translation layer that converts method to
// respective FIDL method
pub fn convert_to_fidl(
    method_name: String,
    args: Value,
    bt_facade: Arc<RwLock<BluetoothFacade>>,
) -> Result<(), Error> {
    // Translate test suite method to FIDL method
    match method_name.as_ref() {
        "BleAdvertise" => {
            let (ad, interval) = match ble_advertise_to_fidl(args) {
                Ok((adv_data, intv)) => (adv_data, intv),
                Err(e) => return Err(e),
            };

            start_adv_sync(ad, interval, bt_facade.write())
        }
        _ => Err(BTError::new("Invalid fidl method.").into()),
    }
}

// Synchronous wrapper for advertising
pub fn start_adv_sync(
    ad: Option<AdvertisingData>,
    interval: Option<u32>,
    mut bt_facade: RwLockWriteGuard<BluetoothFacade>,
) -> Result<(), Error> {
    let mut executor = async::Executor::new().context("Error creating event loop")?;

    // Set up periph proxy and initialize this in our BTF object
    let peripheral_svc: PeripheralProxy = app::client::connect_to_service::<PeripheralMarker>()
        .context("Failed to connect to BLE Peripheral service.")?;
    bt_facade.set_peripheral_proxy(peripheral_svc)?;

    // Initialize the start advertising futures
    // TODO(aniramakri): Setup peripheral state cleanup for all peripheral commands
    let adv_fut = bt_facade.start_adv(ad, interval).then(|res| {
        bt_facade.cleanup_peripheral();
        res
    });

    // Run future to completion
    executor.run_singlethreaded(adv_fut).map_err(Into::into)
}
