// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl::encoding::Decodable;
use fidl_fuchsia_bluetooth_bredr::{
    Attribute, ChannelParameters, DataElement, DataElementData, DataElementType, Information,
    ProfileDescriptor, ProfileEvent, ProfileEventStream, ProfileMarker, ProfileProxy,
    ProtocolDescriptor, ProtocolIdentifier, SecurityLevel, ServiceClassProfileIdentifier,
    ServiceDefinition,
};
use fuchsia_async as fasync;
use fuchsia_component as component;
use fuchsia_syslog::macros::*;
use futures::stream::StreamExt;
use parking_lot::RwLock;
use serde_json::value::Value;

#[derive(Debug)]
struct InnerProfileServerFacade {
    /// The current Profile Server Proxy
    profile_server_proxy: Option<ProfileProxy>,

    /// Service IDs currently active on the Profile Server Proxy
    service_ids: Vec<u64>,
}

/// Perform Profile Server operations.
///
/// Note this object is shared among all threads created by the server.
#[derive(Debug)]
pub struct ProfileServerFacade {
    inner: RwLock<InnerProfileServerFacade>,
}

impl ProfileServerFacade {
    pub fn new() -> ProfileServerFacade {
        ProfileServerFacade {
            inner: RwLock::new(InnerProfileServerFacade {
                profile_server_proxy: None,
                service_ids: Vec::new(),
            }),
        }
    }

