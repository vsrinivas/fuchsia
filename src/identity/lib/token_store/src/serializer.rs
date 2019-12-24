// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{AuthDbError, CredentialValue};

use anyhow::{format_err, Error};
use log::warn;
use serde_derive::Serialize;
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

#[derive(Serialize)]
struct StoredCredentialValue<'a> {
    auth_provider_type: &'a str,
    user_profile_id: String,
    refresh_token: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    private_key: Option<String>,
}

// Note: We provide a custom Serde serialization for CredentialValue so we can
// choose the specific fields that we wish to base64 encode, and to reduce the
// risk of inconsistencies with the manual deserialization.
impl serde::ser::Serialize for CredentialValue {
    fn serialize<S>(&self, serializer: S) -> result::Result<S::Ok, S::Error>
    where
        S: serde::ser::Serializer,
    {
        StoredCredentialValue {
            auth_provider_type: &self.credential_key.auth_provider_type,
            user_profile_id: base64::encode_config(&self.credential_key.user_profile_id, CHARSET),
            refresh_token: base64::encode_config(&self.refresh_token, CHARSET),
            private_key: self.private_key.as_ref().map(|key| base64::encode_config(key, CHARSET)),
        }
        .serialize(serializer)
    }
}

/// A Serializer that uses JSON formatting with the help of serde_json
pub struct JsonSerializer;

impl JsonSerializer {
    /// Constructs a new CredentialValue from the supplied json object.
    fn build_credential_value(json: &Value) -> result::Result<CredentialValue, Error> {
        CredentialValue::new(
            entry_to_str(&json, "auth_provider_type")?.to_string(),
            base64_entry_to_str(&json, "user_profile_id")?,
            base64_entry_to_str(&json, "refresh_token")?,
            match &json.get("private_key") {
                Some(Value::String(_)) => Some(base64_entry_to_bytes(&json, "private_key")?),
                _ => None,
            },
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

/// Returns a string field from the supplied json object, or a descriptive error if this is not
/// possible.
fn entry_to_str<'a>(object: &'a Value, field_name: &'static str) -> result::Result<&'a str, Error> {
    match object.get(field_name) {
        Some(Value::String(s)) => Ok(s),
        _ => Err(format_err!("Invalid credential: {} not found", field_name)),
    }
}

/// Returns the base64 decoded contents of a string field from the supplied json object, or a
/// descriptive error if this is not possible.
fn base64_entry_to_bytes(
    object: &Value,
    field_name: &'static str,
) -> result::Result<Vec<u8>, Error> {
    let encoded_string = entry_to_str(object, field_name)?;
    base64::decode_config(encoded_string, CHARSET)
        .map_err(|_| format_err!("Invalid credential: {} invalid base64", field_name))
}

/// Returns the base64 decoded contents of a string field from the supplied json object, or a
/// descriptive error if this is not possible.
fn base64_entry_to_str(object: &Value, field_name: &'static str) -> result::Result<String, Error> {
    let decoded_bytes = base64_entry_to_bytes(object, field_name)?;
    String::from_utf8(decoded_bytes)
        .map_err(|_| format_err!("Invalid credential: {} invalid UTF-8", field_name))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::CredentialKey;
    use std::io::Cursor;
    use std::str;

    fn build_test_creds(suffix: &str) -> CredentialValue {
        CredentialValue {
            credential_key: CredentialKey {
                auth_provider_type: "test".to_string(),
                user_profile_id: "id".to_string() + suffix,
            },
            refresh_token: "ref".to_string() + suffix,
            private_key: None,
        }
    }

    #[test]
    fn test_json_serialize_deserialize_zero_items() -> Result<()> {
        let input = vec![];

        let mut serialized = Vec::<u8>::new();
        JsonSerializer.serialize(&mut serialized, input.iter())?;
        assert_eq!(str::from_utf8(&serialized).unwrap(), "[]");

        let deserialized = JsonSerializer.deserialize(Cursor::new(serialized))?;
        assert_eq!(deserialized, input);
        Ok(())
    }

    #[test]
    fn test_json_serialize_deserialize_one_item() -> Result<()> {
        let input = vec![build_test_creds("1")];

        let mut serialized = Vec::<u8>::new();
        JsonSerializer.serialize(&mut serialized, input.iter())?;
        assert_eq!(
            str::from_utf8(&serialized).unwrap(),
            "[{\"auth_provider_type\":\"test\",\"user_profile_id\":\"aWQx\",\"refresh_token\":\
             \"cmVmMQ\"}]"
        );

        let deserialized = JsonSerializer.deserialize(Cursor::new(serialized))?;
        assert_eq!(deserialized, input);
        Ok(())
    }

    #[test]
    fn test_json_serialize_deserialize_multiple_items() -> Result<()> {
        let mut input = vec![build_test_creds("1"), build_test_creds("2"), build_test_creds("3")];
        // Include a mix of bound and unbound test credentials.
        input[1].private_key = Some(vec![7, 6, 5, 4, 3, 2, 1]);

        let mut serialized = Vec::<u8>::new();
        JsonSerializer.serialize(&mut serialized, input.iter())?;

        let deserialized = JsonSerializer.deserialize(Cursor::new(serialized))?;
        assert_eq!(deserialized, input);
        Ok(())
    }

    #[test]
    fn test_json_deserialize_bad_root() {
        let content = "{\"auth_provider_type\":\"test\",\"user_profile_id\":\"aWQx\",\
                       \"refresh_token\":\"cmVmMQ\"}";
        let deserialized = JsonSerializer.deserialize(content.as_bytes());
        assert_match!(deserialized, Err(AuthDbError::SerializationError));
    }

    #[test]
    fn test_json_deserialize_missing_field() {
        let content = "[{\"auth_provider_type\":\"test\",\"refresh_token\":\"cmVmMQ\"}]";
        let deserialized = JsonSerializer.deserialize(content.as_bytes());
        match deserialized {
            Err(AuthDbError::DbInvalid) => {}
            _ => panic!(),
        }
        assert_match!(deserialized, Err(AuthDbError::DbInvalid));
    }

    #[test]
    fn test_json_deserialize_invalid_base64() {
        let content = "[{\"auth_provider_type\":\"test\",\"user_profile_id\":\"a$$x\",\
                       \"refresh_token\":\"cmVmMQ\"}]";
        let deserialized = JsonSerializer.deserialize(content.as_bytes());
        assert_match!(deserialized, Err(AuthDbError::DbInvalid));
    }
}
