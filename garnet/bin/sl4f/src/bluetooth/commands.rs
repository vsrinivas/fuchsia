// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{format_err, Error};
use fidl_fuchsia_bluetooth_gatt::ServiceInfo;
use fidl_fuchsia_bluetooth_le::{AdvertisingData, AdvertisingParameters, ScanFilter};
use fuchsia_syslog::macros::*;
use futures::future::{FutureExt, LocalBoxFuture};
use parking_lot::RwLock;
use serde_json::{to_value, Value};

// Bluetooth-related functionality
use crate::bluetooth::avdtp_facade::AvdtpFacade;
use crate::bluetooth::ble_advertise_facade::BleAdvertiseFacade;
use crate::bluetooth::bt_control_facade::BluetoothControlFacade;
use crate::bluetooth::facade::BluetoothFacade;
use crate::bluetooth::gatt_client_facade::GattClientFacade;
use crate::bluetooth::gatt_server_facade::GattServerFacade;
use crate::bluetooth::profile_server_facade::ProfileServerFacade;
use crate::bluetooth::types::{
    BleAdvertiseResponse, BleConnectPeripheralResponse, GattcDiscoverCharacteristicResponse,
};

use crate::common_utils::common::{
    parse_identifier, parse_max_bytes, parse_offset, parse_service_identifier,
    parse_u64_identifier, parse_write_value,
};

use crate::common_utils::common::macros::parse_arg;

// Takes a serde_json::Value and converts it to arguments required for
// a FIDL ble_advertise command
fn ble_advertise_args_to_fidl(args_raw: Value) -> Result<AdvertisingParameters, Error> {
    let adv_data_raw = match args_raw.get("advertising_data") {
        Some(adr) => adr.clone(),
        None => return Err(format_err!("Advertising data missing.")),
    };

    let conn_raw = args_raw.get("connectable").ok_or(format_err!("Connectable missing"))?;

    // Unpack the name for advertising data, as well as interval of advertising
    let name: Option<String> = adv_data_raw["name"].as_str().map(String::from);
    let connectable: bool = conn_raw.as_bool().unwrap_or(false);

    // TODO(NET-1026): Is there a better way to unpack the args into an AdvData
    // struct? Unfortunately, can't derive deserialize for AdvData
    let parameters = AdvertisingParameters {
        data: Some(AdvertisingData {
            name,
            appearance: None,
            tx_power_level: None,
            service_uuids: None,
            service_data: None,
            manufacturer_data: None,
            uris: None,
        }),
        scan_response: None,
        mode_hint: None,
        connectable: Some(connectable),
    };

    fx_log_info!(tag: "ble_advertise_args_to_fidl", "advertising parameters: {:?}", parameters);
    Ok(parameters)
}

// Takes a serde_json::Value and converts it to arguments required for a FIDL
// ble_scan command
fn ble_scan_to_fidl(args_raw: Value) -> Result<Option<ScanFilter>, Error> {
    let scan_filter_raw = match args_raw.get("filter") {
        Some(f) => Some(f).unwrap().clone(),
        None => return Err(format_err!("Scan filter missing.")),
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

impl Facade for BleAdvertiseFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        ble_advertise_method_to_fidl(method, args, self).boxed_local()
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

// Takes ACTS method command and executes corresponding BLE
// Advertise FIDL methods.
// Packages result into serde::Value
async fn ble_advertise_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &BleAdvertiseFacade,
) -> Result<Value, Error> {
    // TODO(armansito): Once the facade supports multi-advertising it should generate and
    // return a unique ID for each instance. For now we return this dummy ID for the
    // singleton advertisement.
    let id = to_value(BleAdvertiseResponse::new(Some("singleton-instance".to_string())))?;
    match method_name.as_ref() {
        "BleAdvertise" => {
            let params = ble_advertise_args_to_fidl(args)?;
            facade.start_adv(params).await?;
            Ok(id)
        }
        "BleStopAdvertise" => {
            facade.stop_adv();
            Ok(id)
        }
        _ => return Err(format_err!("Invalid BleAdvertise FIDL method: {:?}", method_name)),
    }
}

impl Facade for RwLock<BluetoothFacade> {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        ble_method_to_fidl(method, args, self).boxed_local()
    }

    fn cleanup(&self) {
        BluetoothFacade::cleanup(self)
    }

    fn print(&self) {
        self.read().print()
    }
}

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
async fn ble_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &RwLock<BluetoothFacade>,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "BlePublishService" => {
            let (service_info, local_service_id) = ble_publish_service_to_fidl(args)?;
            publish_service_async(&facade, service_info, local_service_id).await
        }
        _ => return Err(format_err!("Invalid BLE FIDL method: {:?}", method_name)),
    }
}

