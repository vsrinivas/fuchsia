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
use crate::bluetooth::bt_control_facade::BluetoothControlFacade;
use crate::bluetooth::facade::BluetoothFacade;
use crate::bluetooth::gatt_client_facade::GattClientFacade;
use crate::bluetooth::gatt_server_facade::GattServerFacade;
use crate::bluetooth::types::{
    BleConnectPeripheralResponse, BluetoothMethod, GattcDiscoverCharacteristicResponse,
};

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
) -> Result<(Option<AdvertisingData>, Option<u32>, bool), Error> {
    let adv_data_raw = match args_raw.get("advertising_data") {
        Some(adr) => adr.clone(),
        None => bail!("Advertising data missing."),
    };

    let interval_raw = match args_raw.get("interval_ms") {
        Some(ir) => ir.clone(),
        None => bail!("Interval_ms missing."),
    };

    let conn_raw = args_raw.get("connectable").ok_or(format_err!("Connectable missing"))?;

    // Unpack the name for advertising data, as well as interval of advertising
    let name: Option<String> = adv_data_raw["name"].as_str().map(String::from);
    let interval: Option<u32> = interval_raw.as_u64().map(|i| i as u32);
    let connectable: bool = conn_raw.as_bool().unwrap_or(false);

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

    Ok((ad, interval, connectable))
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
    _args_raw: Value,
    facade: &BleAdvertiseFacade,
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

fn parse_service_identifier(args_raw: Value) -> Result<u64, Error> {
    parse_arg!(args_raw, as_u64, "service_identifier").map_err(Into::into)
}

fn parse_u64_identifier(args_raw: Value) -> Result<u64, Error> {
    parse_arg!(args_raw, as_u64, "identifier").map_err(Into::into)
}

fn parse_offset(args_raw: Value) -> Result<u64, Error> {
    parse_arg!(args_raw, as_u64, "offset").map_err(Into::into)
}

fn parse_max_bytes(args_raw: Value) -> Result<u64, Error> {
    parse_arg!(args_raw, as_u64, "max_bytes").map_err(Into::into)
}

fn parse_write_value(args_raw: Value) -> Result<Vec<u8>, Error> {
    let arr = parse_arg!(args_raw, as_array, "write_value")?;
    let mut vector: Vec<u8> = Vec::new();
    for value in arr.into_iter() {
        match value.as_u64() {
            Some(num) => vector.push(num as u8),
            None => {}
        };
    }
    Ok(vector)
}

fn ble_publish_service_to_fidl(args_raw: Value) -> Result<(ServiceInfo, String), Error> {
    let id = parse_arg!(args_raw, as_u64, "id")?;
    let primary = parse_arg!(args_raw, as_bool, "primary")?;
    let type_ = parse_arg!(args_raw, as_str, "type")?;
    let local_service_id = parse_arg!(args_raw, as_str, "local_service_id")?;

    // TODO(NET-1293): Add support for GATT characterstics and includes
    let characteristics = None;
    let includes = None;

    let service_info =
        ServiceInfo { id, primary, type_: type_.to_string(), characteristics, includes };
    Ok((service_info, local_service_id.to_string()))
}

// Takes ACTS method command and executes corresponding BLE
// Advertise FIDL methods.
// Packages result into serde::Value
pub async fn ble_advertise_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<BleAdvertiseFacade>,
) -> Result<Value, Error> {
    match BluetoothMethod::from_str(&method_name) {
        BluetoothMethod::BleAdvertise => {
            let (ad, interval, connectable) = ble_advertise_args_to_fidl(args)?;
            await!(facade.start_adv(ad, interval, connectable))?;
            Ok(to_value(facade.get_adv_id())?)
        }
        BluetoothMethod::BleStopAdvertise => {
            let advertisement_id = ble_stop_advertise_args_to_fidl(args, &facade)?;
            await!(facade.stop_adv(advertisement_id))?;
            Ok(to_value(facade.get_adv_id())?)
        }
        _ => bail!("Invalid BleAdvertise FIDL method: {:?}", method_name),
    }
}

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
pub async fn ble_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<RwLock<BluetoothFacade>>,
) -> Result<Value, Error> {
    match BluetoothMethod::from_str(&method_name) {
        BluetoothMethod::BlePublishService => {
            let (service_info, local_service_id) = ble_publish_service_to_fidl(args)?;
            await!(publish_service_async(&facade, service_info, local_service_id))
        }
        _ => bail!("Invalid BLE FIDL method: {:?}", method_name),
    }
}

