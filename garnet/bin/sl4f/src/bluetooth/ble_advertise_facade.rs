// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_bluetooth::{Appearance, Uuid};
use fidl_fuchsia_bluetooth_le::{
    AdvertisingData, AdvertisingHandleMarker, AdvertisingParameters, ConnectionOptions,
    ManufacturerData, PeripheralMarker, PeripheralProxy, ServiceData,
};
use fuchsia_component as app;
use fuchsia_syslog::macros::*;
use parking_lot::RwLock;

// Sl4f-Constants and Ble advertising related functionality
use crate::common_utils::error::Sl4fError;

use serde_json::Value;

use std::convert::TryInto;

#[derive(Debug)]
struct InnerBleAdvertiseFacade {
    /// Advertisement ID of device, only one advertisement at a time.
    // TODO(fxbug.dev/807): Potentially scale up to storing multiple AdvertisingHandles. We may want to
    // generate unique identifiers for each advertisement and to allow RPC clients to manage them.
    adv_handle: Option<ClientEnd<AdvertisingHandleMarker>>,

    ///PeripheralProxy used for Bluetooth Connections
    peripheral: Option<PeripheralProxy>,
}

/// Starts and stops device BLE advertisement(s).
/// Note this object is shared among all threads created by server.
#[derive(Debug)]
pub struct BleAdvertiseFacade {
    inner: RwLock<InnerBleAdvertiseFacade>,
}

impl BleAdvertiseFacade {
    pub fn new() -> BleAdvertiseFacade {
        BleAdvertiseFacade {
            inner: RwLock::new(InnerBleAdvertiseFacade { adv_handle: None, peripheral: None }),
        }
    }

    /// Parse json input UUID and return Bluetooth UUID format.
    ///
    /// # Arguments
    /// * `json_uuid`: The JSON UUID in the form of a list of bytes.
    fn parse_uuid(&self, json_uuid: &Value) -> Result<Uuid, Error> {
        let mut byte_list = vec![];

        for byte_string in json_uuid.as_array().unwrap() {
            let raw_value = match i64::from_str_radix(byte_string.as_str().unwrap(), 16) {
                Ok(v) => v as u8,
                Err(e) => bail!("Failed to convert raw value with: {:?}", e),
            };
            byte_list.push(raw_value);
        }
        Ok(Uuid { value: byte_list.as_slice().try_into().expect("Failed to set UUID value.") })
    }

    /// Parse a list of service uuids from a json input list.
    ///
    /// # Arguments
    /// * `json_service_uuids`: The JSON UUIDs in the form of lists of list of bytes.
    fn parse_service_uuids(&self, json_service_uuids: &Vec<Value>) -> Result<Vec<Uuid>, Error> {
        let mut uuid_list = Vec::new();

        for raw_uuid_list in json_service_uuids {
            uuid_list.push(self.parse_uuid(raw_uuid_list)?);
        }
        Ok(uuid_list)
    }

    /// Parse the json input service data into a list of ServiceData
    ///
    /// # Arguments
    /// * `json_service_data`: The JSON representation of ServiceData to parse.
    fn parse_service_data(
        &self,
        json_service_data: &Vec<Value>,
    ) -> Result<Vec<ServiceData>, Error> {
        let mut manufacturer_data_list = Vec::new();

        for raw_service_data in json_service_data {
            let uuid = match raw_service_data.get("uuid") {
                Some(v) => self.parse_uuid(v)?,
                None => bail!("Missing Service data info 'uuid'."),
            };
            let data = match raw_service_data.get("data") {
                Some(d) => d.to_string().into_bytes(),
                None => bail!("Missing Service data info 'data'."),
            };
            manufacturer_data_list.push(ServiceData { uuid, data });
        }
        Ok(manufacturer_data_list)
    }

    /// Parse the json input manufacturer data into a list of ManufacturerData
    ///
    /// # Arguments
    /// * `json_manufacturer_data`: The JSON representation of ManufacturerData to parse.
    fn parse_manufacturer_data(
        &self,
        json_manufacturer_data: &Vec<Value>,
    ) -> Result<Vec<ManufacturerData>, Error> {
        let mut manufacturer_data_list = Vec::new();

        for raw_manufacturer_data in json_manufacturer_data {
            let company_id = match raw_manufacturer_data.get("id") {
                Some(v) => match v.as_u64() {
                    Some(c) => c as u16,
                    None => bail!("Company id not a valid value."),
                },
                None => bail!("Missing Manufacturer info 'id'."),
            };
            let data = match raw_manufacturer_data.get("data") {
                Some(d) => d.to_string().into_bytes(),
                None => bail!("Missing Manufacturer info 'data'."),
            };
            manufacturer_data_list.push(ManufacturerData { company_id, data });
        }
        Ok(manufacturer_data_list)
    }

