// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::sl4f::macros::dtag;
use failure::{bail, Error};
use fidl_fuchsia_bluetooth_gatt::{
    Characteristic, LocalServiceDelegateMarker, LocalServiceMarker, LocalServiceProxy,
    Server_Marker, Server_Proxy, ServiceInfo,
};
use fuchsia_app as app;
use fuchsia_async as fasync;
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use parking_lot::RwLock;
use regex::Regex;
use serde_json::value::Value;

#[derive(Debug)]
struct Counter {
    count: u64,
}

impl Counter {
    pub fn new() -> Counter {
        Counter { count: 0 }
    }

    fn next(&mut self) -> u64 {
        let id: u64 = self.count;
        self.count += 1;
        id
    }
}

#[derive(Debug)]
struct InnerGattServerFacade {
    // A generic counter GATT server attributes
    generic_id_counter: Counter,

    // The current Gatt Server Proxy
    server_proxy: Option<Server_Proxy>,

    // service_proxies: List of LocalServiceProxy objects in use
    service_proxies: Vec<LocalServiceProxy>,
}

/// Perform Gatt Server operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct GattServerFacade {
    inner: RwLock<InnerGattServerFacade>,
}

impl GattServerFacade {
    pub fn new() -> GattServerFacade {
        GattServerFacade {
            inner: RwLock::new(InnerGattServerFacade {
                generic_id_counter: Counter::new(),
                server_proxy: None,
                service_proxies: Vec::new(),
            }),
        }
    }

    pub fn create_server_proxy(&self) -> Result<Server_Proxy, Error> {
        match self.inner.read().server_proxy.clone() {
            Some(service) => {
                fx_log_info!(tag: &dtag!(), "Current service proxy: {:?}", service);
                Ok(service)
            }
            None => {
                fx_log_info!(tag: &dtag!(), "Setting new server proxy");
                let service = app::client::connect_to_service::<Server_Marker>();
                if let Err(err) = service {
                    fx_log_err!(tag: &dtag!(), "Failed to create server proxy: {:?}", err);
                    bail!("Failed to create server proxy: {:?}", err);
                }
                service
            }
        }
    }

    pub fn generate_characteristics(
        &self, characteristic_list_json: &Value,
    ) -> Vec<Characteristic> {
        // TODO: Parse characteristic_json_list to create Characteristics.
        fx_log_info!(
            tag: &dtag!(),
            "Generating characteristics from json input {:?}",
            characteristic_list_json
        );
        let characteristics: Vec<Characteristic> = Vec::new();
        characteristics
    }

    pub fn generate_service(&self, service_json: &Value) -> Result<ServiceInfo, Error> {
        // Determine if the service is primary or not.
        let service_id = self.inner.write().generic_id_counter.next();
        let service_type = &service_json["type"];
        let is_service_primary = match service_type.as_i64() {
            Some(val) => match val {
                0 => true,
                1 => false,
                _ => bail!("Invalid Service type. Expected 0 or 1."),
            },
            None => bail!("Service type was unable to cast to i64."),
        };

        // Get the service UUID.
        let service_uuid = match service_json["uuid"].as_str() {
            Some(s) => s,
            None => bail!("Service uuid was unable to cast to str.")
        };

        //Get the Characteristics from the service.
        let characteristics = self.generate_characteristics(&service_json["characteristics"]);

        // Includes: TBD
        let includes = None;

        Ok(ServiceInfo {
            id: service_id,
            primary: is_service_primary,
            type_: service_uuid.to_string(),
            characteristics: Some(characteristics),
            includes: includes,
        })
    }

    pub async fn publish_service(
        &self, mut service_info: ServiceInfo, service_uuid: String,
    ) -> Result<(), Error> {
        let (service_local, service_remote) = zx::Channel::create()?;
        let service_local = fasync::Channel::from_channel(service_local)?;
        let service_proxy = LocalServiceProxy::new(service_local);

        // _delegate_request_stream will be used in creating a GATT service delegate in the future.
        let (delegate_client, _delegate_request_stream) =
            fidl::endpoints::create_request_stream::<LocalServiceDelegateMarker>()?;

        let service_server = fidl::endpoints::ServerEnd::<LocalServiceMarker>::new(service_remote);

        self.inner.write().service_proxies.push(service_proxy);

        match &self.inner.read().server_proxy {
            Some(server) => {
                let status = await!(server.publish_service(
                    &mut service_info,
                    delegate_client,
                    service_server
                ))?;
                match status.error {
                    None => fx_log_info!(
                        "Successfully published GATT service with uuid {:?}",
                        service_uuid
                    ),
                    Some(e) => bail!("Failed to create GATT Service: {}", BTError::from(*e)),
                }
            }
            None => bail!("No Server Proxy created."),
        }
        Ok(())
    }

