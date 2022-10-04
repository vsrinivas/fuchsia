// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The crypt mod contains all the key policy logic for the different operations that can be done
//! with hardware keys. It provides functions for unsealing and formatting zxcrypt as well as
//! unwrapping the actual keys for fxfs. Keeping the policy logic in one place makes it easier to
//! audit.

use anyhow::{bail, Context, Error};

pub mod fxfs;
pub mod zxcrypt;

enum Policy {
    Null,
    TeeRequired,
    TeeTransitional,
    TeeOpportunistic,
}

async fn get_policy() -> Result<Policy, Error> {
    let policy = fuchsia_fs::file::read_in_namespace_to_string("/pkg/config/zxcrypt").await?;
    match policy.as_ref() {
        "null" => Ok(Policy::Null),
        "tee" => Ok(Policy::TeeRequired),
        "tee-transitional" => Ok(Policy::TeeTransitional),
        "tee-opportunistic" => Ok(Policy::TeeOpportunistic),
        p => bail!("unrecognized key source policy: {}", p),
    }
}

#[derive(Debug)]
enum KeySource {
    Null,
    Tee,
}

/// Fxfs and zxcrypt have different null keys, so operations have to indicate which is ultimately
/// going to consume the key we produce.
enum KeyConsumer {
    /// The null key for fxfs is a 128-bit key with the bytes "zxcrypt" at the beginning and then
    /// padded with zeros. This is for legacy reasons - earlier versions of this code picked this
    /// key, so we need to continue to use it to avoid wiping everyone's null-key-encrypted fxfs
    /// data partitions.
    Fxfs,
    /// The null key for zxcrypt is a 256-bit key containing all zeros.
    Zxcrypt,
}

impl KeySource {
    async fn get_key(&self, consumer: KeyConsumer) -> Result<Vec<u8>, Error> {
        match self {
            KeySource::Null => match consumer {
                KeyConsumer::Fxfs => {
                    let mut key = b"zxcrypt".to_vec();
                    key.resize(16, 0);
                    Ok(key)
                }
                KeyConsumer::Zxcrypt => Ok(vec![0u8; 32]),
            },
            KeySource::Tee => {
                // Regardless of the consumer of this key, the key we retrieve with kms is always
                // named "zxcrypt". This is so that old recovery images that might not be aware of
                // fxfs can still wipe the data keys during a factory reset.
                kms_stateless::get_hardware_derived_key(kms_stateless::KeyInfo::new_zxcrypt())
                    .await
                    .context("failed to get hardware key")
            }
        }
    }
}

fn format_sources(policy: Policy) -> Vec<KeySource> {
    match policy {
        Policy::Null => vec![KeySource::Null],
        Policy::TeeRequired => vec![KeySource::Tee],
        Policy::TeeTransitional => vec![KeySource::Tee],
        Policy::TeeOpportunistic => vec![KeySource::Tee, KeySource::Null],
    }
}

fn unseal_sources(policy: Policy) -> Vec<KeySource> {
    match policy {
        Policy::Null => vec![KeySource::Null],
        Policy::TeeRequired => vec![KeySource::Tee],
        Policy::TeeTransitional => vec![KeySource::Tee, KeySource::Null],
        Policy::TeeOpportunistic => vec![KeySource::Tee, KeySource::Null],
    }
}
