// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    aes_gcm_siv::{
        aead::{Aead, NewAead},
        Aes256GcmSiv, Key, Nonce,
    },
    anyhow::{Context, Error},
    byteorder::{ByteOrder, LittleEndian},
    fidl_fuchsia_fxfs::{
        CryptCreateKeyResult, CryptManagementAddWrappingKeyResult,
        CryptManagementForgetWrappingKeyResult, CryptManagementRequest,
        CryptManagementRequestStream, CryptManagementSetActiveKeyResult, CryptRequest,
        CryptRequestStream, CryptUnwrapKeyResult, KeyPurpose,
    },
    fuchsia_zircon as zx,
    futures::stream::TryStreamExt,
    std::{
        collections::hash_map::{Entry, HashMap},
        sync::Mutex,
    },
};

pub mod log;
use log::*;

pub enum Services {
    Crypt(CryptRequestStream),
    CryptManagement(CryptManagementRequestStream),
}

const WRAP_XOR: u64 = 0x012345678abcdef;

#[derive(Default)]
struct CryptServiceInner {
    ciphers: HashMap<u64, Aes256GcmSiv>,
    active_data_key: Option<u64>,
    active_metadata_key: Option<u64>,
}

pub struct CryptService {
    // When set, a fake (and insecure) crypto algorithm is used, which ignores the keys provided by
    // CryptManagement and instead uses a hard-coded "key".
    use_legacy_stubbed_crypto: bool,
    inner: Mutex<CryptServiceInner>,
}

fn zero_extended_nonce(val: u64) -> Nonce {
    let mut nonce = Nonce::default();
    nonce.as_mut_slice()[..8].copy_from_slice(&val.to_le_bytes());
    nonce
}

impl CryptService {
    pub fn new(use_legacy_stubbed_crypto: bool) -> Self {
        Self { use_legacy_stubbed_crypto, inner: Mutex::new(CryptServiceInner::default()) }
    }

    fn create_key(&self, owner: u64, purpose: KeyPurpose) -> CryptCreateKeyResult {
        if self.use_legacy_stubbed_crypto {
            return self.create_key_legacy(owner);
        }
        let inner = self.inner.lock().unwrap();
        let wrapping_key_id = match purpose {
            KeyPurpose::Data => inner.active_data_key.as_ref(),
            KeyPurpose::Metadata => inner.active_metadata_key.as_ref(),
            _ => return Err(zx::Status::INVALID_ARGS.into_raw()),
        }
        .ok_or(zx::Status::BAD_STATE.into_raw())?;
        let cipher = inner.ciphers.get(wrapping_key_id).ok_or(zx::Status::BAD_STATE.into_raw())?;
        let nonce = zero_extended_nonce(owner);

        let mut key = [0u8; 32];
        zx::cprng_draw(&mut key);

        let wrapped = cipher.encrypt(&nonce, &key[..]).map_err(|e| {
            error!(error = ?e, "Failed to wrap key");
            zx::Status::INTERNAL.into_raw()
        })?;

        Ok((*wrapping_key_id, wrapped.into(), key.into()))
    }

    fn create_key_legacy(&self, owner: u64) -> CryptCreateKeyResult {
        let mut key = [0; 32];
        zx::cprng_draw(&mut key);
        let mut wrapped = [0; 32];
        for (i, chunk) in key.chunks_exact(8).enumerate() {
            LittleEndian::write_u64(
                &mut wrapped[i * 8..i * 8 + 8],
                LittleEndian::read_u64(chunk) ^ WRAP_XOR ^ owner,
            );
        }
        Ok((0, wrapped.into(), key.into()))
    }

    fn unwrap_key(&self, wrapping_key_id: u64, owner: u64, key: Vec<u8>) -> CryptUnwrapKeyResult {
        if self.use_legacy_stubbed_crypto {
            return self.unwrap_key_legacy(wrapping_key_id, owner, key);
        }
        let inner = self.inner.lock().unwrap();
        let cipher = inner.ciphers.get(&wrapping_key_id).ok_or(zx::Status::NOT_FOUND.into_raw())?;
        let nonce = zero_extended_nonce(owner);

        cipher.decrypt(&nonce, &key[..]).map_err(|_| zx::Status::IO_DATA_INTEGRITY.into_raw())
    }

