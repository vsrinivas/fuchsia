// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(csuter): Make this secure.  For now these are just some stubs that need to be filled out
// with real implementations.  It is also likely that at some point we'll need to use traits here.

use {
    crate::object_store::record::AES256XTSKeys,
    anyhow::{anyhow, Error},
    byteorder::{ByteOrder, LittleEndian},
    rand::RngCore,
};

// This structure stores unwrapped keys. For now, the format is used just makes it convenient for
// the simple XOR scheme we are using, but going forward, this can take whatever form is suitable.
pub struct UnwrappedKeys {
    keys: Vec<(u64, [u64; 4])>,
}

impl UnwrappedKeys {
    /// Decrypt the data in buffer.  `key_id` specifies which of the unwrapped keys to use.
    /// `offset` is the tweak.
    pub fn decrypt(&self, mut offset: u64, key_id: u64, buffer: &mut [u8]) -> Result<(), Error> {
        let key =
            &self.keys.iter().find(|(id, _)| *id == key_id).ok_or(anyhow!("Key not found"))?.1;
        assert_eq!(buffer.len() % 16, 0);
        assert_eq!(offset % 8, 0);
        let mut i = (offset / 8 % 4) as usize;
        for chunk in buffer.chunks_exact_mut(8) {
            LittleEndian::write_u64(chunk, LittleEndian::read_u64(chunk) ^ key[i] ^ offset);
            i = (i + 1) & 3;
            offset += 8;
        }
        Ok(())
    }

    /// Encrypts data in the buffer.  The first key in the unwrapped keys will be used.  `offset` is
    /// the tweak.
    pub fn encrypt(&self, offset: u64, buffer: &mut [u8]) {
        // For now, always use the first key and since it's XOR, we can just use decrypt.
        self.decrypt(offset, self.keys[0].0, buffer).unwrap();
    }
}

pub struct Crypt {}

const WRAP_XOR: u64 = 0x012345678abcdef;

impl Crypt {
    pub fn new() -> Self {
        Crypt {}
    }

    /// `owner` is intended to be used such that when the key is wrapped, it appears to be different
    /// to that of the same key wrapped by a different owner.  In this way, keys can be shared
    /// amongst different filesystem objects (e.g. for clones), but it is not possible to tell just
    /// by looking at the wrapped keys.
    pub fn create_key(
        &self,
        wrapping_key_id: u64,
        owner: u64,
    ) -> Result<(AES256XTSKeys, UnwrappedKeys), Error> {
        assert_eq!(wrapping_key_id, 0);
        let mut rng = rand::thread_rng();
        let mut key = [0; 32];
        rng.fill_bytes(&mut key);
        let mut unwrapped = [0; 4];
        let mut wrapped = [0; 32];
        for (i, chunk) in key.chunks_exact(8).enumerate() {
            let u = LittleEndian::read_u64(chunk);
            unwrapped[i] = u;
            LittleEndian::write_u64(&mut wrapped[i * 8..i * 8 + 8], u ^ WRAP_XOR ^ owner);
        }
        Ok((
            AES256XTSKeys { wrapping_key_id, keys: vec![(0, wrapped)] },
            UnwrappedKeys { keys: vec![(0, unwrapped)] },
        ))
    }

    /// Unwraps the keys and stores the result in UnwrappedKeys.
    pub fn unwrap(&self, keys: &AES256XTSKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
        Ok(UnwrappedKeys {
            keys: keys
                .keys
                .iter()
                .map(|(id, key)| {
                    let mut unwrapped = [0; 4];
                    for (i, chunk) in key.chunks_exact(8).enumerate() {
                        unwrapped[i] = LittleEndian::read_u64(chunk) ^ WRAP_XOR ^ owner;
                    }
                    (*id, unwrapped)
                })
                .collect(),
        })
    }
}
