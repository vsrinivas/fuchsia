// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow, json5, serde_json,
    serde_json::Value,
    std::borrow::Cow,
    std::error,
    std::fmt,
    std::fs::File,
    std::io::{self, Read},
    std::path::Path,
    std::str::Utf8Error,
};

pub mod cm;

#[derive(Debug)]
pub struct JsonSchema<'a> {
    // Cow allows us to store either owned values when needed (as in new_from_file) or borrowed
    // values when lifetimes allow (as in new).
    pub name: Cow<'a, str>,
    pub schema: Cow<'a, str>,
}

impl<'a> JsonSchema<'a> {
    pub const fn new(name: &'a str, schema: &'a str) -> Self {
        Self { name: Cow::Borrowed(name), schema: Cow::Borrowed(schema) }
    }

    pub fn new_from_file(file: &Path) -> Result<Self, Error> {
        let mut schema_buf = String::new();
        File::open(&file)?.read_to_string(&mut schema_buf)?;
        Ok(JsonSchema {
            name: Cow::Owned(file.to_string_lossy().into_owned()),
            schema: Cow::Owned(schema_buf),
        })
    }
}

// Directly include schemas in the library. These are used to parse component manifests.
pub const CML_SCHEMA: &JsonSchema<'_> =
    &JsonSchema::new("cml_schema.json", include_str!("../cml_schema.json"));
pub const CMX_SCHEMA: &JsonSchema<'_> =
    &JsonSchema::new("cmx_schema.json", include_str!("../cmx_schema.json"));

/// Enum type that can represent any error encountered by a cmx operation.
#[derive(Debug)]
pub enum Error {
    InvalidArgs(String),
    Io(io::Error),
    Parse(String),
    Validate { schema_name: Option<String>, err: String },
    ValidateFidl(anyhow::Error),
    Internal(String),
    Utf8(Utf8Error),
}

impl error::Error for Error {}

impl Error {
    pub fn invalid_args(err: impl Into<String>) -> Self {
        Error::InvalidArgs(err.into())
    }

    pub fn parse(err: impl fmt::Display) -> Self {
        Error::Parse(err.to_string())
    }

    pub fn validate(err: impl fmt::Display) -> Self {
        Error::Validate { schema_name: None, err: err.to_string() }
    }

    pub fn validate_schema(schema: &JsonSchema<'_>, err: impl Into<String>) -> Self {
        Error::Validate { schema_name: Some(schema.name.to_string()), err: err.into() }
    }

    pub fn validate_fidl(err: impl Into<anyhow::Error>) -> Self {
        Error::ValidateFidl(err.into())
    }

    pub fn internal(err: impl Into<String>) -> Self {
        Error::Internal(err.into())
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self {
            Error::InvalidArgs(err) => write!(f, "Invalid args: {}", err),
            Error::Io(err) => write!(f, "IO error: {}", err),
            Error::Parse(err) => write!(f, "Parse error: {}", err),
            Error::Validate { schema_name, err } => {
                let schema_str = schema_name
                    .as_ref()
                    .map(|n| format!("Validation against schema '{}' failed: ", n))
                    .unwrap_or("".to_string());
                write!(f, "Validate error: {}{}", schema_str, err)
            }
            Error::ValidateFidl(err) => write!(f, "FIDL validation failed: {}", err),
            Error::Internal(err) => write!(f, "Internal error: {}", err),
            Error::Utf8(err) => write!(f, "UTF8 error: {}", err),
        }
    }
}

impl From<io::Error> for Error {
    fn from(err: io::Error) -> Self {
        Error::Io(err)
    }
}

impl From<Utf8Error> for Error {
    fn from(err: Utf8Error) -> Self {
        Error::Utf8(err)
    }
}

impl From<cm::PathValidationError> for Error {
    fn from(err: cm::PathValidationError) -> Self {
        Error::validate(err)
    }
}

impl From<cm::NameValidationError> for Error {
    fn from(err: cm::NameValidationError) -> Self {
        Error::validate(err)
    }
}

impl From<cm::RightsValidationError> for Error {
    fn from(err: cm::RightsValidationError) -> Self {
        Error::validate(err)
    }
}

impl From<cm::UrlValidationError> for Error {
    fn from(err: cm::UrlValidationError) -> Self {
        Error::validate(err)
    }
}

impl From<serde_json::Error> for Error {
    fn from(err: serde_json::Error) -> Self {
        use serde_json::error::Category;
        match err.classify() {
            Category::Io | Category::Eof => Error::Io(err.into()),
            Category::Syntax => Error::parse(err),
            Category::Data => Error::validate(err),
        }
    }
}

pub fn from_json_str(json: &str) -> Result<Value, Error> {
    let v = serde_json::from_str(json)
        .map_err(|e| Error::parse(format!("Couldn't read input as JSON: {}", e)))?;
    Ok(v)
}

pub fn from_json5_str(json5: &str) -> Result<Value, Error> {
    let v: Value = json5::from_str(json5)
        .map_err(|e| Error::parse(format!("Couldn't read input as JSON5: {}", e)))?;
    Ok(v)
}

#[cfg(test)]
mod tests {
    use super::*;
    use test_util::assert_matches;

    #[test]
    fn test_syntax_error_message() {
        let result = serde_json::from_str::<cm::Name>("foo").map_err(Error::from);
        assert_matches!(result, Err(Error::Parse(_)));
    }

    #[test]
    fn test_validation_error_message() {
        let result = serde_json::from_str::<cm::Name>("\"foo$\"").map_err(Error::from);
        assert_matches!(result, Err(Error::Validate { .. }));
    }
}
