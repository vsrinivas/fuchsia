// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! Library for resolving and encoding the runtime configuration values of a component.

use cm_rust::{
    ConfigChecksum, ConfigDecl, ConfigField as ConfigFieldDecl, ConfigNestedValueType,
    ConfigValueType, NativeIntoFidl, SingleValue, Value, ValueSpec, ValuesData, VectorValue,
};
use dynfidl::{BasicField, Field, Structure, VectorField};
use fidl_fuchsia_component_config as fconfig;
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
                Value::Single(SingleValue::Bool(b)) => {
                    structure.field(Field::Basic(BasicField::Bool(b)))
                }
                Value::Single(SingleValue::Uint8(n)) => {
                    structure.field(Field::Basic(BasicField::UInt8(n)))
                }
                Value::Single(SingleValue::Uint16(n)) => {
                    structure.field(Field::Basic(BasicField::UInt16(n)))
                }
                Value::Single(SingleValue::Uint32(n)) => {
                    structure.field(Field::Basic(BasicField::UInt32(n)))
                }
                Value::Single(SingleValue::Uint64(n)) => {
                    structure.field(Field::Basic(BasicField::UInt64(n)))
                }
                Value::Single(SingleValue::Int8(n)) => {
                    structure.field(Field::Basic(BasicField::Int8(n)))
                }
                Value::Single(SingleValue::Int16(n)) => {
                    structure.field(Field::Basic(BasicField::Int16(n)))
                }
                Value::Single(SingleValue::Int32(n)) => {
                    structure.field(Field::Basic(BasicField::Int32(n)))
                }
                Value::Single(SingleValue::Int64(n)) => {
                    structure.field(Field::Basic(BasicField::Int64(n)))
                }
                Value::Single(SingleValue::String(s)) => {
                    // TODO(https://fxbug.dev/88174) improve string representation too
                    structure.field(Field::Vector(VectorField::UInt8Vector(s.into_bytes())))
                }
                Value::Vector(VectorValue::BoolVector(b)) => {
                    structure.field(Field::Vector(VectorField::BoolVector(b)))
                }
                Value::Vector(VectorValue::Uint8Vector(n)) => {
                    structure.field(Field::Vector(VectorField::UInt8Vector(n)))
                }
                Value::Vector(VectorValue::Uint16Vector(n)) => {
                    structure.field(Field::Vector(VectorField::UInt16Vector(n)))
                }
                Value::Vector(VectorValue::Uint32Vector(n)) => {
                    structure.field(Field::Vector(VectorField::UInt32Vector(n)))
                }
                Value::Vector(VectorValue::Uint64Vector(n)) => {
                    structure.field(Field::Vector(VectorField::UInt64Vector(n)))
                }
                Value::Vector(VectorValue::Int8Vector(n)) => {
                    structure.field(Field::Vector(VectorField::Int8Vector(n)))
                }
                Value::Vector(VectorValue::Int16Vector(n)) => {
                    structure.field(Field::Vector(VectorField::Int16Vector(n)))
                }
                Value::Vector(VectorValue::Int32Vector(n)) => {
                    structure.field(Field::Vector(VectorField::Int32Vector(n)))
                }
                Value::Vector(VectorValue::Int64Vector(n)) => {
                    structure.field(Field::Vector(VectorField::Int64Vector(n)))
                }
                Value::Vector(VectorValue::StringVector(s)) => structure.field(Field::Vector(
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

impl Into<fconfig::ResolvedConfig> for ConfigFields {
    fn into(self) -> fconfig::ResolvedConfig {
        let checksum = self.checksum.native_into_fidl();
        let fields = self.fields.into_iter().map(|f| f.into()).collect();
        fconfig::ResolvedConfig { checksum, fields }
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
    /// Reconciles a config field schema from the manifest with a value from the value file.
    /// If the types and constraints don't match, an error is returned.
    pub fn resolve(
        spec_field: ValueSpec,
        decl_field: &ConfigFieldDecl,
    ) -> Result<Self, ValueError> {
        let key = decl_field.key.clone();

        match (&spec_field.value, &decl_field.type_) {
            (Value::Single(SingleValue::Bool(_)), ConfigValueType::Bool)
            | (Value::Single(SingleValue::Uint8(_)), ConfigValueType::Uint8)
            | (Value::Single(SingleValue::Uint16(_)), ConfigValueType::Uint16)
            | (Value::Single(SingleValue::Uint32(_)), ConfigValueType::Uint32)
            | (Value::Single(SingleValue::Uint64(_)), ConfigValueType::Uint64)
            | (Value::Single(SingleValue::Int8(_)), ConfigValueType::Int8)
            | (Value::Single(SingleValue::Int16(_)), ConfigValueType::Int16)
            | (Value::Single(SingleValue::Int32(_)), ConfigValueType::Int32)
            | (Value::Single(SingleValue::Int64(_)), ConfigValueType::Int64) => (),
            (Value::Single(SingleValue::String(text)), ConfigValueType::String { max_size }) => {
                let max_size = *max_size as usize;
                if text.len() > max_size {
                    return Err(ValueError::StringTooLong { max: max_size, actual: text.len() });
                }
            }
            (Value::Vector(list), ConfigValueType::Vector { nested_type, max_count }) => {
                let max_count = *max_count as usize;
                let actual_count = match (list, nested_type) {
                    (VectorValue::BoolVector(l), ConfigNestedValueType::Bool) => l.len(),
                    (VectorValue::Uint8Vector(l), ConfigNestedValueType::Uint8) => l.len(),
                    (VectorValue::Uint16Vector(l), ConfigNestedValueType::Uint16) => l.len(),
                    (VectorValue::Uint32Vector(l), ConfigNestedValueType::Uint32) => l.len(),
                    (VectorValue::Uint64Vector(l), ConfigNestedValueType::Uint64) => l.len(),
                    (VectorValue::Int8Vector(l), ConfigNestedValueType::Int8) => l.len(),
                    (VectorValue::Int16Vector(l), ConfigNestedValueType::Int16) => l.len(),
                    (VectorValue::Int32Vector(l), ConfigNestedValueType::Int32) => l.len(),
                    (VectorValue::Int64Vector(l), ConfigNestedValueType::Int64) => l.len(),
                    (VectorValue::StringVector(l), ConfigNestedValueType::String { max_size }) => {
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

impl Into<fconfig::ResolvedConfigField> for ConfigField {
    fn into(self) -> fconfig::ResolvedConfigField {
        fconfig::ResolvedConfigField { key: self.key, value: self.value.native_into_fidl() }
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

    use SingleValue::*;
    use Value::*;
    use VectorValue::*;

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
            Single(Bool(false)),
            Single(Uint8(255u8)),
            Single(Uint16(65535u16)),
            Single(Uint32(4000000000u32)),
            Single(Uint64(8000000000u64)),
            Single(Int8(-127i8)),
            Single(Int16(-32766i16)),
            Single(Int32(-2000000000i32)),
            Single(Int64(-4000000000i64)),
            Single(String("hello, world!".into())),
            Vector(BoolVector(vec![true, false])),
            Vector(Uint8Vector(vec![1, 2, 3])),
            Vector(Uint16Vector(vec![2, 3, 4])),
            Vector(Uint32Vector(vec![3, 4, 5])),
            Vector(Uint64Vector(vec![4, 5, 6])),
            Vector(Int8Vector(vec![-1, -2, 3])),
            Vector(Int16Vector(vec![-2, -3, 4])),
            Vector(Int32Vector(vec![-3, -4, 5])),
            Vector(Int64Vector(vec![-4, -5, 6])),
            Vector(StringVector(vec!["valid".into(), "valid".into()])),
        ];

        let expected = ConfigFields {
            fields: vec![
                ConfigField { key: "my_flag".to_string(), value: Single(Bool(false)) },
                ConfigField { key: "my_uint8".to_string(), value: Single(Uint8(255)) },
                ConfigField { key: "my_uint16".to_string(), value: Single(Uint16(65535)) },
                ConfigField { key: "my_uint32".to_string(), value: Single(Uint32(4000000000)) },
                ConfigField { key: "my_uint64".to_string(), value: Single(Uint64(8000000000)) },
                ConfigField { key: "my_int8".to_string(), value: Single(Int8(-127)) },
                ConfigField { key: "my_int16".to_string(), value: Single(Int16(-32766)) },
                ConfigField { key: "my_int32".to_string(), value: Single(Int32(-2000000000)) },
                ConfigField { key: "my_int64".to_string(), value: Single(Int64(-4000000000)) },
                ConfigField {
                    key: "my_string".to_string(),
                    value: Single(String("hello, world!".into())),
                },
                ConfigField {
                    key: "my_vector_of_flag".to_string(),
                    value: Vector(BoolVector(vec![true, false])),
                },
                ConfigField {
                    key: "my_vector_of_uint8".to_string(),
                    value: Vector(Uint8Vector(vec![1, 2, 3])),
                },
                ConfigField {
                    key: "my_vector_of_uint16".to_string(),
                    value: Vector(Uint16Vector(vec![2, 3, 4])),
                },
                ConfigField {
                    key: "my_vector_of_uint32".to_string(),
                    value: Vector(Uint32Vector(vec![3, 4, 5])),
                },
                ConfigField {
                    key: "my_vector_of_uint64".to_string(),
                    value: Vector(Uint64Vector(vec![4, 5, 6])),
                },
                ConfigField {
                    key: "my_vector_of_int8".to_string(),
                    value: Vector(Int8Vector(vec![-1, -2, 3])),
                },
                ConfigField {
                    key: "my_vector_of_int16".to_string(),
                    value: Vector(Int16Vector(vec![-2, -3, 4])),
                },
                ConfigField {
                    key: "my_vector_of_int32".to_string(),
                    value: Vector(Int32Vector(vec![-3, -4, 5])),
                },
                ConfigField {
                    key: "my_vector_of_int64".to_string(),
                    value: Vector(Int64Vector(vec![-4, -5, 6])),
                },
                ConfigField {
                    key: "my_vector_of_string".to_string(),
                    value: Vector(StringVector(vec!["valid".into(), "valid".into()])),
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
            Value::Single(SingleValue::Bool(true)),
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
            Value::Single(SingleValue::Bool(true)),
            Value::Single(SingleValue::Bool(false)),
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
            Value::Single(SingleValue::String("hello, world!".into())),
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
            Value::Vector(VectorValue::Uint8Vector(vec![1, 2, 3])),
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
            Value::Vector(VectorValue::StringVector(vec![
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
                    Single(Bool(true)),
                    Single(Uint8(1)),
                    Single(Uint16(1)),
                    Single(Uint32(1)),
                    Single(Uint64(1)),
                    Single(Int8(1)),
                    Single(Int16(1)),
                    Single(Int32(1)),
                    Single(Int64(1)),
                    Single(String("".to_string())),
                    Vector(BoolVector(vec![])),
                    Vector(Uint8Vector(vec![])),
                    Vector(Uint16Vector(vec![])),
                    Vector(Uint32Vector(vec![])),
                    Vector(Uint64Vector(vec![])),
                    Vector(Int8Vector(vec![])),
                    Vector(Int16Vector(vec![])),
                    Vector(Int32Vector(vec![])),
                    Vector(Int64Vector(vec![])),
                    Vector(StringVector(vec![])),
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

    type_mismatch_test!(bool_type_mismatches: { bool }, Single(Bool(..)));
    type_mismatch_test!(uint8_type_mismatches:  { uint8 },  Single(Uint8(..)));
    type_mismatch_test!(uint16_type_mismatches: { uint16 }, Single(Uint16(..)));
    type_mismatch_test!(uint32_type_mismatches: { uint32 }, Single(Uint32(..)));
    type_mismatch_test!(uint64_type_mismatches: { uint64 }, Single(Uint64(..)));
    type_mismatch_test!(int8_type_mismatches:  { int8 },  Single(Int8(..)));
    type_mismatch_test!(int16_type_mismatches: { int16 }, Single(Int16(..)));
    type_mismatch_test!(int32_type_mismatches: { int32 }, Single(Int32(..)));
    type_mismatch_test!(int64_type_mismatches: { int64 }, Single(Int64(..)));
    type_mismatch_test!(string_type_mismatches: { string, max_size: 10 }, Single(String(..)));

    type_mismatch_test!(
        bool_vector_type_mismatches: { vector, element: bool, max_count: 1 }, Vector(BoolVector(..))
    );
    type_mismatch_test!(
        uint8_vector_type_mismatches:
        { vector, element: uint8, max_count: 1 },
        Vector(Uint8Vector(..))
    );
    type_mismatch_test!(
        uint16_vector_type_mismatches:
        { vector, element: uint16, max_count: 1 },
        Vector(Uint16Vector(..))
    );
    type_mismatch_test!(
        uint32_vector_type_mismatches:
        { vector, element: uint32, max_count: 1 },
        Vector(Uint32Vector(..))
    );
    type_mismatch_test!(
        uint64_vector_type_mismatches:
        { vector, element: uint64, max_count: 1 },
        Vector(Uint64Vector(..))
    );
    type_mismatch_test!(
        int8_vector_type_mismatches:
        { vector, element: int8, max_count: 1 },
        Vector(Int8Vector(..))
    );
    type_mismatch_test!(
        int16_vector_type_mismatches:
        { vector, element: int16, max_count: 1 },
        Vector(Int16Vector(..))
    );
    type_mismatch_test!(
        int32_vector_type_mismatches:
        { vector, element: int32, max_count: 1 },
        Vector(Int32Vector(..))
    );
    type_mismatch_test!(
        int64_vector_type_mismatches:
        { vector, element: int64, max_count: 1 },
        Vector(Int64Vector(..))
    );
    type_mismatch_test!(
        string_vector_type_mismatches:
        { vector, element: { string, max_size: 10 }, max_count: 1 },
        Vector(StringVector(..))
    );
}
