// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_bluetooth::profile::DataElement;
use num_derive::FromPrimitive;
use num_traits::FromPrimitive;
use std::convert::TryFrom;
use tracing::warn;

/// ClassDescriptorType from HID v1.1.1 5.3.4.7
#[derive(Debug, FromPrimitive, PartialEq)]
enum ClassDescriptorType {
    Report = 0x22,
    Physical = 0x23,
}

#[derive(Debug, PartialEq)]
pub struct Descriptor {
    class_descriptor_type: ClassDescriptorType,
    pub data: Vec<u8>,
}

#[derive(Debug, PartialEq)]
pub struct DescriptorList(pub Vec<Descriptor>);

#[derive(Debug, PartialEq)]
pub enum DescriptorReadError {
    DataElementNotSequence(DataElement),
    DescriptorNotSequence(DataElement),
    BadSequence(Vec<Box<DataElement>>),
    TypeNotUint8(DataElement),
    DataNotString(DataElement),
    BadType(u8),
}

impl TryFrom<DataElement> for DescriptorList {
    type Error = DescriptorReadError;

    fn try_from(data_element: DataElement) -> Result<DescriptorList, DescriptorReadError> {
        let srcs = match data_element {
            DataElement::Sequence(srcs) => srcs,
            data_element => {
                warn!("Top level data element {:?} was not a sequence when reading descptiptor list from SDP data element.", data_element);
                Err(DescriptorReadError::DataElementNotSequence(data_element))?
            }
        };

        let mut descs = Vec::new();

        for box_data_element in srcs {
            let desc = Descriptor::try_from(*box_data_element)?;
            descs.push(desc);
        }

        Ok(DescriptorList(descs))
    }
}

impl TryFrom<DataElement> for Descriptor {
    type Error = DescriptorReadError;

    // HID v1.1.1 5.3.4.7 HIDDescriptors are sequences which consist of a uint8 type and string
    // data elements. For future compamtibility, any further elements are not an error but are
    // ignored.
    fn try_from(data_element: DataElement) -> Result<Descriptor, DescriptorReadError> {
        let vec = match data_element {
            DataElement::Sequence(vec) => vec,
            _ => {
                warn!(
                    "Unexpected data element {:?} when reading descriptor list from SDP data element.",
                    data_element
                );
                Err(DescriptorReadError::DescriptorNotSequence(data_element))?
            }
        };

        let (class_descriptor_type, data) = if vec.len() >= 2 {
            let class_descriptor_type = vec[0].clone();
            let data = vec[1].clone();
            (class_descriptor_type, data)
        } else {
            warn!(
                "Unexpected sequence {:?} when reading descriptor list from SDP data elelement",
                vec
            );
            Err(DescriptorReadError::BadSequence(vec))?
        };

        let class_descriptor_type = match *class_descriptor_type {
            DataElement::Uint8(class_descriptor_type) => class_descriptor_type,
            _ => {
                warn!(
                    "Unexptected data element {:?} when reading type from data element",
                    class_descriptor_type
                );
                Err(DescriptorReadError::TypeNotUint8(*class_descriptor_type))?
            }
        };

        let class_descriptor_type = match ClassDescriptorType::from_u8(class_descriptor_type) {
            Some(class_descriptor_type) => class_descriptor_type,
            None => {
                warn!(
                    "Unexpected type {:?} when reading descriptor from SDP data elelement",
                    class_descriptor_type
                );
                Err(DescriptorReadError::BadType(class_descriptor_type))?
            }
        };

        let data = match *data {
            DataElement::Str(data) => data,
            _ => {
                warn!("Unexpected data element {:?} when reading data from SDP data element", data);
                Err(DescriptorReadError::DataNotString(*data))?
            }
        };

        Ok(Descriptor { class_descriptor_type, data })
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use assert_matches::assert_matches;

    #[fuchsia::test]
    fn success() {
        let data_elements = DataElement::Sequence(vec![
            Box::new(DataElement::Sequence(vec![
                Box::new(DataElement::Uint8(0x22)),
                Box::new(DataElement::Str(vec![0x1, 0x2, 0x3])),
            ])),
            Box::new(DataElement::Sequence(vec![
                Box::new(DataElement::Uint8(0x23)),
                Box::new(DataElement::Str(vec![0x3, 0x2, 0x1])),
            ])),
        ]);

        let expected_descriptor_list = DescriptorList(vec![
            Descriptor {
                class_descriptor_type: ClassDescriptorType::Report,
                data: vec![0x1, 0x2, 0x3],
            },
            Descriptor {
                class_descriptor_type: ClassDescriptorType::Physical,
                data: vec![0x3, 0x2, 0x1],
            },
        ]);

        let descriptor_list = DescriptorList::try_from(data_elements).expect("Read data elements.");
        assert_eq!(descriptor_list, expected_descriptor_list);
    }

    #[fuchsia::test]
    fn descriptor_not_sequence() {
        let data_element = DataElement::Uint8(0x1);

        let err = Descriptor::try_from(data_element);
        assert_matches!(err, Err(DescriptorReadError::DescriptorNotSequence(_)));
    }

    #[fuchsia::test]
    fn bad_sequence() {
        let data_element = DataElement::Sequence(vec![Box::new(DataElement::Uint8(0x22))]);

        let err = Descriptor::try_from(data_element);
        assert_matches!(err, Err(DescriptorReadError::BadSequence(_)));
    }

    #[fuchsia::test]
    fn extra_sequence_elements_ignored() {
        let data_element = DataElement::Sequence(vec![
            Box::new(DataElement::Uint8(0x22)),
            Box::new(DataElement::Str(vec![0x1, 0x2, 0x3])),
            Box::new(DataElement::Str(vec![0x4, 0x5, 0x6])),
        ]);
        let expected_descriptor = Descriptor {
            class_descriptor_type: ClassDescriptorType::Report,
            data: vec![0x1, 0x2, 0x3],
        };

        let descriptor = Descriptor::try_from(data_element).expect("Read data element");
        assert_eq!(descriptor, expected_descriptor);
    }

    #[fuchsia::test]
    fn type_not_uint8() {
        let data_element = DataElement::Sequence(vec![
            Box::new(DataElement::Uint16(0x1)),
            Box::new(DataElement::Str(vec![0x1, 0x2, 0x3])),
        ]);

        let err = Descriptor::try_from(data_element);
        assert_matches!(err, Err(DescriptorReadError::TypeNotUint8(_)));
    }

    #[fuchsia::test]
    fn data_not_string() {
        let data_element = DataElement::Sequence(vec![
            Box::new(DataElement::Uint8(0x22)),
            Box::new(DataElement::Uint16(0x1)),
        ]);

        let err = Descriptor::try_from(data_element);
        assert_matches!(err, Err(DescriptorReadError::DataNotString(_)));
    }

    #[fuchsia::test]
    fn bad_type() {
        let data_element = DataElement::Sequence(vec![
            Box::new(DataElement::Uint8(0x1)),
            Box::new(DataElement::Str(vec![0x1, 0x2, 0x3])),
        ]);

        let err = Descriptor::try_from(data_element);
        assert_matches!(err, Err(DescriptorReadError::BadType(_)));
    }
}
