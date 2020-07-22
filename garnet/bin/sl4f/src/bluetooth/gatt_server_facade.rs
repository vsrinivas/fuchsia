// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_bluetooth_gatt::{
    self as gatt, AttributePermissions, Characteristic, Descriptor,
    LocalServiceDelegateControlHandle, LocalServiceDelegateMarker,
    LocalServiceDelegateOnReadValueResponder, LocalServiceDelegateOnWriteValueResponder,
    LocalServiceDelegateRequestStream, LocalServiceMarker, LocalServiceProxy, SecurityRequirements,
    Server_Marker, Server_Proxy, ServiceInfo,
};
use fuchsia_async as fasync;
use fuchsia_component as app;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use futures::stream::TryStreamExt;
use parking_lot::RwLock;
use serde_json::value::Value;
use std::collections::HashMap;

use crate::bluetooth::constants::{
    GATT_MAX_ATTRIBUTE_VALUE_LENGTH, PERMISSION_READ_ENCRYPTED, PERMISSION_READ_ENCRYPTED_MITM,
    PERMISSION_WRITE_ENCRYPTED, PERMISSION_WRITE_ENCRYPTED_MITM, PERMISSION_WRITE_SIGNED,
    PERMISSION_WRITE_SIGNED_MITM, PROPERTY_INDICATE, PROPERTY_NOTIFY,
};

