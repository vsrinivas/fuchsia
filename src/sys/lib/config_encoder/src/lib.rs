// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! Library for resolving and encoding the runtime configuration values of a component.

use cm_rust::{
    ConfigChecksum, ConfigDecl, ConfigField as ConfigFieldDecl, ConfigNestedValueType,
    ConfigValueType, ListValue, SingleValue, Value, ValueSpec, ValuesData,
};
use dynfidl::{BasicField, Field, Structure, VectorField};
use thiserror::Error;

/// The resolved configuration for a component.
#[derive(Clone, Debug, PartialEq)]
pub struct ConfigFields {
    /// A list of all resolved fields, in the order of the compiled manifest.
    pub fields: Vec<ConfigField>,
    /// A checksum from the compiled manifest.
    pub checksum: ConfigChecksum,
}

impl ConfigFields {
    /// Resolve a component's configuration values according to its declared schema. Should not fail if
    /// `decl` and `specs` are well-formed.
    pub fn resolve(decl: &ConfigDecl, specs: ValuesData) -> Result<Self, ResolutionError> {
        if decl.checksum != specs.checksum {
            return Err(ResolutionError::ChecksumFailure {
                expected: decl.checksum.clone(),
                received: specs.checksum.clone(),
            });
        }

        if decl.fields.len() != specs.values.len() {
            return Err(ResolutionError::WrongNumberOfValues {
                expected: decl.fields.len(),
                received: specs.values.len(),
            });
        }

        let fields = decl
            .fields
            .iter()
            .zip(specs.values.into_iter())
            .map(|(decl_field, spec_field)| {
                ConfigField::resolve(spec_field, &decl_field).map_err(|source| {
                    ResolutionError::InvalidValue { key: decl_field.key.clone(), source }
                })
            })
            .collect::<Result<Vec<ConfigField>, _>>()?;

        Ok(Self { fields, checksum: decl.checksum.clone() })
    }

    /// Encode the resolved fields as a FIDL struct with every field non-nullable.
    ///
    /// The first two bytes of the encoded buffer are a little-endian unsigned integer which denotes
    /// the `checksum_length`. Bytes `2..2+checksum_length` are used to store the declaration
    /// checksum. All remaining bytes are used to store the FIDL header and struct.
    pub fn encode_as_fidl_struct(self) -> Vec<u8> {
        let Self { fields, checksum: ConfigChecksum::Sha256(checksum) } = self;
        let mut structure = Structure::default();
        for ConfigField { value, .. } in fields {
            structure = match value {
                Value::Single(SingleValue::Flag(b)) => {
                    structure.field(Field::Basic(BasicField::Bool(b)))
                }
                Value::Single(SingleValue::Unsigned8(n)) => {
                    structure.field(Field::Basic(BasicField::UInt8(n)))
                }
                Value::Single(SingleValue::Unsigned16(n)) => {
                    structure.field(Field::Basic(BasicField::UInt16(n)))
                }
                Value::Single(SingleValue::Unsigned32(n)) => {
                    structure.field(Field::Basic(BasicField::UInt32(n)))
                }
                Value::Single(SingleValue::Unsigned64(n)) => {
                    structure.field(Field::Basic(BasicField::UInt64(n)))
                }
                Value::Single(SingleValue::Signed8(n)) => {
                    structure.field(Field::Basic(BasicField::Int8(n)))
                }
                Value::Single(SingleValue::Signed16(n)) => {
                    structure.field(Field::Basic(BasicField::Int16(n)))
                }
                Value::Single(SingleValue::Signed32(n)) => {
                    structure.field(Field::Basic(BasicField::Int32(n)))
                }
                Value::Single(SingleValue::Signed64(n)) => {
                    structure.field(Field::Basic(BasicField::Int64(n)))
                }
                Value::Single(SingleValue::Text(s)) => {
                    // TODO(https://fxbug.dev/88174) improve string representation too
                    structure.field(Field::Vector(VectorField::UInt8Vector(s.into_bytes())))
                }
                Value::List(ListValue::FlagList(b)) => {
                    structure.field(Field::Vector(VectorField::BoolVector(b)))
                }
                Value::List(ListValue::Unsigned8List(n)) => {
                    structure.field(Field::Vector(VectorField::UInt8Vector(n)))
                }
                Value::List(ListValue::Unsigned16List(n)) => {
                    structure.field(Field::Vector(VectorField::UInt16Vector(n)))
                }
                Value::List(ListValue::Unsigned32List(n)) => {
                    structure.field(Field::Vector(VectorField::UInt32Vector(n)))
                }
                Value::List(ListValue::Unsigned64List(n)) => {
                    structure.field(Field::Vector(VectorField::UInt64Vector(n)))
                }
                Value::List(ListValue::Signed8List(n)) => {
                    structure.field(Field::Vector(VectorField::Int8Vector(n)))
                }
                Value::List(ListValue::Signed16List(n)) => {
                    structure.field(Field::Vector(VectorField::Int16Vector(n)))
                }
                Value::List(ListValue::Signed32List(n)) => {
                    structure.field(Field::Vector(VectorField::Int32Vector(n)))
                }
                Value::List(ListValue::Signed64List(n)) => {
                    structure.field(Field::Vector(VectorField::Int64Vector(n)))
                }
                Value::List(ListValue::TextList(s)) => structure.field(Field::Vector(
                    // TODO(https://fxbug.dev/88174) improve string representation too
                    VectorField::UInt8VectorVector(s.into_iter().map(|s| s.into_bytes()).collect()),
                )),
            };
        }

        let mut buf = Vec::new();
        buf.extend((checksum.len() as u16).to_le_bytes());
        buf.extend(checksum);
        buf.extend(structure.encode_persistent());
        buf
    }
}

