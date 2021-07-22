// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The `cxx` crate's expanded code appears to trigger this elided lifetimes in paths warning
#![allow(elided_lifetimes_in_paths)]
#![allow(dead_code)]

use cxx::UniquePtr;
use fuchsia_zircon_sys as sys;
use std::pin::Pin;

#[cxx::bridge(namespace = "crypto")]
mod ffi {
    // No shared structs

    unsafe extern "C++" {
        // C++ declarations
        include!("src/security/fcrypto-rust/ffi.h");

        type Cipher;

        fn new_cipher() -> UniquePtr<Cipher>;

        fn init_for_encipher(
            cipher: Pin<&mut Cipher>,
            secret: &[u8],
            iv: &[u8],
            alignment: u64,
        ) -> i32;
        fn init_for_decipher(
            cipher: Pin<&mut Cipher>,
            secret: &[u8],
            iv: &[u8],
            alignment: u64,
        ) -> i32;

        fn encipher(
            cipher: Pin<&mut Cipher>,
            plaintext: &[u8],
            offset: u64,
            ciphertext: &mut [u8],
        ) -> i32;
        fn decipher(
            cipher: Pin<&mut Cipher>,
            plaintext: &[u8],
            offset: u64,
            ciphertext: &mut [u8],
        ) -> i32;
    }
}

pub struct Aes256XtsCipher {
    encipherer: Aes256XtsEncipherer,
    decipherer: Aes256XtsDecipherer,
}

// TODO: make these failures return a better error type than i32
impl Aes256XtsCipher {
    pub fn new(secret: &[u8], iv: &[u8]) -> Result<Aes256XtsCipher, i32> {
        // Allocate a pair of C++ Cipher objects.
        let encipher_inner = ffi::new_cipher();
        let decipher_inner = ffi::new_cipher();
        let mut encipherer = Aes256XtsEncipherer { inner: encipher_inner };
        let mut decipherer = Aes256XtsDecipherer { inner: decipher_inner };

        encipherer.init(secret, iv)?;
        decipherer.init(secret, iv)?;

        Ok(Aes256XtsCipher { encipherer, decipherer })
    }

    pub fn encrypt(&mut self, offset: u64, plaintext: &[u8], ciphertext: &mut [u8]) -> Result<(), i32> {
        self.encipherer.encipher(offset, plaintext, ciphertext)
    }

    pub fn decrypt(&mut self, offset: u64, ciphertext: &[u8], plaintext: &mut [u8]) -> Result<(), i32> {
        self.decipherer.decipher(offset, ciphertext, plaintext)
    }
}

pub struct Aes256XtsEncipherer {
    inner: UniquePtr<ffi::Cipher>,
}

pub struct Aes256XtsDecipherer {
    inner: UniquePtr<ffi::Cipher>,
}

const ALIGN: u64 = 128;

impl Aes256XtsEncipherer {
    fn init(&mut self, secret: &[u8], iv: &[u8]) -> Result<(), i32> {
        let mut cref = self.inner.as_mut().ok_or(sys::ZX_ERR_INVALID_ARGS)?;
        let res = ffi::init_for_encipher(Pin::as_mut(&mut cref), secret, iv, ALIGN);
        if res == 0 {
            Ok(())
        } else {
            Err(res)
        }
    }

    fn encipher(&mut self, offset: u64, plaintext: &[u8], ciphertext: &mut [u8]) -> Result<(), i32> {
        assert!(plaintext.len() == ciphertext.len());
        let mut cref = self.inner.as_mut().ok_or(sys::ZX_ERR_INVALID_ARGS)?;
        let res = ffi::encipher(Pin::as_mut(&mut cref), plaintext, offset, ciphertext);
        if res == 0 {
            Ok(())
        } else {
            Err(res)
        }
    }
}

impl Aes256XtsDecipherer {
    fn init(&mut self, secret: &[u8], iv: &[u8]) -> Result<(), i32> {
        let mut cref = self.inner.as_mut().ok_or(sys::ZX_ERR_INVALID_ARGS)?;
        let res = ffi::init_for_decipher(Pin::as_mut(&mut cref), secret, iv, ALIGN);
        if res == 0 {
            Ok(())
        } else {
            Err(res)
        }
    }

    fn decipher(&mut self, offset: u64, ciphertext: &[u8], plaintext: &mut [u8]) -> Result<(), i32> {
        assert!(plaintext.len() == ciphertext.len());
        let mut cref = self.inner.as_mut().ok_or(sys::ZX_ERR_INVALID_ARGS)?;
        let res = ffi::decipher(Pin::as_mut(&mut cref), ciphertext, offset, plaintext);
        if res == 0 {
            Ok(())
        } else {
            Err(res)
        }
    }
}
