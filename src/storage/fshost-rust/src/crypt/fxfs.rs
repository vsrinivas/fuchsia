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

const LEGACY_DATA_KEY: Aes256Key = Aes256Key::create([
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
]);

const LEGACY_METADATA_KEY: Aes256Key = Aes256Key::create([
    0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0,
    0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8, 0xe7, 0xe6, 0xe5, 0xe4, 0xe3, 0xe2, 0xe1, 0xe0,
]);

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
// Returns the name of the data volume as well as a reference to it.
pub async fn unlock_data_volume<'a>(
    fs: &'a mut ServingMultiVolumeFilesystem,
    config: &'a fshost_config::Config,
) -> Result<(String, &'a mut ServingVolume), Error> {
    unlock_or_init_data_volume(fs, config, false).await
}

// Initializes the data volume in `fs`, which should be freshly reformatted.
// Returns the name of the data volume as well as a reference to it.
pub async fn init_data_volume<'a>(
    fs: &'a mut ServingMultiVolumeFilesystem,
    config: &'a fshost_config::Config,
) -> Result<(String, &'a mut ServingVolume), Error> {
    unlock_or_init_data_volume(fs, config, true).await
}

async fn unlock_or_init_data_volume<'a>(
    fs: &'a mut ServingMultiVolumeFilesystem,
    config: &'a fshost_config::Config,
    create: bool,
) -> Result<(String, &'a mut ServingVolume), Error> {
    let mut use_native_fxfs_crypto = config.use_native_fxfs_crypto;
    let has_native_layout = !fs.has_volume("default").await?;
    if !create && (has_native_layout != use_native_fxfs_crypto) {
        tracing::warn!("Overriding use_native_fxfs_crypto due to detected different layout");
        use_native_fxfs_crypto = !use_native_fxfs_crypto;
    }

    let data_volume_name = if use_native_fxfs_crypto {
        // Open up the unencrypted volume so that we can access the key-bag for data.
        let root_vol = if create {
            fs.create_volume("unencrypted", None).await.context("Failed to create unencrypted")?
        } else {
            fs.open_volume("unencrypted", None).await.context("Failed to open unencrypted")?
        };
        root_vol.bind_to_path("/unencrypted_volume")?;
        if create {
            std::fs::create_dir("/unencrypted_volume/keys")?;
        }
        let keybag = KeyBagManager::open(Path::new("/unencrypted_volume/keys/fxfs-data"))?;

        let (data_unwrapped, metadata_unwrapped) = unwrap_or_create_keys(keybag, create).await?;
        init_crypt_service(data_unwrapped, metadata_unwrapped).await?;
        "data".to_string()
    } else {
        init_crypt_service(LEGACY_DATA_KEY, LEGACY_METADATA_KEY).await?;
        "default".to_string()
    };

    let crypt_service = Some(
        connect_to_protocol::<CryptMarker>()
            .expect("Unable to connect to Crypt service")
            .into_channel()
            .unwrap()
            .into_zx_channel()
            .into(),
    );
    Ok((
        data_volume_name.clone(),
        if create {
            fs.create_volume(&data_volume_name, crypt_service)
                .await
                .context("Failed to create data")?
        } else {
            fs.open_volume(&data_volume_name, crypt_service).await.context("Failed to open data")?
        },
    ))
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