/// A single resolved configuration field.
#[derive(Clone, Debug, PartialEq)]
pub struct ConfigField {
    /// The configuration field's key.
    pub key: String,

    /// The configuration field's value.
    pub value: Value,
}

impl ConfigField {
    fn resolve(spec_field: ValueSpec, decl_field: &ConfigFieldDecl) -> Result<Self, ValueError> {
        let key = decl_field.key.clone();

        match (&spec_field.value, &decl_field.type_) {
            (Value::Single(SingleValue::Flag(_)), ConfigValueType::Bool)
            | (Value::Single(SingleValue::Unsigned8(_)), ConfigValueType::Uint8)
            | (Value::Single(SingleValue::Unsigned16(_)), ConfigValueType::Uint16)
            | (Value::Single(SingleValue::Unsigned32(_)), ConfigValueType::Uint32)
            | (Value::Single(SingleValue::Unsigned64(_)), ConfigValueType::Uint64)
            | (Value::Single(SingleValue::Signed8(_)), ConfigValueType::Int8)
            | (Value::Single(SingleValue::Signed16(_)), ConfigValueType::Int16)
            | (Value::Single(SingleValue::Signed32(_)), ConfigValueType::Int32)
            | (Value::Single(SingleValue::Signed64(_)), ConfigValueType::Int64) => (),
            (Value::Single(SingleValue::Text(text)), ConfigValueType::String { max_size }) => {
                let max_size = *max_size as usize;
                if text.len() > max_size {
                    return Err(ValueError::StringTooLong { max: max_size, actual: text.len() });
                }
            }
            (Value::List(list), ConfigValueType::Vector { nested_type, max_count }) => {
                let max_count = *max_count as usize;
                let actual_count = match (list, nested_type) {
                    (ListValue::FlagList(l), ConfigNestedValueType::Bool) => l.len(),
                    (ListValue::Unsigned8List(l), ConfigNestedValueType::Uint8) => l.len(),
                    (ListValue::Unsigned16List(l), ConfigNestedValueType::Uint16) => l.len(),
                    (ListValue::Unsigned32List(l), ConfigNestedValueType::Uint32) => l.len(),
                    (ListValue::Unsigned64List(l), ConfigNestedValueType::Uint64) => l.len(),
                    (ListValue::Signed8List(l), ConfigNestedValueType::Int8) => l.len(),
                    (ListValue::Signed16List(l), ConfigNestedValueType::Int16) => l.len(),
                    (ListValue::Signed32List(l), ConfigNestedValueType::Int32) => l.len(),
                    (ListValue::Signed64List(l), ConfigNestedValueType::Int64) => l.len(),
                    (ListValue::TextList(l), ConfigNestedValueType::String { max_size }) => {
                        let max_size = *max_size as usize;
                        for (i, s) in l.iter().enumerate() {
                            if s.len() > max_size {
                                return Err(ValueError::VectorElementInvalid {
                                    offset: i,
                                    source: Box::new(ValueError::StringTooLong {
                                        max: max_size,
                                        actual: s.len(),
                                    }),
                                });
                            }
                        }
                        l.len()
                    }
                    (other_list, other_ty) => {
                        return Err(ValueError::TypeMismatch {
                            expected: format!("{:?}", other_ty),
                            received: format!("{:?}", other_list),
                        })
                    }
                };

                if actual_count > max_count {
                    return Err(ValueError::VectorTooLong { max: max_count, actual: actual_count });
                }
            }
            (other_val, other_ty) => {
                return Err(ValueError::TypeMismatch {
                    expected: format!("{:?}", other_ty),
                    received: format!("{:?}", other_val),
                });
            }
        }

        Ok(ConfigField { key, value: spec_field.value })
    }
}

