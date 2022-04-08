// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::crypt::{
        Crypt, KeyPurpose, UnwrappedKey, UnwrappedKeys, WrappedKey, WrappedKeyBytes, WrappedKeys,
        KEY_SIZE,
    },
    anyhow::{anyhow, bail, Error},
    async_trait::async_trait,
    fidl_fuchsia_fxfs::{CryptProxy, KeyPurpose as FidlKeyPurpose},
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

impl From<KeyPurpose> for FidlKeyPurpose {
    fn from(other: KeyPurpose) -> Self {
        match other {
            KeyPurpose::Data => Self::Data,
            KeyPurpose::Metadata => Self::Metadata,
        }
    }
}

impl RemoteCrypt {
    async fn unwrap_key(&self, key: &WrappedKey, owner: u64) -> Result<UnwrappedKey, Error> {
        let unwrapped = self
            .client
            .unwrap_key(key.wrapping_key_id, owner, &key.key[..])
            .await?
            .map_err(|e| anyhow!(e))?;
        if unwrapped.len() != KEY_SIZE {
            bail!("Unexpected key length");
        }
        Ok(UnwrappedKey::new(key.key_id, unwrapped.try_into().unwrap()))
    }
}

#[async_trait]
impl Crypt for RemoteCrypt {
    async fn create_key(
        &self,
        owner: u64,
        purpose: KeyPurpose,
    ) -> Result<(WrappedKeys, UnwrappedKeys), Error> {
        let (wrapping_key_id, key, unwrapped_key) =
            self.client.create_key(owner, purpose.into()).await?.map_err(|e| anyhow!(e))?;
        Ok((
            WrappedKeys(vec![WrappedKey {
                wrapping_key_id,
                // TODO(fxbug.dev/96131): For key rolling, we need to assign a key ID which doesn't
                // already exist for the object.
                key_id: 0,
                key: WrappedKeyBytes(
                    key.try_into().map_err(|_| anyhow!("Unexpected wrapped key length"))?,
                ),
            }]),
            vec![UnwrappedKey::new(
                0,
                unwrapped_key.try_into().map_err(|_| anyhow!("Unexpected unwrapped key length"))?,
            )],
        ))
    }

    async fn unwrap_keys(&self, keys: &WrappedKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
        let mut futures = vec![];
        for key in keys.iter() {
            futures.push(self.unwrap_key(&key, owner));
        }
        let results = futures::future::join_all(futures).await;
        let mut keys = vec![];
        for result in results {
            keys.push(result?);
        }
        Ok(keys)
    }
}
