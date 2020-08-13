// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::{Context, Error};
use fidl::encoding::Decodable;
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_bluetooth_bredr::{
    Attribute, Channel, ChannelMode, ChannelParameters, ConnectParameters,
    ConnectionReceiverRequest, ConnectionReceiverRequestStream, DataElement, Information,
    L2capParameters, ProfileDescriptor, ProfileMarker, ProfileProxy, ProtocolDescriptor,
    ProtocolIdentifier, SearchResultsRequest, SearchResultsRequestStream,
    ServiceClassProfileIdentifier, ServiceDefinition,
};
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::{PeerId, Uuid};
use fuchsia_component as component;
use fuchsia_syslog::macros::*;
use futures::channel::oneshot;
use futures::select;
use futures::stream::StreamExt;
use futures::FutureExt;
use parking_lot::RwLock;
use serde_json::value::Value;
use std::{collections::HashMap, convert::TryFrom};

#[derive(Debug)]
struct ProfileServerFacadeInner {
    /// The current Profile Server Proxy
    profile_server_proxy: Option<ProfileProxy>,

    /// Total count of services advertised so far.
    advertisement_count: usize,

    /// Services currently active on the Profile Server Proxy
    advertisement_stoppers: HashMap<usize, oneshot::Sender<()>>,

    // Holds the channel so the connection remains open.
    l2cap_channel_holder: Option<Channel>,
}

/// Perform Profile Server operations.
///
/// Note this object is shared among all threads created by the server.
#[derive(Debug)]
pub struct ProfileServerFacade {
    inner: RwLock<ProfileServerFacadeInner>,
}

