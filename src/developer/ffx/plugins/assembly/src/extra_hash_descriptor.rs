// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use serde::{Deserialize, Deserializer};
use serde_json::Value;
use std::convert::TryFrom;
use vbmeta::{HashDescriptor, RawHashDescriptorBuilder, Salt};

/// Used to deserialize a JSON representation of a HashDescriptor.
#[derive(Debug, Deserialize)]
pub struct ExtraHashDescriptor {
    pub name: Option<String>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_u64_from_value")]
    pub size: Option<u64>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_salt_from_value")]
    pub salt: Option<Salt>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_bytes_from_value")]
    pub digest: Option<[u8; 32]>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_u32_from_value")]
    pub flags: Option<u32>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_version_from_value")]
    pub min_avb_version: Option<[u32; 2]>,
}

impl Into<HashDescriptor> for ExtraHashDescriptor {
    fn into(self) -> HashDescriptor {
        let builder = RawHashDescriptorBuilder::default();
        let builder = match self.name {
            Some(name) => builder.name(name),
            _ => builder,
        };
        let builder = match self.size {
            Some(size) => builder.size(size),
            _ => builder,
        };
        let builder = match self.salt {
            Some(salt) => builder.salt(salt),
            _ => builder,
        };
        let builder = match self.digest {
            Some(digest) => builder.digest(&digest[..]),
            _ => builder,
        };
        let builder = match self.flags {
            Some(flags) => builder.flags(flags),
            _ => builder,
        };
        let builder = match self.min_avb_version {
            Some(min_avb_version) => builder.min_avb_version(min_avb_version),
            _ => builder,
        };
        builder.build()
    }
}

// The following "option_Foo_from_value()" fn's are needed because when Serde is
// parsing into an Option<Foo>, any customized parser needs to return an
// `Option<Foo>`, even though it's only called if the value is present (so it
// acts like an `.and_then()` call, taking a `T` and returning `Option<U>`).

/// Custom parser to deal with situations where a u64 is encoded as either the
/// value `12345678` or `"12344556"` in the JSON that's being deserialized.
fn optional_u64_from_value<'de, D>(value: D) -> Result<Option<u64>, D::Error>
where
    D: Deserializer<'de>,
{
    let value = Value::deserialize(value)?;
    if let Some(u64_value) = value.as_u64() {
        return Ok(Some(u64_value));
    }
    if let Value::String(number) = &value {
        return number
            .parse::<u64>()
            .map(|v| Some(v))
            .map_err(|e| serde::de::Error::custom(e.to_string()));
    }
    Err(serde::de::Error::custom("not a valid value"))
}

/// Custom parser to parse an optional string of hex chars into bytes.
fn optional_bytes_from_value<'de, D>(value: D) -> Result<Option<[u8; 32]>, D::Error>
where
    D: Deserializer<'de>,
{
    let value = String::deserialize(value)?;
    if let Ok(bytes) = hex::decode(value) {
        if bytes.len() == 32 {
            let mut buff = [0u8; 32];
            buff[..].copy_from_slice(bytes.as_slice());
            return Ok(Some(buff));
        }
    }
    Err(serde::de::Error::custom("not a valid value (32 bytes as hex characters)"))
}

/// Custom parser to parse an optional string of hex chars into a Salt
fn optional_salt_from_value<'de, D>(value: D) -> Result<Option<Salt>, D::Error>
where
    D: Deserializer<'de>,
{
    let value = String::deserialize(value)?;
    match Salt::decode_hex(value.as_str()) {
        Ok(salt) => Ok(Some(salt)),
        Err(e) => {
            Err(serde::de::Error::custom(format!("not a valid salt value: {}", e.to_string())))
        }
    }
}

/// Custom parser to deal with situations where a u32 is encoded as either the
/// value `12345678` or `"12344556"` in the JSON that's being deserialized.
fn optional_u32_from_value<'de, D>(value: D) -> Result<Option<u32>, D::Error>
where
    D: Deserializer<'de>,
{
    let value = Value::deserialize(value)?;
    if let Some(u64_value) = value.as_u64() {
        return u32::try_from(u64_value)
            .map(|v| Some(v))
            .map_err(|e| serde::de::Error::custom(e.to_string()));
    }
    if let Value::String(number) = &value {
        return number
            .parse::<u32>()
            .map(|v| Some(v))
            .map_err(|e| serde::de::Error::custom(e.to_string()));
    }
    Err(serde::de::Error::custom("not a valid value"))
}

/// Custom parser to convert `"A.B"` formatted values into an `Option<[u32;2]>`.
fn optional_version_from_value<'de, D>(value: D) -> Result<Option<[u32; 2]>, D::Error>
where
    D: Deserializer<'de>,
{
    let raw_string = String::deserialize(value)?;
    let parts: Vec<&str> = raw_string.split(".").collect();
    if parts.len() != 2 {
        Err(serde::de::Error::custom("version must be in `A.B` format"))
    } else {
        let a = parts[0].parse::<u32>().map_err(|e| {
            serde::de::Error::custom(format!("unable to parse major version: {}", e))
        })?;
        let b = parts[1].parse::<u32>().map_err(|e| {
            serde::de::Error::custom(format!("unable to parse major version: {}", e))
        })?;
        Ok(Some([a, b]))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use serde_json::json;

    #[test]
    fn test_extra_hash_descriptor_deserialization() {
        let input = json!({
            "name": "a name",
            "size": "123456",
            "salt": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "digest": "fedbca9876543210fedbca9876543210fedbca9876543210fedbca9876543210",
            "flags": "546",
            "min_avb_version": "3.5"
        });
        let descriptor: HashDescriptor =
            serde_json::from_value::<ExtraHashDescriptor>(input).unwrap().into();

        assert_eq!(descriptor.image_name(), "a name");
        assert_eq!(descriptor.image_size(), 123456);
        assert_eq!(
            descriptor.salt(),
            Some(
                Salt::decode_hex(
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
                )
                .unwrap()
            )
        );
        assert_eq!(
            descriptor.digest(),
            Some(
                &hex::decode("fedbca9876543210fedbca9876543210fedbca9876543210fedbca9876543210")
                    .unwrap()[..]
            )
        );
        assert_eq!(descriptor.flags(), 546);
        assert_eq!(descriptor.get_min_avb_version(), Some([3, 5]));
    }

    #[test]
    fn test_extra_hash_descriptor_deserialization_with_minimal_fields() {
        let input = json!({
            "name": "another name",
            "size": "1234",
        });
        let descriptor: HashDescriptor =
            serde_json::from_value::<ExtraHashDescriptor>(input).unwrap().into();

        assert_eq!(descriptor.image_name(), "another name");
        assert_eq!(descriptor.image_size(), 1234);
        assert_matches!(descriptor.salt(), None);
        assert_matches!(descriptor.digest(), None);
        assert_eq!(descriptor.flags(), 0);
        assert_matches!(descriptor.get_min_avb_version(), None);
    }
}