impl Facade for BluetoothControlFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        bt_control_method_to_fidl(method, args, self).boxed_local()
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

async fn bt_control_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &BluetoothControlFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "BluetoothAcceptPairing" => {
            let result = facade.accept_pairing().await?;
            Ok(to_value(result)?)
        }
        "BluetoothInitControl" => {
            let result = facade.init_control_interface_proxy().await?;
            Ok(to_value(result)?)
        }
        "BluetoothGetKnownRemoteDevices" => {
            let result = facade.get_known_remote_devices().await?;
            Ok(to_value(result)?)
        }
        "BluetoothSetDiscoverable" => {
            let discoverable = parse_arg!(args, as_bool, "discoverable")?;
            let result = facade.set_discoverable(discoverable).await?;
            Ok(to_value(result)?)
        }
        "BluetoothSetName" => {
            let name = parse_arg!(args, as_str, "name")?;
            let result = facade.set_name(name.to_string()).await?;
            Ok(to_value(result)?)
        }
        "BluetoothForgetDevice" => {
            let identifier = parse_arg!(args, as_str, "identifier")?;
            let result = facade.forget(identifier.to_string()).await?;
            Ok(to_value(result)?)
        }
        "BluetoothConnectDevice" => {
            let identifier = parse_arg!(args, as_str, "identifier")?;
            let result = facade.connect(identifier.to_string()).await?;
            Ok(to_value(result)?)
        }
        "BluetoothDisconnectDevice" => {
            let identifier = parse_arg!(args, as_str, "identifier")?;
            let result = facade.disconnect(identifier.to_string()).await?;
            Ok(to_value(result)?)
        }
        "BluetoothRequestDiscovery" => {
            let discovery = parse_arg!(args, as_bool, "discovery")?;
            let result = facade.request_discovery(discovery).await?;
            Ok(to_value(result)?)
        }
        "BluetoothInputPairingPin" => {
            let pin = parse_arg!(args, as_str, "pin")?;
            let result = facade.input_pairing_pin(pin.to_string()).await?;
            Ok(to_value(result)?)
        }
        "BluetoothGetPairingPin" => {
            let result = facade.get_pairing_pin().await?;
            Ok(to_value(result)?)
        }
        "BluetoothGetActiveAdapterAddress" => {
            let result = facade.get_active_adapter_address().await?;
            Ok(to_value(result)?)
        }
        _ => return Err(format_err!("Invalid Bluetooth control FIDL method: {:?}", method_name)),
    }
}

impl Facade for GattClientFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        gatt_client_method_to_fidl(method, args, self).boxed_local()
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

// Takes ACTS method command and executes corresponding Gatt Client
// FIDL methods.
// Packages result into serde::Value
async fn gatt_client_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &GattClientFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "BleStartScan" => {
            let filter = ble_scan_to_fidl(args)?;
            start_scan_async(&facade, filter).await
        }
        "BleStopScan" => stop_scan_async(&facade).await,
        "BleGetDiscoveredDevices" => le_get_discovered_devices_async(&facade).await,
        "BleConnectPeripheral" => {
            let id = parse_identifier(args)?;
            connect_peripheral_async(&facade, id).await
        }
        "BleDisconnectPeripheral" => {
            let id = parse_identifier(args)?;
            disconnect_peripheral_async(&facade, id).await
        }
        "GattcConnectToService" => {
            let periph_id = parse_identifier(args.clone())?;
            let service_id = parse_service_identifier(args)?;
            gattc_connect_to_service_async(&facade, periph_id, service_id).await
        }
        "GattcDiscoverCharacteristics" => gattc_discover_characteristics_async(&facade).await,
        "GattcWriteCharacteristicById" => {
            let id = parse_u64_identifier(args.clone())?;
            let value = parse_write_value(args)?;
            gattc_write_char_by_id_async(&facade, id, value).await
        }
        "GattcWriteLongCharacteristicById" => {
            let id = parse_u64_identifier(args.clone())?;
            let offset_as_u64 = parse_offset(args.clone())?;
            let offset = offset_as_u64 as u16;
            let value = parse_write_value(args)?;
            gattc_write_long_char_by_id_async(&facade, id, offset, value).await
        }
        "GattcWriteCharacteristicByIdWithoutResponse" => {
            let id = parse_u64_identifier(args.clone())?;
            let value = parse_write_value(args)?;
            gattc_write_char_by_id_without_response_async(&facade, id, value).await
        }
        "GattcEnableNotifyCharacteristic" => {
            let id = parse_u64_identifier(args.clone())?;
            gattc_toggle_notify_characteristic_async(&facade, id, true).await
        }
        "GattcDisableNotifyCharacteristic" => {
            let id = parse_u64_identifier(args.clone())?;
            gattc_toggle_notify_characteristic_async(&facade, id, false).await
        }
        "GattcReadCharacteristicById" => {
            let id = parse_u64_identifier(args.clone())?;
            gattc_read_char_by_id_async(&facade, id).await
        }
        "GattcReadLongCharacteristicById" => {
            let id = parse_u64_identifier(args.clone())?;
            let offset_as_u64 = parse_offset(args.clone())?;
            let offset = offset_as_u64 as u16;
            let max_bytes_as_u64 = parse_max_bytes(args)?;
            let max_bytes = max_bytes_as_u64 as u16;
            gattc_read_long_char_by_id_async(&facade, id, offset, max_bytes).await
        }
        "GattcReadLongDescriptorById" => {
            let id = parse_u64_identifier(args.clone())?;
            let offset_as_u64 = parse_offset(args.clone())?;
            let offset = offset_as_u64 as u16;
            let max_bytes_as_u64 = parse_max_bytes(args)?;
            let max_bytes = max_bytes_as_u64 as u16;
            gattc_read_long_desc_by_id_async(&facade, id, offset, max_bytes).await
        }
        "GattcWriteDescriptorById" => {
            let id = parse_u64_identifier(args.clone())?;
            let value = parse_write_value(args)?;
            gattc_write_desc_by_id_async(&facade, id, value).await
        }
        "GattcWriteLongDescriptorById" => {
            let id = parse_u64_identifier(args.clone())?;
            let offset_as_u64 = parse_offset(args.clone())?;
            let offset = offset_as_u64 as u16;
            let value = parse_write_value(args)?;
            gattc_write_long_desc_by_id_async(&facade, id, offset, value).await
        }
        "GattcReadDescriptorById" => {
            let id = parse_u64_identifier(args.clone())?;
            gattc_read_desc_by_id_async(&facade, id.clone()).await
        }
        "GattcListServices" => {
            let id = parse_identifier(args)?;
            list_services_async(&facade, id).await
        }
        _ => return Err(format_err!("Invalid Gatt Client FIDL method: {:?}", method_name)),
    }
}

