// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_rfcomm::profile::is_rfcomm_protocol,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::{
        profile::{psm_from_protocol, Attribute, ProtocolDescriptor, Psm},
        types::{PeerId, Uuid},
    },
    std::{collections::HashSet, convert::TryFrom},
};

use crate::peer::service::ServiceHandle;

/// Convenience type for storing the fields of a Profile.ServiceFound response.
pub struct ServiceFoundResponse {
    pub id: PeerId,
    pub protocol: Option<Vec<bredr::ProtocolDescriptor>>,
    pub attributes: Vec<bredr::Attribute>,
}

/// Arguments used to launch a profile.
#[derive(Clone, Debug)]
pub struct LaunchInfo {
    pub url: String,
    pub arguments: Vec<String>,
}

impl TryFrom<bredr::LaunchInfo> for LaunchInfo {
    type Error = Error;

    fn try_from(src: bredr::LaunchInfo) -> Result<Self, Self::Error> {
        Ok(LaunchInfo {
            url: src.component_url.ok_or(format_err!("Component URL must be provided"))?,
            arguments: src.arguments.unwrap_or(Vec::new()),
        })
    }
}

/// Converts a ProtocolDescriptor to a collection of DataElements.
fn protocol_descriptor_to_data_elements(
    desc: &bredr::ProtocolDescriptor,
) -> Vec<Option<Box<bredr::DataElement>>> {
    let identifier = Some(Box::new(Uuid::new16(desc.protocol as u16).into()));
    let params: Vec<Option<Box<bredr::DataElement>>> =
        desc.params.iter().map(|elt| Some(Box::new(elt.clone()))).collect();

    // Build the list of data elements - the protocol identifier with all of the parameters.
    let mut result = vec![identifier];
    result.extend(params);
    result
}

/// Builds the ProtocolDescriptorList Attribute from the provided `protocol`.
fn protocol_to_attribute(protocol: &Vec<bredr::ProtocolDescriptor>) -> bredr::Attribute {
    let elements = protocol.iter().map(protocol_descriptor_to_data_elements).flatten().collect();
    bredr::Attribute {
        id: bredr::ATTR_PROTOCOL_DESCRIPTOR_LIST,
        element: bredr::DataElement::Sequence(vec![Some(Box::new(bredr::DataElement::Sequence(
            elements,
        )))]),
    }
}

/// The unique identifier associated with a registered service.
///
/// At any point in time, this ID will be unique in the _entire_ piconet because there is at
/// most one peer with a unique PeerId, and each service registered by the peer
/// is uniquely identified by a ServiceHandle.
/// Therefore, the combination of PeerId and ServiceHandle can uniquely identify a service.
/// However, because a service can be unregistered and potentially re-registered (e.g
/// peer disconnecting, reconnecting, and advertising an identical service), an extra salt
/// parameter is added to introduce randomness. This is because the Slab<T> implementation
/// backing the `ServiceSet` (see the `service` mod) recycles `ServiceHandles` when inserting
/// and removing items.
///
/// The uniqueness prevents duplication of messages when reporting services to a search.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct RegisteredServiceId {
    id: PeerId,
    handle: ServiceHandle,
    salt: [u8; 4],
}

impl RegisteredServiceId {
    pub fn new(id: PeerId, handle: ServiceHandle) -> Self {
        let mut salt = [0; 4];
        let _bytes = fuchsia_zircon::cprng_draw(&mut salt[..]).expect("zx_cprng_draw never fails");
        Self { id, handle, salt }
    }

    fn peer_id(&self) -> PeerId {
        self.id
    }
}

/// A ServiceRecord representing the information about a service.
/// A ServiceRecord is considered "registered" when it has been assigned
/// a unique RegisteredServiceId.
// TODO(fxbug.dev/51454): Store all the fields of the ServiceDefinition here.
#[derive(Clone, Debug, PartialEq)]
pub struct ServiceRecord {
    /// The Service Class IDs specified by this record. There must be at least one.
    svc_ids: HashSet<bredr::ServiceClassProfileIdentifier>,

    /// The primary protocol associated with this record. The entire protocol list is stored so that
    /// when the service is reported to a service search, no information  is lost (e.g the RFCOMM
    /// channel number).
    primary_protocol: Vec<ProtocolDescriptor>,

    /// Any additional PSMs specified by this record. If none, this will be empty.
    additional_psms: HashSet<Psm>,

    /// The ProfileDescriptors specified by this record.
    profile_descriptors: Vec<bredr::ProfileDescriptor>,

    /// The additional attributes specified by this record.
    additional_attributes: Vec<Attribute>,

    /// Metadata about this service. This information will be set when the service is registered.
    /// Use `ServiceRecord::register_service_record()` to mark the ServiceRecord as registered by
    /// assigning a unique `RegisteredServiceId`.
    reg_id: Option<RegisteredServiceId>,
}

