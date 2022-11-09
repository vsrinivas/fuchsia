// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::extra_hash_descriptor::ExtraHashDescriptor;
use crate::vfs::{FilesystemProvider, RealFilesystemProvider};
use anyhow::Result;
use assembly_images_config::{VBMeta, VBMetaDescriptor};
use assembly_manifest::{AssemblyManifest, Image};
use assembly_util::path_relative_from_current_dir;
use camino::{Utf8Path, Utf8PathBuf};
use std::path::Path;
use vbmeta::VBMeta as VBMetaImage;
use vbmeta::{HashDescriptor, Key, Salt};

pub fn construct_vbmeta(
    assembly_manifest: &mut AssemblyManifest,
    outdir: impl AsRef<Utf8Path>,
    vbmeta_config: &VBMeta,
    zbi: impl AsRef<Path>,
) -> Result<Utf8PathBuf> {
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
    let vbmeta_path_relative = path_relative_from_current_dir(vbmeta_path)?;
    assembly_manifest.images.push(Image::VBMeta(vbmeta_path_relative.clone()));
    Ok(vbmeta_path_relative)
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
    use super::{construct_vbmeta, sign};

    use crate::vfs::mock::MockFilesystemProvider;

    use assembly_images_config::VBMeta;
    use assembly_manifest::AssemblyManifest;
    use assembly_util::path_relative_from_current_dir;
    use camino::Utf8Path;
    use std::convert::TryFrom;
    use tempfile::tempdir;
    use vbmeta::{Key, Salt};

    #[test]
    fn construct() {
        let tmp = tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let key_path = dir.join("key");
        let metadata_path = dir.join("key_metadata");
        std::fs::write(&key_path, test_keys::ATX_TEST_KEY).unwrap();
        std::fs::write(&metadata_path, test_keys::TEST_RSA_4096_PEM).unwrap();

        let vbmeta_config = VBMeta {
            name: "fuchsia".into(),
            key: key_path,
            key_metadata: metadata_path,
            additional_descriptors: vec![],
        };

        // Create a fake zbi.
        let zbi_path = dir.join("fuchsia.zbi");
        std::fs::write(&zbi_path, "fake zbi").unwrap();

        let mut assembly_manifest = AssemblyManifest::default();
        let vbmeta_path =
            construct_vbmeta(&mut assembly_manifest, dir, &vbmeta_config, zbi_path).unwrap();
        assert_eq!(
            vbmeta_path,
            path_relative_from_current_dir(dir.join("fuchsia.vbmeta")).unwrap()
        );
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
