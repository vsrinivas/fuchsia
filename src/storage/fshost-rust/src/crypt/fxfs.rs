// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{format_sources, get_policy, unseal_sources, KeyConsumer},
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_fxfs::{CryptManagementMarker, CryptMarker, KeyPurpose},
    fs_management::filesystem::{ServingMultiVolumeFilesystem, ServingVolume},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    key_bag::{Aes256Key, KeyBagManager, WrappingKey, AES128_KEY_SIZE, AES256_KEY_SIZE},
    std::{ops::Deref, path::Path},
};

async fn unwrap_or_create_keys(
    mut keybag: KeyBagManager,
    create: bool,
) -> Result<(Aes256Key, Aes256Key), Error> {
    let policy = get_policy().await?;
    let sources = if create { format_sources(policy) } else { unseal_sources(policy) };

    let mut last_err = anyhow!("no keys?");
    for source in sources {
        let key = source.get_key(KeyConsumer::Fxfs).await?;
        let wrapping_key = match key.len() {
            // unwrap is safe because we know the length of the requested array is the same length
            // as the Vec in both branches.
            AES128_KEY_SIZE => WrappingKey::Aes128(key.try_into().unwrap()),
            AES256_KEY_SIZE => WrappingKey::Aes256(key.try_into().unwrap()),
            _ => {
                tracing::warn!("key from {:?} source was an invalid size - skipping", source);
                last_err = anyhow!("invalid key size");
                continue;
            }
        };

        let mut unwrap_fn = |slot| {
            if create {
                keybag.new_key(slot, &wrapping_key).context("new key")
            } else {
                keybag.unwrap_key(slot, &wrapping_key).context("unwrapping key")
            }
        };

        let data_unwrapped = match unwrap_fn(0) {
            Ok(data_unwrapped) => data_unwrapped,
            Err(e) => {
                last_err = e.context("data key");
                continue;
            }
        };
        let metadata_unwrapped = match unwrap_fn(1) {
            Ok(metadata_unwrapped) => metadata_unwrapped,
            Err(e) => {
                last_err = e.context("metadata key");
                continue;
            }
        };
        return Ok((data_unwrapped, metadata_unwrapped));
    }
    Err(last_err)
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
    let keybag = KeyBagManager::open(Path::new("/unencrypted_volume/keys/fxfs-data"))?;

    let (data_unwrapped, metadata_unwrapped) = unwrap_or_create_keys(keybag, create).await?;

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
