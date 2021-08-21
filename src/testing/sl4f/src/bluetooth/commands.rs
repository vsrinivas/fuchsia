// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use bt_rfcomm::ServerChannel;
use fidl_fuchsia_bluetooth::PeerId;
use fidl_fuchsia_bluetooth_gatt::ServiceInfo;
use fidl_fuchsia_bluetooth_hfp::{CallDirection, CallState, NetworkInformation, SignalStrength};
use fidl_fuchsia_bluetooth_le::ScanFilter;
use fidl_fuchsia_bluetooth_sys::{LeSecurityMode, Settings};
use parking_lot::RwLock;
use serde_json::{from_value, to_value, Value};
use std::convert::TryFrom;
use test_call_manager::TestCallManager as HfpFacade;
use test_rfcomm_client::RfcommManager as RfcommFacade;

// Bluetooth-related functionality
use crate::bluetooth::avdtp_facade::AvdtpFacade;
use crate::bluetooth::avrcp_facade::AvrcpFacade;
use crate::bluetooth::ble_advertise_facade::BleAdvertiseFacade;
use crate::bluetooth::bt_sys_facade::BluetoothSysFacade;
use crate::bluetooth::facade::BluetoothFacade;
use crate::bluetooth::gatt_client_facade::GattClientFacade;
use crate::bluetooth::gatt_server_facade::GattServerFacade;
use crate::bluetooth::profile_server_facade::ProfileServerFacade;
use crate::bluetooth::types::{
    BleAdvertiseResponse, BleConnectPeripheralResponse, CustomBatteryStatus,
    CustomNotificationsFilter, CustomPlayerApplicationSettings,
    CustomPlayerApplicationSettingsAttributeIds, GattcDiscoverCharacteristicResponse,
};

use crate::common_utils::common::{
    parse_identifier, parse_max_bytes, parse_offset, parse_psm, parse_service_identifier,
    parse_u64_identifier, parse_write_value,
};

use crate::common_utils::common::macros::parse_arg;

use super::types::AbsoluteVolumeCommand;

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

    // TODO(fxbug.dev/883): Add support for GATT characterstics and includes
    let characteristics = None;
    let includes = None;

    let service_info =
        ServiceInfo { id, primary, type_: type_.to_string(), characteristics, includes };
    Ok((service_info, local_service_id.to_string()))
}

#[async_trait(?Send)]
impl Facade for BleAdvertiseFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        // TODO(armansito): Once the facade supports multi-advertising it should generate and
        // return a unique ID for each instance. For now we return this dummy ID for the
        // singleton advertisement.
        let id = to_value(BleAdvertiseResponse::new(Some("singleton-instance".to_string())))?;
        match method.as_ref() {
            "BleAdvertise" => {
                self.start_adv(args).await?;
            }
            "BleStopAdvertise" => {
                self.stop_adv();
            }
            _ => return Err(format_err!("Invalid BleAdvertise FIDL method: {:?}", method)),
        };
        Ok(id)
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

#[async_trait(?Send)]
impl Facade for RwLock<BluetoothFacade> {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "BlePublishService" => {
                let (service_info, local_service_id) = ble_publish_service_to_fidl(args)?;
                publish_service_async(self, service_info, local_service_id).await
            }
            _ => return Err(format_err!("Invalid BLE FIDL method: {:?}", method)),
        }
    }

    fn cleanup(&self) {
        BluetoothFacade::cleanup(self)
    }

    fn print(&self) {
        self.read().print()
    }
}

