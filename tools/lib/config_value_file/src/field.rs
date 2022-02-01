// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Validating and encoding individual configuration fields.

use cm_rust::{ConfigNestedValueType, ConfigValueType, ListValue, SingleValue, Value};
use serde_json::Value as JsonValue;
use std::{
    convert::TryFrom,
    fmt::{Display, Formatter, Result as FmtResult},
    num::TryFromIntError,
};

/// Returns a FIDL value to encode in the file for the provided JSON value, if and only if the value
/// matches the type from the declaration.
pub fn config_value_from_json_value(
    val: &JsonValue,
    value_type: &ConfigValueType,
) -> Result<Value, FieldError> {
    Ok(match value_type {
        ConfigValueType::Bool => Value::Single(SingleValue::Flag(val.parse_bool()?)),
        ConfigValueType::Uint8 => Value::Single(SingleValue::Unsigned8(val.parse_u8()?)),
        ConfigValueType::Uint16 => Value::Single(SingleValue::Unsigned16(val.parse_u16()?)),
        ConfigValueType::Uint32 => Value::Single(SingleValue::Unsigned32(val.parse_u32()?)),
        ConfigValueType::Uint64 => Value::Single(SingleValue::Unsigned64(val.parse_u64()?)),
        ConfigValueType::Int8 => Value::Single(SingleValue::Signed8(val.parse_i8()?)),
        ConfigValueType::Int16 => Value::Single(SingleValue::Signed16(val.parse_i16()?)),
        ConfigValueType::Int32 => Value::Single(SingleValue::Signed32(val.parse_i32()?)),
        ConfigValueType::Int64 => Value::Single(SingleValue::Signed64(val.parse_i64()?)),
        ConfigValueType::String { max_size } => {
            Value::Single(SingleValue::Text(val.parse_string(*max_size)?))
        }
        ConfigValueType::Vector { max_count, nested_type } => {
            Value::List(list_value_from_json(val, max_count, nested_type)?)
        }
    })
}

/// Parse `val` as a list/vector of configuration values.
fn list_value_from_json(
    val: &JsonValue,
    max_count: &u32,
    nested_type: &ConfigNestedValueType,
) -> Result<ListValue, FieldError> {
    // define our array up here so its identifier is available for our helper macro
    let array = val.as_array().ok_or_else(|| FieldError::JsonTypeMismatch {
        expected: JsonTy::Array,
        received: val.ty(),
    })?;
    let max = *max_count as usize;
    if array.len() > max {
        return Err(FieldError::VectorTooLong { max, actual: array.len() });
    }

    /// Build a ListValue out of all the array elements.
    ///
    /// A macro because enum variants don't exist at the type level in Rust at time of writing.
    macro_rules! list_from_array {
        ($list_variant:ident, $val:ident => $convert:expr) => {{
            let mut list = vec![];
            for $val in array {
                list.push($convert);
            }
            ListValue::$list_variant(list)
        }};
    }

    Ok(match nested_type {
        ConfigNestedValueType::Bool => {
            list_from_array!(FlagList, v => v.parse_bool()?)
        }
        ConfigNestedValueType::Uint8 => list_from_array!(Unsigned8List, v => v.parse_u8()?),
        ConfigNestedValueType::Uint16 => {
            list_from_array!(Unsigned16List, v => v.parse_u16()?)
        }
        ConfigNestedValueType::Uint32 => {
            list_from_array!(Unsigned32List, v => v.parse_u32()?)
        }
        ConfigNestedValueType::Uint64 => {
            list_from_array!(Unsigned64List, v => v.parse_u64()?)
        }
        ConfigNestedValueType::Int8 => list_from_array!(Signed8List,  v => v.parse_i8()?),
        ConfigNestedValueType::Int16 => list_from_array!(Signed16List, v => v.parse_i16()?),
        ConfigNestedValueType::Int32 => list_from_array!(Signed32List, v => v.parse_i32()?),
        ConfigNestedValueType::Int64 => list_from_array!(Signed64List, v => v.parse_i64()?),
        ConfigNestedValueType::String { max_size } => {
            list_from_array!(TextList, v => v.parse_string(*max_size)?)
        }
    })
}

