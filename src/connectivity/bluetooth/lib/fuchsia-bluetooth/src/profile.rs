// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth as fidl_bt,
    fidl_fuchsia_bluetooth_bredr::{
        self as fidl_bredr, ProfileDescriptor, ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
    },
    std::convert::{TryFrom, TryInto},
};

use crate::types::Uuid;

/// Try to interpret a DataElement as a ProfileDesciptor.
/// Returns None if the DataElement is not in the correct format to represent a ProfileDescriptor.
pub fn elem_to_profile_descriptor(elem: &fidl_bredr::DataElement) -> Option<ProfileDescriptor> {
    if let fidl_bredr::DataElement::Sequence(seq) = elem {
        if seq.len() != 2 {
            return None;
        }

        if seq[0].is_none() {
            return None;
        }
        let profile_id = match **seq[0].as_ref().expect("not none") {
            fidl_bredr::DataElement::Uuid(uuid) => {
                let uuid: Uuid = uuid.into();
                match uuid.try_into() {
                    Err(_) => return None,
                    Ok(profile_id) => profile_id,
                }
            }
            _ => return None,
        };

        if seq[1].is_none() {
            return None;
        }
        let [major_version, minor_version] = match **seq[1].as_ref().expect("not none") {
            fidl_bredr::DataElement::Uint16(val) => val.to_be_bytes(),
            _ => return None,
        };
        return Some(ProfileDescriptor { profile_id, major_version, minor_version });
    }
    None
}

/// Find an element representing the Bluetooth Profile Descriptor List in `attributes`, and
/// convert the elements in the list into ProfileDescriptors.
/// Returns an Error if no matching element was found, or if any element of the list couldn't be converted
/// into a ProfileDescriptor.
pub fn find_profile_descriptors(
    attributes: &[fidl_bredr::Attribute],
) -> Result<Vec<ProfileDescriptor>, Error> {
    for attr in attributes {
        match attr.id {
            ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST => {
                if let fidl_bredr::DataElement::Sequence(profiles) = &attr.element {
                    let mut result = Vec::new();
                    for elem in profiles {
                        let elem = elem
                            .as_ref()
                            .ok_or(format_err!("DataElements in sequences shouldn't be null"))?;
                        result.push(
                            elem_to_profile_descriptor(&*elem)
                                .ok_or(format_err!("Couldn't convert to a ProfileDescriptor"))?,
                        );
                    }
                    if result.is_empty() {
                        return Err(format_err!("Profile Descriptor List had no profiles!"));
                    }
                    return Ok(result);
                } else {
                    return Err(format_err!(
                        "Profile Descriptor List Element was not formatted correctly"
                    ));
                }
            }
            _ => {}
        }
    }
    Err(format_err!("Profile Descriptor not found"))
}

/// The basic building block for elements in a SDP record.
/// Corresponds directly to the FIDL `DataElement` definition - with the extra
/// properties of Clone and PartialEq.
/// See [fuchsia.bluetooth.bredr.DataElement] for more documentation.
#[derive(Clone, Debug, PartialEq)]
pub enum DataElement {
    Int8(i8),
    Int16(i16),
    Int32(i32),
    Int64(i64),
    Uint8(u8),
    Uint16(u16),
    Uint32(u32),
    Uint64(u64),
    Str(String),
    Url(String),
    Uuid(fidl_bt::Uuid),
    Bool(bool),
    Sequence(Vec<Box<DataElement>>),
    Alternatives(Vec<Box<DataElement>>),
}

