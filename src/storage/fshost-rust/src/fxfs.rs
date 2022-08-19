// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_fxfs::{CryptManagementMarker, CryptMarker, KeyPurpose},
    fs_management::filesystem::{ServingMultiVolumeFilesystem, ServingVolume},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    key_bag::{Aes256Key, KeyBagManager, WrappingKey, AES128_KEY_SIZE},
    std::{ops::Deref, path::Path},
};

// As an invariant, `name` must be shorter than `key_bag::AES128_KEY_SIZE`.
fn generate_insecure_key(name: &[u8]) -> WrappingKey {
    let mut bytes = [0u8; AES128_KEY_SIZE];
    bytes[..name.len()].copy_from_slice(&name);
    WrappingKey::Aes128(bytes)
}

// Unwraps the data volume in `fs`.  Any failures should be treated as fatal and the filesystem
// should be reformatted and re-initialized.
pub async fn unlock_data_volume<'a>(
    fs: &'a mut ServingMultiVolumeFilesystem,
) -> Result<&'a mut ServingVolume, Error> {
    unlock_or_init_data_volume(fs, false).await
}

// Initializes the data volume in `fs`, which should be freshly reformatted.
pub async fn init_data_volume<'a>(
    fs: &'a mut ServingMultiVolumeFilesystem,
) -> Result<&'a mut ServingVolume, Error> {
    unlock_or_init_data_volume(fs, true).await
}

async fn unlock_or_init_data_volume<'a>(
    fs: &'a mut ServingMultiVolumeFilesystem,
    create: bool,
) -> Result<&'a mut ServingVolume, Error> {
    // Open up the unencrypted volume so that we can access the key-bag for data.
    let root_vol = if create {
        fs.create_volume("unencrypted", None).await?
    } else {
        fs.open_volume("unencrypted", None).await?
    };
    root_vol.bind_to_path("/unencrypted_volume")?;
    if create {
        std::fs::create_dir("/unencrypted_volume/keys")?;
    }
    let mut keybag = KeyBagManager::open(Path::new("/unencrypted_volume/keys/fxfs-data"))?;

    // TODO(fxbug.dev/94587): Use real hardware keys where available (i.e. load the policy from
    // /pkg/config/zxcrypt).
    // For legacy reasons, the key name is "zxcrypt"; this is so old recovery images will correctly
    // wipe the data key when performing a factory reset.
    // zxcrypt is the legacy crypto mechanism for minfs, which doesn't have its own encryption.
    let unwrap_key = generate_insecure_key(b"zxcrypt");

    let mut unwrap_fn = |slot, wrapping_key| {
        if create {
            keybag.new_key(slot, wrapping_key).map_err(|e| anyhow!(e))
        } else {
            keybag.unwrap_key(slot, wrapping_key).map_err(|e| anyhow!(e))
        }
    };
    let data_unwrapped = unwrap_fn(0, &unwrap_key).context("Failed to add or unwrap data key")?;
    let metadata_unwrapped =
        unwrap_fn(1, &unwrap_key).context("Failed to add or unwrap metadata key")?;
    init_crypt_service(data_unwrapped, metadata_unwrapped).await?;

    // OK, crypt is seeded with the stored keys, so we can finally open the data volume.
    let crypt_service = Some(
        connect_to_protocol::<CryptMarker>()
            .expect("Unable to connect to Crypt service")
            .into_channel()
            .unwrap()
            .into_zx_channel()
            .into(),
    );
    if create {
        fs.create_volume("data", crypt_service).await
    } else {
        fs.open_volume("data", crypt_service).await
    }
}

async fn init_crypt_service(data_key: Aes256Key, metadata_key: Aes256Key) -> Result<(), Error> {
    let crypt_management = connect_to_protocol::<CryptManagementMarker>()?;
    crypt_management.add_wrapping_key(0, data_key.deref()).await?.map_err(zx::Status::from_raw)?;
    crypt_management
        .add_wrapping_key(1, metadata_key.deref())
        .await?
        .map_err(zx::Status::from_raw)?;
    crypt_management.set_active_key(KeyPurpose::Data, 0).await?.map_err(zx::Status::from_raw)?;
    crypt_management
        .set_active_key(KeyPurpose::Metadata, 1)
        .await?
        .map_err(zx::Status::from_raw)?;
    Ok(())
}