trait JsonValueExt {
    fn parse_bool(&self) -> Result<bool, FieldError>;
    fn parse_u8(&self) -> Result<u8, FieldError>;
    fn parse_u16(&self) -> Result<u16, FieldError>;
    fn parse_u32(&self) -> Result<u32, FieldError>;
    fn parse_u64(&self) -> Result<u64, FieldError>;
    fn parse_i8(&self) -> Result<i8, FieldError>;
    fn parse_i16(&self) -> Result<i16, FieldError>;
    fn parse_i32(&self) -> Result<i32, FieldError>;
    fn parse_i64(&self) -> Result<i64, FieldError>;
    fn parse_string(&self, max: u32) -> Result<String, FieldError>;
    fn ty(&self) -> JsonTy;
    fn expected(&self, ty: JsonTy) -> FieldError;
}

fn check_integer(v: &JsonValue) -> Result<(), FieldError> {
    if !v.is_number() {
        Err(FieldError::JsonTypeMismatch { expected: JsonTy::Number, received: v.ty() })
    } else if !(v.is_i64()) {
        Err(FieldError::NumberNotInteger)
    } else {
        Ok(())
    }
}

fn check_unsigned(v: &JsonValue) -> Result<(), FieldError> {
    if v.is_u64() {
        Ok(())
    } else {
        Err(FieldError::NumberNotUnsigned)
    }
}

impl JsonValueExt for JsonValue {
    fn parse_bool(&self) -> Result<bool, FieldError> {
        self.as_bool().ok_or_else(|| self.expected(JsonTy::Bool))
    }
    fn parse_u8(&self) -> Result<u8, FieldError> {
        check_integer(self)?;
        check_unsigned(self)?;
        Ok(<u8>::try_from(self.as_u64().ok_or_else(|| self.expected(JsonTy::Number))?)?)
    }
    fn parse_u16(&self) -> Result<u16, FieldError> {
        check_integer(self)?;
        check_unsigned(self)?;
        Ok(<u16>::try_from(self.as_u64().ok_or_else(|| self.expected(JsonTy::Number))?)?)
    }
    fn parse_u32(&self) -> Result<u32, FieldError> {
        check_integer(self)?;
        check_unsigned(self)?;
        Ok(<u32>::try_from(self.as_u64().ok_or_else(|| self.expected(JsonTy::Number))?)?)
    }
    fn parse_u64(&self) -> Result<u64, FieldError> {
        check_integer(self)?;
        check_unsigned(self)?;
        self.as_u64().ok_or_else(|| self.expected(JsonTy::Number))
    }
    fn parse_i8(&self) -> Result<i8, FieldError> {
        check_integer(self)?;
        Ok(<i8>::try_from(self.as_i64().ok_or_else(|| self.expected(JsonTy::Number))?)?)
    }
    fn parse_i16(&self) -> Result<i16, FieldError> {
        check_integer(self)?;
        Ok(<i16>::try_from(self.as_i64().ok_or_else(|| self.expected(JsonTy::Number))?)?)
    }
    fn parse_i32(&self) -> Result<i32, FieldError> {
        check_integer(self)?;
        Ok(<i32>::try_from(self.as_i64().ok_or_else(|| self.expected(JsonTy::Number))?)?)
    }
    fn parse_i64(&self) -> Result<i64, FieldError> {
        check_integer(self)?;
        self.as_i64().ok_or_else(|| self.expected(JsonTy::Number))
    }
    fn parse_string(&self, max: u32) -> Result<String, FieldError> {
        let max = max as usize;
        let s = self.as_str().ok_or_else(|| FieldError::JsonTypeMismatch {
            expected: JsonTy::String,
            received: self.ty(),
        })?;
        if s.len() > max {
            Err(FieldError::StringTooLong { max, actual: s.len() })
        } else {
            Ok(s.to_owned())
        }
    }

