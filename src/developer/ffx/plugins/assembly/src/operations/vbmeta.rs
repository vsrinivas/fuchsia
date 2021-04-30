// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::vfs::{FilesystemProvider, RealFilesystemProvider};
use anyhow::{anyhow, Context, Result};
use ffx_assembly_args::SignArgs;
use serde::{Deserialize, Deserializer};
use serde_json::{self, Value};
use std::convert::TryFrom;
use vbmeta::{HashDescriptor, Key, RawHashDescriptorBuilder, Salt, VBMeta};

pub fn sign(args: SignArgs) -> Result<()> {
    let outfile = args.output.clone();

    let vbmeta = sign_impl(args, &RealFilesystemProvider {}, &RealSaltProvider {})?;

    // Now write the output.
    std::fs::write(outfile, &vbmeta.as_bytes()).context("Unable to write output file")?;

    Ok(())
}

fn sign_impl<FSP: FilesystemProvider, SP: SaltProvider>(
    args: SignArgs,
    fs: &FSP,
    salt_provider: &SP,
) -> Result<VBMeta> {
    // Read the signing key's bytes and metadata.
    let key_pem = fs.read_to_string(args.key)?;
    let key_metadata = fs.read(args.key_metadata)?;
    // And then create the signing key from those.
    let key = Key::try_new(&key_pem, key_metadata).unwrap();

    // If the salt was given on the command line, use those bytes, otherwise
    // generate a new set Salt.
    let salt: Salt = match &args.salt_file {
        Some(salt_file) => {
            let file_contents = fs.read_to_string(salt_file)?; // hex chars
            Salt::decode_hex(file_contents.as_str())?
        }
        _ => match salt_provider.random() {
            Ok(salt) => salt,
            Err(_e) => {
                return Err(anyhow!("random salt failed to generate"));
            }
        },
    };

    // If any additional files were specified for reading descriptor information
    // from, read them in.
    let mut descriptors = Vec::new();

    for path in args.additional_descriptor {
        let descriptor_json = fs.read(path)?;
        let descriptor: ExtraHashDescriptor = serde_json::from_slice(descriptor_json.as_slice())?;
        descriptors.push(descriptor.into());
    }

    // Read the image into memory, so that it can be hashed.
    let image = fs.read(args.image_path)?;

    // Create the descriptor for the image.
    let descriptor = HashDescriptor::new(&args.name, &image, salt);
    descriptors.push(descriptor);

    // And do the signing operation itself.
    VBMeta::sign(descriptors, key).map_err(Into::into)
}

/// A shim trait so that tests can inject a known salt value, while the "real"
/// operation uses `Salt::random()` to create a Salt when none is specified in
/// the arguments.
trait SaltProvider {
    fn random(&self) -> Result<Salt, vbmeta::SaltError>;
}

/// The "real" implementation
struct RealSaltProvider;
impl SaltProvider for RealSaltProvider {
    fn random(&self) -> Result<Salt, vbmeta::SaltError> {
        Salt::random()
    }
}

/// Used to deserialize a JSON representation of a HashDescriptor.
#[derive(Debug, Deserialize)]
struct ExtraHashDescriptor {
    name: Option<String>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_u64_from_value")]
    size: Option<u64>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_salt_from_value")]
    salt: Option<Salt>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_bytes_from_value")]
    digest: Option<[u8; 32]>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_u32_from_value")]
    flags: Option<u32>,
    #[serde(default)]
    #[serde(deserialize_with = "optional_version_from_value")]
    min_avb_version: Option<[u32; 2]>,
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
    use crate::vfs::mock::MockFilesystemProvider;
    use matches::assert_matches;
    use serde_json::json;
    use std::convert::TryFrom;

