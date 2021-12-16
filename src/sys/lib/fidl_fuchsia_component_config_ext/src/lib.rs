// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// declare a [`cm_rust::ConfigValueType`] from a shorthand similar to the CML syntax
#[macro_export]
macro_rules! config_ty {
    (bool) => {
        cm_rust::ConfigValueType::Bool(cm_rust::ConfigBooleanType {})
    };
    (uint8) => {
        cm_rust::ConfigValueType::Uint8(cm_rust::ConfigUnsigned8Type {})
    };
    (uint16) => {
        cm_rust::ConfigValueType::Uint16(cm_rust::ConfigUnsigned16Type {})
    };
    (uint32) => {
        cm_rust::ConfigValueType::Uint32(cm_rust::ConfigUnsigned32Type {})
    };
    (uint64) => {
        cm_rust::ConfigValueType::Uint64(cm_rust::ConfigUnsigned64Type {})
    };
    (int8) => {
        cm_rust::ConfigValueType::Int8(cm_rust::ConfigSigned8Type {})
    };
    (int16) => {
        cm_rust::ConfigValueType::Int16(cm_rust::ConfigSigned16Type {})
    };
    (int32) => {
        cm_rust::ConfigValueType::Int32(cm_rust::ConfigSigned32Type {})
    };
    (int64) => {
        cm_rust::ConfigValueType::Int64(cm_rust::ConfigSigned64Type {})
    };
    (string, max_size: $size:expr ) => {
        cm_rust::ConfigValueType::String(cm_rust::ConfigStringType { max_size: $size })
    };
    (vector, element: bool, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Bool(cm_rust::ConfigBooleanType {}),
        })
    };
    (vector, element: uint8, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Uint8(cm_rust::ConfigUnsigned8Type {}),
        })
    };
    (vector, element: uint16, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Uint16(
                cm_rust::ConfigUnsigned16Type {},
            ),
        })
    };
    (vector, element: uint32, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Uint32(
                cm_rust::ConfigUnsigned32Type {},
            ),
        })
    };
    (vector, element: uint64, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Uint64(
                cm_rust::ConfigUnsigned64Type {},
            ),
        })
    };
    (vector, element: int8, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Int8(cm_rust::ConfigSigned8Type {}),
        })
    };
    (vector, element: int16, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Int16(cm_rust::ConfigSigned16Type {}),
        })
    };
    (vector, element: int32, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Int32(cm_rust::ConfigSigned32Type {}),
        })
    };
    (vector, element: int64, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::Int64(cm_rust::ConfigSigned64Type {}),
        })
    };
    (vector, element: { string, max_size: $size:expr }, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector(cm_rust::ConfigVectorType {
            max_count: $count,
            element_type: cm_rust::ConfigVectorElementType::String(cm_rust::ConfigStringType {
                max_size: $size,
            }),
        })
    };
}

/// shorthand for declaring config decls since we don't want to depend on cmc here
#[macro_export]
macro_rules! config_decl {
    (ck@ $checksum:expr, $($key:ident: { $($type_toks:tt)+ },)*) => {{
        let mut fields = vec![];
        $(
            fields.push(cm_rust::ConfigField {
                key: stringify!($key).to_string(),
                value_type: $crate::config_ty!($($type_toks)+ ),
            });
        )*
        cm_rust::ConfigDecl {
            fields,
            declaration_checksum: $checksum,
            value_source: cm_rust::ConfigValueSource::PackagePath("fake.cvf".to_string()),
        }
    }};
}

/// wraps a list of values in the verbose optional table elements
#[macro_export]
macro_rules! values_data {
    [ck@ $checksum:expr, $($value:expr,)+] => {{
        let mut values = vec![];
        $(
            values.push(ValueSpec {
                value: Some($value),
                ..ValueSpec::EMPTY
            });
        )+
        ValuesData {
            values: Some(values),
            declaration_checksum: Some($checksum),
            ..ValuesData::EMPTY
        }
    }};
}
