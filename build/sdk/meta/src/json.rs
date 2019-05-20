// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;
use serde::{Deserialize, Serialize};
use serde_json::{from_reader, from_str, to_string, to_value};
use std::io::Read;
use valico::json_schema;

/// The various types of errors raised by this module.
#[derive(Debug, Fail)]
enum Error {
    #[fail(display = "invalid JSON schema: {:?}", _0)]
    SchemaInvalid(valico::json_schema::schema::SchemaError),
    #[fail(display = "could not validate file: {}", errors)]
    #[allow(dead_code)] // The compiler complains about the variant not being constructed.
    JsonFileInvalid { errors: String },
}

type Result<T> = std::result::Result<T, failure::Error>;

impl From<valico::json_schema::schema::SchemaError> for Error {
    fn from(err: valico::json_schema::schema::SchemaError) -> Self {
        Error::SchemaInvalid(err)
    }
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
        let validator = scope
            .compile_and_return(schema, false)
            .map_err(Error::SchemaInvalid)?;
        let value = to_value(self)?;
        let result = validator.validate(&value);
        if !result.is_valid() {
            let mut error_messages: Vec<String> = result
                .errors
                .iter()
                .map(|e| format!("{} at {}", e.get_title(), e.get_path()))
                .collect();
            error_messages.sort_unstable();
            return Err(Error::JsonFileInvalid {
                errors: error_messages.join(", "),
            }
            .into());
        }
        Ok(())
    }

    /// Serializes the object into its string representation.
    fn to_string(&self) -> Result<String> {
        Ok(to_string(self)?)
    }
}