impl From<&fidl_bredr::DataElement> for DataElement {
    fn from(src: &fidl_bredr::DataElement) -> DataElement {
        use fidl_bredr::DataElement as fDataElement;
        match src {
            fDataElement::Int8(x) => DataElement::Int8(*x),
            fDataElement::Int16(x) => DataElement::Int16(*x),
            fDataElement::Int32(x) => DataElement::Int32(*x),
            fDataElement::Int64(x) => DataElement::Int64(*x),
            fDataElement::Uint8(x) => DataElement::Uint8(*x),
            fDataElement::Uint16(x) => DataElement::Uint16(*x),
            fDataElement::Uint32(x) => DataElement::Uint32(*x),
            fDataElement::Uint64(x) => DataElement::Uint64(*x),
            fDataElement::Str(s) => DataElement::Str(s.to_string()),
            fDataElement::Url(s) => DataElement::Url(s.to_string()),
            fDataElement::Uuid(uuid) => DataElement::Uuid(uuid.clone()),
            fDataElement::B(b) => DataElement::Bool(*b),
            fDataElement::Sequence(x) => {
                let mapped = x
                    .into_iter()
                    .filter_map(|opt| opt.as_ref().map(|t| Box::new(DataElement::from(&**t))))
                    .collect::<Vec<_>>();
                DataElement::Sequence(mapped)
            }
            fDataElement::Alternatives(x) => {
                let mapped = x
                    .into_iter()
                    .filter_map(|opt| opt.as_ref().map(|t| Box::new(DataElement::from(&**t))))
                    .collect::<Vec<_>>();
                DataElement::Alternatives(mapped)
            }
        }
    }
}

impl From<&DataElement> for fidl_bredr::DataElement {
    fn from(src: &DataElement) -> fidl_bredr::DataElement {
        use fidl_bredr::DataElement as fDataElement;
        match src {
            DataElement::Int8(x) => fDataElement::Int8(*x),
            DataElement::Int16(x) => fDataElement::Int16(*x),
            DataElement::Int32(x) => fDataElement::Int32(*x),
            DataElement::Int64(x) => fDataElement::Int64(*x),
            DataElement::Uint8(x) => fDataElement::Uint8(*x),
            DataElement::Uint16(x) => fDataElement::Uint16(*x),
            DataElement::Uint32(x) => fDataElement::Uint32(*x),
            DataElement::Uint64(x) => fDataElement::Uint64(*x),
            DataElement::Str(s) => fDataElement::Str(s.to_string()),
            DataElement::Url(s) => fDataElement::Url(s.to_string()),
            DataElement::Uuid(uuid) => fDataElement::Uuid(uuid.clone()),
            DataElement::Bool(b) => fDataElement::B(*b),
            DataElement::Sequence(x) => {
                let mapped = x
                    .into_iter()
                    .map(|t| Some(Box::new(fDataElement::from(&**t))))
                    .collect::<Vec<_>>();
                fDataElement::Sequence(mapped)
            }
            DataElement::Alternatives(x) => {
                let mapped = x
                    .into_iter()
                    .map(|t| Some(Box::new(fDataElement::from(&**t))))
                    .collect::<Vec<_>>();
                fDataElement::Alternatives(mapped)
            }
        }
    }
}

/// Information about a communications protocol.
/// Corresponds directly to the FIDL `ProtocolDescriptor` definition - with the extra
/// properties of Clone and PartialEq.
/// See [fuchsia.bluetooth.bredr.ProtocolDescriptor] for more documentation.
#[derive(Clone, Debug, PartialEq)]
pub struct ProtocolDescriptor {
    pub protocol: fidl_bredr::ProtocolIdentifier,
    pub params: Vec<DataElement>,
}

impl From<&fidl_bredr::ProtocolDescriptor> for ProtocolDescriptor {
    fn from(src: &fidl_bredr::ProtocolDescriptor) -> ProtocolDescriptor {
        let params = src.params.iter().map(|elem| DataElement::from(elem)).collect();
        ProtocolDescriptor { protocol: src.protocol, params }
    }
}

impl From<&ProtocolDescriptor> for fidl_bredr::ProtocolDescriptor {
    fn from(src: &ProtocolDescriptor) -> fidl_bredr::ProtocolDescriptor {
        let params = src.params.iter().map(|elem| fidl_bredr::DataElement::from(elem)).collect();
        fidl_bredr::ProtocolDescriptor { protocol: src.protocol, params }
    }
}

/// A generic attribute used for protocol information.
/// Corresponds directly to the FIDL `Attribute` definition - with the extra
/// properties of Clone and PartialEq.
/// See [fuchsia.bluetooth.bredr.Attribute] for more documentation.
#[derive(Clone, Debug, PartialEq)]
pub struct Attribute {
    pub id: u16,
    pub element: DataElement,
}