    /// A non-random source of Salts, for testing that the random() value is
    /// being called.
    struct FakeSaltProvider<'a>(&'a [u8]);
    impl SaltProvider for FakeSaltProvider<'_> {
        fn random(&self) -> Result<Salt, vbmeta::SaltError> {
            Salt::try_from(self.0)
        }
    }

    #[test]
    fn test_signing_arg_setup_with_salt() {
        let salt_provider = FakeSaltProvider(&[0x11u8; 32]);
        let key_expected =
            Key::try_new(test_keys::TEST_RSA_4096_PEM, "TEST_METADATA".as_bytes()).unwrap();

        let mut vfs = MockFilesystemProvider::new();
        vfs.add("key", test_keys::TEST_RSA_4096_PEM.as_bytes());
        vfs.add("key_metadata", &b"TEST_METADATA"[..]);
        vfs.add("image", &[0x00u8; 128]);
        vfs.add("salt", &hex::encode(&[0xAAu8; 32]).as_bytes());

        let vbmeta = sign_impl(
            SignArgs {
                name: "some_name".to_owned(),
                image_path: "image".to_owned(),
                key: "key".to_owned(),
                key_metadata: "key_metadata".to_owned(),
                salt_file: Some("salt".to_owned()),
                additional_descriptor: Vec::new(),
                output: "output".to_owned(),
            },
            &vfs,
            &salt_provider,
        )
        .unwrap();

        // Validate that the key in the arguments was the key that was passed to
        // the vbmeta library for the signing operation.
        assert_eq!(vbmeta.key().public_key().as_ref() as &[u8], key_expected.public_key().as_ref());

        // Validate that there's only the one descriptor.
        let descriptors = vbmeta.descriptors();
        assert_eq!(descriptors.len(), 1);

        let salt = descriptors[0].salt().unwrap();
        let name = descriptors[0].image_name();
        let digest = descriptors[0].digest();
        let expected_digest =
            hex::decode("caeaacb8208cfd8d214de6baef8d535f6fce499524c60aa5dcd2fce7043a9700")
                .unwrap();

        // Validate that the salt was the one from the args.
        assert_eq!(salt.bytes, [0xAAu8; 32]); // the salt from the args.

        // Validate that the image name was set to the one passed in the arguments.
        assert_eq!(name, "some_name");

        // Validate that the digest is the expected one, based on the image that
        // was provided in the arguments.
        assert_eq!(digest, Some(expected_digest.as_ref()));
    }

    #[test]
    fn test_signing_arg_setup_with_random_salt() {
        let salt_provider = FakeSaltProvider(&[0x11u8; 32]);
        let key_expected =
            Key::try_new(test_keys::TEST_RSA_4096_PEM, "TEST_METADATA".as_bytes()).unwrap();

        let mut vfs = MockFilesystemProvider::new();
        vfs.add("key", test_keys::TEST_RSA_4096_PEM.as_bytes());
        vfs.add("key_metadata", &b"TEST_METADATA"[..]);
        vfs.add("image", &[0x00u8; 128]);

        let vbmeta = sign_impl(
            SignArgs {
                name: "some other name".to_owned(),
                image_path: "image".to_owned(),
                key: "key".to_owned(),
                key_metadata: "key_metadata".to_owned(),
                salt_file: None,
                additional_descriptor: Vec::new(),
                output: "output".to_owned(),
            },
            &vfs,
            &salt_provider,
        )
        .unwrap();

        // Validate that the key in the arguments was the key that was passed to
        // the vbmeta library for the signing operation.
        assert_eq!(vbmeta.key().public_key().as_ref() as &[u8], key_expected.public_key().as_ref());

        // Validate that there's only the one descriptor.
        let descriptors = vbmeta.descriptors();
        assert_eq!(descriptors.len(), 1);

        let salt = descriptors[0].salt().unwrap();
        let name = descriptors[0].image_name();
        let digest = descriptors[0].digest();
        let expected_digest =
            hex::decode("9c08c0f20f68d5ce8e1e112dea84232a9358f9ee51491009f4c807d0e23554ee")
                .unwrap();

        // Validate that the salt was the one the mock provider returns
        assert_eq!(salt.bytes, [0x11u8; 32]);

        // Validate that the image name was set to the one passed in the arguments.
        assert_eq!(name, "some other name");

        // Validate that the digest is the expected one, based on the image that
        // was provided in the arguments.
        assert_eq!(digest, Some(expected_digest.as_ref()));
    }

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
