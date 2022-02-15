// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::VBMetaConfig;
use crate::extra_hash_descriptor::ExtraHashDescriptor;
use crate::util::read_config;
use crate::vfs::{FilesystemProvider, RealFilesystemProvider};
use anyhow::Result;
use assembly_images_config::{VBMeta, VBMetaDescriptor};
use std::path::{Path, PathBuf};
use vbmeta::VBMeta as VBMetaImage;
use vbmeta::{HashDescriptor, Key, Salt};

pub fn convert_to_new_config(name: impl AsRef<str>, config: &VBMetaConfig) -> Result<VBMeta> {
    let VBMetaConfig { kernel_partition: _, key, key_metadata, additional_descriptor_files } =
        config;
    let mut additional_descriptors = Vec::new();
    for path in additional_descriptor_files {
        let descriptor = read_config(path)?;
        additional_descriptors.push(descriptor);
    }

    let name = name.as_ref().to_string();
    let key = key.clone();
    let key_metadata = key_metadata.clone();
    Ok(VBMeta { name, key, key_metadata, additional_descriptors })
}

pub fn construct_vbmeta(
    outdir: impl AsRef<Path>,
    vbmeta_config: &VBMeta,
    zbi: impl AsRef<Path>,
) -> Result<PathBuf> {
    // Generate the salt.
    let salt = Salt::random()?;

    // Sign the image and construct a VBMeta.
    let (vbmeta, _salt) = crate::vbmeta::sign(
        "zircon",
        zbi,
        &vbmeta_config.key,
        &vbmeta_config.key_metadata,
        &vbmeta_config.additional_descriptors,
        salt,
        &RealFilesystemProvider {},
    )?;

    // Write VBMeta to a file and return the path.
    let vbmeta_path = outdir.as_ref().join(format!("{}.vbmeta", vbmeta_config.name));
    std::fs::write(&vbmeta_path, vbmeta.as_bytes())?;
    Ok(vbmeta_path)
}

pub fn sign<FSP: FilesystemProvider>(
    name: impl AsRef<str>,
    image_path: impl AsRef<Path>,
    key: impl AsRef<Path>,
    key_metadata: impl AsRef<Path>,
    additional_descriptors: &Vec<VBMetaDescriptor>,
    salt: Salt,
    fs: &FSP,
) -> Result<(VBMetaImage, Salt)> {
    // Read the signing key's bytes and metadata.
    let key_pem = fs.read_to_string(key)?;
    let key_metadata = fs.read(key_metadata)?;
    // And then create the signing key from those.
    let key = Key::try_new(&key_pem, key_metadata).unwrap();

    let mut descriptors: Vec<HashDescriptor> = additional_descriptors
        .iter()
        .map(|d| {
            ExtraHashDescriptor {
                name: Some(d.name.clone()),
                size: Some(d.size),
                salt: None,
                digest: None,
                flags: Some(d.flags),
                min_avb_version: None, //Some(d.min_avb_version),
            }
            .into()
        })
        .collect();

    // Read the image into memory, so that it can be hashed.
    let image = fs.read(image_path)?;

    // Create the descriptor for the image.
    let descriptor = HashDescriptor::new(name.as_ref(), &image, salt.clone());
    descriptors.push(descriptor);

    // And do the signing operation itself.
    VBMetaImage::sign(descriptors, key).map_err(Into::into).map(|vbmeta| (vbmeta, salt))
}

#[cfg(test)]
mod tests {
    use super::{construct_vbmeta, convert_to_new_config, sign};

    use crate::config::VBMetaConfig;
    use crate::vfs::mock::MockFilesystemProvider;

    use assembly_images_config::VBMeta;
    use serde_json::json;
    use std::convert::TryFrom;
    use std::path::PathBuf;
    use tempfile::tempdir;
    use vbmeta::{Key, Salt};

    #[test]
    fn old_config() {
        // Write some descriptor files.
        let dir = tempdir().unwrap();
        let descriptor_path = dir.path().join("descriptor.json");
        let descriptor_value = json!({
            "name": "partition",
            "size": 1234,
            "flags": 1,
            "min_avb_version": "1.0",
        });
        let descriptor_bytes = serde_json::to_vec(&descriptor_value).unwrap();
        std::fs::write(&descriptor_path, descriptor_bytes).unwrap();

        let old_config = VBMetaConfig {
            kernel_partition: "kernel".into(),
            key: "path/to/key".into(),
            key_metadata: "path/to/key_metadata".into(),
            additional_descriptor_files: vec![descriptor_path],
        };
        let new_config = convert_to_new_config("name", &old_config).unwrap();
        assert_eq!(new_config.name, "name");
        assert_eq!(new_config.key, PathBuf::from("path/to/key"));
        assert_eq!(new_config.key_metadata, PathBuf::from("path/to/key_metadata"));
        assert_eq!(new_config.additional_descriptors.len(), 1);
        assert_eq!(new_config.additional_descriptors[0].name, "partition");
        assert_eq!(new_config.additional_descriptors[0].size, 1234);
        assert_eq!(new_config.additional_descriptors[0].flags, 1);
        assert_eq!(new_config.additional_descriptors[0].min_avb_version, "1.0");
    }

    #[test]
    fn construct() {
        let dir = tempdir().unwrap();

        let key_path = dir.path().join("key");
        let metadata_path = dir.path().join("key_metadata");
        std::fs::write(&key_path, test_keys::ATX_TEST_KEY).unwrap();
        std::fs::write(&metadata_path, test_keys::TEST_RSA_4096_PEM).unwrap();

        let vbmeta_config = VBMeta {
            name: "fuchsia".into(),
            key: key_path,
            key_metadata: metadata_path,
            additional_descriptors: vec![],
        };

        // Create a fake zbi.
        let zbi_path = dir.path().join("fuchsia.zbi");
        std::fs::write(&zbi_path, "fake zbi").unwrap();

        let vbmeta_path = construct_vbmeta(dir.path(), &vbmeta_config, zbi_path).unwrap();
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
            sign("some_name", "image", "key", "key_metadata", &Vec::new(), salt, &vfs).unwrap();

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