impl From<&fidl_bredr::Attribute> for Attribute {
    fn from(src: &fidl_bredr::Attribute) -> Attribute {
        Attribute { id: src.id, element: DataElement::from(&src.element) }
    }
}

impl From<&Attribute> for fidl_bredr::Attribute {
    fn from(src: &Attribute) -> fidl_bredr::Attribute {
        fidl_bredr::Attribute { id: src.id, element: fidl_bredr::DataElement::from(&src.element) }
    }
}

/// Human-readable information about a service.
/// Corresponds directly to the FIDL `Information` definition - with the extra
/// properties of Clone and PartialEq.
/// See [fuchsia.bluetooth.bredr.Information] for more documentation.
#[derive(Clone, Debug, PartialEq)]
pub struct Information {
    pub language: String,
    pub name: Option<String>,
    pub description: Option<String>,
    pub provider: Option<String>,
}

impl TryFrom<&fidl_bredr::Information> for Information {
    type Error = Error;

    fn try_from(src: &fidl_bredr::Information) -> Result<Information, Self::Error> {
        let language = match src.language.as_ref().map(String::as_str) {
            None | Some("") => return Err(format_err!("language must be provided")),
            Some(l) => l.to_string().clone(),
        };

        Ok(Information {
            language,
            name: src.name.clone(),
            description: src.description.clone(),
            provider: src.provider.clone(),
        })
    }
}

impl TryFrom<&Information> for fidl_bredr::Information {
    type Error = Error;

    fn try_from(src: &Information) -> Result<fidl_bredr::Information, Self::Error> {
        if src.language.is_empty() {
            return Err(format_err!("language must be provided"));
        }

        Ok(fidl_bredr::Information {
            language: Some(src.language.clone()),
            name: src.name.clone(),
            description: src.description.clone(),
            provider: src.provider.clone(),
        })
    }
}

/// Definition of a service that is to be advertised via Bluetooth BR/EDR.
/// Corresponds directly to the FIDL `ServiceDefinition` definition - with the extra
/// properties of Clone and PartialEq.
/// See [fuchsia.bluetooth.bredr.ServiceDefinition] for more documentation.
#[derive(Clone, Debug, PartialEq)]
pub struct ServiceDefinition {
    pub service_class_uuids: Vec<fidl_bt::Uuid>,
    pub protocol_descriptor_list: Vec<ProtocolDescriptor>,
    pub additional_protocol_descriptor_lists: Vec<Vec<ProtocolDescriptor>>,
    pub profile_descriptors: Vec<fidl_bredr::ProfileDescriptor>,
    pub information: Vec<Information>,
    pub additional_attributes: Vec<Attribute>,
}

impl TryFrom<&fidl_bredr::ServiceDefinition> for ServiceDefinition {
    type Error = Error;

    fn try_from(src: &fidl_bredr::ServiceDefinition) -> Result<ServiceDefinition, Self::Error> {
        let service_class_uuids =
            src.service_class_uuids.clone().ok_or(format_err!("Must contain uuids"))?;
        if service_class_uuids.is_empty() {
            return Err(format_err!("There must be at least one service class UUID"));
        }

        let protocol_descriptor_list: Vec<ProtocolDescriptor> = src
            .protocol_descriptor_list
            .as_ref()
            .map_or(vec![], |p| p.into_iter().map(|d| ProtocolDescriptor::from(d)).collect());
        let additional_protocol_descriptor_lists: Vec<Vec<ProtocolDescriptor>> =
            src.additional_protocol_descriptor_lists.as_ref().map_or(vec![], |desc_lists| {
                desc_lists
                    .into_iter()
                    .map(|desc_list| {
                        desc_list.into_iter().map(|d| ProtocolDescriptor::from(d)).collect()
                    })
                    .collect()
            });
        let profile_descriptors: Vec<fidl_bredr::ProfileDescriptor> =
            src.profile_descriptors.clone().unwrap_or(vec![]);
        let information: Result<Vec<Information>, Error> =
            src.information.as_ref().map_or(Ok(vec![]), |infos| {
                infos.into_iter().map(|i| Information::try_from(i)).collect()
            });
        let additional_attributes: Vec<Attribute> = src
            .additional_attributes
            .as_ref()
            .map_or(vec![], |attrs| attrs.into_iter().map(|a| Attribute::from(a)).collect());

        Ok(ServiceDefinition {
            service_class_uuids,
            protocol_descriptor_list,
            additional_protocol_descriptor_lists,
            profile_descriptors,
            information: information?,
            additional_attributes,
        })
    }
}

