// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::crypt::{Crypt, UnwrappedKey, UnwrappedKeys, WrappedKey, WrappedKeys},
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
            WrappedKeys(vec![WrappedKey {
                wrapping_key_id,
                // TODO(jfsulliv): For key rolling, we need to assign a key ID which doesn't already
                // exist for the object.
                key_id: 0,
                key: key.try_into().map_err(|_| anyhow!("Unexpected key length"))?,
            }]),
            vec![UnwrappedKey::new(
                0,
                unwrapped_key.try_into().map_err(|_| anyhow!("Unexpected key length"))?,
            )],
        ))
    }

    async fn unwrap_keys(&self, keys: &WrappedKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
        // TODO(jfsulliv): Should we just change the Crypt interface to do one key at a time, or
        // attempt to batch the calls by wrapped key?  It seems that in practice we'll never have a
        // WrappedKey with the same wrapping key appearing twice, so the batch interface might not
        // make sense.
        let unwrap_key = |key: WrappedKey| async move {
            let raw_keys = vec![&key.key[..]];
            // Have to split this up because the &mut raw_keys... part isn't Send.
            let unwrapped_fut =
                self.client.unwrap_keys(key.wrapping_key_id, owner, &mut raw_keys.into_iter());
            let mut unwrapped = unwrapped_fut.await?.map_err(|e| anyhow!(e))?;
            assert!(unwrapped.len() == 1);
            if unwrapped[0].len() != 32 {
                bail!("Unexpected key length");
            }
            Ok(UnwrappedKey::new(key.key_id, unwrapped.pop().unwrap().try_into().unwrap()))
        };
        let mut futures = vec![];
        for key in keys.iter().cloned() {
            futures.push(unwrap_key(key));
        }
        let results = futures::future::join_all(futures).await;
        let mut keys = vec![];
        for result in results {
            keys.push(result?);
        }
        Ok(keys)
    }
}
