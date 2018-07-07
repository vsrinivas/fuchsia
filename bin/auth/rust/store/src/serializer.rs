// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate base64;
extern crate serde;
extern crate serde_json;

use super::{AuthDbError, CredentialValue};

use failure::Error;
use serde::ser::SerializeStruct;
use serde_json::Value;
use std::io::{Read, Write};
use std::result;

pub type Result<T> = result::Result<T, AuthDbError>;

/// The character set used for all base64 encoding
const CHARSET: base64::Config = base64::STANDARD_NO_PAD;

/// A trait defining the ability to convert CredentialValues to bytes and back.
///
/// Currently we supply a single implementation of this trait using serde_json,
/// but in the future we may want to transition to some binary format such as
/// FIDL tables.
pub trait Serializer {
    /// Outputs a serialized form of credentials into writer.
    fn serialize<'a, W, I>(&self, writer: W, credentials: I) -> Result<()>
    where
        W: Write,
        I: Iterator<Item = &'a CredentialValue>;

    /// Deserializes data from reader to return a vector of credentials. Any
    /// malformed credentials in the input data will be logged and discarded.
    /// If all the credentials in a file are malformed the method returns an
    /// error.
    fn deserialize<R: Read>(&self, reader: R) -> Result<Vec<CredentialValue>>;
}

// Note: We provide a custom Serde serialization for CredentialValue so we can
// choose the specific fields that we wish to base64 encode, and to reduce the
// risk of inconsistencies with the manual deserialization.
impl serde::ser::Serialize for CredentialValue {
    fn serialize<S>(&self, serializer: S) -> result::Result<S::Ok, S::Error>
    where
        S: serde::ser::Serializer,
    {
        let mut state =
            serializer.serialize_struct("CredentialValue", 3 /* Number of fields */)?;
        state.serialize_field("identity_provider", &self.credential_key.identity_provider)?;
        state.serialize_field(
            "id",
            &base64::encode_config(&self.credential_key.id, CHARSET),
        )?;
        state.serialize_field(
            "refresh_token",
            &base64::encode_config(&self.refresh_token, CHARSET),
        )?;
        state.end()
    }
}

/// A Serializer that uses JSON formatting with the help of serde_json
pub struct JsonSerializer;

impl JsonSerializer {
    /// Returns a string field from the supplied json object, or logs and
    /// returns an empty string if this is not possible.
    fn entry_to_str<'a>(object: &'a Value, field_name: &'static str) -> &'a str {
        match object.get(field_name) {
            Some(Value::String(s)) => s,
            _ => {
                warn!("Invalid credential: {} not found", field_name);
                ""
            }
        }
    }

    /// Returns the base64 decoded contents of a string field from the supplied
    /// json object, or logs and returns an empty string if this is not
    /// possible.
    fn base64_entry_to_str(object: &Value, field_name: &'static str) -> String {
        let encoded_string = Self::entry_to_str(object, field_name);
        let decoded_bytes: Vec<u8> =
            base64::decode_config(encoded_string, CHARSET).unwrap_or_else(|_| {
                warn!("Invalid credential: {} invalid base64", field_name);
                vec![]
            });
        let decoded_string = String::from_utf8(decoded_bytes).unwrap_or_else(|_| {
            warn!("Invalid credential: {} invalid UTF-8", field_name);
            "".to_string()
        });
        decoded_string
    }

    /// Constructs a new CredentialValue from the supplied json object.
    fn build_credential_value(json: &Value) -> result::Result<CredentialValue, Error> {
        // For now we need only to handle one serialized form so we just pass all the
        // expected keys to the constructor, relying on it to return an error if
        // anything was missing. In the future we may need to be more selective to
        // handle e.g. different IdP formats.
        CredentialValue::new(
            Self::entry_to_str(&json, "identity_provider").to_string(),
            Self::base64_entry_to_str(&json, "id"),
            Self::base64_entry_to_str(&json, "refresh_token"),
        )
    }
}

impl Serializer for JsonSerializer {
    fn serialize<'a, W, I>(&self, writer: W, credentials: I) -> Result<()>
    where
        W: Write,
        I: Iterator<Item = &'a CredentialValue>,
    {
        let credential_vec: Vec<&CredentialValue> = credentials.collect();
        serde_json::to_writer(writer, &credential_vec).map_err(|err| {
            warn!("Credential serialization error: {:?}", err);
            AuthDbError::SerializationError
        })
    }

