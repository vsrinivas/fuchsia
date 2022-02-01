// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// declare a [`cm_rust::ConfigValueType`] from a shorthand similar to the CML syntax
#[macro_export]
macro_rules! config_ty {
    (bool) => {
        cm_rust::ConfigValueType::Bool
    };
    (uint8) => {
        cm_rust::ConfigValueType::Uint8
    };
    (uint16) => {
        cm_rust::ConfigValueType::Uint16
    };
    (uint32) => {
        cm_rust::ConfigValueType::Uint32
    };
    (uint64) => {
        cm_rust::ConfigValueType::Uint64
    };
    (int8) => {
        cm_rust::ConfigValueType::Int8
    };
    (int16) => {
        cm_rust::ConfigValueType::Int16
    };
    (int32) => {
        cm_rust::ConfigValueType::Int32
    };
    (int64) => {
        cm_rust::ConfigValueType::Int64
    };
    (string, max_size: $size:expr ) => {
        cm_rust::ConfigValueType::String { max_size: $size }
    };
    (vector, element: bool, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Bool,
        }
    };
    (vector, element: uint8, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Uint8,
        }
    };
    (vector, element: uint16, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Uint16,
        }
    };
    (vector, element: uint32, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Uint32,
        }
    };
    (vector, element: uint64, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Uint64,
        }
    };
    (vector, element: int8, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Int8,
        }
    };
    (vector, element: int16, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Int16,
        }
    };
    (vector, element: int32, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Int32,
        }
    };
    (vector, element: int64, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::Int64,
        }
    };
    (vector, element: { string, max_size: $size:expr }, max_count: $count:expr ) => {
        cm_rust::ConfigValueType::Vector {
            max_count: $count,
            nested_type: cm_rust::ConfigNestedValueType::String { max_size: $size },
        }
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
                type_: $crate::config_ty!($($type_toks)+ ),
            });
        )*
        cm_rust::ConfigDecl {
            fields,
            checksum: $checksum,
            value_source: cm_rust::ConfigValueSource::PackagePath("fake.cvf".to_string()),
        }
    }};
}

/// wraps a list of values in the verbose optional table elements
#[macro_export]
macro_rules! values_data {
    [ck@ $checksum:expr, $($value:expr,)*] => {{
        let mut values = vec![];
        $(
            values.push(cm_rust::ValueSpec {
                value: $value,
            });
        )*
        cm_rust::ValuesData {
            values,
            checksum: $checksum
        }
    }};
}