#[async_trait(?Send)]
impl Facade for BluetoothSysFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "BluetoothAcceptPairing" => {
                let input = parse_arg!(args, as_str, "input")?;
                let output = parse_arg!(args, as_str, "output")?;
                let result = self.accept_pairing(&input.to_string(), &output.to_string()).await?;
                Ok(to_value(result)?)
            }
            "BluetoothInitSys" => {
                let result = self.init_access_proxy().await?;
                Ok(to_value(result)?)
            }
            "BluetoothGetKnownRemoteDevices" => {
                let result = self.get_known_remote_devices().await?;
                Ok(to_value(result)?)
            }
            "BluetoothSetDiscoverable" => {
                let discoverable = parse_arg!(args, as_bool, "discoverable")?;
                let result = self.set_discoverable(discoverable).await?;
                Ok(to_value(result)?)
            }
            "BluetoothSetName" => {
                let name = parse_arg!(args, as_str, "name")?;
                let result = self.set_name(name.to_string()).await?;
                Ok(to_value(result)?)
            }
            "BluetoothForgetDevice" => {
                let identifier = parse_arg!(args, as_u64, "identifier")?;
                let result = self.forget(identifier).await?;
                Ok(to_value(result)?)
            }
            "BluetoothConnectDevice" => {
                let identifier = parse_arg!(args, as_u64, "identifier")?;
                let result = self.connect(identifier).await?;
                Ok(to_value(result)?)
            }
            "BluetoothDisconnectDevice" => {
                let identifier = parse_arg!(args, as_u64, "identifier")?;
                let result = self.disconnect(identifier).await?;
                Ok(to_value(result)?)
            }
            "BluetoothRequestDiscovery" => {
                let discovery = parse_arg!(args, as_bool, "discovery")?;
                let result = self.start_discovery(discovery).await?;
                Ok(to_value(result)?)
            }
            "BluetoothInputPairingPin" => {
                let pin = parse_arg!(args, as_str, "pin")?;
                let result = self.input_pairing_pin(pin.to_string()).await?;
                Ok(to_value(result)?)
            }
            "BluetoothGetPairingPin" => {
                let result = self.get_pairing_pin().await?;
                Ok(to_value(result)?)
            }
            "BluetoothGetActiveAdapterAddress" => {
                let result = self.get_active_adapter_address().await?;
                Ok(to_value(result)?)
            }
            "BluetoothPairDevice" => {
                let identifier = parse_arg!(args, as_u64, "identifier")?;
                let pairing_security_level =
                    match parse_arg!(args, as_u64, "pairing_security_level") {
                        Ok(v) => Some(v),
                        Err(_e) => None,
                    };
                let non_bondable = match parse_arg!(args, as_bool, "non_bondable") {
                    Ok(v) => Some(v),
                    Err(_e) => None,
                };
                let transport = parse_arg!(args, as_u64, "transport")?;

                let result =
                    self.pair(identifier, pairing_security_level, non_bondable, transport).await?;
                Ok(to_value(result)?)
            }
            "BluetoothUpdateSystemSettings" => {
                let le_privacy = parse_arg!(args, as_bool, "le_privacy").ok();
                let le_background_scan = parse_arg!(args, as_bool, "le_background_scan").ok();
                let bredr_connectable_mode =
                    parse_arg!(args, as_bool, "bredr_connectable_mode").ok();
                let le_security_mode = if let Ok(s) = parse_arg!(args, as_str, "le_security_mode") {
                    match s.to_uppercase().as_ref() {
                        "MODE_1" => Some(LeSecurityMode::Mode1),
                        "SECURE_CONNECTIONS_ONLY" => Some(LeSecurityMode::SecureConnectionsOnly),
                        _ => bail!(
                            "Invalid le_security_mode {} passed to BluetoothUpdateSystemSettings",
                            s
                        ),
                    }
                } else {
                    None
                };
                let result = self
                    .update_settings(Settings {
                        le_privacy,
                        le_background_scan,
                        bredr_connectable_mode,
                        le_security_mode,
                        ..Settings::EMPTY
                    })
                    .await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid Bluetooth control FIDL method: {:?}", method),
        }
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

