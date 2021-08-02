// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::crypt::{AES256XTSKeys, Crypt, UnwrappedKeys},
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
    async fn create_key(
        &self,
        wrapping_key_id: u64,
        owner: u64,
    ) -> Result<(AES256XTSKeys, UnwrappedKeys), Error> {
        let (key, unwrapped_key) =
            self.client.create_key(wrapping_key_id, owner).await?.map_err(|e| anyhow!(e))?;
        Ok((
            AES256XTSKeys {
                wrapping_key_id,
                keys: vec![(0, key.try_into().map_err(|_| anyhow!("Unexpected key length"))?)],
            },
            UnwrappedKeys::new([(0, &unwrapped_key[..])])?,
        ))
    }

    async fn unwrap_keys(&self, keys: &AES256XTSKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
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
        Ok(UnwrappedKeys::new(
            keys.keys.iter().zip(unwrapped_keys.iter()).map(|((id, _), key)| (*id, &key[..])),
        )?)
    }
}