    fn unwrap_key_legacy(
        &self,
        wrapping_key_id: u64,
        owner: u64,
        key: Vec<u8>,
    ) -> CryptUnwrapKeyResult {
        assert_eq!(wrapping_key_id, 0, "Key ID must be 0 for legacy keys.");
        let mut unwrapped = vec![0; 32];
        for (chunk, mut unwrapped) in key.chunks_exact(8).zip(unwrapped.chunks_exact_mut(8)) {
            LittleEndian::write_u64(
                &mut unwrapped,
                LittleEndian::read_u64(chunk) ^ WRAP_XOR ^ owner,
            );
        }
        Ok(unwrapped)
    }

    fn add_wrapping_key(
        &self,
        wrapping_key_id: u64,
        key: Vec<u8>,
    ) -> CryptManagementAddWrappingKeyResult {
        let mut inner = self.inner.lock().unwrap();
        match inner.ciphers.entry(wrapping_key_id) {
            Entry::Occupied(_) => Err(zx::Status::ALREADY_EXISTS.into_raw()),
            Entry::Vacant(vacant) => {
                info!(wrapping_key_id, "Adding wrapping key");
                vacant.insert(Aes256GcmSiv::new(Key::from_slice(&key[..])));
                Ok(())
            }
        }
    }

    fn set_active_key(
        &self,
        purpose: KeyPurpose,
        wrapping_key_id: u64,
    ) -> CryptManagementSetActiveKeyResult {
        let mut inner = self.inner.lock().unwrap();
        if !inner.ciphers.contains_key(&wrapping_key_id) {
            return Err(zx::Status::NOT_FOUND.into_raw());
        }
        match purpose {
            KeyPurpose::Data => inner.active_data_key = Some(wrapping_key_id),
            KeyPurpose::Metadata => inner.active_metadata_key = Some(wrapping_key_id),
            _ => return Err(zx::Status::INVALID_ARGS.into_raw()),
        }
        Ok(())
    }

    fn forget_wrapping_key(&self, wrapping_key_id: u64) -> CryptManagementForgetWrappingKeyResult {
        info!(wrapping_key_id, "Removing wrapping key");
        let mut inner = self.inner.lock().unwrap();
        if let Some(id) = &inner.active_data_key {
            if *id == wrapping_key_id {
                return Err(zx::Status::INVALID_ARGS.into_raw());
            }
        }
        if let Some(id) = &inner.active_metadata_key {
            if *id == wrapping_key_id {
                return Err(zx::Status::INVALID_ARGS.into_raw());
            }
        }
        inner.ciphers.remove(&wrapping_key_id);
        Ok(())
    }

