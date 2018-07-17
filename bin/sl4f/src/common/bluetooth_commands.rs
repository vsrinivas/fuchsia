// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use app;
use async::TimeoutExt;
use bt::error::Error as BTError;
use common::bluetooth_facade::{listen_central_events, BluetoothFacade};
use common::constants::*;
use failure::{Error, ResultExt};
use fidl_ble::{AdvertisingData, PeripheralMarker, PeripheralProxy};
use fidl_ble::{CentralMarker, CentralProxy, ScanFilter};
use futures::future::ok as fok;
use futures::future::Either::{Left, Right};
use futures::prelude::*;
use futures::FutureExt;
use parking_lot::RwLock;
use serde_json::{to_value, Value};
use std::sync::Arc;
use zx::prelude::*;

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

// Takes a serde_json::Value and converts it to arguments required for a FIDL
// ble_scan command
fn ble_scan_to_fidl(scan_args_raw: Value) -> Result<(Option<ScanFilter>, Option<u64>), Error> {
    let timeout_raw = match scan_args_raw.get("scan_time_ms") {
        Some(t) => Some(t).unwrap().clone(),
        None => return Err(BTError::new("Timeout_ms missing.").into()),
    };

    let scan_filter_raw = match scan_args_raw.get("filter") {
        Some(f) => Some(f).unwrap().clone(),
        None => return Err(BTError::new("Scan filter missing.").into()),
    };

    let timeout: Option<u64> = timeout_raw.as_u64();
    let name_substring: Option<String> =
        scan_filter_raw["name_substring"].as_str().map(String::from);

    // For now, no scan profile, so default to empty ScanFilter
    let filter = Some(ScanFilter {
        service_uuids: None,
        service_data_uuids: None,
        manufacturer_identifier: None,
        connectable: None,
        name_substring: name_substring,
        max_path_loss: None,
    });

    Ok((filter, timeout))
}

// Takes a serde_json::Value and converts it to arguments required for a FIDL
// stop_advertising command. For stop advertise, no arguments are sent, rather
// uses current advertisement id (if it exists)
fn ble_stop_advertise_to_fidl(
    _stop_adv_args_raw: Value, bt_facade: Arc<RwLock<BluetoothFacade>>,
) -> Result<String, Error> {
    let adv_id = bt_facade.read().get_adv_id().clone();

    match adv_id {
        Some(aid) => Ok(aid.to_string()),
        None => Err(BTError::new("No advertisement id outstanding.").into()),
    }
}

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
// Nested Either types are for type coercion of the Future return values
// Nested Left() handles error case.
// This is not scalable for new methods that return a different type
// TODO(aniramakri): Implement using many_futures! macro for scalable solution
pub fn method_to_fidl(
    method_name: String, args: Value, bt_facade: Arc<RwLock<BluetoothFacade>>,
) -> impl Future<Item = Result<Value, Error>, Error = Never> {
    match method_name.as_ref() {
        "BleAdvertise" => {
            let (ad, interval) = match ble_advertise_to_fidl(args) {
                Ok((adv_data, intv)) => (adv_data, intv),
                Err(e) => return Left(Left(fok(Err(e)))),
            };

            let adv_fut = start_adv_async(bt_facade.clone(), ad, interval);
            Right(Right(adv_fut))
        }
        "BleScan" => {
            let (filter, timeout) = match ble_scan_to_fidl(args) {
                Ok((f, t)) => (f, t),
                Err(e) => return Left(Left(fok(Err(e)))),
            };

            let scan_fut = start_scan_async(bt_facade.clone(), filter, timeout);
            Left(Right(scan_fut))
        }
        "BleStopAdvertise" => {
            let advertisement_id = match ble_stop_advertise_to_fidl(args, bt_facade.clone()) {
                Ok(aid) => aid,
                Err(e) => return Left(Left(fok(Err(e)))),
            };

            let stop_fut = stop_adv_async(bt_facade.clone(), advertisement_id.clone());
            Right(Left(stop_fut))
        }
        _ => Left(Left(fok(Err(BTError::new("Invalid FIDL method").into())))),
    }
}

fn start_adv_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, ad: Option<AdvertisingData>, interval: Option<u32>,
) -> impl Future<Item = Result<Value, Error>, Error = Never> {
    // TODO(aniramakri): Change structure of proxy creation such that
    // BluetoothFacade deals with opening proxies. This allows for all ownership
    // (set/get) to be done on the BluetoothFacade level
    let peripheral_svc: PeripheralProxy = app::client::connect_to_service::<PeripheralMarker>()
        .context("Failed to connect to BLE Peripheral service.")
        .unwrap();
    bt_facade.write().set_peripheral_proxy(peripheral_svc);

    let start_adv = bt_facade.write().start_adv(ad, interval);
    let adv_fut = start_adv.then(move |aid| match aid {
        Ok(adv_id) => {
            let id = adv_id.clone();
            bt_facade.write().update_adv_id(id);
            Ok(adv_id.clone())
        }
        Err(_e) => Ok(None),
    });

    adv_fut.and_then(|adv_res| match to_value(adv_res) {
        Ok(val) => fok(Ok(val)),
        Err(e) => fok(Err(e.into())),
    })
}

fn stop_adv_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, advertisement_id: String,
) -> impl Future<Item = Result<Value, Error>, Error = Never> {
    let stop_adv_fut = bt_facade.write().stop_adv(advertisement_id);

    stop_adv_fut.then(move |res| {
        bt_facade.write().cleanup_peripheral();
        match res {
            Ok(r) => match to_value(r) {
                Ok(val) => fok(Ok(val)),
                Err(e) => fok(Err(e.into())),
            },
            Err(e) => fok(Err(e.into())),
        }
    })
}

// Synchronous wrapper for scanning
fn start_scan_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, filter: Option<ScanFilter>, timeout: Option<u64>,
) -> impl Future<Item = Result<Value, Error>, Error = Never> {
    // TODO(aniramakri): Change structure of proxy creation such that
    // BluetoothFacade deals with opening proxies. This allows for all ownership
    // (set/get) to be done on the BluetoothFacade level
    let central_svc: CentralProxy = app::client::connect_to_service::<CentralMarker>()
        .context("Failed to connect to BLE Central Service")
        .unwrap();
    bt_facade.write().set_central_proxy(central_svc);

    // Create scanning future and listen on central events for scan
    let scan_fut = bt_facade.write().start_scan(filter);
    let event_fut = listen_central_events(bt_facade.clone())
        .map_err(|_| unreachable!("Listening to events should never fail"));
    let fut = scan_fut.and_then(move |_| event_fut);

    // Wrap the future in the timeout window. Scan for timeout_ms duration
    let timeout_ms = timeout.unwrap_or(DEFAULT_SCAN_TIMEOUT_MS).millis();
    let fut = fut
        .on_timeout(timeout_ms.after_now(), || Ok(()))
        .expect("failed to set timeout");

    fut.then(move |_| {
        let devices = bt_facade.read().get_devices();

        // Cleanup the central state after reading devices discovered
        bt_facade.write().cleanup_central();
        match to_value(devices) {
            Ok(dev) => fok(Ok(dev)),
            Err(e) => fok(Err(e.into())),
        }
    })
}