impl ServiceRecord {
    pub fn new(
        svc_ids: HashSet<bredr::ServiceClassProfileIdentifier>,
        primary_protocol: Vec<ProtocolDescriptor>,
        additional_psms: HashSet<Psm>,
        profile_descriptors: Vec<bredr::ProfileDescriptor>,
        additional_attributes: Vec<Attribute>,
    ) -> Self {
        Self {
            svc_ids,
            primary_protocol,
            additional_psms,
            profile_descriptors,
            additional_attributes,
            reg_id: None,
        }
    }

    pub fn service_ids(&self) -> &HashSet<bredr::ServiceClassProfileIdentifier> {
        &self.svc_ids
    }

    /// Returns all the PSMs specified by this record.
    pub fn psms(&self) -> HashSet<Psm> {
        let mut psms = self.additional_psms.clone();

        // For RFCOMM-dependent services, the PSM is typically not supplied in the protocol list
        // of the service definition.
        if is_rfcomm_protocol(&self.primary_protocol) {
            let _ = psms.insert(Psm::RFCOMM);
        } else {
            if let Some(psm) = psm_from_protocol(&self.primary_protocol) {
                let _ = psms.insert(psm);
            }
        }
        psms
    }

    /// Returns true if the Service has been registered. Namely, it must be assigned a PeerId and
    /// ServiceHandle.
    #[cfg(test)]
    fn is_registered(&self) -> bool {
        self.reg_id.is_some()
    }

    /// Every registered ServiceRecord has a unique identifier. This is data associated with the
    /// piconet member that registered it.
    ///
    /// Returns an error if the service has not been registered.
    pub fn unique_service_id(&self) -> Result<RegisteredServiceId, Error> {
        self.reg_id.ok_or(format_err!("The ServiceRecord has not been registered"))
    }

    /// Returns true if the provided `id` is specified by this record.
    pub fn contains_service_class_identifier(
        &self,
        id: &bredr::ServiceClassProfileIdentifier,
    ) -> bool {
        self.svc_ids.contains(id)
    }

    /// Marks the ServiceRecord as registered by assigning it a unique `reg_id`.
    pub fn register_service_record(&mut self, reg_id: RegisteredServiceId) {
        self.reg_id = Some(reg_id);
    }

