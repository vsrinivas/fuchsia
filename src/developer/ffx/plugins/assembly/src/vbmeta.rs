// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::VBMetaConfig;
use crate::extra_hash_descriptor::ExtraHashDescriptor;
use crate::vfs::{FilesystemProvider, RealFilesystemProvider};
use anyhow::Result;
use std::path::{Path, PathBuf};
use vbmeta::{HashDescriptor, Key, Salt, VBMeta};

pub fn construct_vbmeta(
    outdir: impl AsRef<Path>,
    name: impl AsRef<str>,
    vbmeta: &VBMetaConfig,
    zbi: impl AsRef<Path>,
) -> Result<PathBuf> {
    // Generate the salt, or use one provided by the board.
    let salt = match &vbmeta.salt {
        Some(salt_path) => {
            let salt_str = std::fs::read_to_string(salt_path)?;
            Salt::decode_hex(&salt_str)?
        }
        _ => Salt::random()?,
    };

    // Sign the image and construct a VBMeta.
    let (vbmeta, _salt) = crate::vbmeta::sign(
        &vbmeta.kernel_partition,
        zbi,
        &vbmeta.key,
        &vbmeta.key_metadata,
        vbmeta.additional_descriptor_files.clone(),
        salt,
        &RealFilesystemProvider {},
    )?;

    // Write VBMeta to a file and return the path.
    let vbmeta_path = outdir.as_ref().join(format!("{}.vbmeta", name.as_ref()));
    std::fs::write(&vbmeta_path, vbmeta.as_bytes())?;
    Ok(vbmeta_path)
}

pub fn sign<FSP: FilesystemProvider>(
    name: impl AsRef<str>,
    image_path: impl AsRef<Path>,
    key: impl AsRef<Path>,
    key_metadata: impl AsRef<Path>,
    additional_descriptors: Vec<PathBuf>,
    salt: Salt,
    fs: &FSP,
) -> Result<(VBMeta, Salt)> {
    // Read the signing key's bytes and metadata.
    let key_pem = fs.read_to_string(key)?;
    let key_metadata = fs.read(key_metadata)?;
    // And then create the signing key from those.
    let key = Key::try_new(&key_pem, key_metadata).unwrap();

    // If any additional files were specified for reading descriptor information
    // from, read them in.
    let mut descriptors = Vec::new();

    for path in additional_descriptors {
        let descriptor_json = fs.read(path)?;
        let descriptor: ExtraHashDescriptor = serde_json::from_slice(descriptor_json.as_slice())?;
        descriptors.push(descriptor.into());
    }

    // Read the image into memory, so that it can be hashed.
    let image = fs.read(image_path)?;

    // Create the descriptor for the image.
    let descriptor = HashDescriptor::new(name.as_ref(), &image, salt.clone());
    descriptors.push(descriptor);

    // And do the signing operation itself.
    VBMeta::sign(descriptors, key).map_err(Into::into).map(|vbmeta| (vbmeta, salt))
}

#[cfg(test)]
mod tests {
    use super::{construct_vbmeta, sign};

    use crate::config::VBMetaConfig;
    use crate::vfs::mock::MockFilesystemProvider;
    use std::convert::TryFrom;
    use tempfile::tempdir;
    use vbmeta::{Key, Salt};

    #[test]
    fn construct() {
        let dir = tempdir().unwrap();

        let key_path = dir.path().join("key");
        let metadata_path = dir.path().join("key_metadata");
        std::fs::write(&key_path, test_keys::ATX_TEST_KEY).unwrap();
        std::fs::write(&metadata_path, test_keys::TEST_RSA_4096_PEM).unwrap();

        let vbmeta_config = VBMetaConfig {
            partition: "vbmeta".to_string(),
            kernel_partition: "zircon".to_string(),
            key: key_path,
            key_metadata: metadata_path,
            additional_descriptor_files: vec![],
            salt: None,
        };

        // Create a fake zbi.
        let zbi_path = dir.path().join("fuchsia.zbi");
        std::fs::write(&zbi_path, "fake zbi").unwrap();

        let vbmeta_path =
            construct_vbmeta(dir.path(), "fuchsia", &vbmeta_config, zbi_path).unwrap();
        assert_eq!(vbmeta_path, dir.path().join("fuchsia.vbmeta"));
    }

    #[test]
    fn sign_vbmeta() {
        let key_expected =
            Key::try_new(test_keys::TEST_RSA_4096_PEM, "TEST_METADATA".as_bytes()).unwrap();

        let mut vfs = MockFilesystemProvider::new();
        vfs.add("key", test_keys::TEST_RSA_4096_PEM.as_bytes());
        vfs.add("key_metadata", &b"TEST_METADATA"[..]);
        vfs.add("image", &[0x00u8; 128]);
        vfs.add("salt", &hex::encode(&[0xAAu8; 32]).as_bytes());

        let salt = Salt::try_from(&[0xAAu8; 32][..]).unwrap();

        let (vbmeta, salt) =
            sign("some_name", "image", "key", "key_metadata", Vec::new(), salt, &vfs).unwrap();

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
}