    /// Creates a Profile Server Proxy.
    pub fn create_profile_server_proxy(&self) -> Result<ProfileProxy, Error> {
        let tag = "ProfileServerFacade::create_profile_server_proxy";
        match self.inner.read().profile_server_proxy.clone() {
            Some(profile_server_proxy) => {
                fx_log_info!(
                    tag: &with_line!(tag),
                    "Current profile server proxy: {:?}",
                    profile_server_proxy
                );
                Ok(profile_server_proxy)
            }
            None => {
                fx_log_info!(tag: &with_line!(tag), "Setting new profile server proxy");
                let profile_server_proxy = component::client::connect_to_service::<ProfileMarker>();
                if let Err(err) = profile_server_proxy {
                    fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to create profile server proxy: {}", err)
                    );
                }
                profile_server_proxy
            }
        }
    }

    /// Initialize the ProfileServer proxy.
    pub async fn init_profile_server_proxy(&self) -> Result<(), Error> {
        let tag = "ProfileServerFacade::init_profile_server_proxy";
        self.inner.write().profile_server_proxy = Some(self.create_profile_server_proxy()?);
        let event_stream = match &self.inner.write().profile_server_proxy {
            Some(p) => p.take_event_stream(),
            None => fx_err_and_bail!(&with_line!(tag), "Failed to take event stream from proxy."),
        };

        let profile_server_future = ProfileServerFacade::monitor_profile_event_stream(event_stream);
        let fut = async {
            let result = profile_server_future.await;
            if let Err(_err) = result {
                fx_log_err!("Failed to monitor profile server event stream.");
            }
        };
        fasync::spawn(fut);
        Ok(())
    }

    /// Returns a list of String UUIDs from a Serde JSON list of Values.
    ///
    /// # Arguments
    /// * `uuid_list` - A serde json list of Values to parse.
    ///  Example input:
    /// 'uuid_list': ["0001"]
    pub fn generate_service_class_uuids(
        &self,
        uuid_list: &Vec<Value>,
    ) -> Result<Vec<String>, Error> {
        let tag = "ProfileServerFacade::generate_service_class_uuids";
        let mut service_class_uuid_list = Vec::new();
        for raw_uuid in uuid_list {
            let uuid = if let Some(u) = raw_uuid.as_str() {
                u
            } else {
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Unable to convert Value to String.")
                )
            };
            service_class_uuid_list.push(uuid.to_string());
        }
        Ok(service_class_uuid_list)
    }

    /// Returns a list of ProtocolDescriptors from a Serde JSON input.
    ///
    /// Defined Protocol Identifiers for the Protocol Descriptor
    /// We intentionally omit deprecated profile identifiers.
    /// From Bluetooth Assigned Numbers:
    /// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
    ///
    /// # Arguments
    /// * `protocol_descriptors`: A Json Representation of the ProtocolDescriptors
    ///     to set up. Example:
    ///  'protocol_descriptors': [
    ///      {
    ///          'protocol': 25,  # u64 Representation of ProtocolIdentifier::AVDTP
    ///          'params': [
    ///              {
    ///                 'data': 0x0103  # to indicate 1.3
    ///              },
    ///              {
    ///                  'data': 0x0105  # to indicate 1.5
    ///              }
    ///          ]
    ///      },
    ///      {
    ///          'protocol': 1,  # u64 Representation of ProtocolIdentifier::SDP
    ///          'params': [{
    ///              'data': 0x0019
    ///          }]
    ///      }
    ///  ]
    pub fn generate_protocol_descriptors(
        &self,
        protocol_descriptors: &Vec<Value>,
    ) -> Result<Vec<ProtocolDescriptor>, Error> {
        let tag = "ProfileServerFacade::generate_protocol_descriptors";
        let mut protocol_descriptor_list = Vec::new();

        for raw_protocol_descriptor in protocol_descriptors {
            let protocol = match raw_protocol_descriptor["protocol"].as_u64() {
                Some(p) => match p as u16 {
                    1 => ProtocolIdentifier::Sdp,
                    3 => ProtocolIdentifier::Rfcomm,
                    7 => ProtocolIdentifier::Att,
                    8 => ProtocolIdentifier::Obex,
                    15 => ProtocolIdentifier::Bnep,
                    17 => ProtocolIdentifier::Hidp,
                    18 => ProtocolIdentifier::HardcopyControlChannel,
                    20 => ProtocolIdentifier::HardcopyDataChannel,
                    22 => ProtocolIdentifier::HardcopyNotification,
                    23 => ProtocolIdentifier::Avctp,
                    25 => ProtocolIdentifier::Avdtp,
                    30 => ProtocolIdentifier::McapControlChannel,
                    31 => ProtocolIdentifier::McapDataChannel,
                    256 => ProtocolIdentifier::L2Cap,
                    _ => fx_err_and_bail!(
                        &with_line!(tag),
                        format!("Input protocol does not match supported protocols: {}", p)
                    ),
                },
                None => fx_err_and_bail!(&with_line!(tag), "Value 'protocol' not found."),
            };

            let raw_params = if let Some(p) = raw_protocol_descriptor["params"].as_array() {
                p
            } else {
                fx_err_and_bail!(&with_line!(tag), "Value 'params' not found or invalid type.")
            };

            let mut params = Vec::new();
            for param in raw_params {
                let data = if let Some(d) = param["data"].as_u64() {
                    d as i64
                } else {
                    fx_err_and_bail!(&with_line!(tag), "Value 'data' not found or invalid type.")
                };

                params.push(DataElement {
                    type_: DataElementType::UnsignedInteger,
                    size: 2,
                    data: DataElementData::Integer(data),
                });
            }

            protocol_descriptor_list
                .push(ProtocolDescriptor { protocol: protocol, params: params });
        }
        Ok(protocol_descriptor_list)
    }

    /// Returns a list of ProfileDescriptors from a Serde JSON input.
    ///
    /// Identifiers that are valid for Bluetooth Classes / Profiles
    /// We intentionally omit classes and profile IDs that are unsupported, deprecated,
    /// or reserved for use by Fuchsia Bluetooth.
    /// From Bluetooth Assigned Numbers for SDP
    /// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
    ///
    /// # Arguments
    /// * `profile_descriptors`: A Json Representation of the ProtocolDescriptors.
    /// Example:
    ///  'profile_descriptors': [{
    ///      'profile_id': 0x110D, # Represents ServiceClassProfileIdentifier::AdvancedAudioDistribution
    ///      'major_version': 1, # u64 representation of the major_version.
    ///      'minor_version': 3, # u64 representation of the minor_version.
    ///  }],
    pub fn generate_profile_descriptors(
        &self,
        profile_descriptors: &Vec<Value>,
    ) -> Result<Vec<ProfileDescriptor>, Error> {
        let tag = "ProfileServerFacade::generate_profile_descriptors";
        let mut profile_descriptor_list = Vec::new();
        for raw_profile_descriptor in profile_descriptors.into_iter() {
            let profile_id = if let Some(r) = raw_profile_descriptor.get("profile_id") {
                match self.get_service_class_profile_identifier_from_id(r) {
                    Ok(id) => id,
                    Err(e) => fx_err_and_bail!(&with_line!(tag), e),
                }
            } else {
                let log_err = "Invalid SDP search input. Missing 'profile_id'";
                fx_err_and_bail!(&with_line!(tag), log_err)
            };

            let minor_version = if let Some(num) = raw_profile_descriptor["minor_version"].as_u64()
            {
                num as u8
            } else {
                let log_err = "Type of 'minor_version' incorrect or incorrect type.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            };

            let major_version = if let Some(num) = raw_profile_descriptor["major_version"].as_u64()
            {
                num as u8
            } else {
                let log_err = "Type of 'major_version' incorrect or incorrect type.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            };

            profile_descriptor_list.push(ProfileDescriptor {
                profile_id,
                minor_version,
                major_version,
            });
        }
        Ok(profile_descriptor_list)
    }

    /// Returns a list of Information objects from a Serde JSON input.
    ///
    /// # Arguments
    /// * `information_list`: A Json Representation of the Information objects.
    ///  Example:
    ///  'information_list': [{
    ///      'language': "en",
    ///      'name': "A2DP",
    ///      'description': "Advanced Audio Distribution Profile",
    ///      'provider': "Fuchsia"
    ///  }],
    pub fn generate_information(
        &self,
        information_list: &Vec<Value>,
    ) -> Result<Vec<Information>, Error> {
        let tag = "ProfileServerFacade::generate_information";
        let mut info_list = Vec::new();
        for raw_information in information_list {
            let language = if let Some(v) = raw_information["language"].as_str() {
                v.to_string()
            } else {
                let log_err = "Type of 'language' incorrect of invalid type.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            };

            let name = if let Some(v) = raw_information["name"].as_str() {
                Some(v.to_string())
            } else {
                None
            };

            let description = if let Some(v) = raw_information["description"].as_str() {
                Some(v.to_string())
            } else {
                None
            };

            let provider = if let Some(v) = raw_information["provider"].as_str() {
                Some(v.to_string())
            } else {
                None
            };

            info_list.push(Information { language, name, description, provider });
        }
        Ok(info_list)
    }

    /// Returns a list of Attributes from a Serde JSON input.
    ///
    /// # Arguments
    /// * `additional_attributes_list`: A Json Representation of the Attribute objects.
    ///  Example:
    ///    'additional_attributes': [{
    ///         'id': 201,
    ///         'element': {
    ///             'data': int(sig_uuid_constants['AVDTP'], 16)
    ///         }
    ///    }]
    pub fn generate_additional_attributes(
        &self,
        additional_attributes_list: &Vec<Value>,
    ) -> Result<Vec<Attribute>, Error> {
        let tag = "ProfileServerFacade::generate_additional_attributes";
        let mut attribute_list = Vec::new();
        for raw_attribute in additional_attributes_list {
            let id = if let Some(v) = raw_attribute["id"].as_u64() {
                v as u16
            } else {
                let log_err = "Type of 'id' incorrect or invalid type.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            };

            let raw_element = if let Some(e) = raw_attribute.get("element") {
                e
            } else {
                let log_err = "Type of 'element' incorrect.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            };

            let data_element = if let Some(d) = raw_element["data"].as_u64() {
                DataElement {
                    type_: DataElementType::UnsignedInteger,
                    size: 2,
                    data: DataElementData::Integer(d as i64),
                }
            } else {
                fx_err_and_bail!(&with_line!(tag), "Value 'data' not found.")
            };

            attribute_list.push(Attribute { id: id, element: data_element })
        }
        Ok(attribute_list)
    }

    /// A function to monitor incoming events from the ProfileEventStream.
    pub async fn monitor_profile_event_stream(mut stream: ProfileEventStream) -> Result<(), Error> {
        let tag = "ProfileServerFacade::monitor_profile_event_stream";
        while let Some(request) = stream.next().await {
            match request {
                Ok(r) => match r {
                    ProfileEvent::OnServiceFound { peer_id, profile, attributes } => {
                        fx_log_info!(
                            tag: &with_line!(tag),
                            "Peer {} with profile {:?}: {:?}",
                            peer_id,
                            profile,
                            attributes
                        );
                    }
                    ProfileEvent::OnConnected { device_id, service_id: _, channel, protocol } => {
                        fx_log_info!(
                            tag: &with_line!(tag),
                            "Connection from {}: {:?} {:?}!",
                            device_id,
                            channel,
                            protocol
                        );
                    }
                },
                Err(r) => {
                    let log_err = format_err!("Error during handling request stream: {}", r);
                    fx_err_and_bail!(&with_line!(tag), log_err)
                }
            };
        }
        Ok(())
    }

    /// Adds a service record based on a JSON dictrionary.
    ///
    /// # Arguments:
    /// * `args` : A Json object representing the service to add:
    ///Example Python dictionary pre JSON conversion
    ///args:
    ///{
    ///    'service_class_uuids': ["0001"],
    ///    'protocol_descriptors': [
    ///        {
    ///            'protocol':
    ///            int(sig_uuid_constants['AVDTP'], 16),
    ///            'params': [
    ///                {
    ///                    'data': 0x0103
    ///                }
    ///            ]
    ///        },
    ///        {
    ///            'protocol': int(sig_uuid_constants['SDP'], 16),
    ///            'params': [{
    ///                'data': int(sig_uuid_constants['AVDTP'], 16),
    ///            }]
    ///        }
    ///    ],
    ///    'profile_descriptors': [{
    ///        'profile_id': int(sig_uuid_constants['AdvancedAudioDistribution'], 16),
    ///        'major_version': 1,
    ///        'minor_version': 3,
    ///    }],
    ///    'additional_protocol_descriptors': [{
    ///        'protocol': int(sig_uuid_constants['L2CAP'], 16),
    ///        'params': [{
    ///            'data': int(sig_uuid_constants['AVDTP'], 16),
    ///        }]
    ///    }],
    ///    'information': [{
    ///        'language': "en",
    ///        'name': "A2DP",
    ///        'description': "Advanced Audio Distribution Profile",
    ///        'provider': "Fuchsia"
    ///    }],
    ///    'additional_attributes': [{
    ///         'id': 201,
    ///         'element': {
    ///             'data': int(sig_uuid_constants['AVDTP'], 16)
    ///         }
    ///    }]
    ///}
    pub async fn add_service(&self, args: Value) -> Result<u64, Error> {
        let tag = "ProfileServerFacade::write_sdp_record";
        fx_log_info!(tag: &with_line!(tag), "Writing SDP record");

        let record_description = if let Some(r) = args.get("record") {
            r
        } else {
            let log_err = "Invalid SDP record input. Missing 'record'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        let service_class_uuids = if let Some(v) = record_description.get("service_class_uuids") {
            if let Some(r) = v.as_array() {
                self.generate_service_class_uuids(r)?
            } else {
                let log_err = "Invalid type for service_class_uuids in record input.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            }
        } else {
            let log_err = "Invalid SDP record input. Missing 'service_class_uuids'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        let protocol_descriptors = if let Some(v) = record_description.get("protocol_descriptors") {
            if let Some(r) = v.as_array() {
                self.generate_protocol_descriptors(r)?
            } else {
                let log_err = "Invalid type for protocol_descriptors in record input.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            }
        } else {
            let log_err = "Invalid SDP record input. Missing 'protocol_descriptors'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        let profile_descriptors = if let Some(v) = record_description.get("profile_descriptors") {
            if let Some(r) = v.as_array() {
                self.generate_profile_descriptors(r)?
            } else {
                let log_err = "Invalid type for profile_descriptors in record input.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            }
        } else {
            let log_err = "Invalid SDP record input. Missing 'profile_descriptors'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        let raw_additional_protocol_descriptors =
            if let Some(v) = record_description.get("additional_protocol_descriptors") {
                if let Some(arr) = v.as_array() {
                    Some(self.generate_protocol_descriptors(arr)?)
                } else {
                    let log_err = "Invalid type for protocol_descriptors in record input.";
                    fx_err_and_bail!(&with_line!(tag), log_err)
                }
            } else {
                None
            };

        let information = if let Some(v) = record_description.get("information") {
            if let Some(r) = v.as_array() {
                self.generate_information(r)?
            } else {
                let log_err = "Invalid type for information in record input.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            }
        } else {
            let log_err = "Invalid SDP record input. Missing 'information'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        let additional_attributes = if let Some(v) = record_description.get("additional_attributes")
        {
            if let Some(r) = v.as_array() {
                Some(self.generate_additional_attributes(r)?)
            } else {
                None
            }
        } else {
            let log_err = "Invalid SDP record input. Missing 'additional_attributes'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        let mut service_def = ServiceDefinition {
            service_class_uuids,
            protocol_descriptors,
            profile_descriptors,
            additional_protocol_descriptors: match raw_additional_protocol_descriptors {
                Some(d) => Some(vec![d]),
                None => None,
            },
            information,
            additional_attributes,
        };

        let (status, service_id) = match &self.inner.read().profile_server_proxy {
            Some(server) => {
                server
                    .add_service(
                        &mut service_def,
                        SecurityLevel::EncryptionOptional,
                        ChannelParameters::new_empty(),
                    )
                    .await?
            }
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };

        if let Some(e) = status.error {
            let log_err = format!("Couldn't add service: {:?}", e);
            fx_err_and_bail!(&with_line!(tag), log_err)
        } else {
            self.inner.write().service_ids.push(service_id);
        }

        Ok(service_id)
    }

    /// Removes a remote service by id.
    ///
    /// # Arguments:
    /// * `service_id`: The u64 service id to remove.
    pub async fn remove_service(&self, service_id: u64) -> Result<(), Error> {
        let tag = "ProfileServerFacade::remove_service";
        match &self.inner.read().profile_server_proxy {
            Some(server) => {
                let _result = server.remove_service(service_id);
            }
            None => fx_err_and_bail!(&with_line!(tag), "No profile proxy set"),
        };
        Ok(())
    }

    pub fn get_service_class_profile_identifier_from_id(
        &self,
        raw_profile_id: &Value,
    ) -> Result<ServiceClassProfileIdentifier, Error> {
        let tag = "ProfileServerFacade::get_service_class_profile_identifier_from_id";
        let id = match raw_profile_id.as_u64() {
            Some(id) => match id as u64 {
                0x1101 => ServiceClassProfileIdentifier::SerialPort,
                0x1103 => ServiceClassProfileIdentifier::DialupNetworking,
                0x1105 => ServiceClassProfileIdentifier::ObexObjectPush,
                0x1106 => ServiceClassProfileIdentifier::ObexFileTransfer,
                0x1108 => ServiceClassProfileIdentifier::Headset,
                0x1112 => ServiceClassProfileIdentifier::HeadsetAudioGateway,
                0x1131 => ServiceClassProfileIdentifier::HeadsetHs,
                0x110A => ServiceClassProfileIdentifier::AudioSource,
                0x110B => ServiceClassProfileIdentifier::AudioSink,
                0x110D => ServiceClassProfileIdentifier::AdvancedAudioDistribution,
                0x110C => ServiceClassProfileIdentifier::AvRemoteControlTarget,
                0x110E => ServiceClassProfileIdentifier::AvRemoteControl,
                0x110F => ServiceClassProfileIdentifier::AvRemoteControlController,
                0x1115 => ServiceClassProfileIdentifier::Panu,
                0x1116 => ServiceClassProfileIdentifier::Nap,
                0x1117 => ServiceClassProfileIdentifier::Gn,
                0x111E => ServiceClassProfileIdentifier::Handsfree,
                0x111F => ServiceClassProfileIdentifier::HandsfreeAudioGateway,
                0x112D => ServiceClassProfileIdentifier::SimAccess,
                0x112E => ServiceClassProfileIdentifier::PhonebookPce,
                0x112F => ServiceClassProfileIdentifier::PhonebookPse,
                0x1130 => ServiceClassProfileIdentifier::Phonebook,
                0x1132 => ServiceClassProfileIdentifier::MessageAccessServer_,
                0x1133 => ServiceClassProfileIdentifier::MessageNotificationServer_,
                0x1134 => ServiceClassProfileIdentifier::MessageAccessProfile,
                0x113A => ServiceClassProfileIdentifier::MpsProfile,
                0x113B => ServiceClassProfileIdentifier::MpsClass,
                0x1303 => ServiceClassProfileIdentifier::VideoSource,
                0x1304 => ServiceClassProfileIdentifier::VideoSink,
                0x1305 => ServiceClassProfileIdentifier::VideoDistribution,
                0x1400 => ServiceClassProfileIdentifier::Hdp,
                0x1401 => ServiceClassProfileIdentifier::HdpSource,
                0x1402 => ServiceClassProfileIdentifier::HdpSink,
                _ => {
                    let log_err = format!("UUID {} not supported by profile server.", id);
                    fx_err_and_bail!(&with_line!(tag), log_err)
                }
            },
            None => fx_err_and_bail!(&with_line!(tag), "Type of raw_profile_id incorrect."),
        };

        Ok(id)
    }

    pub async fn add_search(&self, args: Value) -> Result<(), Error> {
        let tag = "ProfileServerFacade::add_search";
        fx_log_info!(tag: &with_line!(tag), "Adding Search");

        let raw_attribute_list = if let Some(v) = args.get("attribute_list") {
            if let Some(r) = v.as_array() {
                r
            } else {
                let log_err = "Expected 'attribute_list' as an array.";
                fx_err_and_bail!(&with_line!(tag), log_err)
            }
        } else {
            let log_err = "Invalid SDP search input. Missing 'attribute_list'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        let mut attribute_list = Vec::new();
        for item in raw_attribute_list {
            match item.as_u64() {
                Some(v) => attribute_list.push(v as u16),
                None => fx_err_and_bail!(
                    &with_line!(tag),
                    "Failed to convert value in attribute_list to u16."
                ),
            };
        }

        let profile_id = if let Some(r) = args.get("profile_id") {
            self.get_service_class_profile_identifier_from_id(r)?
        } else {
            let log_err = "Invalid SDP search input. Missing 'profile_id'";
            fx_err_and_bail!(&with_line!(tag), log_err)
        };

        match &self.inner.read().profile_server_proxy {
            Some(server) => server.add_search(profile_id, &mut attribute_list.into_iter())?,
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };

        Ok(())
    }

    pub async fn connect_l2cap(&self, id: String, psm: u16) -> Result<(), Error> {
        let tag = "ProfileServerFacade::connect_l2cap";
        let connect_l2cap_future = match &self.inner.read().profile_server_proxy {
            Some(server) => server.connect_l2cap(&id, psm, ChannelParameters::new_empty()),
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };

        let connect_l2cap_future = async {
            let result = connect_l2cap_future.await;
            if let Err(err) = result {
                fx_log_err!("Failed connect_l2cap with: {:?}", err);
            };
        };
        fasync::spawn(connect_l2cap_future);

        Ok(())
    }

    /// Cleanup any Profile Server related objects.
    pub async fn cleanup(&self) -> Result<(), Error> {
        let tag = "ProfileServerFacade::cleanup";
        match &self.inner.read().profile_server_proxy {
            Some(server) => {
                for id in &self.inner.read().service_ids {
                    let _result = server.remove_service(*id);
                }
            }
            None => fx_err_and_bail!(&with_line!(tag), "No profile proxy set"),
        };
        self.inner.write().service_ids.clear();
        self.inner.write().profile_server_proxy = None;
        Ok(())
    }
}