    /// Function to parse relevant advertising data based on json input.
    ///
    /// # Arguments
    /// * `data` - A serde json object representing the Advertising data.
    ///
    /// Note: A human readable uuid is represented as a list of bytes:
    ///     Example Human Readable UUID: '00001801-0000-1000-8000-00805f9b34fb'
    ///     Actual input:
    ///         ['fb', '34', '9b', '5f', '80', '00', '00', '80', '00', '10', '00', '00', '01', '18', '00', '00']
    /// Example input
    /// data = {
    ///     'advertising_data': {
    ///         'name': Some(String),
    ///         'appearance': Some(u64),
    ///         'service_data': {
    ///             'uuid': ['fb', '34', '9b', '5f', '80', '00', '00', '80', '00', '10', '00', '00', '01', '18', '00', '00'].
    ///             'data': "1"
    ///         },
    ///         'service_uuids': [
    ///             ['fb', '34', '9b', '5f', '80', '00', '00', '80', '00', '10', '00', '00', '01', '18', '00', '00'].
    ///             ['fb', '34', '9b', '5f', '80', '00', '00', '80', '00', '10', '00', '00', '00', '18', '00', '00']
    ///         ],
    ///         'manufacturer_data': {
    ///             'id': 10,
    ///             'data'
    ///         },
    ///         'uris': Some(['telnet://192.0.2.16:80/']),
    ///         'tx_power_level': Some(1),
    ///
    ///     }
    ///
    /// }
    fn generate_advertising_data(&self, data: Value) -> Result<Option<AdvertisingData>, Error> {
        let name: Option<String> = data["name"].as_str().map(String::from);
        let service_uuids = match data.get("service_uuids") {
            Some(v) => {
                if v.is_null() {
                    None
                } else {
                    match v.clone().as_array() {
                        Some(list) => Some(self.parse_service_uuids(list)?),
                        None => bail!("Attribute 'service_uuids' is not a parseable list."),
                    }
                }
            }
            None => None,
        };

        let appearance = match data.get("appearance") {
            Some(v) => {
                if v.is_null() {
                    None
                } else {
                    match v.as_u64() {
                        Some(c) => Appearance::from_primitive(c as u16),
                        None => None,
                    }
                }
            }
            None => bail!("Value 'appearance' missing."),
        };

        let tx_power_level = match data.get("tx_power_level") {
            Some(v) => {
                if v.is_null() {
                    None
                } else {
                    match v.as_u64() {
                        Some(c) => Some(c as i8),
                        None => None,
                    }
                }
            }
            None => bail!("Value 'tx_power_level' missing."),
        };

        let service_data = match data.get("service_data") {
            Some(raw) => {
                if raw.is_null() {
                    None
                } else {
                    match raw.as_array() {
                        Some(list) => Some(self.parse_service_data(list)?),
                        None => None,
                    }
                }
            }
            None => bail!("Value 'service_data' missing."),
        };

        let manufacturer_data = match data.get("manufacturer_data") {
            Some(raw) => {
                if raw.is_null() {
                    None
                } else {
                    match raw.as_array() {
                        Some(list) => Some(self.parse_manufacturer_data(list)?),
                        None => None,
                    }
                }
            }
            None => bail!("Value 'manufacturer_data' missing."),
        };

        let uris = match data.get("uris") {
            Some(raw) => {
                if raw.is_null() {
                    None
                } else {
                    match raw.as_array() {
                        Some(list) => {
                            let mut uri_list = Vec::new();
                            for item in list {
                                match item.as_str() {
                                    Some(i) => uri_list.push(String::from(i)),
                                    None => bail!("Expected URI string"),
                                }
                            }
                            Some(uri_list)
                        }
                        None => None,
                    }
                }
            }
            None => bail!("Value 'uris' missing."),
        };

        Ok(Some(AdvertisingData {
            name,
            appearance,
            tx_power_level,
            service_uuids,
            service_data,
            manufacturer_data,
            uris: uris,
        }))
    }