#[derive(Clone, Debug, Error, PartialEq)]
#[allow(missing_docs)]
pub enum ResolutionError {
    #[error("Checksums in declaration and value file do not match. Expected {expected:04x?}, received {received:04x?}")]
    ChecksumFailure { expected: ConfigChecksum, received: ConfigChecksum },

    #[error("Value file has a different number of values ({received}) than declaration has fields ({expected}).")]
    WrongNumberOfValues { expected: usize, received: usize },

    #[error("Value file has an invalid entry for `{key}`.")]
    InvalidValue {
        key: String,
        #[source]
        source: ValueError,
    },
}

#[derive(Clone, Debug, Error, PartialEq)]
#[allow(missing_docs)]
pub enum ValueError {
    #[error("Value of type `{received}` does not match declaration of type {expected}.")]
    TypeMismatch { expected: String, received: String },

    #[error("Received string of length {actual} for a field with a max of {max}.")]
    StringTooLong { max: usize, actual: usize },

    #[error("Received vector of length {actual} for a field with a max of {max}.")]
    VectorTooLong { max: usize, actual: usize },

    #[error("Vector element at {offset} index is invalid.")]
    VectorElementInvalid {
        offset: usize,
        #[source]
        source: Box<Self>,
    },
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_component_config_ext::{config_decl, values_data};

    use ListValue::*;
    use SingleValue::*;
    use Value::*;

