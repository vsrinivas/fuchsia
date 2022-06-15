// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::crypt::{Crypt, KeyPurpose, UnwrappedKey, WrappedKey, WrappedKeyBytes, KEY_SIZE},
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

#[async_trait]
impl Crypt for RemoteCrypt {
    async fn create_key(
        &self,
        owner: u64,
        purpose: KeyPurpose,
    ) -> Result<(WrappedKey, UnwrappedKey), Error> {
        let (wrapping_key_id, key, unwrapped_key) =
            self.client.create_key(owner, purpose.into()).await?.map_err(|e| anyhow!(e))?;
        Ok((
            WrappedKey {
                wrapping_key_id,
                key: WrappedKeyBytes(
                    key.try_into().map_err(|_| anyhow!("Unexpected wrapped key length"))?,
                ),
            },
            UnwrappedKey::new(
                unwrapped_key.try_into().map_err(|_| anyhow!("Unexpected unwrapped key length"))?,
            ),
        ))
    }

    async fn unwrap_key(
        &self,
        wrapped_key: &WrappedKey,
        owner: u64,
    ) -> Result<UnwrappedKey, Error> {
        let unwrapped = self
            .client
            .unwrap_key(wrapped_key.wrapping_key_id, owner, &wrapped_key.key[..])
            .await?
            .map_err(|e| anyhow!(e))?;
        if unwrapped.len() != KEY_SIZE {
            bail!("Unexpected key length");
        }
        Ok(UnwrappedKey::new(unwrapped.try_into().unwrap()))
    }
}