pub async fn bt_control_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<BluetoothControlFacade>,
) -> Result<Value, Error> {
    match BluetoothMethod::from_str(&method_name) {
        BluetoothMethod::BluetoothInitControl => {
            let result = await!(facade.init_control_interface_proxy())?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothGetKnownRemoteDevices => {
            let result = await!(facade.get_known_remote_devices())?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothSetDiscoverable => {
            let discoverable = parse_arg!(args, as_bool, "discoverable")?;
            let result = await!(facade.set_discoverable(discoverable))?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothSetName => {
            let name = parse_arg!(args, as_str, "name")?;
            let result = await!(facade.set_name(name.to_string()))?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothForgetDevice => {
            let identifier = parse_arg!(args, as_str, "identifier")?;
            let result = await!(facade.forget(identifier.to_string()))?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothConnectDevice => {
            let identifier = parse_arg!(args, as_str, "identifier")?;
            let result = await!(facade.connect(identifier.to_string()))?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothDisconnectDevice => {
            let identifier = parse_arg!(args, as_str, "identifier")?;
            let result = await!(facade.disconnect(identifier.to_string()))?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothRequestDiscovery => {
            let discovery = parse_arg!(args, as_bool, "discovery")?;
            let result = await!(facade.request_discovery(discovery))?;
            Ok(to_value(result)?)
        }
        BluetoothMethod::BluetoothGetActiveAdapterAddress => {
            let result = await!(facade.get_active_adapter_address())?;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid Bluetooth control FIDL method: {:?}", method_name),
    }
}

// Takes ACTS method command and executes corresponding Gatt Client
// FIDL methods.
// Packages result into serde::Value
pub async fn gatt_client_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<GattClientFacade>,
) -> Result<Value, Error> {
    match BluetoothMethod::from_str(&method_name) {
        BluetoothMethod::BleStartScan => {
            let filter = ble_scan_to_fidl(args)?;
            await!(start_scan_async(&facade, filter))
        }
        BluetoothMethod::BleStopScan => await!(stop_scan_async(&facade)),
        BluetoothMethod::BleGetDiscoveredDevices => {
            await!(le_get_discovered_devices_async(&facade))
        }
        BluetoothMethod::BleConnectPeripheral => {
            let id = parse_identifier(args)?;
            await!(connect_peripheral_async(&facade, id))
        }
        BluetoothMethod::BleDisconnectPeripheral => {
            let id = parse_identifier(args)?;
            await!(disconnect_peripheral_async(&facade, id))
        }
        BluetoothMethod::GattcConnectToService => {
            let periph_id = parse_identifier(args.clone())?;
            let service_id = parse_service_identifier(args)?;
            await!(gattc_connect_to_service_async(&facade, periph_id, service_id))
        }
        BluetoothMethod::GattcDiscoverCharacteristics => {
            await!(gattc_discover_characteristics_async(&facade))
        }
        BluetoothMethod::GattcWriteCharacteristicById => {
            let id = parse_u64_identifier(args.clone())?;
            let offset_as_u64 = parse_offset(args.clone())?;
            let offset = offset_as_u64 as u16;
            let value = parse_write_value(args)?;
            await!(gattc_write_char_by_id_async(&facade, id, offset, value))
        }
        BluetoothMethod::GattcWriteCharacteristicByIdWithoutResponse => {
            let id = parse_u64_identifier(args.clone())?;
            let value = parse_write_value(args)?;
            await!(gattc_write_char_by_id_without_response_async(&facade, id, value))
        }
        BluetoothMethod::GattcEnableNotifyCharacteristic => {
            let id = parse_u64_identifier(args.clone())?;
            await!(gattc_toggle_notify_characteristic_async(&facade, id, true))
        }
        BluetoothMethod::GattcDisableNotifyCharacteristic => {
            let id = parse_u64_identifier(args.clone())?;
            await!(gattc_toggle_notify_characteristic_async(&facade, id, false))
        }
        BluetoothMethod::GattcReadCharacteristicById => {
            let id = parse_u64_identifier(args.clone())?;
            await!(gattc_read_char_by_id_async(&facade, id))
        }
        BluetoothMethod::GattcReadLongCharacteristicById => {
            let id = parse_u64_identifier(args.clone())?;
            let offset_as_u64 = parse_offset(args.clone())?;
            let offset = offset_as_u64 as u16;
            let max_bytes_as_u64 = parse_max_bytes(args)?;
            let max_bytes = max_bytes_as_u64 as u16;
            await!(gattc_read_long_char_by_id_async(&facade, id, offset, max_bytes))
        }
        BluetoothMethod::GattcReadLongDescriptorById => {
            let id = parse_u64_identifier(args.clone())?;
            let offset_as_u64 = parse_offset(args.clone())?;
            let offset = offset_as_u64 as u16;
            let max_bytes_as_u64 = parse_max_bytes(args)?;
            let max_bytes = max_bytes_as_u64 as u16;
            await!(gattc_read_long_desc_by_id_async(&facade, id, offset, max_bytes))
        }
        BluetoothMethod::GattcWriteDescriptorById => {
            let id = parse_u64_identifier(args.clone())?;
            let value = parse_write_value(args)?;
            await!(gattc_write_desc_by_id_async(&facade, id, value))
        }
        BluetoothMethod::GattcReadDescriptorById => {
            let id = parse_u64_identifier(args.clone())?;
            await!(gattc_read_desc_by_id_async(&facade, id.clone()))
        }
        BluetoothMethod::GattcListServices => {
            let id = parse_identifier(args)?;
            await!(list_services_async(&facade, id))
        }
        _ => bail!("Invalid Gatt Client FIDL method: {:?}", method_name),
    }
}

pub async fn gatt_server_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<GattServerFacade>,
) -> Result<Value, Error> {
    match BluetoothMethod::from_str(&method_name) {
        BluetoothMethod::GattServerPublishServer => {
            let result = await!(facade.publish_server(args))?;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid Gatt Server FIDL method: {:?}", method_name),
    }
}

async fn start_scan_async(
    facade: &GattClientFacade,
    filter: Option<ScanFilter>,
) -> Result<Value, Error> {
    let start_scan_result = await!(facade.start_scan(filter))?;
    Ok(to_value(start_scan_result)?)
}

async fn stop_scan_async(facade: &GattClientFacade) -> Result<Value, Error> {
    let central = facade.get_central_proxy().clone().expect("No central proxy.");
    if let Err(e) = central.stop_scan() {
        bail!("Error stopping scan: {}", e)
    } else {
        // Get the list of devices discovered by the scan.
        let devices = facade.get_devices();
        match to_value(devices) {
            Ok(dev) => Ok(dev),
            Err(e) => Err(e.into()),
        }
    }
}

async fn le_get_discovered_devices_async(facade: &GattClientFacade) -> Result<Value, Error> {
    // Get the list of devices discovered by the scan.
    match to_value(facade.get_devices()) {
        Ok(dev) => Ok(dev),
        Err(e) => Err(e.into()),
    }
}

async fn connect_peripheral_async(facade: &GattClientFacade, id: String) -> Result<Value, Error> {
    let connect_periph_result = await!(facade.connect_peripheral(id))?;
    Ok(to_value(connect_periph_result)?)
}

async fn disconnect_peripheral_async(
    facade: &GattClientFacade,
    id: String,
) -> Result<Value, Error> {
    let value = await!(facade.disconnect_peripheral(id))?;
    Ok(to_value(value)?)
}

// Uses the same return type as connect_peripheral_async -- Returns subset of
// fidl::ServiceInfo
async fn list_services_async(facade: &GattClientFacade, id: String) -> Result<Value, Error> {
    let list_services_result = await!(facade.list_services(id))?;
    Ok(to_value(BleConnectPeripheralResponse::new(list_services_result))?)
}

async fn gattc_connect_to_service_async(
    facade: &GattClientFacade,
    periph_id: String,
    service_id: u64,
) -> Result<Value, Error> {
    let connect_to_service_result = await!(facade.gattc_connect_to_service(periph_id, service_id))?;
    Ok(to_value(connect_to_service_result)?)
}

async fn gattc_discover_characteristics_async(facade: &GattClientFacade) -> Result<Value, Error> {
    let discover_characteristics_results = await!(facade.gattc_discover_characteristics())?;
    Ok(to_value(GattcDiscoverCharacteristicResponse::new(discover_characteristics_results))?)
}

async fn gattc_write_char_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    offset: u16,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_char_status = await!(facade.gattc_write_char_by_id(id, offset, write_value))?;
    Ok(to_value(write_char_status)?)
}

async fn gattc_write_char_by_id_without_response_async(
    facade: &GattClientFacade,
    id: u64,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_char_status =
        await!(facade.gattc_write_char_by_id_without_response(id, write_value))?;
    Ok(to_value(write_char_status)?)
}

async fn gattc_read_char_by_id_async(facade: &GattClientFacade, id: u64) -> Result<Value, Error> {
    let read_char_status = await!(facade.gattc_read_char_by_id(id))?;
    Ok(to_value(read_char_status)?)
}

async fn gattc_read_long_char_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<Value, Error> {
    let read_long_char_status = await!(facade.gattc_read_long_char_by_id(id, offset, max_bytes))?;
    Ok(to_value(read_long_char_status)?)
}

async fn gattc_read_long_desc_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<Value, Error> {
    let read_long_desc_status = await!(facade.gattc_read_long_desc_by_id(id, offset, max_bytes))?;
    Ok(to_value(read_long_desc_status)?)
}

async fn gattc_read_desc_by_id_async(facade: &GattClientFacade, id: u64) -> Result<Value, Error> {
    let read_desc_status = await!(facade.gattc_read_desc_by_id(id))?;
    Ok(to_value(read_desc_status)?)
}

async fn gattc_write_desc_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_desc_status = await!(facade.gattc_write_desc_by_id(id, write_value))?;
    Ok(to_value(write_desc_status)?)
}

async fn gattc_toggle_notify_characteristic_async(
    facade: &GattClientFacade,
    id: u64,
    value: bool,
) -> Result<Value, Error> {
    let toggle_notify_result = await!(facade.gattc_toggle_notify_characteristic(id, value))?;
    Ok(to_value(toggle_notify_result)?)
}

async fn publish_service_async(
    facade: &RwLock<BluetoothFacade>,
    service_info: ServiceInfo,
    local_service_id: String,
) -> Result<Value, Error> {
    let publish_service_result =
        await!(BluetoothFacade::publish_service(&facade, service_info, local_service_id))?;
    Ok(to_value(publish_service_result)?)
}
