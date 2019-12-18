// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitflags::bitflags,
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_bluetooth as bt,
    fidl_fuchsia_bluetooth_bredr::*,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{
        future::{self, BoxFuture, FutureExt},
        stream::{BoxStream, StreamExt},
    },
    std::{fmt::Debug, string::String},
};

use crate::types::PeerId;

bitflags! {
    #[allow(dead_code)]
    pub struct AvcrpTargetFeatures: u16 {
        const CATEGORY1         = 1 << 0;
        const CATEGORY2         = 1 << 1;
        const CATEGORY3         = 1 << 2;
        const CATEGORY4         = 1 << 3;
        const PLAYERSETTINGS    = 1 << 4;
        const GROUPNAVIGATION   = 1 << 5;
        const SUPPORTSBROWSING  = 1 << 6;
        const SUPPORTSMULTIPLEMEDIAPLAYERS = 1 << 7;
        const SUPPORTSCOVERART  = 1 << 8;
        // 9-15 Reserved
    }
}

bitflags! {
    #[allow(dead_code)]
    pub struct AvcrpControllerFeatures: u16 {
        const CATEGORY1         = 1 << 0;
        const CATEGORY2         = 1 << 1;
        const CATEGORY3         = 1 << 2;
        const CATEGORY4         = 1 << 3;
        // 4-5 RESERVED
        const SUPPORTSBROWSING  = 1 << 6;
        const SUPPORTSCOVERARTGETIMAGEPROPERTIES = 1 << 7;
        const SUPPORTSCOVERARTGETIMAGE = 1 << 8;
        const SUPPORTSCOVERARTGETLINKEDTHUMBNAIL = 1 << 9;
        // 10-15 RESERVED
    }
}

const SDP_SUPPORTED_FEATURES: u16 = 0x0311;

const AV_REMOTE_TARGET_CLASS: &str = "0000110c-0000-1000-8000-00805f9b34fb";
const AV_REMOTE_CLASS: &str = "0000110e-0000-1000-8000-00805f9b34fb";
const AV_REMOTE_CONTROLLER_CLASS: &str = "0000110f-0000-1000-8000-00805f9b34fb";

/// Make the SDP definition for the AVRCP service.
/// TODO(1346): We need two entries in SDP in the future. One for target and one for controller.
///       We are using using one unified profile for both because of limitations in BrEdr service.
fn make_profile_service_definition() -> ServiceDefinition {
    let service_class_uuids: Vec<String> = vec![
        String::from(AV_REMOTE_TARGET_CLASS),
        String::from(AV_REMOTE_CLASS),
        String::from(AV_REMOTE_CONTROLLER_CLASS),
    ];
    ServiceDefinition {
        service_class_uuids, // AVRCP UUID
        protocol_descriptors: vec![
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::L2Cap,
                params: vec![DataElement {
                    type_: DataElementType::UnsignedInteger,
                    size: 2,
                    data: DataElementData::Integer(PSM_AVCTP),
                }],
            },
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::Avctp,
                params: vec![DataElement {
                    type_: DataElementType::UnsignedInteger,
                    size: 2,
                    data: DataElementData::Integer(0x0103), // Indicate v1.3
                }],
            },
        ],
        profile_descriptors: vec![ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AvRemoteControl,
            major_version: 1,
            minor_version: 6,
        }],
        additional_protocol_descriptors: None,
        information: vec![Information {
            language: "en".to_string(),
            name: Some("AVRCP".to_string()),
            description: Some("AVRCP".to_string()),
            provider: Some("Fuchsia".to_string()),
        }],
        additional_attributes: Some(vec![Attribute {
            id: SDP_SUPPORTED_FEATURES, // SDP Attribute "SUPPORTED FEATURES"
            element: DataElement {
                type_: DataElementType::UnsignedInteger,
                size: 2,
                data: DataElementData::Integer(i64::from(
                    AvcrpTargetFeatures::CATEGORY1.bits() | AvcrpTargetFeatures::CATEGORY2.bits(),
                )),
            },
        }]),
    }
}

#[derive(Debug, PartialEq, Hash, Clone, Copy)]
pub struct AvrcpProtocolVersion(pub u8, pub u8);

#[derive(Debug, PartialEq, Clone)]
pub enum AvrcpService {
    Target {
        features: AvcrpTargetFeatures,
        psm: u16,
        protocol_version: AvrcpProtocolVersion,
    },
    Controller {
        features: AvcrpControllerFeatures,
        psm: u16,
        protocol_version: AvrcpProtocolVersion,
    },
}

#[derive(Debug, PartialEq)]
pub enum AvrcpProfileEvent {
    ///Incoming connection on the control channel PSM.
    ///TODO(2744): add browse channel.
    IncomingControlConnection {
        peer_id: PeerId,
        channel: zx::Socket,
    },
    ServicesDiscovered {
        peer_id: PeerId,
        services: Vec<AvrcpService>,
    },
}

pub trait ProfileService: Debug {
    fn connect_to_device<'a>(
        &'a self,
        peer_id: &'a PeerId,
        psm: u16,
    ) -> BoxFuture<Result<zx::Socket, Error>>;

    fn take_event_stream(&self) -> BoxStream<Result<AvrcpProfileEvent, Error>>;
}

#[derive(Debug)]
pub struct ProfileServiceImpl {
    profile_svc: ProfileProxy,
    service_id: u64,
}