#[async_trait(?Send)]
impl Facade for GattClientFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "BleStartScan" => {
                let filter = ble_scan_to_fidl(args)?;
                start_scan_async(&self, filter).await
            }
            "BleStopScan" => stop_scan_async(self).await,
            "BleGetDiscoveredDevices" => le_get_discovered_devices_async(self).await,
            "BleConnectPeripheral" => {
                let id = parse_identifier(args)?;
                connect_peripheral_async(self, id).await
            }
            "BleDisconnectPeripheral" => {
                let id = parse_identifier(args)?;
                disconnect_peripheral_async(self, id).await
            }
            "GattcConnectToService" => {
                let periph_id = parse_identifier(args.clone())?;
                let service_id = parse_service_identifier(args)?;
                gattc_connect_to_service_async(self, periph_id, service_id).await
            }
            "GattcDiscoverCharacteristics" => gattc_discover_characteristics_async(self).await,
            "GattcWriteCharacteristicById" => {
                let id = parse_u64_identifier(args.clone())?;
                let value = parse_write_value(args)?;
                gattc_write_char_by_id_async(self, id, value).await
            }
            "GattcWriteLongCharacteristicById" => {
                let id = parse_u64_identifier(args.clone())?;
                let offset_as_u64 = parse_offset(args.clone())?;
                let offset = offset_as_u64 as u16;
                let value = parse_write_value(args.clone())?;
                let reliable_mode = parse_arg!(args, as_bool, "reliable_mode")?;
                gattc_write_long_char_by_id_async(self, id, offset, value, reliable_mode).await
            }
            "GattcWriteCharacteristicByIdWithoutResponse" => {
                let id = parse_u64_identifier(args.clone())?;
                let value = parse_write_value(args)?;
                gattc_write_char_by_id_without_response_async(self, id, value).await
            }
            "GattcEnableNotifyCharacteristic" => {
                let id = parse_u64_identifier(args.clone())?;
                gattc_toggle_notify_characteristic_async(self, id, true).await
            }
            "GattcDisableNotifyCharacteristic" => {
                let id = parse_u64_identifier(args.clone())?;
                gattc_toggle_notify_characteristic_async(self, id, false).await
            }
            "GattcReadCharacteristicById" => {
                let id = parse_u64_identifier(args.clone())?;
                gattc_read_char_by_id_async(self, id).await
            }
            "GattcReadCharacteristicByType" => {
                let uuid = parse_arg!(args, as_str, "uuid")?;
                let result = self.gattc_read_char_by_type(uuid.to_string()).await?;
                Ok(to_value(result)?)
            }
            "GattcReadLongCharacteristicById" => {
                let id = parse_u64_identifier(args.clone())?;
                let offset_as_u64 = parse_offset(args.clone())?;
                let offset = offset_as_u64 as u16;
                let max_bytes_as_u64 = parse_max_bytes(args)?;
                let max_bytes = max_bytes_as_u64 as u16;
                gattc_read_long_char_by_id_async(self, id, offset, max_bytes).await
            }
            "GattcReadLongDescriptorById" => {
                let id = parse_u64_identifier(args.clone())?;
                let offset_as_u64 = parse_offset(args.clone())?;
                let offset = offset_as_u64 as u16;
                let max_bytes_as_u64 = parse_max_bytes(args)?;
                let max_bytes = max_bytes_as_u64 as u16;
                gattc_read_long_desc_by_id_async(self, id, offset, max_bytes).await
            }
            "GattcWriteDescriptorById" => {
                let id = parse_u64_identifier(args.clone())?;
                let value = parse_write_value(args)?;
                gattc_write_desc_by_id_async(self, id, value).await
            }
            "GattcWriteLongDescriptorById" => {
                let id = parse_u64_identifier(args.clone())?;
                let offset_as_u64 = parse_offset(args.clone())?;
                let offset = offset_as_u64 as u16;
                let value = parse_write_value(args)?;
                gattc_write_long_desc_by_id_async(self, id, offset, value).await
            }
            "GattcReadDescriptorById" => {
                let id = parse_u64_identifier(args.clone())?;
                gattc_read_desc_by_id_async(self, id.clone()).await
            }
            "GattcListServices" => {
                let id = parse_identifier(args)?;
                list_services_async(self, id).await
            }
            _ => return Err(format_err!("Invalid Gatt Client FIDL method: {:?}", method)),
        }
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