    #[test]
    fn basic_success() {
        let decl = config_decl! {
            ck@ ConfigChecksum::Sha256([0; 32]),
            my_flag: { bool },
            my_uint8: { uint8 },
            my_uint16: { uint16 },
            my_uint32: { uint32 },
            my_uint64: { uint64 },
            my_int8: { int8 },
            my_int16: { int16 },
            my_int32: { int32 },
            my_int64: { int64 },
            my_string: { string, max_size: 100 },
            my_vector_of_flag: { vector, element: bool, max_count: 100 },
            my_vector_of_uint8: { vector, element: uint8, max_count: 100 },
            my_vector_of_uint16: { vector, element: uint16, max_count: 100 },
            my_vector_of_uint32: { vector, element: uint32, max_count: 100 },
            my_vector_of_uint64: { vector, element: uint64, max_count: 100 },
            my_vector_of_int8: { vector, element: int8, max_count: 100 },
            my_vector_of_int16: { vector, element: int16, max_count: 100 },
            my_vector_of_int32: { vector, element: int32, max_count: 100 },
            my_vector_of_int64: { vector, element: int64, max_count: 100 },
            my_vector_of_string: {
                vector,
                element: { string, max_size: 100 },
                max_count: 100
            },
        };

        let specs = values_data![
            ck@ decl.checksum.clone(),
            Single(Flag(false)),
            Single(Unsigned8(255u8)),
            Single(Unsigned16(65535u16)),
            Single(Unsigned32(4000000000u32)),
            Single(Unsigned64(8000000000u64)),
            Single(Signed8(-127i8)),
            Single(Signed16(-32766i16)),
            Single(Signed32(-2000000000i32)),
            Single(Signed64(-4000000000i64)),
            Single(Text("hello, world!".into())),
            List(FlagList(vec![true, false])),
            List(Unsigned8List(vec![1, 2, 3])),
            List(Unsigned16List(vec![2, 3, 4])),
            List(Unsigned32List(vec![3, 4, 5])),
            List(Unsigned64List(vec![4, 5, 6])),
            List(Signed8List(vec![-1, -2, 3])),
            List(Signed16List(vec![-2, -3, 4])),
            List(Signed32List(vec![-3, -4, 5])),
            List(Signed64List(vec![-4, -5, 6])),
            List(TextList(vec!["valid".into(), "valid".into()])),
        ];

        let expected = ConfigFields {
            fields: vec![
                ConfigField { key: "my_flag".to_string(), value: Single(Flag(false)) },
                ConfigField { key: "my_uint8".to_string(), value: Single(Unsigned8(255)) },
                ConfigField { key: "my_uint16".to_string(), value: Single(Unsigned16(65535)) },
                ConfigField { key: "my_uint32".to_string(), value: Single(Unsigned32(4000000000)) },
                ConfigField { key: "my_uint64".to_string(), value: Single(Unsigned64(8000000000)) },
                ConfigField { key: "my_int8".to_string(), value: Single(Signed8(-127)) },
                ConfigField { key: "my_int16".to_string(), value: Single(Signed16(-32766)) },
                ConfigField { key: "my_int32".to_string(), value: Single(Signed32(-2000000000)) },
                ConfigField { key: "my_int64".to_string(), value: Single(Signed64(-4000000000)) },
                ConfigField {
                    key: "my_string".to_string(),
                    value: Single(Text("hello, world!".into())),
                },
                ConfigField {
                    key: "my_vector_of_flag".to_string(),
                    value: List(FlagList(vec![true, false])),
                },
                ConfigField {
                    key: "my_vector_of_uint8".to_string(),
                    value: List(Unsigned8List(vec![1, 2, 3])),
                },
                ConfigField {
                    key: "my_vector_of_uint16".to_string(),
                    value: List(Unsigned16List(vec![2, 3, 4])),
                },
                ConfigField {
                    key: "my_vector_of_uint32".to_string(),
                    value: List(Unsigned32List(vec![3, 4, 5])),
                },
                ConfigField {
                    key: "my_vector_of_uint64".to_string(),
                    value: List(Unsigned64List(vec![4, 5, 6])),
                },
                ConfigField {
                    key: "my_vector_of_int8".to_string(),
                    value: List(Signed8List(vec![-1, -2, 3])),
                },
                ConfigField {
                    key: "my_vector_of_int16".to_string(),
                    value: List(Signed16List(vec![-2, -3, 4])),
                },
                ConfigField {
                    key: "my_vector_of_int32".to_string(),
                    value: List(Signed32List(vec![-3, -4, 5])),
                },
                ConfigField {
                    key: "my_vector_of_int64".to_string(),
                    value: List(Signed64List(vec![-4, -5, 6])),
                },
                ConfigField {
                    key: "my_vector_of_string".to_string(),
                    value: List(TextList(vec!["valid".into(), "valid".into()])),
                },
            ],
            checksum: decl.checksum.clone(),
        };
        assert_eq!(ConfigFields::resolve(&decl, specs).unwrap(), expected);
    }

