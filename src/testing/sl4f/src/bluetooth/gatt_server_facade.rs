// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl::prelude::*;
use fidl_fuchsia_bluetooth_gatt2::{
    self as gatt, AttributePermissions, Characteristic, CharacteristicPropertyBits, Descriptor,
    Handle, LocalServiceControlHandle, LocalServiceMarker, LocalServiceReadValueResponder,
    LocalServiceRequest, LocalServiceRequestStream, LocalServiceWriteValueResponder,
    SecurityRequirements, Server_Marker, Server_Proxy, ServiceHandle, ServiceInfo, ServiceKind,
    ValueChangedParameters,
};
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::{PeerId, Uuid};
use fuchsia_component as app;
use futures::stream::TryStreamExt;
use parking_lot::RwLock;
use serde_json::value::Value;
use std::{collections::HashMap, str::FromStr};
use tracing::{error, info, warn};

use crate::bluetooth::constants::{
    CHARACTERISTIC_EXTENDED_PROPERTIES_UUID, GATT_MAX_ATTRIBUTE_VALUE_LENGTH,
    PERMISSION_READ_ENCRYPTED, PERMISSION_READ_ENCRYPTED_MITM, PERMISSION_WRITE_ENCRYPTED,
    PERMISSION_WRITE_ENCRYPTED_MITM, PERMISSION_WRITE_SIGNED, PERMISSION_WRITE_SIGNED_MITM,
    PROPERTY_INDICATE, PROPERTY_NOTIFY, PROPERTY_READ, PROPERTY_WRITE,
};

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

    /// List of active LocalService server tasks.
    service_tasks: Vec<fasync::Task<()>>,
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
                service_tasks: vec![],
            }),
        }
    }

    fn create_server_proxy(&self) -> Result<Server_Proxy, Error> {
        let tag = "GattServerFacade::create_server_proxy:";
        match self.inner.read().server_proxy.clone() {
            Some(service) => {
                info!(
                    tag = &[tag, &line!().to_string()].join("").as_str(),
                    "Current service proxy: {:?}", service
                );
                Ok(service)
            }
            None => {
                info!(
                    tag = &[tag, &line!().to_string()].join("").as_str(),
                    "Setting new server proxy"
                );
                let service = app::client::connect_to_protocol::<Server_Marker>();
                if let Err(err) = service {
                    error!(
                        tag = &[tag, &line!().to_string()].join("").as_str(),
                        ?err,
                        "Failed to create server proxy"
                    );
                    return Err(format_err!("Failed to create server proxy: {:?}", err));
                }
                service
            }
        }
    }

    /// Function to take the input attribute value and parse it to
    /// a byte array. Types can be Strings, u8, or generic Array.
    fn parse_attribute_value_to_byte_array(&self, value_to_parse: &Value) -> Vec<u8> {
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

    fn on_characteristic_configuration(
        peer_id: PeerId,
        handle: Handle,
        notify: bool,
        indicate: bool,
        control_handle: &LocalServiceControlHandle,
    ) {
        let tag = "GattServerFacade::on_characteristic_configuration:";
        info!(
            tag = &[tag, &line!().to_string()].join("").as_str(),
            %notify,
            %indicate,
            id = %peer_id,
            "OnCharacteristicConfiguration"
        );

        if indicate {
            let value = ValueChangedParameters {
                handle: Some(handle),
                value: Some(vec![0x02, 0x00]),
                peer_ids: Some(vec![peer_id.into()]),
                ..ValueChangedParameters::EMPTY
            };
            // Ignore the confirmation.
            let (confirmation, _) = fidl::EventPair::create().unwrap();
            let _ = control_handle.send_on_indicate_value(value, confirmation);
        } else if notify {
            let value = ValueChangedParameters {
                handle: Some(handle),
                value: Some(vec![0x01, 0x00]),
                peer_ids: Some(vec![peer_id.into()]),
                ..ValueChangedParameters::EMPTY
            };
            let _ = control_handle.send_on_notify_value(value);
        }
    }

    fn on_read_value(
        peer_id: PeerId,
        handle: Handle,
        offset: i32,
        responder: LocalServiceReadValueResponder,
        value_in_mapping: Option<&(Vec<u8>, bool)>,
    ) {
        let tag = "GattServerFacade::on_read_value:";
        info!(
            tag = &[tag, &line!().to_string()].join("").as_str(),
            at_id = ?handle.value,
            offset = ?offset,
            id = %peer_id,
            "OnReadValue request",
        );
        match value_in_mapping {
            Some(v) => {
                let (value, _enforce_initial_attribute_length) = v;
                if value.len() < offset as usize {
                    let _result = responder.send(&mut Err(gatt::Error::InvalidOffset));
                } else {
                    let value_to_write = value.clone().split_off(offset as usize);
                    let _result = responder.send(&mut Ok(value_to_write));
                }
            }
            None => {
                // ID doesn't exist in the database
                let _result = responder.send(&mut Err(gatt::Error::ReadNotPermitted));
            }
        };
    }

    fn write_and_extend(value: &mut Vec<u8>, value_to_write: Vec<u8>, offset: usize) {
        let split_idx = (value.len() - offset).min(value_to_write.len());
        let (overlapping, extending) = value_to_write.split_at(split_idx);
        let end_of_overlap = offset + overlapping.len();
        value.splice(offset..end_of_overlap, overlapping.iter().cloned());
        value.extend_from_slice(extending);
    }

    fn on_write_value(
        peer_id: PeerId,
        handle: Handle,
        offset: u32,
        value_to_write: Vec<u8>,
        responder: LocalServiceWriteValueResponder,
        value_in_mapping: Option<&mut (Vec<u8>, bool)>,
    ) {
        let tag = "GattServerFacade::on_write_value:";
        info!(
            tag = &[tag, &line!().to_string()].join("").as_str(),
            at_id = handle.value,
            offset = offset,
            value = ?value_to_write,
            id = %peer_id,
            "OnWriteValue request",
        );

        match value_in_mapping {
            Some(v) => {
                let (value, enforce_initial_attribute_length) = v;
                let max_attribute_size: usize = match enforce_initial_attribute_length {
                    true => value.len(),
                    false => GATT_MAX_ATTRIBUTE_VALUE_LENGTH,
                };
                if max_attribute_size < (value_to_write.len() + offset as usize) {
                    let _result =
                        responder.send(&mut Err(gatt::Error::InvalidAttributeValueLength));
                } else if value.len() < offset as usize {
                    let _result = responder.send(&mut Err(gatt::Error::InvalidOffset));
                } else {
                    GattServerFacade::write_and_extend(value, value_to_write, offset as usize);
                    let _result = responder.send(&mut Ok(()));
                }
            }
            None => {
                // ID doesn't exist in the database
                let _result = responder.send(&mut Err(gatt::Error::WriteNotPermitted));
            }
        }
    }

    async fn monitor_service_request_stream(
        stream: LocalServiceRequestStream,
        control_handle: LocalServiceControlHandle,
        mut attribute_value_mapping: HashMap<u64, (Vec<u8>, bool)>,
    ) -> Result<(), Error> {
        stream
            .map_ok(move |request| match request {
                LocalServiceRequest::CharacteristicConfiguration {
                    peer_id,
                    handle,
                    notify,
                    indicate,
                    responder,
                } => {
                    GattServerFacade::on_characteristic_configuration(
                        peer_id.into(),
                        handle,
                        notify,
                        indicate,
                        &control_handle,
                    );
                    let _ = responder.send();
                }
                LocalServiceRequest::ReadValue { peer_id, handle, offset, responder } => {
                    GattServerFacade::on_read_value(
                        peer_id.into(),
                        handle,
                        offset,
                        responder,
                        attribute_value_mapping.get(&handle.value),
                    );
                }
                LocalServiceRequest::WriteValue { payload, responder } => {
                    GattServerFacade::on_write_value(
                        payload.peer_id.unwrap().into(),
                        payload.handle.unwrap(),
                        payload.offset.unwrap(),
                        payload.value.unwrap(),
                        responder,
                        attribute_value_mapping.get_mut(&payload.handle.unwrap().value),
                    );
                }
                LocalServiceRequest::PeerUpdate { payload: _, responder } => {
                    responder.drop_without_shutdown();
                }
                LocalServiceRequest::ValueChangedCredit { .. } => {}
            })
            .try_collect::<()>()
            .await
            .map_err(|e| e.into())
    }

    /// Convert a number representing permissions into AttributePermissions.
    ///
    /// Fuchsia GATT Server uses a u32 as a property value and an AttributePermissions
    /// object to represent Characteristic and Descriptor permissions. In order to
    /// simplify the incoming json object the incoming permission value will be
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
    fn permissions_and_properties_from_raw_num(
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
            Some(SecurityRequirements {
                encryption_required: Some(update_encryption_required),
                authentication_required: Some(update_authentication_required),
                authorization_required: Some(update_authorization_required),
                ..SecurityRequirements::EMPTY
            })
        } else {
            None
        };

        let read_sec_requirement = if properties & PROPERTY_READ != 0 {
            Some(SecurityRequirements {
                encryption_required: Some(read_encryption_required),
                authentication_required: Some(read_authentication_required),
                authorization_required: Some(read_authorization_required),
                ..SecurityRequirements::EMPTY
            })
        } else {
            None
        };

        let write_sec_requirement = if properties & PROPERTY_WRITE != 0 {
            Some(SecurityRequirements {
                encryption_required: Some(write_encryption_required),
                authentication_required: Some(write_authentication_required),
                authorization_required: Some(write_authorization_required),
                ..SecurityRequirements::EMPTY
            })
        } else {
            None
        };

        AttributePermissions {
            read: read_sec_requirement,
            write: write_sec_requirement,
            update: update_sec_requirement,
            ..AttributePermissions::EMPTY
        }
    }

    /// Converts `descriptor_list_json` to FIDL descriptors and filters out descriptors banned by
    /// the Server FIDL API. The Characteristic Extended Properties descriptor is one such banned
    /// descriptor, and its value will be returned.
    ///
    /// Returns a tuple of (filtered FIDL descriptors, extended property bits)
    fn process_descriptors(
        &self,
        descriptor_list_json: &Value,
    ) -> Result<(Vec<Descriptor>, CharacteristicPropertyBits), Error> {
        let mut descriptors: Vec<Descriptor> = Vec::new();
        // Fuchsia will automatically setup these descriptors and manage them.
        // Skip setting them up if found in the input descriptor list.
        let banned_descriptor_uuids = [
            Uuid::from_str("00002900-0000-1000-8000-00805f9b34fb").unwrap(), // CCC Descriptor
            Uuid::from_str("00002902-0000-1000-8000-00805f9b34fb").unwrap(), // Client Configuration Descriptor
            Uuid::from_str("00002903-0000-1000-8000-00805f9b34fb").unwrap(), // Server Configuration Descriptor
        ];

        if descriptor_list_json.is_null() {
            return Ok((descriptors, CharacteristicPropertyBits::empty()));
        }

        let descriptor_list = descriptor_list_json
            .as_array()
            .ok_or(format_err!("Attribute 'descriptors' is not a parseable list."))?;

        let mut ext_property_bits = CharacteristicPropertyBits::empty();

        for descriptor in descriptor_list.into_iter() {
            let descriptor_uuid: Uuid = match descriptor["uuid"].as_str() {
                Some(uuid_str) => Uuid::from_str(uuid_str)
                    .map_err(|_| format_err!("Descriptor uuid is invalid"))?,
                None => return Err(format_err!("Descriptor uuid was unable to cast to str.")),
            };
            let descriptor_value = self.parse_attribute_value_to_byte_array(&descriptor["value"]);

            // Intercept the Extended Properties descriptor.
            if descriptor_uuid == Uuid::new16(CHARACTERISTIC_EXTENDED_PROPERTIES_UUID) {
                if descriptor_value.is_empty() {
                    warn!("Extended properties descriptor has empty value. Ignoring.");
                    continue;
                }
                // The second byte in CharacteristicPropertyBits is for extended property bits.
                let ext_bits_raw: u16 = (descriptor_value[0] as u16) << u8::BITS;
                ext_property_bits = CharacteristicPropertyBits::from_bits_truncate(ext_bits_raw);
                continue;
            }

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
            let fidl_descriptor = Descriptor {
                handle: Some(Handle { value: descriptor_id }),
                type_: Some(descriptor_uuid.into()),
                permissions: Some(desc_permission_attributes),
                ..Descriptor::EMPTY
            };

            descriptors.push(fidl_descriptor);
        }
        Ok((descriptors, ext_property_bits))
    }

    fn generate_characteristics(
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
                Some(uuid_str) => Uuid::from_str(uuid_str)
                    .map_err(|_| format_err!("Invalid characteristic uuid: {}", uuid_str))?,
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
            let (fidl_descriptors, ext_properties_bits) =
                self.process_descriptors(descriptor_list)?;

            let characteristic_permissions = self.permissions_and_properties_from_raw_num(
                raw_characteristic_permissions,
                characteristic_properties,
            );

            // Properties map directly to CharacteristicPropertyBits except for
            // property_extended_props (0x80), so we truncate. The extended properties descriptor is
            // intercepted and added to the property bits (the Bluetooth stack will add the
            // descriptor later).
            let characteristic_properties =
                CharacteristicPropertyBits::from_bits_truncate(characteristic_properties as u16)
                    | ext_properties_bits;

            let characteristic_id = self.inner.write().generic_id_counter.next();
            self.inner.write().attribute_value_mapping.insert(
                characteristic_id,
                (characteristic_value, raw_enforce_enforce_initial_attribute_length),
            );
            let fidl_characteristic = Characteristic {
                handle: Some(Handle { value: characteristic_id }),
                type_: Some(characteristic_uuid.into()),
                properties: Some(characteristic_properties),
                permissions: Some(characteristic_permissions),
                descriptors: Some(fidl_descriptors),
                ..Characteristic::EMPTY
            };

            characteristics.push(fidl_characteristic);
        }
        Ok(characteristics)
    }

    fn generate_service(&self, service_json: &Value) -> Result<ServiceInfo, Error> {
        // Determine if the service is primary or not.
        let service_id = self.inner.write().generic_id_counter.next();
        let service_kind =
            match service_json["type"].as_i64().ok_or(format_err!("Invalid service type"))? {
                0 => ServiceKind::Primary,
                1 => ServiceKind::Secondary,
                _ => return Err(format_err!("Invalid Service type")),
            };

        // Get the service UUID.
        let service_uuid_str = service_json["uuid"]
            .as_str()
            .ok_or(format_err!("Service uuid was unable to cast  to str"))?;
        let service_uuid =
            Uuid::from_str(service_uuid_str).map_err(|_| format_err!("Invalid service uuid"))?;

        //Get the Characteristics from the service.
        let characteristics = self.generate_characteristics(&service_json["characteristics"])?;

        Ok(ServiceInfo {
            handle: Some(ServiceHandle { value: service_id }),
            kind: Some(service_kind),
            type_: Some(service_uuid.into()),
            characteristics: Some(characteristics),
            ..ServiceInfo::EMPTY
        })
    }

    async fn publish_service(
        &self,
        service_info: ServiceInfo,
        service_uuid: String,
    ) -> Result<(), Error> {
        let tag = "GattServerFacade::publish_service:";
        let (service_client, service_server) =
            fidl::endpoints::create_endpoints::<LocalServiceMarker>()?;
        let (service_request_stream, service_control_handle) =
            service_server.into_stream_and_control_handle()?;

        let server_proxy = self
            .inner
            .read()
            .server_proxy
            .as_ref()
            .ok_or(format_err!("No Server Proxy created."))?
            .clone();
        match server_proxy.publish_service(service_info, service_client).await? {
            Ok(()) => info!(
                tag = &[tag, &line!().to_string()].join("").as_str(),
                uuid = ?service_uuid,
                "Successfully published GATT service",
            ),
            Err(e) => return Err(format_err!("PublishService error: {:?}", e)),
        }

        let monitor_delegate_fut = GattServerFacade::monitor_service_request_stream(
            service_request_stream,
            service_control_handle,
            self.inner.read().attribute_value_mapping.clone(),
        );
        let fut = async {
            let result = monitor_delegate_fut.await;
            if let Err(err) = result {
                error!(
                    tag = "publish_service",
                    ?err,
                    "Failed to create or monitor the gatt service delegate"
                );
            }
        };
        self.inner.write().service_tasks.push(fasync::Task::spawn(fut));
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
    /// A "database" key wraps the database at:
    /// <aosp root>/tools/test/connectivity/acts/framework/acts/controllers/fuchsia_lib/bt/gatts_lib.py
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
        info!(tag = &[tag, &line!().to_string()].join("").as_str(), "Publishing service");
        let server_proxy = self.create_server_proxy()?;
        self.inner.write().server_proxy = Some(server_proxy);
        let services = args
            .get("database")
            .ok_or(format_err!("Could not find the 'database' key in the json database."))?
            .get("services")
            .ok_or(format_err!("Could not find the 'services' key in the json database."))?;

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
        self.inner.write().server_proxy = None;
        let _ = self.inner.write().service_tasks.drain(..).collect::<Vec<fasync::Task<()>>>();
    }

    // GattServerFacade for cleaning up objects in use.
    pub fn cleanup(&self) {
        let tag = "GattServerFacade::cleanup:";
        info!(tag = &[tag, &line!().to_string()].join("").as_str(), "Cleanup GATT server objects");
        self.inner.write().server_proxy = None;
        let _ = self.inner.write().service_tasks.drain(..).collect::<Vec<fasync::Task<()>>>();
    }

    // GattServerFacade for printing useful information pertaining to the facade for
    // debug purposes.
    pub fn print(&self) {
        let tag = "GattServerFacade::print:";
        info!(tag = &[tag, &line!().to_string()].join("").as_str(), "Unimplemented print function");
    }
}
