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

    // Test private key that was generated with:
    //   openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 \
    //     -pkeyopt rsa_keygen_pubexp:65537
    pub const TEST_PEM: &'static str = r"-----BEGIN PRIVATE KEY-----
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
cI/N08jLg0hu0xf793n1EDrcxE1pv1FB
-----END PRIVATE KEY-----";

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
        let key_expected = Key::try_new(&TEST_PEM, "TEST_METADATA".as_bytes()).unwrap();

        let mut vfs = MockFilesystemProvider::new();
        vfs.add("key", TEST_PEM.as_bytes());
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
        let key_expected = Key::try_new(&TEST_PEM, "TEST_METADATA".as_bytes()).unwrap();

        let mut vfs = MockFilesystemProvider::new();
        vfs.add("key", TEST_PEM.as_bytes());
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