    #[test]
    fn checksums_must_match() {
        let expected = ConfigChecksum::Sha256([0; 32]);
        let received = ConfigChecksum::Sha256([0xFF; 32]);
        let decl = config_decl! {
            ck@ expected.clone(),
            foo: { bool },
        };
        let specs = values_data! [
            ck@ received.clone(),
            Value::Single(SingleValue::Flag(true)),
        ];
        assert_eq!(
            ConfigFields::resolve(&decl, specs).unwrap_err(),
            ResolutionError::ChecksumFailure { expected, received }
        );
    }

    #[test]
    fn too_many_values_fails() {
        let decl = config_decl! {
            ck@ ConfigChecksum::Sha256([0; 32]),
            foo: { bool },
        };
        let specs = values_data! [
            ck@ decl.checksum.clone(),
            Value::Single(SingleValue::Flag(true)),
            Value::Single(SingleValue::Flag(false)),
        ];
        assert_eq!(
            ConfigFields::resolve(&decl, specs).unwrap_err(),
            ResolutionError::WrongNumberOfValues { expected: 1, received: 2 }
        );
    }

    #[test]
    fn not_enough_values_fails() {
        let decl = config_decl! {
            ck@ ConfigChecksum::Sha256([0; 32]),
            foo: { bool },
        };
        let specs = values_data! {
            ck@ decl.checksum.clone(),
        };
        assert_eq!(
            ConfigFields::resolve(&decl, specs).unwrap_err(),
            ResolutionError::WrongNumberOfValues { expected: 1, received: 0 }
        );
    }

    #[test]
    fn string_length_is_validated() {
        let decl = config_decl! {
            ck@ ConfigChecksum::Sha256([0; 32]),
            foo: { string, max_size: 10 },
        };
        let specs = values_data! [
            ck@ decl.checksum.clone(),
            Value::Single(SingleValue::Text("hello, world!".into())),
        ];
        assert_eq!(
            ConfigFields::resolve(&decl, specs).unwrap_err(),
            ResolutionError::InvalidValue {
                key: "foo".to_string(),
                source: ValueError::StringTooLong { max: 10, actual: 13 },
            }
        );
    }

    #[test]
    fn vector_length_is_validated() {
        let decl = config_decl! {
            ck@ ConfigChecksum::Sha256([0; 32]),
            foo: { vector, element: uint8, max_count: 2 },
        };
        let specs = values_data! [
            ck@ decl.checksum.clone(),
            Value::List(ListValue::Unsigned8List(vec![1, 2, 3])),
        ];
        assert_eq!(
            ConfigFields::resolve(&decl, specs).unwrap_err(),
            ResolutionError::InvalidValue {
                key: "foo".to_string(),
                source: ValueError::VectorTooLong { max: 2, actual: 3 },
            }
        );
    }

    #[test]
    fn vector_elements_validated() {
        let decl = config_decl! {
            ck@ ConfigChecksum::Sha256([0; 32]),
            foo: { vector, element: { string, max_size: 5 }, max_count: 2 },
        };
        let specs = values_data! [
            ck@ decl.checksum.clone(),
            Value::List(ListValue::TextList(vec![
                "valid".into(),
                "invalid".into(),
            ])),
        ];
        assert_eq!(
            ConfigFields::resolve(&decl, specs).unwrap_err(),
            ResolutionError::InvalidValue {
                key: "foo".to_string(),
                source: ValueError::VectorElementInvalid {
                    offset: 1,
                    source: Box::new(ValueError::StringTooLong { max: 5, actual: 7 })
                },
            }
        );
    }