    // Takes a serde_json::Value and converts it to arguments required for
    // a FIDL ble_advertise command
    fn ble_advertise_args_to_fidl(&self, args_raw: Value) -> Result<AdvertisingParameters, Error> {
        let advertising_data = match args_raw.get("advertising_data") {
            Some(adr) => self.generate_advertising_data(adr.clone())?,

            None => bail!("Value 'advertising_data' missing."),
        };

        let scan_response = match args_raw.get("scan_response") {
            Some(scn) => {
                if scn.is_null() {
                    None
                } else {
                    self.generate_advertising_data(scn.clone())?
                }
            }
            None => None,
        };

        let conn_raw = args_raw.get("connectable").ok_or(format_err!("Connectable missing"))?;

        let connectable: bool = conn_raw.as_bool().unwrap_or(false);

        let conn_opts = if connectable {
            Some(ConnectionOptions { bondable_mode: Some(true), service_filter: None })
        } else {
            None
        };

        let parameters = AdvertisingParameters {
            data: advertising_data,
            scan_response: scan_response,
            mode_hint: None,
            connectable: None,
            connection_options: conn_opts,
        };

        fx_log_info!(tag: "ble_advertise_args_to_fidl", "advertising parameters: {:?}", parameters);
        Ok(parameters)
    }

    // Store a new advertising handle.
    fn set_adv_handle(&self, adv_handle: Option<ClientEnd<AdvertisingHandleMarker>>) {
        if adv_handle.is_some() {
            fx_log_info!(tag: "set_adv_handle", "Assigned new advertising handle");
        } else {
            fx_log_info!(tag: "set_adv_handle", "Cleared advertising handle");
        }
        self.inner.write().adv_handle = adv_handle;
    }

    pub fn print(&self) {
        let adv_status = match &self.inner.read().adv_handle {
            Some(_) => "Valid",
            None => "None",
        };
        fx_log_info!(tag: "print",
            "BleAdvertiseFacade: Adv Status: {}, Peripheral: {:?}",
            adv_status,
            self.get_peripheral_proxy(),
        );
    }

    // Set the peripheral proxy only if none exists, otherwise, use existing
    pub fn set_peripheral_proxy(&self) {
        let new_peripheral = match self.inner.read().peripheral.clone() {
            Some(p) => {
                fx_log_warn!(tag: "set_peripheral_proxy",
                    "Current peripheral: {:?}",
                    p,
                );
                Some(p)
            }
            None => {
                let peripheral_svc: PeripheralProxy =
                    app::client::connect_to_service::<PeripheralMarker>()
                        .context("Failed to connect to BLE Peripheral service.")
                        .unwrap();
                Some(peripheral_svc)
            }
        };

        self.inner.write().peripheral = new_peripheral
    }

    /// Start BLE advertisement
    ///
    /// # Arguments
    /// * `args`: A JSON input representing advertisement characteristics.
    pub async fn start_adv(&self, args: Value) -> Result<(), Error> {
        self.set_peripheral_proxy();
        let parameters = self.ble_advertise_args_to_fidl(args)?;
        let periph = &self.inner.read().peripheral.clone();
        match &periph {
            Some(p) => {
                let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
                let result = p.start_advertising(parameters, handle_remote).await?;
                if let Err(err) = result {
                    fx_log_err!(tag: "start_adv", "Failed to start adveritising: {:?}", err);
                    return Err(Sl4fError::new(&format!("{:?}", err)).into());
                }
                fx_log_info!(tag: "start_adv", "Started advertising");
                self.set_adv_handle(Some(handle));
                Ok(())
            }
            None => {
                fx_log_err!(tag: "start_adv", "No peripheral created.");
                return Err(format_err!("No peripheral proxy created."));
            }
        }
    }

    pub fn stop_adv(&self) {
        fx_log_info!(tag: "stop_adv", "Stop advertising");
        self.set_adv_handle(None);
    }

    pub fn get_peripheral_proxy(&self) -> Option<PeripheralProxy> {
        self.inner.read().peripheral.clone()
    }

    pub fn cleanup_advertisements(&self) {
        self.set_adv_handle(None);
    }

    // Close peripheral proxy
    pub fn cleanup_peripheral_proxy(&self) {
        self.inner.write().peripheral = None;
    }

    // Close both central and peripheral proxies
    pub fn cleanup(&self) {
        self.cleanup_advertisements();
        self.cleanup_peripheral_proxy();
    }
}