use crate::common_utils::error::Sl4fError;

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
    /// attribute_value_mapping: A Hashmap that will be used for capturing
    /// and updating Characteristic and Descriptor values for each service.
    /// The bool value represents whether value size of the initial Characteristic
    /// Descriptor value should be enforced or not for prepared writes. True
    /// for enforce, false to allow the size to grow to max values.
    attribute_value_mapping: HashMap<u64, (Vec<u8>, bool)>,

    /// A generic counter GATT server attributes
    generic_id_counter: Counter,

    /// The current Gatt Server Proxy
    server_proxy: Option<Server_Proxy>,

    /// service_proxies: List of LocalServiceProxy objects in use
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
                attribute_value_mapping: HashMap::new(),
                generic_id_counter: Counter::new(),
                server_proxy: None,
                service_proxies: Vec::new(),
            }),
        }
    }

    pub fn create_server_proxy(&self) -> Result<Server_Proxy, Error> {
        let tag = "GattServerFacade::create_server_proxy:";
        match self.inner.read().server_proxy.clone() {
            Some(service) => {
                fx_log_info!(tag: &[tag, &line!().to_string()].join(""), "Current service proxy: {:?}", service);
                Ok(service)
            }
            None => {
                fx_log_info!(tag: &[tag, &line!().to_string()].join(""), "Setting new server proxy");
                let service = app::client::connect_to_service::<Server_Marker>();
                if let Err(err) = service {
                    fx_log_err!(tag: &[tag, &line!().to_string()].join(""), "Failed to create server proxy: {:?}", err);
                    return Err(format_err!("Failed to create server proxy: {:?}", err));
                }
                service
            }
        }
    }

    /// Function to take the input attribute value and parse it to
    /// a byte array. Types can be Strings, u8, or generic Array.
    pub fn parse_attribute_value_to_byte_array(&self, value_to_parse: &Value) -> Vec<u8> {
        match value_to_parse {
            Value::String(obj) => String::from(obj.as_str()).into_bytes(),
            Value::Number(obj) => match obj.as_u64() {
                Some(num) => vec![num as u8],
                None => vec![],
            },
            Value::Array(obj) => {
                obj.into_iter().filter_map(|v| v.as_u64()).map(|v| v as u8).collect()
            }
            _ => vec![],
        }
    }

    pub fn on_characteristic_configuration(
        peer_id: String,
        notify: bool,
        indicate: bool,
        characteristic_id: u64,
        control_handle: LocalServiceDelegateControlHandle,
        service_proxy: &LocalServiceProxy,
    ) {
        let tag = "GattServerFacade::on_characteristic_configuration:";
        fx_log_info!(
            tag: &[tag, &line!().to_string()].join(""),
            "OnCharacteristicConfiguration: (notify: {}, indicate: {}, id: {})",
            notify,
            indicate,
            peer_id
        );
        control_handle.shutdown();
        let value: [u8; 2] = if indicate {
            [0x02, 0x00]
        } else if notify {
            [0x01, 0x00]
        } else {
            [0x00, 0x00]
        };
        let confirm = true;
        let _result = service_proxy.notify_value(characteristic_id, &peer_id, &value, confirm);
    }

    pub fn on_read_value(
        id: u64,
        offset: i32,
        responder: LocalServiceDelegateOnReadValueResponder,
        value_in_mapping: Option<&(Vec<u8>, bool)>,
    ) {
        let tag = "GattServerFacade::on_read_value:";
        fx_log_info!(
            tag: &[tag, &line!().to_string()].join(""),
            "OnReadValue request at id: {:?}, with offset: {:?}",
            id,
            offset
        );
        match value_in_mapping {
            Some(v) => {
                let (value, _enforce_initial_attribute_length) = v;
                if value.len() < offset as usize {
                    let _result = responder.send(None, gatt::ErrorCode::InvalidOffset);
                } else {
                    let value_to_write = value.clone().split_off(offset as usize);
                    let _result = responder.send(Some(&value_to_write), gatt::ErrorCode::NoError);
                }
            }
            None => {
                // ID doesn't exist in the database
                let _result = responder.send(None, gatt::ErrorCode::NotPermitted);
            }
        };
    }

    pub fn write_and_extend(value: &mut Vec<u8>, value_to_write: Vec<u8>, offset: usize) {
        let split_idx = (value.len() - offset).min(value_to_write.len());
        let (overlapping, extending) = value_to_write.split_at(split_idx);
        let end_of_overlap = offset + overlapping.len();
        value.splice(offset..end_of_overlap, overlapping.iter().cloned());
        value.extend_from_slice(extending);
    }

    pub fn on_write_value(
        id: u64,
        offset: u16,
        value_to_write: Vec<u8>,
        responder: LocalServiceDelegateOnWriteValueResponder,
        value_in_mapping: Option<&mut (Vec<u8>, bool)>,
    ) {
        let tag = "GattServerFacade::on_write_value:";
        fx_log_info!(
            tag: &[tag, &line!().to_string()].join(""),
            "OnWriteValue request at id: {:?}, with offset: {:?}, with value: {:?}",
            id,
            offset,
            value_to_write
        );

        match value_in_mapping {
            Some(v) => {
                let (value, enforce_initial_attribute_length) = v;
                let max_attribute_size: usize = match enforce_initial_attribute_length {
                    true => value.len(),
                    false => GATT_MAX_ATTRIBUTE_VALUE_LENGTH,
                };
                if max_attribute_size < (value_to_write.len() + offset as usize) {
                    let _result = responder.send(gatt::ErrorCode::InvalidValueLength);
                } else if value.len() < offset as usize {
                    let _result = responder.send(gatt::ErrorCode::InvalidOffset);
                } else {
                    &mut GattServerFacade::write_and_extend(value, value_to_write, offset as usize);
                    let _result = responder.send(gatt::ErrorCode::NoError);
                }
            }
            None => {
                // ID doesn't exist in the database
                let _result = responder.send(gatt::ErrorCode::NotPermitted);
            }
        }
    }

    pub fn on_write_without_response(
        id: u64,
        offset: u16,
        value_to_write: Vec<u8>,
        value_in_mapping: Option<&mut (Vec<u8>, bool)>,
    ) {
        let tag = "GattServerFacade::on_write_without_response:";
        fx_log_info!(
            tag: &[tag, &line!().to_string()].join(""),
            "OnWriteWithoutResponse request at id: {:?}, with offset: {:?}, with value: {:?}",
            id,
            offset,
            value_to_write
        );
        if let Some(v) = value_in_mapping {
            let (value, _enforce_initial_attribute_length) = v;
            &mut GattServerFacade::write_and_extend(value, value_to_write, offset as usize);
        }
    }

    pub async fn monitor_delegate_request_stream(
        stream: LocalServiceDelegateRequestStream,
        mut attribute_value_mapping: HashMap<u64, (Vec<u8>, bool)>,
        service_proxy: LocalServiceProxy,
    ) -> Result<(), Error> {
        use fidl_fuchsia_bluetooth_gatt::LocalServiceDelegateRequest::*;
        stream
            .map_ok(move |request| match request {
                OnCharacteristicConfiguration {
                    peer_id,
                    notify,
                    indicate,
                    characteristic_id,
                    control_handle,
                } => {
                    GattServerFacade::on_characteristic_configuration(
                        peer_id,
                        notify,
                        indicate,
                        characteristic_id,
                        control_handle,
                        &service_proxy,
                    );
                }
                OnReadValue { id, offset, responder } => {
                    GattServerFacade::on_read_value(
                        id,
                        offset,
                        responder,
                        attribute_value_mapping.get(&id),
                    );
                }
                OnWriteValue { id, offset, value, responder } => {
                    GattServerFacade::on_write_value(
                        id,
                        offset,
                        value,
                        responder,
                        attribute_value_mapping.get_mut(&id),
                    );
                }
                OnWriteWithoutResponse { id, offset, value, .. } => {
                    GattServerFacade::on_write_without_response(
                        id,
                        offset,
                        value,
                        attribute_value_mapping.get_mut(&id),
                    );
                }
            })
            .try_collect::<()>()
            .await
            .map_err(|e| e.into())
    }

    /// Convert a number representing permissions into AttributePermissions.
    ///
    /// Fuchsia GATT Server uses a u32 as a property value and an AttributePermissions
    /// object to represent Characteristic and Descriptor permissions. In order to
    /// simplify the incomming json object the incoming permission value will be
    /// treated as a u32 and converted into the proper AttributePermission object.
    ///
    /// The incoming permissions number is represented by adding the numbers representing
    /// the permission level.
    /// Values:
    /// 0x001 - Allow read permission
    /// 0x002 - Allow encrypted read operations
    /// 0x004 - Allow reading with man-in-the-middle protection
    /// 0x010 - Allow write permission
    /// 0x020 - Allow encrypted writes
    /// 0x040 - Allow writing with man-in-the-middle protection
    /// 0x080 - Allow signed writes
    /// 0x100 - Allow signed write perations with man-in-the-middle protection
    ///
    /// Example input that allows read and write: 0x01 | 0x10 = 0x11
    /// This function will convert this to the proper AttributePermission permissions.
    pub fn permissions_and_properties_from_raw_num(
        &self,
        permissions: u32,
        properties: u32,
    ) -> AttributePermissions {
        let mut read_encryption_required = false;
        let mut read_authentication_required = false;
        let mut read_authorization_required = false;

        let mut write_encryption_required = false;
        let mut write_authentication_required = false;
        let mut write_authorization_required = false;

        let mut update_encryption_required = false;
        let mut update_authentication_required = false;
        let mut update_authorization_required = false;

        if permissions & PERMISSION_READ_ENCRYPTED != 0 {
            read_encryption_required = true;
            read_authentication_required = true;
            read_authorization_required = true;
        }

        if permissions & PERMISSION_READ_ENCRYPTED_MITM != 0 {
            read_encryption_required = true;
            update_encryption_required = true;
        }

        if permissions & PERMISSION_WRITE_ENCRYPTED != 0 {
            write_encryption_required = true;
            update_encryption_required = true;
        }

        if permissions & PERMISSION_WRITE_ENCRYPTED_MITM != 0 {
            write_encryption_required = true;
            update_encryption_required = true;
            update_authentication_required = true;
            update_authorization_required = true;
        }

        if permissions & PERMISSION_WRITE_SIGNED != 0 {
            write_authorization_required = true;
        }

        if permissions & PERMISSION_WRITE_SIGNED_MITM != 0 {
            write_encryption_required = true;
            write_authentication_required = true;
            write_authorization_required = true;
            update_encryption_required = true;
            update_authentication_required = true;
            update_authorization_required = true;
        }

        // Update Security Requirements only required if notify or indicate
        // properties set.
        let update_sec_requirement = if properties & (PROPERTY_NOTIFY | PROPERTY_INDICATE) != 0 {
            Some(Box::new(SecurityRequirements {
                encryption_required: update_encryption_required,
                authentication_required: update_authentication_required,
                authorization_required: update_authorization_required,
            }))
        } else {
            None
        };

        let read_sec_requirement = SecurityRequirements {
            encryption_required: read_encryption_required,
            authentication_required: read_authentication_required,
            authorization_required: read_authorization_required,
        };

        let write_sec_requirement = SecurityRequirements {
            encryption_required: write_encryption_required,
            authentication_required: write_authentication_required,
            authorization_required: write_authorization_required,
        };

        AttributePermissions {
            read: Some(Box::new(read_sec_requirement)),
            write: Some(Box::new(write_sec_requirement)),
            update: update_sec_requirement,
        }
    }

    pub fn generate_descriptors(
        &self,
        descriptor_list_json: &Value,
    ) -> Result<Vec<Descriptor>, Error> {
        let mut descriptors: Vec<Descriptor> = Vec::new();
        // Fuchsia will automatically setup these descriptors and manage them.
        // Skip setting them up if found in the input descriptor list.
        let banned_descriptor_uuids = [
            "00002900-0000-1000-8000-00805f9b34fb".to_string(), // CCC Descriptor
            "00002902-0000-1000-8000-00805f9b34fb".to_string(), // Client Configuration Descriptor
            "00002903-0000-1000-8000-00805f9b34fb".to_string(), // Server Configuration Descriptor
        ];

        if descriptor_list_json.is_null() {
            return Ok(descriptors);
        }

        let descriptor_list = match descriptor_list_json.as_array() {
            Some(d) => d,
            None => return Err(format_err!("Attribute 'descriptors' is not a parseable list.")),
        };

        for descriptor in descriptor_list.into_iter() {
            let descriptor_uuid = match descriptor["uuid"].as_str() {
                Some(uuid) => uuid.to_string(),
                None => return Err(format_err!("Descriptor uuid was unable to cast to str.")),
            };
            let descriptor_value = self.parse_attribute_value_to_byte_array(&descriptor["value"]);

            let raw_enforce_enforce_initial_attribute_length =
                descriptor["enforce_initial_attribute_length"].as_bool().unwrap_or(false);

            // No properties for descriptors.
            let properties = 0u32;

            if banned_descriptor_uuids.contains(&descriptor_uuid) {
                continue;
            }

            let raw_descriptor_permissions = match descriptor["permissions"].as_u64() {
                Some(permissions) => permissions as u32,
                None => {
                    return Err(format_err!("Descriptor permissions was unable to cast to u64."))
                }
            };

            let desc_permission_attributes = self
                .permissions_and_properties_from_raw_num(raw_descriptor_permissions, properties);

            let descriptor_id = self.inner.write().generic_id_counter.next();
            self.inner.write().attribute_value_mapping.insert(
                descriptor_id,
                (descriptor_value, raw_enforce_enforce_initial_attribute_length),
            );
            let descriptor_obj = Descriptor {
                id: descriptor_id,
                type_: descriptor_uuid.clone(),
                permissions: Some(Box::new(desc_permission_attributes)),
            };

            descriptors.push(descriptor_obj);
        }
        Ok(descriptors)
    }

    pub fn generate_characteristics(
        &self,
        characteristic_list_json: &Value,
    ) -> Result<Vec<Characteristic>, Error> {
        let mut characteristics: Vec<Characteristic> = Vec::new();
        if characteristic_list_json.is_null() {
            return Ok(characteristics);
        }

        let characteristic_list = match characteristic_list_json.as_array() {
            Some(c) => c,
            None => {
                return Err(format_err!("Attribute 'characteristics' is not a parseable list."))
            }
        };

        for characteristic in characteristic_list.into_iter() {
            let characteristic_uuid = match characteristic["uuid"].as_str() {
                Some(uuid) => uuid.to_string(),
                None => return Err(format_err!("Characteristic uuid was unable to cast to str.")),
            };

            let characteristic_properties = match characteristic["properties"].as_u64() {
                Some(properties) => properties as u32,
                None => {
                    return Err(format_err!("Characteristic properties was unable to cast to u64."))
                }
            };

            let raw_characteristic_permissions = match characteristic["permissions"].as_u64() {
                Some(permissions) => permissions as u32,
                None => {
                    return Err(format_err!(
                        "Characteristic permissions was unable to cast to u64."
                    ))
                }
            };

            let characteristic_value =
                self.parse_attribute_value_to_byte_array(&characteristic["value"]);

            let raw_enforce_enforce_initial_attribute_length =
                characteristic["enforce_initial_attribute_length"].as_bool().unwrap_or(false);

            let descriptor_list = &characteristic["descriptors"];
            let descriptors = self.generate_descriptors(descriptor_list)?;

            let characteristic_permissions = self.permissions_and_properties_from_raw_num(
                raw_characteristic_permissions,
                characteristic_properties,
            );

            let characteristic_id = self.inner.write().generic_id_counter.next();
            self.inner.write().attribute_value_mapping.insert(
                characteristic_id,
                (characteristic_value, raw_enforce_enforce_initial_attribute_length),
            );
            let characteristic_obj = Characteristic {
                id: characteristic_id,
                type_: characteristic_uuid,
                properties: characteristic_properties,
                permissions: Some(Box::new(characteristic_permissions)),
                descriptors: Some(descriptors),
            };

            characteristics.push(characteristic_obj);
        }
        Ok(characteristics)
    }

    pub fn generate_service(&self, service_json: &Value) -> Result<ServiceInfo, Error> {
        // Determine if the service is primary or not.
        let service_id = self.inner.write().generic_id_counter.next();
        let service_type = &service_json["type"];
        let is_service_primary = match service_type.as_i64() {
            Some(val) => match val {
                0 => true,
                1 => false,
                _ => return Err(format_err!("Invalid Service type. Expected 0 or 1.")),
            },
            None => return Err(format_err!("Service type was unable to cast to i64.")),
        };

        // Get the service UUID.
        let service_uuid = match service_json["uuid"].as_str() {
            Some(s) => s,
            None => return Err(format_err!("Service uuid was unable to cast to str.")),
        };

        //Get the Characteristics from the service.
        let characteristics = self.generate_characteristics(&service_json["characteristics"])?;

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
        &self,
        mut service_info: ServiceInfo,
        service_uuid: String,
    ) -> Result<(), Error> {
        let tag = "GattServerFacade::publish_service:";
        let (service_local, service_remote) = zx::Channel::create()?;
        let service_local = fasync::Channel::from_channel(service_local)?;
        let service_proxy = LocalServiceProxy::new(service_local);

        let (delegate_client, delegate_request_stream) =
            fidl::endpoints::create_request_stream::<LocalServiceDelegateMarker>()?;

        let service_server = fidl::endpoints::ServerEnd::<LocalServiceMarker>::new(service_remote);

        self.inner.write().service_proxies.push(service_proxy.clone());

        match &self.inner.read().server_proxy {
            Some(server) => {
                let status = server
                    .publish_service(&mut service_info, delegate_client, service_server)
                    .await?;
                match status.error {
                    None => fx_log_info!( tag: &[tag, &line!().to_string()].join(""),
                        "Successfully published GATT service with uuid {:?}",
                        service_uuid
                    ),
                    Some(e) => {
                        return Err(format_err!(
                            "Failed to create GATT Service: {}",
                            Sl4fError::from(*e)
                        ))
                    }
                }
            }
            None => return Err(format_err!("No Server Proxy created.")),
        }
        let monitor_delegate_fut = GattServerFacade::monitor_delegate_request_stream(
            delegate_request_stream,
            self.inner.read().attribute_value_mapping.clone(),
            service_proxy,
        );
        let fut = async {
            let result = monitor_delegate_fut.await;
            if let Err(err) = result {
                fx_log_err!(tag: "publish_service",
                    "Failed to create or monitor the gatt service delegate: {:?}", err);
            }
        };
        fasync::Task::spawn(fut).detach();
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
        let tag = "GattServerFacade::publish_server:";
        fx_log_info!(tag: &[tag, &line!().to_string()].join(""), "Publishing service");
        let server_proxy = self.create_server_proxy()?;
        self.inner.write().server_proxy = Some(server_proxy);
        let database = args.get("database");
        let services = match database {
            Some(d) => match d.get("services") {
                Some(s) => s,
                None => return Err(format_err!("No services found.")),
            },
            None => {
                return Err(format_err!("Could not find the 'services' key in the json database."))
            }
        };

        let service_list = match services.as_array() {
            Some(s) => s,
            None => return Err(format_err!("Attribute 'service' is not a parseable list.")),
        };

        for service in service_list.into_iter() {
            self.inner.write().attribute_value_mapping.clear();
            let service_info = self.generate_service(service)?;
            let service_uuid = &service["uuid"];
            self.publish_service(service_info, service_uuid.to_string()).await?;
        }
        Ok(())
    }

    pub async fn close_server(&self) {
        self.inner.write().server_proxy = None
    }

    // GattServerFacade for cleaning up objects in use.
    pub fn cleanup(&self) {
        let tag = "GattServerFacade::cleanup:";
        fx_log_info!(tag: &[tag, &line!().to_string()].join(""), "Cleanup GATT server objects");
        let mut inner = self.inner.write();
        inner.server_proxy = None;
        inner.service_proxies.clear();
    }

    // GattServerFacade for printing useful information pertaining to the facade for
    // debug purposes.
    pub fn print(&self) {
        let tag = "GattServerFacade::print:";
        fx_log_info!(tag: &[tag, &line!().to_string()].join(""), "Unimplemented print function");
    }
}