    fn ty(&self) -> JsonTy {
        match self {
            JsonValue::Null => JsonTy::Null,
            JsonValue::Bool(_) => JsonTy::Bool,
            JsonValue::Number(_) => JsonTy::Number,
            JsonValue::String(_) => JsonTy::String,
            JsonValue::Array(_) => JsonTy::Array,
            JsonValue::Object(_) => JsonTy::Object,
        }
    }

    fn expected(&self, expected: JsonTy) -> FieldError {
        FieldError::JsonTypeMismatch { expected, received: self.ty() }
    }
}

/// Error from working with a field from a configuration value file.
#[derive(Debug, thiserror::Error, PartialEq)]
#[allow(missing_docs)]
pub enum FieldError {
    #[error("Expected value of type {expected}, received {received}.")]
    JsonTypeMismatch { expected: JsonTy, received: JsonTy },

    #[error("Expected number to be unsigned.")]
    NumberNotUnsigned,

    #[error("Expected number to be an integer.")]
    NumberNotInteger,

    #[error("String of size {actual} provided for a field with maximum of {max}.")]
    StringTooLong { max: usize, actual: usize },

    #[error("Vector of count {actual} provided for a field with maximum of {max}.")]
    VectorTooLong { max: usize, actual: usize },

    #[error("Couldn't parse provided integer as expected type.")]
    InvalidNumber(
        #[from]
        #[source]
        TryFromIntError,
    ),
}

/// The types a [`serde_json::Value`] can have. Used for error reporting.
#[allow(missing_docs)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum JsonTy {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
}

