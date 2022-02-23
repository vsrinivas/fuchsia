// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::crypt::{Crypt, UnwrappedKey, UnwrappedKeys, WrappedKeys},
    anyhow::{anyhow, bail, Error},
    async_trait::async_trait,
    fidl_fuchsia_fxfs::CryptProxy,
    std::convert::TryInto,
};

pub struct RemoteCrypt {
    client: CryptProxy,
}

impl RemoteCrypt {
    pub fn new(client: CryptProxy) -> Self {
        Self { client }
    }
}

#[async_trait]
impl Crypt for RemoteCrypt {
    async fn create_key(&self, owner: u64) -> Result<(WrappedKeys, UnwrappedKeys), Error> {
        let (wrapping_key_id, key, unwrapped_key) =
            self.client.create_key(owner).await?.map_err(|e| anyhow!(e))?;
        Ok((
            WrappedKeys {
                wrapping_key_id,
                keys: vec![(0, key.try_into().map_err(|_| anyhow!("Unexpected key length"))?)],
            },
            vec![UnwrappedKey::new(
                0,
                unwrapped_key.try_into().map_err(|_| anyhow!("Unexpected key length"))?,
            )],
        ))
    }

    async fn unwrap_keys(&self, keys: &WrappedKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
        // Have to split this up because the &mut keys... bit isn't Send.
        let unwrapped_keys_fut = self.client.unwrap_keys(
            keys.wrapping_key_id,
            owner,
            &mut keys.keys.iter().map(|(_, key)| &key[..]),
        );
        let unwrapped_keys = unwrapped_keys_fut.await?.map_err(|e| anyhow!(e))?;
        for k in &unwrapped_keys {
            if k.len() != 32 {
                bail!("Unexpected key length");
            }
        }
        Ok(keys
            .keys
            .iter()
            .zip(unwrapped_keys.into_iter())
            .map(|((id, _), key)| UnwrappedKey::new(*id, key.try_into().unwrap()))
            .collect())
    }
}