impl ProfileServiceImpl {
    pub async fn connect_and_register_service() -> Result<ProfileServiceImpl, Error> {
        let profile_svc = fuchsia_component::client::connect_to_service::<ProfileMarker>()
            .context("Failed to connect to Bluetooth profile service")?;

        const SEARCH_ATTRIBUTES: [u16; 5] = [
            ATTR_SERVICE_CLASS_ID_LIST,
            ATTR_PROTOCOL_DESCRIPTOR_LIST,
            ATTR_ADDITIONAL_PROTOCOL_DESCRIPTOR_LIST,
            ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            SDP_SUPPORTED_FEATURES,
        ];

        profile_svc.add_search(
            ServiceClassProfileIdentifier::AvRemoteControl,
            &mut SEARCH_ATTRIBUTES.to_vec().into_iter(),
        )?;

        let mut service_def = make_profile_service_definition();
        let (status, service_id) = profile_svc
            .add_service(&mut service_def, SecurityLevel::EncryptionOptional, false)
            .await?;

        fx_log_info!("Registered Service ID {}", service_id);

        match status.error {
            Some(e) => Err(format_err!("Couldn't add AVRCP service: {:?}", e)),
            _ => Ok(ProfileServiceImpl { profile_svc, service_id }),
        }
    }
}

impl ProfileService for ProfileServiceImpl {
    fn connect_to_device(
        &self,
        peer_id: &PeerId,
        psm: u16,
    ) -> BoxFuture<Result<zx::Socket, Error>> {
        let peer_id = peer_id.clone();
        async move {
            let (status, socket) = self
                .profile_svc
                .connect_l2cap(&peer_id, psm)
                .await
                .map_err(|e| format_err!("Profile service error: {:?}", e))?;
            let status: bt::Status = status;
            if let Some(error) = status.error {
                return Err(format_err!("Error connecting to device: {} {:?}", peer_id, *error));
            }
            match socket {
                Some(sock) => Ok(sock),
                // Hopefully we should have an error if we don't have a socket.
                None => Err(format_err!("No socket returned from profile service {}", peer_id)),
            }
        }
        .boxed()
    }

    fn take_event_stream(&self) -> BoxStream<Result<AvrcpProfileEvent, Error>> {
        let event_stream: ProfileEventStream = self.profile_svc.take_event_stream();
        let expected_service_id = self.service_id;
        event_stream
            .filter_map(move |event| {
                future::ready(match event {
                    Ok(ProfileEvent::OnConnected {
                        device_id: peer_id,
                        service_id,
                        channel,
                        protocol: _,
                    }) => {
                        if expected_service_id != service_id {
                            fx_log_err!("Unexpected service id received {}", service_id);
                            None
                        } else {
                            Some(Ok(AvrcpProfileEvent::IncomingControlConnection {
                                peer_id: PeerId::from(peer_id),
                                channel,
                            }))
                        }
                    }
                    Ok(ProfileEvent::OnServiceFound { peer_id, profile, attributes }) => {
                        fx_log_info!("Service found on {}: {:#?}", peer_id, profile);
                        let mut features: Option<u16> = None;
                        let mut service_uuids: Option<Vec<String>> = None;

                        for attr in attributes {
                            fx_log_info!("attribute: {:#?} ", attr);
                            match attr.id {
                                ATTR_SERVICE_CLASS_ID_LIST => {
                                    if let DataElementData::Sequence(seq) = attr.element.data {
                                        let uuids: Vec<String> = seq
                                            .into_iter()
                                            .flatten()
                                            .filter_map(|item| {
                                                if let DataElementData::Uuid(uuid) = item.data {
                                                    Some(uuid)
                                                } else {
                                                    None
                                                }
                                            })
                                            .collect();
                                        if uuids.len() > 0 {
                                            service_uuids = Some(uuids);
                                        }
                                    }
                                }
                                SDP_SUPPORTED_FEATURES => {
                                    if let DataElementData::Integer(value) = attr.element.data {
                                        features = Some(value as u16);
                                    }
                                }
                                _ => {}
                            }
                        }

                        if service_uuids.is_none() || features.is_none() {
                            None
                        } else {
                            let service_uuids =
                                service_uuids.expect("service_uuids should not be none");

                            let mut services = vec![];

                            if service_uuids.contains(&AV_REMOTE_TARGET_CLASS.to_string()) {
                                if let Some(feature_flags) =
                                    AvcrpTargetFeatures::from_bits(features.unwrap())
                                {
                                    services.push(AvrcpService::Target {
                                        features: feature_flags,
                                        psm: PSM_AVCTP as u16, // TODO: Parse this out instead of assuming it's default
                                        protocol_version: AvrcpProtocolVersion(
                                            profile.major_version,
                                            profile.minor_version,
                                        ),
                                    })
                                }
                            } else if service_uuids.contains(&AV_REMOTE_CLASS.to_string())
                                || service_uuids.contains(&AV_REMOTE_CONTROLLER_CLASS.to_string())
                            {
                                if let Some(feature_flags) =
                                    AvcrpControllerFeatures::from_bits(features.unwrap())
                                {
                                    services.push(AvrcpService::Controller {
                                        features: feature_flags,
                                        psm: PSM_AVCTP as u16, // TODO: Parse this out instead of assuming it's default
                                        protocol_version: AvrcpProtocolVersion(
                                            profile.major_version,
                                            profile.minor_version,
                                        ),
                                    })
                                }
                            }

                            if services.is_empty() {
                                None
                            } else {
                                Some(Ok(AvrcpProfileEvent::ServicesDiscovered {
                                    peer_id,
                                    services,
                                }))
                            }
                        }
                    }
                    Err(e) => Some(Err(Error::from(e))),
                })
            })
            .boxed()
    }
}