impl Display for JsonTy {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self {
            JsonTy::Null => write!(f, "null"),
            JsonTy::Bool => write!(f, "bool"),
            JsonTy::Number => write!(f, "number"),
            JsonTy::String => write!(f, "string"),
            JsonTy::Array => write!(f, "array"),
            JsonTy::Object => write!(f, "object"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_component_config_ext::config_ty;
    use serde_json::json;

    use FieldError::*;

    fn try_from_int_error() -> TryFromIntError {
        u64::try_from(-1i32).unwrap_err()
    }

    macro_rules! field_parse_tests {
        (
            mod: $mod_name:ident,
            type: { $($type_toks:tt)+ },
            tests: [$(
                $eq_test_name:ident: $eq_input:expr => $eq_output:expr,
            )+]
        ) => {
            mod $mod_name {
                use super::*;

                // macro repetitions need to have the same nesting in expansion as they do in
                // parsing, so we need a top-level function here rather than inside the tests repeat
                fn __config_value_type() -> ConfigValueType {
                    config_ty!( $($type_toks)+ )
                }
                $(
                    #[test]
                    fn $eq_test_name() {
                        let value = $eq_input;
                        let config_ty = __config_value_type();
                        assert_eq!(
                            config_value_from_json_value(&value, &config_ty),
                            $eq_output,
                        );
                    }
                )+
            }
        };
    }

    field_parse_tests! {
        mod: parse_bool,
        type: { bool },
        tests: [
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Bool, received: JsonTy::Null }),
            cant_be_number: json!(1) =>
                Err(JsonTypeMismatch { expected: JsonTy::Bool, received: JsonTy::Number }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Bool, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Bool, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Bool, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_uint8,
        type: { uint8 },
        tests: [
            cant_overflow: json!(256) => Err(InvalidNumber(try_from_int_error())),
            cant_be_negative: json!(-1) =>
                Err(NumberNotUnsigned),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_uint16,
        type: { uint16 },
        tests: [
            cant_overflow: json!(65_536) => Err(InvalidNumber(try_from_int_error())),
            cant_be_negative: json!(-1) =>
                Err(NumberNotUnsigned),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_uint32,
        type: { uint32 },
        tests: [
            cant_overflow: json!(4_294_967_296u64) => Err(InvalidNumber(try_from_int_error())),
            cant_be_negative: json!(-1) =>
                Err(NumberNotUnsigned),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_uint64,
        type: { uint64 },
        tests: [
            // TODO(http://fxbug.dev/91616): serde_json5 does not currently support values
            // between i64::MAX and u64::MAX. Enable this test once this is fixed.
            // can_be_larger_than_i64_max: json!(9_223_372_036_854_775_808u64)=> Ok(Value::Single(SingleValue::Unsigned64(9_223_372_036_854_775_808u64))),
            cant_be_negative: json!(-1) =>
                Err(NumberNotUnsigned),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_int8,
        type: { int8 },
        tests: [
            cant_underflow: json!(-129) => Err(InvalidNumber(try_from_int_error())),
            cant_overflow: json!(128) => Err(InvalidNumber(try_from_int_error())),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_int16,
        type: { int16 },
        tests: [
            cant_underflow: json!(-32_769i32) => Err(InvalidNumber(try_from_int_error())),
            cant_overflow: json!(32_768) => Err(InvalidNumber(try_from_int_error())),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_int32,
        type: { int32 },
        tests: [
            cant_underflow: json!(-2_147_483_649i64) => Err(InvalidNumber(try_from_int_error())),
            cant_overflow: json!(2_147_483_648i64) => Err(InvalidNumber(try_from_int_error())),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_int64,
        type: { int64 },
        tests: [
            cant_overflow: json!(9_223_372_036_854_775_808u64) =>
                Err(NumberNotInteger),
            cant_be_float: json!(1.0) =>
                Err(NumberNotInteger),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Bool }),
            cant_be_string: json!("hello, world!") =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_string,
        type: { string, max_size: 13 },
        tests: [
            can_be_empty: json!("") => Ok(Value::Single(SingleValue::Text("".into()))),
            max_length_fits: json!("hello, world!") =>
                Ok(Value::Single(SingleValue::Text("hello, world!".into()))),
            cant_be_too_long: json!("1234567890 uhoh") =>
                Err(StringTooLong { max: 13, actual: 15 }),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::String, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::String, received: JsonTy::Bool }),
            cant_be_number: json!(1) =>
                Err(JsonTypeMismatch { expected: JsonTy::String, received: JsonTy::Number }),
            cant_be_array: json!([1, 2, 3]) =>
                Err(JsonTypeMismatch { expected: JsonTy::String, received: JsonTy::Array }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::String, received: JsonTy::Object }),
        ]
    }

    field_parse_tests! {
        mod: parse_vector,
        type: { vector, element: int32, max_count: 5 },
        tests: [
            max_length_fits: json!([1, 2, 3, 4, 5]) =>
                Ok(Value::List(ListValue::Signed32List(vec![1, 2, 3, 4, 5]))),
            can_be_empty: json!([]) => Ok(Value::List(ListValue::Signed32List(vec![]))),
            cant_be_too_long: json!([1, 2, 3, 4, 5, 6]) => Err(VectorTooLong { max: 5, actual: 6}),
            element_type_must_match: json!(["foo"]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            needs_uniform_elements: json!([1, 2, "hello, world!"]) =>
                Err(JsonTypeMismatch { expected: JsonTy::Number, received: JsonTy::String }),
            cant_be_null: json!(null) =>
                Err(JsonTypeMismatch { expected: JsonTy::Array, received: JsonTy::Null }),
            cant_be_bool: json!(true) =>
                Err(JsonTypeMismatch { expected: JsonTy::Array, received: JsonTy::Bool }),
            cant_be_number: json!(1) =>
                Err(JsonTypeMismatch { expected: JsonTy::Array, received: JsonTy::Number }),
            cant_be_object: json!({"foo": 1, "bar": 2}) =>
                Err(JsonTypeMismatch { expected: JsonTy::Array, received: JsonTy::Object }),
        ]
    }
}
