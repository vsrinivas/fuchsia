// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::vfs::{FilesystemProvider, RealFilesystemProvider};
use anyhow::{anyhow, Context, Result};
use ffx_assembly_args::SignArgs;
use vbmeta::{Salt, VBMeta};

pub fn sign(args: SignArgs) -> Result<()> {
    let outfile = args.output.clone();

    let (vbmeta, _salt) = sign_impl(args, &RealFilesystemProvider {}, &RealSaltProvider {})?;

    // Now write the output.
    std::fs::write(outfile, &vbmeta.as_bytes()).context("Failed to write vbmeta file")?;

    Ok(())
}

fn sign_impl<FSP: FilesystemProvider, SP: SaltProvider>(
    args: SignArgs,
    fs: &FSP,
    salt_provider: &SP,
) -> Result<(VBMeta, Salt)> {
    let salt: Salt = match args.salt_file {
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

    crate::vbmeta::sign(
        args.name,
        args.image_path,
        args.key,
        args.key_metadata,
        args.additional_descriptor,
        salt,
        fs,
    )
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::vfs::mock::MockFilesystemProvider;
    use std::convert::TryFrom;
    use vbmeta::Key;

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

        let (vbmeta, salt) = sign_impl(
            SignArgs {
                name: "some_name".to_owned(),
                image_path: "image".into(),
                key: "key".into(),
                key_metadata: "key_metadata".into(),
                salt_file: Some("salt".into()),
                additional_descriptor: Vec::new(),
                output: "output".into(),
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

        assert_eq!(salt, descriptors[0].salt().unwrap());
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

        let (vbmeta, salt) = sign_impl(
            SignArgs {
                name: "some other name".to_owned(),
                image_path: "image".into(),
                key: "key".into(),
                key_metadata: "key_metadata".into(),
                salt_file: None,
                additional_descriptor: Vec::new(),
                output: "output".into(),
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

        assert_eq!(salt, descriptors[0].salt().unwrap());
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
}