impl TryFrom<&ServiceDefinition> for fidl_bredr::ServiceDefinition {
    type Error = Error;

    fn try_from(src: &ServiceDefinition) -> Result<fidl_bredr::ServiceDefinition, Self::Error> {
        let service_class_uuids = src.service_class_uuids.clone();
        if service_class_uuids.is_empty() {
            return Err(format_err!("There must be at least one service class UUID"));
        }

        let protocol_descriptor_list: Vec<fidl_bredr::ProtocolDescriptor> = src
            .protocol_descriptor_list
            .iter()
            .map(|d| fidl_bredr::ProtocolDescriptor::from(d))
            .collect();
        let additional_protocol_descriptor_lists: Vec<Vec<fidl_bredr::ProtocolDescriptor>> = src
            .additional_protocol_descriptor_lists
            .iter()
            .map(|desc_list| {
                desc_list.into_iter().map(|d| fidl_bredr::ProtocolDescriptor::from(d)).collect()
            })
            .collect();
        let profile_descriptors: Vec<fidl_bredr::ProfileDescriptor> =
            src.profile_descriptors.clone();
        let information: Result<Vec<fidl_bredr::Information>, Error> =
            src.information.iter().map(|i| fidl_bredr::Information::try_from(i)).collect();
        let additional_attributes: Vec<fidl_bredr::Attribute> =
            src.additional_attributes.iter().map(|a| fidl_bredr::Attribute::from(a)).collect();

        Ok(fidl_bredr::ServiceDefinition {
            service_class_uuids: Some(service_class_uuids),
            protocol_descriptor_list: Some(protocol_descriptor_list),
            additional_protocol_descriptor_lists: Some(additional_protocol_descriptor_lists),
            profile_descriptors: Some(profile_descriptors),
            information: Some(information?),
            additional_attributes: Some(additional_attributes),
        })
    }
}

/// Authentication and permission requirements for an advertised service.
/// Corresponds directly to the FIDL `SecurityRequirements` definition - with the extra properties
/// of Clone and PartialEq.
/// See [fuchsia.bluetooth.bredr.SecurityRequirements] for more documentation.
#[derive(Clone, Debug, PartialEq)]
pub struct SecurityRequirements {
    authentication_required: Option<bool>,
    secure_connections_required: Option<bool>,
}

impl From<&fidl_bredr::SecurityRequirements> for SecurityRequirements {
    fn from(src: &fidl_bredr::SecurityRequirements) -> SecurityRequirements {
        SecurityRequirements {
            authentication_required: src.authentication_required,
            secure_connections_required: src.secure_connections_required,
        }
    }
}

impl From<&SecurityRequirements> for fidl_bredr::SecurityRequirements {
    fn from(src: &SecurityRequirements) -> fidl_bredr::SecurityRequirements {
        fidl_bredr::SecurityRequirements {
            authentication_required: src.authentication_required,
            secure_connections_required: src.secure_connections_required,
        }
    }
}

/// Minimum SDU size the service is capable of accepting.
/// See [fuchsia.bluetooth.bredr.ChannelParameters] for more documentation.
const MIN_RX_SDU_SIZE: u16 = 48;

/// Preferred L2CAP channel parameters for an advertised service.
/// Corresponds directly to the FIDL `ChannelParameters` definition - with the extra properties
/// of Clone and PartialEq.
/// The invariants of the FIDL definition are enforced - the max SDU size must be >= 48.
/// See [fuchsia.bluetooth.bredr.ChannelParameters] for more documentation.
#[derive(Clone, Debug, PartialEq)]
pub struct ChannelParameters {
    channel_mode: Option<fidl_bredr::ChannelMode>,
    max_rx_sdu_size: Option<u16>,
}