    fn deserialize<R: Read>(&self, reader: R) -> Result<Vec<CredentialValue>> {
        // Manually parse the json tree so that we can be more tolerant of partially
        // invalid data and support changes in the credential definition over time.
        match serde_json::from_reader(reader) {
            Ok(Value::Array(json_creds)) => {
                let creds: Vec<CredentialValue> = json_creds
                    .iter()
                    .filter_map(|c| {
                        Self::build_credential_value(c)
                            .map_err(|err| warn!("Error parsing credential {:?}", err))
                            .ok()
                    })
                    .collect();
                // Return an error if the file contained credentials but *none* were valid.
                if creds.is_empty() && !json_creds.is_empty() {
                    Err(AuthDbError::DbInvalid)
                } else {
                    Ok(creds)
                }
            }
            Ok(_) => {
                warn!("Credential JSON root element is not an array");
                Err(AuthDbError::SerializationError)
            }
            Err(err) => {
                warn!("Credential deserialization error: {:?}", err);
                Err(AuthDbError::SerializationError)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::super::CredentialKey;
    use super::*;
    use std::io::Cursor;
    use std::str;

    fn build_test_creds(suffix: &str) -> CredentialValue {
        CredentialValue {
            credential_key: CredentialKey {
                identity_provider: "test".to_string(),
                id: "id".to_string() + suffix,
            },
            refresh_token: "ref".to_string() + suffix,
        }
    }

    #[test]
    fn test_json_serialize_deserialize_zero_items() {
        let input = vec![];

        let mut serialized = Vec::<u8>::new();
        JsonSerializer
            .serialize(&mut serialized, input.iter())
            .unwrap();
        assert_eq!(str::from_utf8(&serialized).unwrap(), "[]");

        let deserialized = JsonSerializer.deserialize(Cursor::new(serialized)).unwrap();
        assert_eq!(deserialized, input);
    }

    #[test]
    fn test_json_serialize_deserialize_one_item() {
        let input = vec![build_test_creds("1")];

        let mut serialized = Vec::<u8>::new();
        JsonSerializer
            .serialize(&mut serialized, input.iter())
            .unwrap();
        assert_eq!(
            str::from_utf8(&serialized).unwrap(),
            "[{\"identity_provider\":\"test\",\"id\":\"aWQx\",\"refresh_token\":\"cmVmMQ\"}]"
        );

        let deserialized = JsonSerializer.deserialize(Cursor::new(serialized)).unwrap();
        assert_eq!(deserialized, input);
    }

    #[test]
    fn test_json_serialize_deserialize_multiple_items() {
        let input = vec![
            build_test_creds("1"),
            build_test_creds("2"),
            build_test_creds("3"),
        ];

        let mut serialized = Vec::<u8>::new();
        JsonSerializer
            .serialize(&mut serialized, input.iter())
            .unwrap();

        let deserialized = JsonSerializer.deserialize(Cursor::new(serialized)).unwrap();
        assert_eq!(deserialized, input);
    }

    #[test]
    fn test_json_deserialize_bad_root() {
        let content =
            "{\"identity_provider\":\"test\",\"id\":\"aWQx\",\"refresh_token\":\"cmVmMQ\"}";
        let deserialized = JsonSerializer.deserialize(content.as_bytes());
        assert_match!(deserialized, Err(AuthDbError::SerializationError));
    }

    #[test]
    fn test_json_deserialize_missing_field() {
        let content = "[{\"identity_provider\":\"test\",\"refresh_token\":\"cmVmMQ\"}]";
        let deserialized = JsonSerializer.deserialize(content.as_bytes());
        match deserialized {
            Err(AuthDbError::DbInvalid) => {}
            _ => panic!(),
        }
        assert_match!(deserialized, Err(AuthDbError::DbInvalid));
    }

    #[test]
    fn test_json_deserialize_invalid_base64() {
        let content =
            "[{\"identity_provider\":\"test\",\"id\":\"a$$x\",\"refresh_token\":\"cmVmMQ\"}]";
        let deserialized = JsonSerializer.deserialize(content.as_bytes());
        assert_match!(deserialized, Err(AuthDbError::DbInvalid));
    }
}