#[async_trait(?Send)]
impl Facade for GattServerFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "GattServerPublishServer" => {
                let result = self.publish_server(args).await?;
                Ok(to_value(result)?)
            }
            "GattServerCloseServer" => {
                let result = self.close_server().await;
                Ok(to_value(result)?)
            }
            _ => return Err(format_err!("Invalid Gatt Server FIDL method: {:?}", method)),
        }
    }

    fn cleanup(&self) {
        Self::cleanup(self)
    }

    fn print(&self) {
        Self::print(self)
    }
}

#[async_trait(?Send)]
impl Facade for ProfileServerFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "ProfileServerInit" => {
                let result = self.init_profile_server_proxy().await?;
                Ok(to_value(result)?)
            }
            "ProfileServerAddSearch" => {
                let result = self.add_search(args).await?;
                Ok(to_value(result)?)
            }
            "ProfileServerAddService" => {
                let result = self.add_service(args).await?;
                Ok(to_value(result)?)
            }
            "ProfileServerCleanup" => {
                let result = self.cleanup().await?;
                Ok(to_value(result)?)
            }
            "ProfileServerConnectL2cap" => {
                let id = parse_identifier(args.clone())?;
                let psm = parse_psm(args.clone())?;
                let mode = parse_arg!(args, as_str, "mode")?;
                let result = self.connect(id, psm as u16, &mode.to_string()).await?;
                Ok(to_value(result)?)
            }
            "ProfileServerRemoveService" => {
                let service_id = parse_u64_identifier(args)? as usize;
                let result = self.remove_service(service_id).await?;
                Ok(to_value(result)?)
            }
            _ => return Err(format_err!("Invalid Profile Server FIDL method: {:?}", method)),
        }
    }
}