impl TryFrom<&fidl_bredr::ChannelParameters> for ChannelParameters {
    type Error = Error;

    fn try_from(src: &fidl_bredr::ChannelParameters) -> Result<ChannelParameters, Self::Error> {
        if let Some(size) = src.max_rx_sdu_size {
            if size < MIN_RX_SDU_SIZE {
                return Err(format_err!("Min RX SDU size too small: {:?}", size));
            }
        }

        Ok(ChannelParameters {
            channel_mode: src.channel_mode,
            max_rx_sdu_size: src.max_rx_sdu_size,
        })
    }
}

impl TryFrom<&ChannelParameters> for fidl_bredr::ChannelParameters {
    type Error = Error;

    fn try_from(src: &ChannelParameters) -> Result<fidl_bredr::ChannelParameters, Self::Error> {
        if let Some(size) = src.max_rx_sdu_size {
            if size < MIN_RX_SDU_SIZE {
                return Err(format_err!("Min RX SDU size too small: {:?}", size));
            }
        }

        Ok(fidl_bredr::ChannelParameters {
            channel_mode: src.channel_mode,
            max_rx_sdu_size: src.max_rx_sdu_size,
            security_requirements: None,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::encoding::Decodable;
    use fidl_fuchsia_bluetooth_bredr as fidl_bredr;

    #[test]
    fn test_find_descriptors_fails_with_no_descriptors() {
        assert!(find_profile_descriptors(&[]).is_err());

        let mut attributes = vec![fidl_bredr::Attribute {
            id: 0x3001,
            element: fidl_bredr::DataElement::Uint32(0xF00FC0DE),
        }];

        assert!(find_profile_descriptors(&attributes).is_err());

        // Wrong element type
        attributes.push(fidl_bredr::Attribute {
            id: fidl_bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: fidl_bredr::DataElement::Uint32(0xABADC0DE),
        });

        assert!(find_profile_descriptors(&attributes).is_err());

        // Empty sequence
        attributes[1].element = fidl_bredr::DataElement::Sequence(vec![]);

        assert!(find_profile_descriptors(&attributes).is_err());
    }

    #[test]
    fn test_find_descriptors_returns_descriptors() {
        let attributes = vec![fidl_bredr::Attribute {
            id: fidl_bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: fidl_bredr::DataElement::Sequence(vec![
                Some(Box::new(fidl_bredr::DataElement::Sequence(vec![
                    Some(Box::new(fidl_bredr::DataElement::Uuid(Uuid::new16(0x1101).into()))),
                    Some(Box::new(fidl_bredr::DataElement::Uint16(0x0103))),
                ]))),
                Some(Box::new(fidl_bredr::DataElement::Sequence(vec![
                    Some(Box::new(fidl_bredr::DataElement::Uuid(Uuid::new16(0x113A).into()))),
                    Some(Box::new(fidl_bredr::DataElement::Uint16(0x0302))),
                ]))),
            ]),
        }];

        let result = find_profile_descriptors(&attributes);
        assert!(result.is_ok());
        let result = result.expect("result");
        assert_eq!(2, result.len());

        assert_eq!(fidl_bredr::ServiceClassProfileIdentifier::SerialPort, result[0].profile_id);
        assert_eq!(1, result[0].major_version);
        assert_eq!(3, result[0].minor_version);
    }

    #[test]
    fn test_elem_to_profile_descriptor_works() {
        let element = fidl_bredr::DataElement::Sequence(vec![
            Some(Box::new(fidl_bredr::DataElement::Uuid(Uuid::new16(0x1101).into()))),
            Some(Box::new(fidl_bredr::DataElement::Uint16(0x0103))),
        ]);

        let descriptor =
            elem_to_profile_descriptor(&element).expect("descriptor should be returned");

        assert_eq!(fidl_bredr::ServiceClassProfileIdentifier::SerialPort, descriptor.profile_id);
        assert_eq!(1, descriptor.major_version);
        assert_eq!(3, descriptor.minor_version);
    }

    #[test]
    fn test_elem_to_profile_descriptor_wrong_element_types() {
        let element = fidl_bredr::DataElement::Sequence(vec![
            Some(Box::new(fidl_bredr::DataElement::Uint16(0x1101))),
            Some(Box::new(fidl_bredr::DataElement::Uint16(0x0103))),
        ]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = fidl_bredr::DataElement::Sequence(vec![
            Some(Box::new(fidl_bredr::DataElement::Uuid(Uuid::new16(0x1101).into()))),
            Some(Box::new(fidl_bredr::DataElement::Uint32(0x0103))),
        ]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = fidl_bredr::DataElement::Sequence(vec![Some(Box::new(
            fidl_bredr::DataElement::Uint32(0x0103),
        ))]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = fidl_bredr::DataElement::Sequence(vec![None]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = fidl_bredr::DataElement::Uint32(0xDEADC0DE);
        assert!(elem_to_profile_descriptor(&element).is_none());
    }

    #[test]
    fn test_invalid_information_fails_gracefully() {
        let empty_language = "".to_string();

        let invalid_local = Information {
            language: empty_language.clone(),
            name: None,
            description: None,
            provider: None,
        };
        let fidl = fidl_bredr::Information::try_from(&invalid_local);
        assert!(fidl.is_err());

        let no_lang_fidl = fidl_bredr::Information {
            language: None,
            name: None,
            description: None,
            provider: None,
        };
        let local = Information::try_from(&no_lang_fidl);
        assert!(local.is_err());

        let empty_lang_fidl = fidl_bredr::Information {
            language: Some(empty_language),
            name: None,
            description: None,
            provider: None,
        };
        let local = Information::try_from(&empty_lang_fidl);
        assert!(local.is_err());
    }

    #[test]
    fn test_service_definition_conversions() {
        let uuid = fidl_bt::Uuid { value: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15] };
        let prof_descs = vec![ProfileDescriptor {
            profile_id: fidl_bredr::ServiceClassProfileIdentifier::AvRemoteControl,
            major_version: 1,
            minor_version: 6,
        }];
        let language = "en".to_string();
        let name = "foobar".to_string();
        let description = "fake".to_string();
        let provider = "random".to_string();
        let attribute_id = 0x3001;
        let attribute_value = 0xF00FC0DE;

        let local = ServiceDefinition {
            service_class_uuids: vec![uuid],
            protocol_descriptor_list: vec![ProtocolDescriptor {
                protocol: fidl_bredr::ProtocolIdentifier::L2Cap,
                params: vec![DataElement::Uint16(10)],
            }],
            additional_protocol_descriptor_lists: vec![
                vec![ProtocolDescriptor {
                    protocol: fidl_bredr::ProtocolIdentifier::L2Cap,
                    params: vec![DataElement::Uint16(12)],
                }],
                vec![ProtocolDescriptor {
                    protocol: fidl_bredr::ProtocolIdentifier::Avdtp,
                    params: vec![DataElement::Uint16(3)],
                }],
            ],
            profile_descriptors: prof_descs.clone(),
            information: vec![Information {
                language: language.clone(),
                name: Some(name.clone()),
                description: Some(description.clone()),
                provider: Some(provider.clone()),
            }],
            additional_attributes: vec![Attribute {
                id: attribute_id,
                element: DataElement::Sequence(vec![Box::new(DataElement::Uint32(
                    attribute_value,
                ))]),
            }],
        };

        let fidl = fidl_bredr::ServiceDefinition {
            service_class_uuids: Some(vec![uuid]),
            protocol_descriptor_list: Some(vec![fidl_bredr::ProtocolDescriptor {
                protocol: fidl_bredr::ProtocolIdentifier::L2Cap,
                params: vec![fidl_bredr::DataElement::Uint16(10)],
            }]),
            additional_protocol_descriptor_lists: Some(vec![
                vec![fidl_bredr::ProtocolDescriptor {
                    protocol: fidl_bredr::ProtocolIdentifier::L2Cap,
                    params: vec![fidl_bredr::DataElement::Uint16(12)],
                }],
                vec![fidl_bredr::ProtocolDescriptor {
                    protocol: fidl_bredr::ProtocolIdentifier::Avdtp,
                    params: vec![fidl_bredr::DataElement::Uint16(3)],
                }],
            ]),
            profile_descriptors: Some(prof_descs.clone()),
            information: Some(vec![fidl_bredr::Information {
                language: Some(language.clone()),
                name: Some(name.clone()),
                description: Some(description.clone()),
                provider: Some(provider.clone()),
            }]),
            additional_attributes: Some(vec![fidl_bredr::Attribute {
                id: attribute_id,
                element: fidl_bredr::DataElement::Sequence(vec![Some(Box::new(
                    fidl_bredr::DataElement::Uint32(attribute_value),
                ))]),
            }]),
        };

        // Converting from local ServiceDefinition to the FIDL ServiceDefinition should work.
        let local_to_fidl: fidl_bredr::ServiceDefinition =
            fidl_bredr::ServiceDefinition::try_from(&local).expect("should work");
        assert_eq!(local_to_fidl, fidl);

        // Converting from FIDL ServiceDefinition to the local ServiceDefinition should work.
        let fidl_to_local: ServiceDefinition =
            ServiceDefinition::try_from(&fidl).expect("should work");
        assert_eq!(fidl_to_local, local);
    }

    #[test]
    fn test_invalid_service_definition_fails_gracefully() {
        let no_uuids_fidl = fidl_bredr::ServiceDefinition::new_empty();
        let fidl_to_local = ServiceDefinition::try_from(&no_uuids_fidl);
        assert!(fidl_to_local.is_err());

        let empty_uuids_fidl = fidl_bredr::ServiceDefinition {
            service_class_uuids: Some(vec![]),
            ..fidl_bredr::ServiceDefinition::new_empty()
        };
        let fidl_to_local = ServiceDefinition::try_from(&empty_uuids_fidl);
        assert!(fidl_to_local.is_err());
    }

    #[test]
    fn test_channel_parameters_conversions() {
        let channel_mode = Some(fidl_bredr::ChannelMode::EnhancedRetransmission);
        let max_rx_sdu_size = Some(MIN_RX_SDU_SIZE);

        let local = ChannelParameters { channel_mode, max_rx_sdu_size };
        let fidl = fidl_bredr::ChannelParameters {
            channel_mode,
            max_rx_sdu_size,
            security_requirements: None,
        };

        let local_to_fidl =
            fidl_bredr::ChannelParameters::try_from(&local).expect("conversion should work");
        assert_eq!(local_to_fidl, fidl);

        let fidl_to_local = ChannelParameters::try_from(&fidl).expect("conversion should work");
        assert_eq!(fidl_to_local, local);

        // Empty FIDL parameters is OK.
        let fidl = fidl_bredr::ChannelParameters::new_empty();
        let expected = ChannelParameters { channel_mode: None, max_rx_sdu_size: None };

        let fidl_to_local = ChannelParameters::try_from(&fidl).expect("conversion should work");
        assert_eq!(fidl_to_local, expected);
    }

    #[test]
    fn test_invalid_channel_parameters_fails_gracefully() {
        let too_small_sdu = Some(MIN_RX_SDU_SIZE - 1);
        let local = ChannelParameters { channel_mode: None, max_rx_sdu_size: too_small_sdu };
        let fidl = fidl_bredr::ChannelParameters {
            channel_mode: None,
            max_rx_sdu_size: too_small_sdu,
            security_requirements: None,
        };

        let local_to_fidl = fidl_bredr::ChannelParameters::try_from(&local);
        assert!(local_to_fidl.is_err());

        let fidl_to_local = ChannelParameters::try_from(&fidl);
        assert!(fidl_to_local.is_err());
    }

    #[test]
    fn test_security_requirements_conversions() {
        let authentication_required = Some(false);
        let secure_connections_required = Some(true);

        let local = SecurityRequirements { authentication_required, secure_connections_required };
        let fidl = fidl_bredr::SecurityRequirements {
            authentication_required,
            secure_connections_required,
        };

        let local_to_fidl = fidl_bredr::SecurityRequirements::from(&local);
        assert_eq!(local_to_fidl, fidl);

        let fidl_to_local = SecurityRequirements::from(&fidl);
        assert_eq!(fidl_to_local, local);
    }
}