impl Facade for GattServerFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        gatt_server_method_to_fidl(method, args, self).boxed_local()
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

async fn gatt_server_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &GattServerFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "GattServerPublishServer" => {
            let result = facade.publish_server(args).await?;
            Ok(to_value(result)?)
        }
        "GattServerCloseServer" => {
            let result = facade.close_server().await;
            Ok(to_value(result)?)
        }
        _ => return Err(format_err!("Invalid Gatt Server FIDL method: {:?}", method_name)),
    }
}

impl Facade for ProfileServerFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        profile_server_method_to_fidl(method, args, self).boxed_local()
    }
}

async fn profile_server_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &ProfileServerFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "ProfileServerInit" => {
            let result = facade.init_profile_server_proxy().await?;
            Ok(to_value(result)?)
        }
        "ProfileServerAddSearch" => {
            let result = facade.add_search(args).await?;
            Ok(to_value(result)?)
        }
        "ProfileServerAddService" => {
            let result = facade.add_service(args).await?;
            Ok(to_value(result)?)
        }
        "ProfileServerCleanup" => {
            let result = facade.cleanup().await?;
            Ok(to_value(result)?)
        }
        "ProfileServerRemoveService" => {
            let service_id = parse_u64_identifier(args)?;
            let result = facade.remove_service(service_id).await?;
            Ok(to_value(result)?)
        }
        _ => return Err(format_err!("Invalid Profile Server FIDL method: {:?}", method_name)),
    }
}

impl Facade for AvdtpFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        avdtp_method_to_fidl(method, args, self).boxed_local()
    }
}