#[async_trait(?Send)]
impl Facade for AvdtpFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "AvdtpInit" => {
                let initiator_delay = match parse_arg!(args, as_str, "initiator_delay") {
                    Ok(v) => Some(v.to_string()),
                    Err(_e) => None,
                };
                let result = self.init_avdtp_service_proxy(initiator_delay).await?;
                Ok(to_value(result)?)
            }
            "AvdtpGetConnectedPeers" => {
                let result = self.get_connected_peers().await?;
                Ok(to_value(result)?)
            }
            "AvdtpSetConfiguration" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.set_configuration(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpGetConfiguration" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.get_configuration(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpGetCapabilities" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.get_capabilities(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpGetAllCapabilities" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.get_all_capabilities(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpReconfigureStream" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.reconfigure_stream(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpSuspendStream" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.suspend_stream(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpSuspendAndReconfigure" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.suspend_and_reconfigure(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpReleaseStream" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.release_stream(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpEstablishStream" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.establish_stream(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpStartStream" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.start_stream(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpAbortStream" => {
                let peer_id = parse_u64_identifier(args)?;
                let result = self.abort_stream(peer_id).await?;
                Ok(to_value(result)?)
            }
            "AvdtpRemoveService" => {
                let result = self.remove_service().await;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid AVDTP FIDL method: {:?}", method),
        }
    }
}

#[async_trait(?Send)]
impl Facade for AvrcpFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "AvrcpInit" => {
                let target_id = parse_arg!(args, as_u64, "target_id")?;
                let result = self.init_avrcp(target_id).await?;
                Ok(to_value(result)?)
            }
            "AvrcpGetMediaAttributes" => {
                let result = self.get_media_attributes().await?;
                Ok(to_value(result)?)
            }
            "AvrcpGetPlayStatus" => {
                let result = self.get_play_status().await?;
                Ok(to_value(result)?)
            }
            "AvrcpGetPlayerApplicationSettings" => {
                let attribute_ids: CustomPlayerApplicationSettingsAttributeIds =
                    match from_value(args.clone()) {
                        Ok(attribute_ids) => attribute_ids,
                        _ => bail!(
                            "Invalid json argument to AvrcpGetPlayerApplicationSettings! - {}",
                            args
                        ),
                    };
                let result = self.get_player_application_settings(attribute_ids.clone()).await?;
                Ok(to_value(result)?)
            }
            "AvrcpInformBatteryStatus" => {
                let battery_status: CustomBatteryStatus = match from_value(args.clone()) {
                    Ok(battery_status) => battery_status,
                    _ => bail!("Invalid json argument to AvrcpInformBatteryStatus! - {}", args),
                };
                let result = self.inform_battery_status(battery_status.clone()).await?;
                Ok(to_value(result)?)
            }
            "AvrcpNotifyNotificationHandled" => {
                let result = self.notify_notification_handled().await?;
                Ok(to_value(result)?)
            }
            "AvrcpSetAddressedPlayer" => {
                let player_id: u16 = match from_value(args.clone()) {
                    Ok(settings) => settings,
                    _ => bail!("Invalid json argument to AvrcpSetAddressedPlayer! - {}", args),
                };
                let result = self.set_addressed_player(player_id.clone()).await?;
                Ok(to_value(result)?)
            }
            "AvrcpSetPlayerApplicationSettings" => {
                let settings: CustomPlayerApplicationSettings = match from_value(args.clone()) {
                    Ok(settings) => settings,
                    _ => bail!(
                        "Invalid json argument to AvrcpSetPlayerApplicationSettings! - {}",
                        args
                    ),
                };
                let result = self.set_player_application_settings(settings.clone()).await?;
                Ok(to_value(result)?)
            }
            "AvrcpSendCommand" => {
                let command_str = parse_arg!(args, as_str, "command")?;
                let result = self.send_command(command_str.to_string().into()).await?;
                Ok(to_value(result)?)
            }
            "AvrcpSetAbsoluteVolume" => {
                let absolute_volume_command: AbsoluteVolumeCommand = match from_value(args.clone())
                {
                    Ok(absolute_volume) => absolute_volume,
                    _ => bail!("Invalid json argument to AvrcpSetAbsoluteVolume! - {}", args),
                };
                let result =
                    self.set_absolute_volume(absolute_volume_command.absolute_volume).await?;
                Ok(to_value(result)?)
            }
            "AvrcpSetNotificationFilter" => {
                let custom_notifications: CustomNotificationsFilter = match from_value(args.clone())
                {
                    Ok(custom_notifications) => custom_notifications,
                    _ => bail!("Invalid json argument to AvrcpSetNotificationFilter! - {}", args),
                };
                let result = self
                    .set_notification_filter(
                        custom_notifications.notifications,
                        match custom_notifications.position_change_interval {
                            Some(position_change_interval) => position_change_interval,
                            _ => 0,
                        },
                    )
                    .await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid AVRCP FIDL method: {:?}", method),
        }
    }

    fn cleanup(&self) {}

    fn print(&self) {}
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
    reliable_mode: bool,
) -> Result<Value, Error> {
    let write_char_status =
        facade.gattc_write_long_char_by_id(id, offset, write_value, reliable_mode).await?;
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

#[async_trait(?Send)]
impl Facade for HfpFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "HfpInit" => {
                let result = self.init_hfp_service().await?;
                Ok(to_value(result)?)
            }
            "HfpRemoveService" => {
                self.cleanup().await;
                Ok(to_value(())?)
            }
            "ListPeers" => {
                let result = self.list_peers().await?;
                Ok(to_value(result)?)
            }
            "SetActivePeer" => {
                let id = parse_arg!(args, as_u64, "peer_id")?;
                let result = self.set_active_peer(id).await?;
                Ok(to_value(result)?)
            }
            "ListCalls" => {
                let result = self.list_calls().await?;
                Ok(to_value(result)?)
            }
            "NewCall" => {
                let remote = parse_arg!(args, as_str, "remote")?;
                let state = parse_arg!(args, as_str, "state")?.to_lowercase();
                let state = match &*state {
                    "ringing" => CallState::IncomingRinging,
                    "waiting" => CallState::IncomingWaiting,
                    "dialing" => CallState::OutgoingDialing,
                    "alerting" => CallState::OutgoingAlerting,
                    "active" => CallState::OngoingActive,
                    "held" => CallState::OngoingHeld,
                    _ => bail!("Invalid state argument: {}", state),
                };
                let direction = parse_arg!(args, as_str, "direction")?;
                let direction = match &*direction {
                    "incoming" => CallDirection::MobileTerminated,
                    "outgoing" => CallDirection::MobileOriginated,
                    _ => bail!("Invalid direction argument: {}", direction),
                };
                let result = self.new_call(&remote, state, direction).await?;
                Ok(to_value(result)?)
            }
            "IncomingCall" => {
                let remote = parse_arg!(args, as_str, "remote")?;
                let result = self.incoming_call(&remote).await?;
                Ok(to_value(result)?)
            }
            "OutgoingCall" => {
                let remote = parse_arg!(args, as_str, "remote")?;
                let result = self.outgoing_call(&remote).await?;
                Ok(to_value(result)?)
            }
            "SetCallActive" => {
                let call_id = parse_arg!(args, as_u64, "call_id")?;
                let result = self.set_call_active(call_id).await?;
                Ok(to_value(result)?)
            }
            "SetCallHeld" => {
                let call_id = parse_arg!(args, as_u64, "call_id")?;
                let result = self.set_call_held(call_id).await?;
                Ok(to_value(result)?)
            }
            "SetCallTerminated" => {
                let call_id = parse_arg!(args, as_u64, "call_id")?;
                let result = self.set_call_terminated(call_id).await?;
                Ok(to_value(result)?)
            }
            "SetCallTransferredToAg" => {
                let call_id = parse_arg!(args, as_u64, "call_id")?;
                let result = self.set_call_transferred_to_ag(call_id).await?;
                Ok(to_value(result)?)
            }
            "SetSpeakerGain" => {
                let value = parse_arg!(args, as_u64, "value")?;
                let result = self.set_speaker_gain(value).await?;
                Ok(to_value(result)?)
            }
            "SetMicrophoneGain" => {
                let value = parse_arg!(args, as_u64, "value")?;
                let result = self.set_microphone_gain(value).await?;
                Ok(to_value(result)?)
            }
            "SetServiceAvailable" => {
                let service_available = Some(parse_arg!(args, as_bool, "value")?);
                let result = self
                    .update_network_information(NetworkInformation {
                        service_available,
                        ..NetworkInformation::EMPTY
                    })
                    .await?;
                Ok(to_value(result)?)
            }
            "SetRoaming" => {
                let roaming = Some(parse_arg!(args, as_bool, "value")?);
                let result = self
                    .update_network_information(NetworkInformation {
                        roaming,
                        ..NetworkInformation::EMPTY
                    })
                    .await?;
                Ok(to_value(result)?)
            }
            "SetSignalStrength" => {
                let signal_strength = parse_arg!(args, as_u64, "value")?;
                let signal_strength = Some(match signal_strength {
                    0 => SignalStrength::None,
                    1 => SignalStrength::VeryLow,
                    2 => SignalStrength::Low,
                    3 => SignalStrength::Medium,
                    4 => SignalStrength::High,
                    5 => SignalStrength::VeryHigh,
                    _ => panic!(""),
                });
                let result = self
                    .update_network_information(NetworkInformation {
                        signal_strength,
                        ..NetworkInformation::EMPTY
                    })
                    .await?;
                Ok(to_value(result)?)
            }
            "SetSubscriberNumber" => {
                let number = parse_arg!(args, as_str, "value")?;
                self.set_subscriber_number(number).await;
                Ok(to_value(())?)
            }
            "SetOperator" => {
                let value = parse_arg!(args, as_str, "value")?;
                self.set_operator(value).await;
                Ok(to_value(())?)
            }
            "SetNrecSupport" => {
                let support = parse_arg!(args, as_bool, "value")?;
                self.set_nrec_support(support).await;
                Ok(to_value(())?)
            }
            "SetBatteryLevel" => {
                let level = parse_arg!(args, as_u64, "value")?;
                self.set_battery_level(level).await?;
                Ok(to_value(())?)
            }
            "SetLastDialed" => {
                let number = parse_arg!(args, as_str, "number")?.to_string();
                self.set_last_dialed(Some(number)).await;
                Ok(to_value(())?)
            }
            "ClearLastDialed" => {
                self.set_last_dialed(None).await;
                Ok(to_value(())?)
            }
            "SetMemoryLocation" => {
                let location = parse_arg!(args, as_str, "location")?.to_string();
                let number = parse_arg!(args, as_str, "number")?.to_string();
                self.set_memory_location(location, Some(number)).await;
                Ok(to_value(())?)
            }
            "ClearMemoryLocation" => {
                let location = parse_arg!(args, as_str, "location")?.to_string();
                self.set_memory_location(location, None).await;
                Ok(to_value(())?)
            }
            "SetDialResult" => {
                use std::convert::TryInto;
                let number = parse_arg!(args, as_str, "number")?.to_string();
                let status = parse_arg!(args, as_i64, "status")?.try_into()?;
                let status = fuchsia_zircon::Status::from_raw(status);
                self.set_dial_result(number, status).await;
                Ok(to_value(())?)
            }
            "GetState" => {
                let result = self.get_state().await;
                Ok(to_value(result)?)
            }
            "SetConnectionBehavior" => {
                let autoconnect = parse_arg!(args, as_bool, "autoconnect")?;
                self.set_connection_behavior(autoconnect).await?;
                Ok(to_value(())?)
            }
            _ => bail!("Invalid Hfp FIDL method: {:?}", method),
        }
    }
}