    /// Publish a GATT Server.
    ///
    /// The input is a JSON object representing the attributes of the GATT
    /// server Database to setup. This function will also start listening for
    /// incoming requests to Characteristics and Descriptors in each Service.
    ///
    /// This is primarially using the same input syntax as in the Android AOSP
    /// ACTS test framework at:
    /// <aosp_root>/tools/test/connectivity/acts/framework/acts/test_utils/bt/gatt_test_database.py
    ///
    ///
    /// Example python dictionary that's turned into JSON (sub dic values can be found
    /// in <aosp_root>/tools/test/connectivity/acts/framework/acts/test_utils/bt/bt_constants.py:
    ///
    /// SMALL_DATABASE = {
    ///     'services': [{
    ///         'uuid': '00001800-0000-1000-8000-00805f9b34fb',
    ///         'type': gatt_service_types['primary'],
    ///         'characteristics': [{
    ///             'uuid': gatt_char_types['device_name'],
    ///             'properties': gatt_characteristic['property_read'],
    ///             'permissions': gatt_characteristic['permission_read'],
    ///             'handle': 0x0003,
    ///             'value_type': gatt_characteristic_value_format['string'],
    ///             'value': 'Test Database'
    ///         }, {
    ///             'uuid': gatt_char_types['appearance'],
    ///             'properties': gatt_characteristic['property_read'],
    ///             'permissions': gatt_characteristic['permission_read'],
    ///             'handle': 0x0005,
    ///             'value_type': gatt_characteristic_value_format['sint32'],
    ///             'offset': 0,
    ///             'value': 17
    ///         }, {
    ///             'uuid': gatt_char_types['peripheral_pref_conn'],
    ///             'properties': gatt_characteristic['property_read'],
    ///             'permissions': gatt_characteristic['permission_read'],
    ///             'handle': 0x0007
    ///         }]
    ///     }, {
    ///         'uuid': '00001801-0000-1000-8000-00805f9b34fb',
    ///         'type': gatt_service_types['primary'],
    ///         'characteristics': [{
    ///             'uuid': gatt_char_types['service_changed'],
    ///             'properties': gatt_characteristic['property_indicate'],
    ///             'permissions': gatt_characteristic['permission_read'] |
    ///             gatt_characteristic['permission_write'],
    ///             'handle': 0x0012,
    ///             'value_type': gatt_characteristic_value_format['byte'],
    ///             'value': [0x0000],
    ///             'descriptors': [{
    ///                 'uuid': gatt_char_desc_uuids['client_char_cfg'],
    ///                 'permissions': gatt_descriptor['permission_read'] |
    ///                 gatt_descriptor['permission_write'],
    ///                 'value': [0x0000]
    ///             }]
    ///         }, {
    ///             'uuid': '0000b004-0000-1000-8000-00805f9b34fb',
    ///             'properties': gatt_characteristic['property_read'],
    ///             'permissions': gatt_characteristic['permission_read'],
    ///             'handle': 0x0015,
    ///             'value_type': gatt_characteristic_value_format['byte'],
    ///             'value': [0x04]
    ///         }]
    ///     }]
    /// }
    pub async fn publish_server(&self, args: Value) -> Result<(), Error> {
        fx_log_info!(tag: &dtag!(), "Publishing service");
        let server_proxy = self.create_server_proxy()?;
        self.inner.write().server_proxy = Some(server_proxy);
        let database = args.get("database");
        let services = match database {
            Some(d) => match d.get("services") {
                Some(s) => s,
                None => bail!("No services found.")
            },
            None => bail!("Could not find the 'services' key in the json database."),
        };

        let service_list = match services.as_array() {
            Some(s) => s,
            None => bail!("Attribute 'service' is not a parseable list.")
        };

        for service in service_list.into_iter() {
            let service_info = self.generate_service(service)?;
            let service_uuid = &service["uuid"];
            await!(self.publish_service(service_info, service_uuid.to_string()))?;
        }
        Ok(())
    }

    // GattServerFacade for cleaning up objects in use.
    pub fn cleanup(&self) {
        fx_log_info!(tag: &dtag!(), "Cleanup GATT server objects");
        let mut inner = self.inner.write();
        inner.server_proxy = None;
        inner.service_proxies.clear();
    }

    // GattServerFacade for printing useful information pertaining to the facade for
    // debug purposes.
    pub fn print(&self) {
        fx_log_info!(tag: &dtag!(), "Unimplemented print function");
    }
}