impl ProfileServerFacade {
    pub fn new() -> ProfileServerFacade {
        ProfileServerFacade {
            inner: RwLock::new(ProfileServerFacadeInner {
                profile_server_proxy: None,
                advertisement_count: 0,
                advertisement_stoppers: HashMap::new(),
                l2cap_channel_holder: None,
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
        self.inner.write().profile_server_proxy = Some(self.create_profile_server_proxy()?);
        Ok(())
    }

    /// Returns a list of String UUIDs from a Serde JSON list of Values.
    ///
    /// # Arguments
    /// * `uuid_list` - A serde json list of Values to parse.
    ///  Example input:
    /// 'uuid_list': ["00000001-0000-1000-8000-00805F9B34FB"]
    pub fn generate_service_class_uuids(&self, uuid_list: &Vec<Value>) -> Result<Vec<Uuid>, Error> {
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
            let uuid: Uuid = match uuid.parse() {
                Ok(uuid) => uuid,
                Err(e) => {
                    fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Unable to convert to Uuid: {:?}", e)
                    );
                }
            };
            service_class_uuid_list.push(uuid);
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
                    d as u16
                } else {
                    fx_err_and_bail!(&with_line!(tag), "Value 'data' not found or invalid type.")
                };

                params.push(DataElement::Uint16(data as u16));
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
                Some(v.to_string())
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
                DataElement::Uint8(d as u8)
            } else {
                fx_err_and_bail!(&with_line!(tag), "Value 'data' not found.")
            };

            attribute_list.push(Attribute { id: id, element: data_element })
        }
        Ok(attribute_list)
    }

    /// Monitor the connection request stream, printing outputs when connections happen.
    pub async fn monitor_connection_receiver(
        mut requests: ConnectionReceiverRequestStream,
        end_signal: oneshot::Receiver<()>,
    ) -> Result<(), Error> {
        let tag = "ProfileServerFacade::monitor_connection_receiver";
        let mut fused_end_signal = end_signal.fuse();
        loop {
            select! {
                _ = fused_end_signal => {
                    fx_log_info!("Ending advertisement on signal..");
                    return Ok(());
                },
                request = requests.next() => {
                    let request = match request {
                        None => {
                            let log_err = format_err!("Connection request stream ended");
                            fx_err_and_bail!(&with_line!(tag), log_err)
                        }
                        Some(Err(e)) => {
                            let log_err = format_err!("Error during connection request: {}", e);
                            fx_err_and_bail!(&with_line!(tag), log_err)
                        },
                        Some(Ok(r)) => r,
                    };
                    let ConnectionReceiverRequest::Connected { peer_id, channel, .. } = request;
                    let peer_id: PeerId = peer_id.into();
                    fx_log_info!(
                        tag: &with_line!(tag),
                        "Connection from {}: {:?}!",
                        peer_id,
                        channel
                    );
                }
            }
        }
    }

    /// Monitor the search results stream, printing logs when results are produced.
    pub async fn monitor_search_results(
        mut requests: SearchResultsRequestStream,
    ) -> Result<(), Error> {
        let tag = "ProfileServerFacade::monitor_search_results";
        while let Some(request) = requests.next().await {
            let request = match request {
                Err(e) => {
                    let log_err = format_err!("Error during search results request: {}", e);
                    fx_err_and_bail!(&with_line!(tag), log_err)
                }
                Ok(r) => r,
            };
            let SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } =
                request;
            let peer_id: PeerId = peer_id.into();
            fx_log_info!(
                tag: &with_line!(tag),
                "Search Result: Peer {} with protocol {:?}: {:?}",
                peer_id,
                protocol,
                attributes
            );
            responder.send()?;
        }
        let log_err = format_err!("Search result request stream ended");
        fx_err_and_bail!(&with_line!(tag), log_err)
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
    pub async fn add_service(&self, args: Value) -> Result<usize, Error> {
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
                } else if v.is_null() {
                    None
                } else {
                    let log_err =
                    "Invalid type for 'additional_protocol_descriptors'. Expected null or array.";
                    fx_err_and_bail!(&with_line!(tag), log_err)
                }
            } else {
                let log_err = "Invalid SDP record input. Missing 'additional_protocol_descriptors'";
                fx_err_and_bail!(&with_line!(tag), log_err)
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

        let service_defs = vec![ServiceDefinition {
            service_class_uuids: Some(service_class_uuids.into_iter().map(Into::into).collect()),
            protocol_descriptor_list: Some(protocol_descriptors),
            profile_descriptors: Some(profile_descriptors),
            additional_protocol_descriptor_lists: match raw_additional_protocol_descriptors {
                Some(d) => Some(vec![d]),
                None => None,
            },
            information: Some(information),
            additional_attributes,
        }];

        let (connect_client, connect_requests) =
            create_request_stream().context("ConnectionReceiver creation")?;

        match &self.inner.read().profile_server_proxy {
            Some(server) => {
                server.advertise(
                    &mut service_defs.into_iter(),
                    ChannelParameters::new_empty(),
                    connect_client,
                )?;
            }
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };

        let (end_ad_sender, end_ad_receiver) = oneshot::channel::<()>();
        let request_handler_fut =
            Self::monitor_connection_receiver(connect_requests, end_ad_receiver);
        fasync::Task::spawn(async move {
            if let Err(e) = request_handler_fut.await {
                fx_log_err!("Connection receiver handler ended with error: {:?}", e);
            }
        })
        .detach();

        let next = self.inner.write().advertisement_count + 1;
        self.inner.write().advertisement_stoppers.insert(next, end_ad_sender);
        self.inner.write().advertisement_count = next;
        Ok(next)
    }

    /// Removes a remote service by id.
    ///
    /// # Arguments:
    /// * `service_id`: The service id to remove.
    pub async fn remove_service(&self, service_id: usize) -> Result<(), Error> {
        let tag = "ProfileServerFacade::remove_service";
        match self.inner.write().advertisement_stoppers.remove(&service_id) {
            Some(_) => Ok(()),
            None => fx_err_and_bail!(&with_line!(tag), "Service ID not found"),
        }
    }

    pub fn get_service_class_profile_identifier_from_id(
        &self,
        raw_profile_id: &Value,
    ) -> Result<ServiceClassProfileIdentifier, Error> {
        let tag = "ProfileServerFacade::get_service_class_profile_identifier_from_id";
        match raw_profile_id.as_u64().map(u16::try_from) {
            Some(Ok(id)) => match ServiceClassProfileIdentifier::from_primitive(id) {
                Some(id) => return Ok(id),
                None => {
                    let log_err = format!("UUID {} not supported by profile server.", id);
                    fx_err_and_bail!(&with_line!(tag), log_err)
                }
            },
            _ => fx_err_and_bail!(&with_line!(tag), "Type of raw_profile_id incorrect."),
        };
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

        let (search_client, result_requests) =
            create_request_stream().context("SearchResults creation")?;

        match &self.inner.read().profile_server_proxy {
            Some(server) => server.search(profile_id, &attribute_list, search_client)?,
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };

        let search_fut = Self::monitor_search_results(result_requests);
        fasync::Task::spawn(async move {
            if let Err(e) = search_fut.await {
                fx_log_err!("Search result handler ended with error: {:?}", e);
            }
        })
        .detach();

        Ok(())
    }

    /// Sends an outgoing l2cap connection request
    ///
    /// # Arguments:
    /// * `id`: String - The peer id to connect to.
    /// * `psm`: u16 - The PSM value to connect to:
    ///     Valid PSM values: https://www.bluetooth.com/specifications/assigned-numbers/logical-link-control/
    /// * `mode`: String - The channel mode to connect over
    ///     Available Values: BASIC, ERTM
    pub async fn connect(&self, id: String, psm: u16, mode: &str) -> Result<(), Error> {
        let tag = "ProfileServerFacade::connect";
        let peer_id: PeerId = match id.parse() {
            Ok(id) => id,
            Err(_) => {
                fx_err_and_bail!(
                    &with_line!(tag),
                    "Failed to convert value in attribute_list to u16."
                );
            }
        };

        let mode = match mode {
            "BASIC" => ChannelMode::Basic,
            "ERTM" => ChannelMode::EnhancedRetransmission,
            _ => fx_err_and_bail!(&with_line!(tag), format!("Invalid mode: {:?}.", mode)),
        };

        let connection_result = match &self.inner.read().profile_server_proxy {
            Some(server) => {
                let l2cap_params = L2capParameters {
                    psm: Some(psm),
                    parameters: Some(ChannelParameters {
                        channel_mode: Some(mode),
                        ..ChannelParameters::new_empty()
                    }),
                    ..L2capParameters::new_empty()
                };
                server
                    .connect(&mut peer_id.into(), &mut ConnectParameters::L2cap(l2cap_params))
                    .await?
            }
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };

        match connection_result {
            Ok(r) => self.inner.write().l2cap_channel_holder = Some(r),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format!("Failed to connect with error: {:?}", e))
            }
        };

        Ok(())
    }

    /// Cleanup any Profile Server related objects.
    pub async fn cleanup(&self) -> Result<(), Error> {
        // Dropping these will signal the other end with an Err, which is enough.
        self.inner.write().advertisement_stoppers.clear();
        self.inner.write().advertisement_count = 0;
        self.inner.write().l2cap_channel_holder = None;
        self.inner.write().profile_server_proxy = None;
        Ok(())
    }
}