#[async_trait(?Send)]
impl Facade for RfcommFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "RfcommInit" => {
                let result = self.advertise()?;
                Ok(to_value(result)?)
            }
            "RfcommRemoveService" => {
                let result = self.clear_services();
                Ok(to_value(result)?)
            }
            "DisconnectSession" => {
                let id = PeerId { value: parse_arg!(args, as_u64, "peer_id")? };
                let result = self.close_session(id.into())?;
                Ok(to_value(result)?)
            }
            "ConnectRfcommChannel" => {
                let id = PeerId { value: parse_arg!(args, as_u64, "peer_id")? };
                let channel_number = parse_arg!(args, as_u64, "server_channel_number")?;
                let channel_number = ServerChannel::try_from(u8::try_from(channel_number)?)?;
                let result = self.outgoing_rfcomm_channel(id.into(), channel_number).await?;
                Ok(to_value(result)?)
            }
            "DisconnectRfcommChannel" => {
                let id = PeerId { value: parse_arg!(args, as_u64, "peer_id")? };
                let channel_number = parse_arg!(args, as_u64, "server_channel_number")?;
                let channel_number = ServerChannel::try_from(u8::try_from(channel_number)?)?;
                let result = self.close_rfcomm_channel(id.into(), channel_number)?;
                Ok(to_value(result)?)
            }
            "SendRemoteLineStatus" => {
                let id = PeerId { value: parse_arg!(args, as_u64, "peer_id")? };
                let channel_number = parse_arg!(args, as_u64, "server_channel_number")?;
                let channel_number = ServerChannel::try_from(u8::try_from(channel_number)?)?;
                let result = self.send_rls(id.into(), channel_number)?;
                Ok(to_value(result)?)
            }
            "RfcommWrite" => {
                let id = PeerId { value: parse_arg!(args, as_u64, "peer_id")? };
                let channel_number = parse_arg!(args, as_u64, "server_channel_number")?;
                let channel_number = ServerChannel::try_from(u8::try_from(channel_number)?)?;
                let data = parse_arg!(args, as_str, "data")?.to_string();
                let result = self.send_user_data(id.into(), channel_number, data.into_bytes())?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid RFCOMM method: {:?}", method),
        }
    }
}
