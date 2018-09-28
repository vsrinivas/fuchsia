// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error};
use fidl_fuchsia_bluetooth_gatt::ServiceInfo;
use fidl_fuchsia_bluetooth_le::{AdvertisingData, ScanFilter};
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_syslog::macros::*;
use parking_lot::RwLock;
use serde_json::{to_value, Value};
use std::sync::Arc;

// Bluetooth-related functionality
use crate::bluetooth::ble_advertise_facade::BleAdvertiseFacade;
use crate::bluetooth::facade::BluetoothFacade;
use crate::bluetooth::types::{BleConnectPeripheralResponse, BluetoothMethod};

macro_rules! parse_arg {
    ($args:ident, $func:ident, $name:expr) => {
        match $args.get($name) {
            Some(v) => match v.$func() {
                Some(val) => Ok(val),
                None => Err(BTError::new(format!("malformed {}", $name).as_str())),
            },
            None => Err(BTError::new(format!("{} missing", $name).as_str())),
        }
    };
}

// Takes a serde_json::Value and converts it to arguments required for
// a FIDL ble_advertise command
fn ble_advertise_args_to_fidl(
    args_raw: Value,
) -> Result<(Option<AdvertisingData>, Option<u32>), Error> {
    let adv_data_raw = match args_raw.get("advertising_data") {
        Some(adr) => Some(adr).unwrap().clone(),
        None => bail!("Advertising data missing."),
    };

    let interval_raw = match args_raw.get("interval_ms") {
        Some(ir) => Some(ir).unwrap().clone(),
        None => bail!("Interval_ms missing."),
    };

    // Unpack the name for advertising data, as well as interval of advertising
    let name: Option<String> = adv_data_raw["name"].as_str().map(String::from);
    let interval: Option<u32> = interval_raw.as_u64().map(|i| i as u32);

    // TODO(NET-1026): Is there a better way to unpack the args into an AdvData
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

    fx_log_info!(tag: "ble_advertise_args_to_fidl", "AdvData: {:?}", ad);

    Ok((ad, interval))
}

// Takes a serde_json::Value and converts it to arguments required for a FIDL
// ble_scan command
fn ble_scan_to_fidl(args_raw: Value) -> Result<Option<ScanFilter>, Error> {
    let scan_filter_raw = match args_raw.get("filter") {
        Some(f) => Some(f).unwrap().clone(),
        None => bail!("Scan filter missing."),
    };

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

    Ok(filter)
}

// Takes a serde_json::Value and converts it to arguments required for a FIDL
// stop_advertising command. For stop advertise, no arguments are sent, rather
// uses current advertisement id (if it exists)
fn ble_stop_advertise_args_to_fidl(
    _args_raw: Value, facade: &BleAdvertiseFacade,
) -> Result<String, Error> {
    let adv_id = facade.get_adv_id().clone();

    match adv_id.name {
        Some(aid) => Ok(aid.to_string()),
        None => bail!("No advertisement id outstanding."),
    }
}

fn parse_identifier(args_raw: Value) -> Result<String, Error> {
    let id_raw = match args_raw.get("identifier") {
        Some(id) => id,
        None => bail!("Connect peripheral identifier missing"),
    };

    let id = id_raw.as_str().map(String::from);

    match id {
        Some(id) => Ok(id),
        None => bail!("Identifier missing"),
    }
}

fn ble_publish_service_to_fidl(args_raw: Value) -> Result<(ServiceInfo, String), Error> {
    let id = parse_arg!(args_raw, as_u64, "id")?;
    let primary = parse_arg!(args_raw, as_bool, "primary")?;
    let type_ = parse_arg!(args_raw, as_str, "type")?;
    let local_service_id = parse_arg!(args_raw, as_str, "local_service_id")?;

    // TODO(NET-1293): Add support for GATT characterstics and includes
    let characteristics = None;
    let includes = None;

    let service_info = ServiceInfo {
        id,
        primary,
        type_: type_.to_string(),
        characteristics,
        includes,
    };
    Ok((service_info, local_service_id.to_string()))
}