    pub async fn handle_request(&self, stream: Services) -> Result<(), Error> {
        match stream {
            Services::Crypt(mut stream) => {
                while let Some(request) = stream.try_next().await.context("Reading request")? {
                    match request {
                        CryptRequest::CreateKey { owner, purpose, responder } => {
                            let mut response = self.create_key(owner, purpose);
                            responder.send(&mut response).unwrap_or_else(|e| {
                                error!(error = e.as_value(), "Failed to send CreateKey response")
                            });
                        }
                        CryptRequest::UnwrapKey { wrapping_key_id, owner, key, responder } => {
                            let mut response = self.unwrap_key(wrapping_key_id, owner, key);
                            responder.send(&mut response).unwrap_or_else(|e| {
                                error!(error = e.as_value(), "Failed to send UnwrapKey response")
                            });
                        }
                    }
                }
            }
            Services::CryptManagement(mut stream) => {
                while let Some(request) = stream.try_next().await.context("Reading request")? {
                    match request {
                        CryptManagementRequest::AddWrappingKey {
                            wrapping_key_id,
                            key,
                            responder,
                        } => {
                            let mut response = self.add_wrapping_key(wrapping_key_id, key);
                            responder.send(&mut response).unwrap_or_else(|e| {
                                error!(
                                    error = e.as_value(),
                                    "Failed to send AddWrappingKey response"
                                )
                            });
                        }
                        CryptManagementRequest::SetActiveKey {
                            purpose,
                            wrapping_key_id,
                            responder,
                        } => {
                            let mut response = self.set_active_key(purpose, wrapping_key_id);
                            responder.send(&mut response).unwrap_or_else(|e| {
                                error!(error = e.as_value(), "Failed to send SetActiveKey response")
                            });
                        }
                        CryptManagementRequest::ForgetWrappingKey {
                            wrapping_key_id,
                            responder,
                        } => {
                            let mut response = self.forget_wrapping_key(wrapping_key_id);
                            responder.send(&mut response).unwrap_or_else(|e| {
                                error!(
                                    error = e.as_value(),
                                    "Failed to send ForgetWrappingKey response"
                                )
                            });
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::CryptService, fidl_fuchsia_fxfs::KeyPurpose};

    #[test]
    fn wrap_unwrap_legacy_key() {
        let service = CryptService::new(true);
        let (wrapping_key_id, wrapped, unwrapped) =
            service.create_key(0, KeyPurpose::Data).expect("create_key failed");
        let unwrap_result =
            service.unwrap_key(wrapping_key_id, 0, wrapped).expect("unwrap_key failed");
        assert_eq!(unwrap_result, unwrapped);
    }

    #[test]
    fn unwrap_legacy_key_wrong_key() {
        let service = CryptService::new(true);
        let (wrapping_key_id, mut wrapped, unwrapped) =
            service.create_key(0, KeyPurpose::Data).expect("create_key failed");
        for byte in &mut wrapped {
            *byte ^= 0xff;
        }
        // The legacy algorithm has no authentication, so the call will succeed but return the wrong
        // value.
        let unwrap_result =
            service.unwrap_key(wrapping_key_id, 0, wrapped).expect("unwrap_key failed");
        assert_ne!(unwrap_result, unwrapped);
    }

    #[test]
    fn wrap_unwrap_key() {
        let service = CryptService::new(false);
        let key = vec![0xABu8; 32];
        service.add_wrapping_key(1, key.clone()).expect("add_key failed");
        service.set_active_key(KeyPurpose::Data, 1).expect("set_active_key failed");

        let (wrapping_key_id, wrapped, unwrapped) =
            service.create_key(0, KeyPurpose::Data).expect("create_key failed");
        assert_eq!(wrapping_key_id, 1);
        let unwrap_result =
            service.unwrap_key(wrapping_key_id, 0, wrapped).expect("unwrap_key failed");
        assert_eq!(unwrap_result, unwrapped);

        // Do it twice to make sure the service can use the same key repeatedly.
        let (wrapping_key_id, wrapped, unwrapped) =
            service.create_key(1, KeyPurpose::Data).expect("create_key failed");
        assert_eq!(wrapping_key_id, 1);
        let unwrap_result =
            service.unwrap_key(wrapping_key_id, 1, wrapped).expect("unwrap_key failed");
        assert_eq!(unwrap_result, unwrapped);
    }

    #[test]
    fn unwrap_key_wrong_key() {
        let service = CryptService::new(false);
        let key = vec![0xABu8; 32];
        service.add_wrapping_key(0, key.clone()).expect("add_key failed");
        service.set_active_key(KeyPurpose::Data, 0).expect("set_active_key failed");

        let (wrapping_key_id, mut wrapped, _) =
            service.create_key(0, KeyPurpose::Data).expect("create_key failed");
        for byte in &mut wrapped {
            *byte ^= 0xff;
        }
        service.unwrap_key(wrapping_key_id, 0, wrapped).expect_err("unwrap_key should fail");
    }

    #[test]
    fn unwrap_key_wrong_owner() {
        let service = CryptService::new(false);
        let key = vec![0xABu8; 32];
        service.add_wrapping_key(0, key.clone()).expect("add_key failed");
        service.set_active_key(KeyPurpose::Data, 0).expect("set_active_key failed");

        let (wrapping_key_id, wrapped, _) =
            service.create_key(0, KeyPurpose::Data).expect("create_key failed");
        service.unwrap_key(wrapping_key_id, 1, wrapped).expect_err("unwrap_key should fail");
    }

    #[test]
    fn add_forget_key() {
        let service = CryptService::new(true);
        let key = vec![0xABu8; 32];
        service.add_wrapping_key(0, key.clone()).expect("add_key failed");
        service.add_wrapping_key(0, key.clone()).expect_err("add_key should fail on a used slot");
        service.add_wrapping_key(1, key.clone()).expect("add_key failed");

        service.forget_wrapping_key(0).expect("forget_key failed");

        service.add_wrapping_key(0, key.clone()).expect("add_key failed");
    }

    #[test]
    fn set_active_key() {
        let service = CryptService::new(true);
        let key = vec![0xABu8; 32];

        service
            .set_active_key(KeyPurpose::Data, 0)
            .expect_err("set_active_key should fail when targeting nonexistent keys");

        service.add_wrapping_key(0, key.clone()).expect("add_key failed");
        service.add_wrapping_key(1, key.clone()).expect("add_key failed");

        service.set_active_key(KeyPurpose::Data, 0).expect("set_active_key failed");
        service.set_active_key(KeyPurpose::Metadata, 1).expect("set_active_key failed");

        service.forget_wrapping_key(0).expect_err("forget_key should fail on an active key");
        service.forget_wrapping_key(1).expect_err("forget_key should fail on an active key");
    }
}