pub async fn avdtp_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &AvdtpFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "AvdtpInit" => {
            let role = parse_arg!(args, as_str, "role")?;
            let result = facade.init_avdtp_service_proxy(role.to_string()).await?;
            Ok(to_value(result)?)
        }
        "AvdtpGetConnectedPeers" => {
            let result = facade.get_connected_peers().await?;
            Ok(to_value(result)?)
        }
        "AvdtpSetConfiguration" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.set_configuration(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpGetConfiguration" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.get_configuration(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpGetCapabilities" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.get_capabilities(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpGetAllCapabilities" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.get_all_capabilities(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpReconfigureStream" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.reconfigure_stream(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpSuspendStream" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.suspend_stream(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpSuspendAndReconfigure" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.suspend_and_reconfigure(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpReleaseStream" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.release_stream(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpEstablishStream" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.establish_stream(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpStartStream" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.start_stream(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpAbortStream" => {
            let peer_id = parse_u64_identifier(args)?;
            let result = facade.abort_stream(peer_id).await?;
            Ok(to_value(result)?)
        }
        "AvdtpRemoveService" => {
            let result = facade.remove_service().await;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid AVDTP FIDL method: {:?}", method_name),
    }
}

async fn start_scan_async(
    facade: &GattClientFacade,
    filter: Option<ScanFilter>,
) -> Result<Value, Error> {
    let start_scan_result = facade.start_scan(filter).await?;
    Ok(to_value(start_scan_result)?)
}

async fn stop_scan_async(facade: &GattClientFacade) -> Result<Value, Error> {
    let central = facade.get_central_proxy().clone().expect("No central proxy.");
    if let Err(e) = central.stop_scan() {
        return Err(format_err!("Error stopping scan: {}", e));
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
    let connect_periph_result = facade.connect_peripheral(id).await?;
    Ok(to_value(connect_periph_result)?)
}

async fn disconnect_peripheral_async(
    facade: &GattClientFacade,
    id: String,
) -> Result<Value, Error> {
    let value = facade.disconnect_peripheral(id).await?;
    Ok(to_value(value)?)
}

// Uses the same return type as connect_peripheral_async -- Returns subset of
// fidl::ServiceInfo
async fn list_services_async(facade: &GattClientFacade, id: String) -> Result<Value, Error> {
    let list_services_result = facade.list_services(id).await?;
    Ok(to_value(BleConnectPeripheralResponse::new(list_services_result))?)
}

async fn gattc_connect_to_service_async(
    facade: &GattClientFacade,
    periph_id: String,
    service_id: u64,
) -> Result<Value, Error> {
    let connect_to_service_result = facade.gattc_connect_to_service(periph_id, service_id).await?;
    Ok(to_value(connect_to_service_result)?)
}

async fn gattc_discover_characteristics_async(facade: &GattClientFacade) -> Result<Value, Error> {
    let discover_characteristics_results = facade.gattc_discover_characteristics().await?;
    Ok(to_value(GattcDiscoverCharacteristicResponse::new(discover_characteristics_results))?)
}

async fn gattc_write_char_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_char_status = facade.gattc_write_char_by_id(id, write_value).await?;
    Ok(to_value(write_char_status)?)
}

async fn gattc_write_long_char_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    offset: u16,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_char_status = facade.gattc_write_long_char_by_id(id, offset, write_value).await?;
    Ok(to_value(write_char_status)?)
}

async fn gattc_write_char_by_id_without_response_async(
    facade: &GattClientFacade,
    id: u64,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_char_status = facade.gattc_write_char_by_id_without_response(id, write_value).await?;
    Ok(to_value(write_char_status)?)
}

async fn gattc_read_char_by_id_async(facade: &GattClientFacade, id: u64) -> Result<Value, Error> {
    let read_char_status = facade.gattc_read_char_by_id(id).await?;
    Ok(to_value(read_char_status)?)
}

async fn gattc_read_long_char_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<Value, Error> {
    let read_long_char_status = facade.gattc_read_long_char_by_id(id, offset, max_bytes).await?;
    Ok(to_value(read_long_char_status)?)
}

async fn gattc_read_long_desc_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<Value, Error> {
    let read_long_desc_status = facade.gattc_read_long_desc_by_id(id, offset, max_bytes).await?;
    Ok(to_value(read_long_desc_status)?)
}

async fn gattc_read_desc_by_id_async(facade: &GattClientFacade, id: u64) -> Result<Value, Error> {
    let read_desc_status = facade.gattc_read_desc_by_id(id).await?;
    Ok(to_value(read_desc_status)?)
}

async fn gattc_write_desc_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_desc_status = facade.gattc_write_desc_by_id(id, write_value).await?;
    Ok(to_value(write_desc_status)?)
}

async fn gattc_write_long_desc_by_id_async(
    facade: &GattClientFacade,
    id: u64,
    offset: u16,
    write_value: Vec<u8>,
) -> Result<Value, Error> {
    let write_desc_status = facade.gattc_write_long_desc_by_id(id, offset, write_value).await?;
    Ok(to_value(write_desc_status)?)
}

async fn gattc_toggle_notify_characteristic_async(
    facade: &GattClientFacade,
    id: u64,
    value: bool,
) -> Result<Value, Error> {
    let toggle_notify_result = facade.gattc_toggle_notify_characteristic(id, value).await?;
    Ok(to_value(toggle_notify_result)?)
}

async fn publish_service_async(
    facade: &RwLock<BluetoothFacade>,
    service_info: ServiceInfo,
    local_service_id: String,
) -> Result<Value, Error> {
    let publish_service_result =
        BluetoothFacade::publish_service(&facade, service_info, local_service_id).await?;
    Ok(to_value(publish_service_result)?)
}
