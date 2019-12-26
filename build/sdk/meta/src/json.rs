// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use serde_json::{from_reader, from_str, to_string, to_value};
use std::io::Read;
use thiserror::Error;
use valico::common::error::ValicoError;
use valico::json_schema;

/// The various types of errors raised by this module.
#[derive(Debug, Error)]
enum Error {
    #[error("invalid JSON schema: {:?}", _0)]
    SchemaInvalid(valico::json_schema::schema::SchemaError),
    #[error("could not validate file: {}", errors)]
    #[allow(dead_code)] // The compiler complains about the variant not being constructed.
    JsonFileInvalid { errors: String },
}

type Result<T> = std::result::Result<T, anyhow::Error>;

impl From<valico::json_schema::schema::SchemaError> for Error {
    fn from(err: valico::json_schema::schema::SchemaError) -> Self {
        Error::SchemaInvalid(err)
    }
}

/// Generates user-friendly messages for validation errors.
fn format_valico_error(error: &Box<dyn ValicoError>) -> String {
    // $title[at $path][: $detail] ($code)
    let mut result = String::new();
    result.push_str(error.get_title());
    let path = error.get_path();
    if !path.is_empty() {
        result.push_str(" at ");
        result.push_str(path);
    }
    if let Some(detail) = error.get_detail() {
        result.push_str(": ");
        result.push_str(detail);
    }
    result.push_str(" (");
    result.push_str(error.get_code());
    result.push_str(")");
    result
}

/// Augments metadata representations with utility methods to serialize/deserialize and validate
/// their contents.
pub trait JsonObject: for<'a> Deserialize<'a> + Serialize + Sized {
    /// Creates a new instance from its raw data.
    fn new<R: Read>(source: R) -> Result<Self> {
        Ok(from_reader(source)?)
    }

    /// Returns the schema matching the object type.
    fn get_schema() -> &'static str;

    /// Checks whether the object satisfies its associated JSON schema.
    fn validate(&self) -> Result<()> {
        let schema = from_str(Self::get_schema())?;
        let mut scope = json_schema::Scope::new();

        // Add the schema including all the common definitions.
        let common_schema = from_str(include_str!("../common.json"))?;
        scope.compile(common_schema, true).map_err(Error::SchemaInvalid)?;

        let validator = scope.compile_and_return(schema, true).map_err(Error::SchemaInvalid)?;
        let value = to_value(self)?;
        let result = validator.validate(&value);
        if !result.is_valid() {
            let mut error_messages: Vec<String> =
                result.errors.iter().map(format_valico_error).collect();
            error_messages.sort_unstable();
            return Err(Error::JsonFileInvalid { errors: error_messages.join(", ") }.into());
        }
        Ok(())
    }

    /// Serializes the object into its string representation.
    fn to_string(&self) -> Result<String> {
        Ok(to_string(self)?)
    }
}

#[cfg(test)]
mod tests {
    use serde_derive::{Deserialize, Serialize};

    use super::*;

    #[derive(Deserialize, Serialize)]
    struct Metadata {
        target: String,
    }

    impl JsonObject for Metadata {
        fn get_schema() -> &'static str {
            r#"{
                "$schema": "http://json-schema.org/draft-04/schema#",
                "id": "http://fuchsia.com/schemas/sdk/test_metadata.json",
                "properties": {
                    "target": {
                        "$ref": "common.json#/definitions/target_arch"
                    }
                }
            }"#
        }
    }

    #[test]
    /// Checks that references to common.json are properly resolved.
    fn test_common_reference() {
        let metadata = Metadata {
            target: "y128".to_string(), // Not a valid architecture.
        };
        let result = metadata.validate();
        assert!(result.is_err(), "Validation did not respect common schema.");
    }
}