// Takes ACTS method command and executes corresponding BLE
// Advertise FIDL methods.
// Packages result into serde::Value
// To add new methods, add to the unsafe_many_futures! macro
pub async fn ble_advertise_method_to_fidl(
    method_name: String, args: Value, facade: Arc<BleAdvertiseFacade>,
) -> Result<Value, Error> {
    match BluetoothMethod::from_str(&method_name) {
        BluetoothMethod::BleAdvertise => {
            let (ad, interval) = ble_advertise_args_to_fidl(args)?;
            await!(facade.start_adv(ad, interval))?;
            Ok(to_value(facade.get_adv_id())?)
        }
        BluetoothMethod::BleStopAdvertise => {
            let advertisement_id = ble_stop_advertise_args_to_fidl(args, &facade)?;
            await!(facade.stop_adv(advertisement_id))?;
            Ok(to_value(facade.get_adv_id())?)
        }
        _ => bail!("Invalid BleAdvertise FIDL method"),
    }
}

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
// To add new methods, add to the unsafe_many_futures! macro
pub async fn ble_method_to_fidl(
    method_name: String, args: Value, facade: Arc<RwLock<BluetoothFacade>>,
) -> Result<Value, Error> {
    match BluetoothMethod::from_str(&method_name) {
        BluetoothMethod::BleStartScan => {
            let filter = ble_scan_to_fidl(args)?;
            await!(start_scan_async(facade.clone(), filter))
        }
        BluetoothMethod::BleStopScan => await!(stop_scan_async(&facade)),
        BluetoothMethod::BleGetDiscoveredDevices => {
            await!(le_get_discovered_devices_async(&facade))
        }
        BluetoothMethod::BleConnectPeripheral => {
            let id = parse_identifier(args)?;
            await!(connect_peripheral_async(facade.clone(), id.clone()))
        }
        BluetoothMethod::BleDisconnectPeripheral => {
            let id = parse_identifier(args)?;
            await!(disconnect_peripheral_async(&facade, id.clone()))
        }
        BluetoothMethod::BleListServices => {
            let id = parse_identifier(args)?;
            await!(list_services_async(&facade, id.clone()))
        }
        BluetoothMethod::BlePublishService => {
            let (service_info, local_service_id) = ble_publish_service_to_fidl(args)?;
            await!(publish_service_async(
                &facade,
                service_info,
                local_service_id
            ))
        }
        _ => bail!("Invalid BLE FIDL method: {:?}", method_name),
    }
}

async fn start_scan_async(
    facade: Arc<RwLock<BluetoothFacade>>, filter: Option<ScanFilter>,
) -> Result<Value, Error> {
    let start_scan_result = await!(BluetoothFacade::start_scan(facade.clone(), filter))?;
    Ok(to_value(start_scan_result)?)
}

async fn stop_scan_async(facade: &RwLock<BluetoothFacade>) -> Result<Value, Error> {
    let central = facade
        .read()
        .get_central_proxy()
        .clone()
        .expect("No central proxy.");
    if let Err(e) = central.stop_scan() {
        bail!("Error stopping scan: {}", e)
    } else {
        // Get the list of devices discovered by the scan.
        let devices = facade.read().get_devices();
        match to_value(devices) {
            Ok(dev) => Ok(dev),
            Err(e) => Err(e.into()),
        }
    }
}

async fn le_get_discovered_devices_async(facade: &RwLock<BluetoothFacade>) -> Result<Value, Error> {
    // Get the list of devices discovered by the scan.
    match to_value(facade.read().get_devices()) {
        Ok(dev) => Ok(dev),
        Err(e) => Err(e.into()),
    }
}

async fn connect_peripheral_async(
    facade: Arc<RwLock<BluetoothFacade>>, id: String,
) -> Result<Value, Error> {
    let connect_periph_result = await!(BluetoothFacade::connect_peripheral(
        facade.clone(),
        id.clone()
    ))?;
    Ok(to_value(connect_periph_result)?)
}

async fn disconnect_peripheral_async(
    facade: &RwLock<BluetoothFacade>, id: String,
) -> Result<Value, Error> {
    let value = await!(BluetoothFacade::disconnect_peripheral(&facade, id.clone()))?;
    Ok(to_value(value)?)
}

// Uses the same return type as connect_peripheral_async -- Returns subset of
// fidl::ServiceInfo
async fn list_services_async(facade: &RwLock<BluetoothFacade>, id: String) -> Result<Value, Error> {
    let list_services_result = await!(BluetoothFacade::list_services(&facade, id.clone()))?;
    Ok(to_value(BleConnectPeripheralResponse::new(
        list_services_result,
    ))?)
}

async fn publish_service_async(
    facade: &RwLock<BluetoothFacade>, service_info: ServiceInfo, local_service_id: String,
) -> Result<Value, Error> {
    let publish_service_result = await!(BluetoothFacade::publish_service(
        &facade,
        service_info,
        local_service_id
    ))?;
    Ok(to_value(publish_service_result)?)
}
