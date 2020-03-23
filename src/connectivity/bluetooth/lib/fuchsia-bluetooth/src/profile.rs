// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_bredr::{
        Attribute, DataElement, ProfileDescriptor, ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
    },
    std::convert::TryInto,
};

use crate::types::Uuid;

/// Try to interpret a DataElement as a ProfileDesciptor.
/// Returns None if the DataElement is not in the correct format to represent a ProfileDescriptor.
pub fn elem_to_profile_descriptor(elem: &DataElement) -> Option<ProfileDescriptor> {
    if let DataElement::Sequence(seq) = elem {
        if seq.len() != 2 {
            return None;
        }

        if seq[0].is_none() {
            return None;
        }
        let profile_id = match **seq[0].as_ref().expect("not none") {
            DataElement::Uuid(uuid) => {
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
            DataElement::Uint16(val) => val.to_be_bytes(),
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
pub fn find_profile_descriptors(attributes: &[Attribute]) -> Result<Vec<ProfileDescriptor>, Error> {
    for attr in attributes {
        match attr.id {
            ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST => {
                if let DataElement::Sequence(profiles) = &attr.element {
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

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth_bredr::ServiceClassProfileIdentifier;

    #[test]
    fn test_find_descriptors_fails_with_no_descriptors() {
        assert!(find_profile_descriptors(&[]).is_err());

        let mut attributes =
            vec![Attribute { id: 0x3001, element: DataElement::Uint32(0xF00FC0DE) }];

        assert!(find_profile_descriptors(&attributes).is_err());

        // Wrong element type
        attributes.push(Attribute {
            id: ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: DataElement::Uint32(0xABADC0DE),
        });

        assert!(find_profile_descriptors(&attributes).is_err());

        // Empty sequence
        attributes[1].element = DataElement::Sequence(vec![]);

        assert!(find_profile_descriptors(&attributes).is_err());
    }

    #[test]
    fn test_find_descriptors_returns_descriptors() {
        let attributes = vec![Attribute {
            id: ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: DataElement::Sequence(vec![
                Some(Box::new(DataElement::Sequence(vec![
                    Some(Box::new(DataElement::Uuid(Uuid::new16(0x1101).into()))),
                    Some(Box::new(DataElement::Uint16(0x0103))),
                ]))),
                Some(Box::new(DataElement::Sequence(vec![
                    Some(Box::new(DataElement::Uuid(Uuid::new16(0x113A).into()))),
                    Some(Box::new(DataElement::Uint16(0x0302))),
                ]))),
            ]),
        }];

        let result = find_profile_descriptors(&attributes);
        assert!(result.is_ok());
        let result = result.expect("result");
        assert_eq!(2, result.len());

        assert_eq!(ServiceClassProfileIdentifier::SerialPort, result[0].profile_id);
        assert_eq!(1, result[0].major_version);
        assert_eq!(3, result[0].minor_version);
    }

    #[test]
    fn test_elem_to_profile_descriptor_works() {
        let element = DataElement::Sequence(vec![
            Some(Box::new(DataElement::Uuid(Uuid::new16(0x1101).into()))),
            Some(Box::new(DataElement::Uint16(0x0103))),
        ]);

        let descriptor =
            elem_to_profile_descriptor(&element).expect("descriptor should be returned");

        assert_eq!(ServiceClassProfileIdentifier::SerialPort, descriptor.profile_id);
        assert_eq!(1, descriptor.major_version);
        assert_eq!(3, descriptor.minor_version);
    }

    #[test]
    fn test_elem_to_profile_descriptor_wrong_element_types() {
        let element = DataElement::Sequence(vec![
            Some(Box::new(DataElement::Uint16(0x1101))),
            Some(Box::new(DataElement::Uint16(0x0103))),
        ]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = DataElement::Sequence(vec![
            Some(Box::new(DataElement::Uuid(Uuid::new16(0x1101).into()))),
            Some(Box::new(DataElement::Uint32(0x0103))),
        ]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = DataElement::Sequence(vec![Some(Box::new(DataElement::Uint32(0x0103)))]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = DataElement::Sequence(vec![None]);
        assert!(elem_to_profile_descriptor(&element).is_none());

        let element = DataElement::Uint32(0xDEADC0DE);
        assert!(elem_to_profile_descriptor(&element).is_none());
    }
}