    macro_rules! type_mismatch_test {
        ($test_name:ident: { $($ty_toks:tt)* }, $valid_spec:pat) => {
            #[test]
            fn $test_name() {
                let type_ = fidl_fuchsia_component_config_ext::config_ty!($($ty_toks)*);
                let decl = ConfigFieldDecl { key: "test_key".to_string(), type_ };
                for value in [
                    // one value of each type
                    Single(Flag(true)),
                    Single(Unsigned8(1)),
                    Single(Unsigned16(1)),
                    Single(Unsigned32(1)),
                    Single(Unsigned64(1)),
                    Single(Signed8(1)),
                    Single(Signed16(1)),
                    Single(Signed32(1)),
                    Single(Signed64(1)),
                    Single(Text("".to_string())),
                    List(FlagList(vec![])),
                    List(Unsigned8List(vec![])),
                    List(Unsigned16List(vec![])),
                    List(Unsigned32List(vec![])),
                    List(Unsigned64List(vec![])),
                    List(Signed8List(vec![])),
                    List(Signed16List(vec![])),
                    List(Signed32List(vec![])),
                    List(Signed64List(vec![])),
                    List(TextList(vec![])),
                ] {
                    let should_succeed = matches!(value, $valid_spec);
                    let spec = ValueSpec { value };
                    match ConfigField::resolve(spec, &decl) {
                        Ok(..) if should_succeed => (),
                        Err(ValueError::TypeMismatch { .. }) if !should_succeed => (),
                        other => panic!(
                            "test case {:?} received unexpected resolved value {:#?}",
                            decl, other
                        ),
                    }
                }
            }
        };
    }

    type_mismatch_test!(bool_type_mismatches: { bool }, Single(Flag(..)));
    type_mismatch_test!(uint8_type_mismatches:  { uint8 },  Single(Unsigned8(..)));
    type_mismatch_test!(uint16_type_mismatches: { uint16 }, Single(Unsigned16(..)));
    type_mismatch_test!(uint32_type_mismatches: { uint32 }, Single(Unsigned32(..)));
    type_mismatch_test!(uint64_type_mismatches: { uint64 }, Single(Unsigned64(..)));
    type_mismatch_test!(int8_type_mismatches:  { int8 },  Single(Signed8(..)));
    type_mismatch_test!(int16_type_mismatches: { int16 }, Single(Signed16(..)));
    type_mismatch_test!(int32_type_mismatches: { int32 }, Single(Signed32(..)));
    type_mismatch_test!(int64_type_mismatches: { int64 }, Single(Signed64(..)));
    type_mismatch_test!(string_type_mismatches: { string, max_size: 10 }, Single(Text(..)));

    type_mismatch_test!(
        bool_vector_type_mismatches: { vector, element: bool, max_count: 1 }, List(FlagList(..))
    );
    type_mismatch_test!(
        uint8_vector_type_mismatches:
        { vector, element: uint8, max_count: 1 },
        List(Unsigned8List(..))
    );
    type_mismatch_test!(
        uint16_vector_type_mismatches:
        { vector, element: uint16, max_count: 1 },
        List(Unsigned16List(..))
    );
    type_mismatch_test!(
        uint32_vector_type_mismatches:
        { vector, element: uint32, max_count: 1 },
        List(Unsigned32List(..))
    );
    type_mismatch_test!(
        uint64_vector_type_mismatches:
        { vector, element: uint64, max_count: 1 },
        List(Unsigned64List(..))
    );
    type_mismatch_test!(
        int8_vector_type_mismatches:
        { vector, element: int8, max_count: 1 },
        List(Signed8List(..))
    );
    type_mismatch_test!(
        int16_vector_type_mismatches:
        { vector, element: int16, max_count: 1 },
        List(Signed16List(..))
    );
    type_mismatch_test!(
        int32_vector_type_mismatches:
        { vector, element: int32, max_count: 1 },
        List(Signed32List(..))
    );
    type_mismatch_test!(
        int64_vector_type_mismatches:
        { vector, element: int64, max_count: 1 },
        List(Signed64List(..))
    );
    type_mismatch_test!(
        string_vector_type_mismatches:
        { vector, element: { string, max_size: 10 }, max_count: 1 },
        List(TextList(..))
    );
}