    /// Converts the ServiceRecord into a ServiceFoundResponse. Builds the ProtocolDescriptorList
    /// from the data in the ServiceRecord.
    ///
    /// Returns an error if the ServiceRecord has not been registered.
    // TODO(fxbug.dev/51454): Build the full ServiceFoundResponse. Right now, we just
    // build the primary L2CAP Protocol, ServiceClassIdentifiers, and Profile Descriptors.
    pub fn to_service_found_response(&self) -> Result<ServiceFoundResponse, Error> {
        let peer_id =
            self.reg_id.ok_or(format_err!("The service has not been registered."))?.peer_id();
        let mut attributes = vec![];

        // The primary protocol list. This is both returned and included in `attributes`.
        let protocol: Vec<bredr::ProtocolDescriptor> =
            self.primary_protocol.iter().map(Into::into).collect();
        attributes.push(protocol_to_attribute(&protocol));

        // The service class identifiers.
        let svc_ids_list: Vec<bredr::ServiceClassProfileIdentifier> =
            self.svc_ids.iter().cloned().collect();
        let svc_ids_sequence =
            svc_ids_list.into_iter().map(|id| Some(Box::new(Uuid::from(id).into()))).collect();
        attributes.push(bredr::Attribute {
            id: bredr::ATTR_SERVICE_CLASS_ID_LIST,
            element: bredr::DataElement::Sequence(svc_ids_sequence),
        });

        // Profile descriptors.
        if !self.profile_descriptors.is_empty() {
            let mut prof_desc_sequence = vec![];
            for descriptor in &self.profile_descriptors {
                let desc_list = vec![
                    Some(Box::new(Uuid::from(descriptor.profile_id).into())),
                    Some(Box::new(bredr::DataElement::Uint16(u16::from_be_bytes([
                        descriptor.major_version,
                        descriptor.minor_version,
                    ])))),
                ];
                prof_desc_sequence.push(Some(Box::new(bredr::DataElement::Sequence(desc_list))));
            }
            attributes.push(bredr::Attribute {
                id: bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
                element: bredr::DataElement::Sequence(prof_desc_sequence),
            });
        }

        // Additional attributes.
        let mut additional_attributes = self.additional_attributes.iter().map(Into::into).collect();
        attributes.append(&mut additional_attributes);

        Ok(ServiceFoundResponse { id: peer_id, protocol: Some(protocol), attributes })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profile::{build_l2cap_descriptor, tests::rfcomm_service_definition};
    use {
        bt_rfcomm::ServerChannel, fidl_fuchsia_bluetooth as fidl_bt,
        fuchsia_bluetooth::profile::DataElement,
    };

    /// Returns the expected attributes in raw form.
    fn expected_attributes() -> Vec<bredr::Attribute> {
        vec![
            bredr::Attribute {
                id: 4,
                element: bredr::DataElement::Sequence(vec![Some(Box::new(
                    bredr::DataElement::Sequence(vec![
                        Some(Box::new(bredr::DataElement::Uuid(fidl_bt::Uuid {
                            value: [251, 52, 155, 95, 128, 0, 0, 128, 0, 16, 0, 0, 0, 1, 0, 0],
                        }))),
                        Some(Box::new(bredr::DataElement::Uint16(20))),
                    ]),
                ))]),
            },
            bredr::Attribute {
                id: 1,
                element: bredr::DataElement::Sequence(vec![Some(Box::new(
                    bredr::DataElement::Uuid(fidl_bt::Uuid {
                        value: [251, 52, 155, 95, 128, 0, 0, 128, 0, 16, 0, 0, 10, 17, 0, 0],
                    }),
                ))]),
            },
            bredr::Attribute {
                id: 9,
                element: bredr::DataElement::Sequence(vec![Some(Box::new(
                    bredr::DataElement::Sequence(vec![
                        Some(Box::new(bredr::DataElement::Uuid(fidl_bt::Uuid {
                            value: [251, 52, 155, 95, 128, 0, 0, 128, 0, 16, 0, 0, 10, 17, 0, 0],
                        }))),
                        Some(Box::new(bredr::DataElement::Uint16(258))),
                    ]),
                ))]),
            },
            bredr::Attribute { id: 9216, element: bredr::DataElement::Uint8(10) },
        ]
    }

    /// Tests operations on a ServiceRecord.
    #[test]
    fn test_service_record() {
        let primary_psm = Psm::new(20);
        let additional_psm = Psm::new(10);
        let additional = vec![additional_psm].into_iter().collect();
        let ids = vec![bredr::ServiceClassProfileIdentifier::AudioSource].into_iter().collect();
        let descs = vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AudioSource,
            major_version: 1,
            minor_version: 2,
        }];
        let additional_attrs = vec![Attribute { id: 0x2400, element: DataElement::Uint8(10) }];
        let mut service_record = ServiceRecord::new(
            ids,
            build_l2cap_descriptor(primary_psm).iter().map(Into::into).collect(),
            additional,
            descs,
            additional_attrs,
        );

        // Creating the initial ServiceRecord should not be registered.
        assert_eq!(false, service_record.is_registered());
        assert!(service_record
            .contains_service_class_identifier(&bredr::ServiceClassProfileIdentifier::AudioSource));
        let expected_psms: HashSet<_> = vec![additional_psm, primary_psm].into_iter().collect();
        assert_eq!(expected_psms, service_record.psms());

        // Register the record, as ServiceManager would, by updating the unique handles.
        let peer_id = PeerId(123);
        let handle: RegisteredServiceId = RegisteredServiceId::new(peer_id, 99);
        service_record.register_service_record(handle);
        assert_eq!(true, service_record.is_registered());

        let response = service_record.to_service_found_response().expect("conversion should work");
        assert_eq!(response.id, peer_id);
        assert_eq!(
            response.protocol,
            Some(vec![bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![bredr::DataElement::Uint16(primary_psm.into())]
            }])
        );
        assert_eq!(response.attributes, expected_attributes());
    }

    #[test]
    fn service_record_with_rfcomm() {
        let example_channel = ServerChannel::try_from(4).expect("valid");
        let (_, mut rfcomm_record) = rfcomm_service_definition(example_channel);
        assert!(!rfcomm_record.is_registered());

        // RFCOMM should be associated with this record.
        let expected_psms = vec![Psm::RFCOMM].into_iter().collect();
        assert_eq!(rfcomm_record.psms(), expected_psms);

        // Registration is OK.
        let handle = RegisteredServiceId::new(PeerId(6313), /* handle= */ 9);
        rfcomm_record.register_service_record(handle);
        assert!(rfcomm_record.is_registered());

        let response = rfcomm_record.to_service_found_response().expect("valid record");
        assert_eq!(
            response.protocol,
            Some(vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Rfcomm,
                    params: vec![bredr::DataElement::Uint8(example_channel.into())]
                }
            ])
        );
    }

    #[test]
    fn reusing_same_handle_has_unique_registered_service_id() {
        let id = PeerId(234);
        let handle = 900;
        let reg_id = RegisteredServiceId::new(id, handle);
        let duplicate_reg_id = RegisteredServiceId::new(id, handle);

        assert_ne!(reg_id, duplicate_reg_id);
    }
}
