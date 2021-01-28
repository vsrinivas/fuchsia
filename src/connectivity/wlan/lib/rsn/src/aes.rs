// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AES cryptography.
//!
//! This module exposes AES primitives provided by `boringssl-sys`. Namely, RSN requires the AES
//! cipher for protocols employing RFC 3394 AES key wrapping and RFC 4493 AES-CMAC.
//!
//! # Security
//!
//! **RSN uses insecure AES primitives. This module exposes insecure primitives to implement legacy
//! protocols that are insecure. Do not use these primitives outside of this crate or context.**
//! While the AES cipher is considered secure, some protocols that employ the cipher are insecure.
//!
//! In general, avoid using the AES cipher directly. Prefer AE or AEAD modes like AES-GCM so that
//! ciphertext can be authenticated. Without authenticated ciphertext, it is possible to introduce
//! vulnerabilities when using AES.

// NOTE: At the time of commit, some APIs in this module remain unused. However, some of these
//       APIs remain to inform subsequent usage and changes and to avoid mistakes. This module is
//       treated as if it were exported, though it is not and must not be.
#![allow(dead_code)]

use {
    boringssl_sys::{
        AES_set_decrypt_key, AES_set_encrypt_key, AES_unwrap_key, AES_wrap_key, AES_CMAC, AES_KEY,
    },
    std::{convert::TryInto, ptr},
    thiserror::Error,
};

/// Size of the RFC 4493 AES-CMAC output MAC in bytes.
pub const CMAC_LEN: usize = 16;
/// Size of the RFC 3394 AES key wrapping IV in bytes.
pub const KEY_WRAP_IV_LEN: usize = 8;

/// Size of the RFC 3394 AES key wrapping block in bytes.
const KEY_WRAP_BLOCK_LEN: usize = 8;

/// Errors concerning AES primitives.
///
/// This is the top-level error exposed by this module and its APIs.
#[derive(Clone, Debug, Error)]
#[non_exhaustive]
pub enum AesError {
    // TODO(68448): Use a type that makes it clear in code and text that this size is expressed in
    //              bytes.
    #[error("invalid AES key size: {}", _0)]
    KeySize(usize),
    #[error("RFC 3394 AES key wrap failed: {}", _0)]
    KeyWrap(KeyWrapError),
}

impl From<KeyWrapError> for AesError {
    fn from(error: KeyWrapError) -> Self {
        AesError::KeyWrap(error)
    }
}

/// Errors concerning RFC 3394 AES key wrapping.
///
/// These errors are typically exposed via `AesError`.
#[derive(Clone, Debug, Error)]
#[non_exhaustive]
pub enum KeyWrapError {
    /// The AES key used to unwrap does not match the key used to wrap.
    #[error("incorrect AES key")]
    UnwrapKey,
    /// The input buffer is an invalid size.
    ///
    /// The input buffer is expected to contain either a wrapped or unwrapped key depending on the
    /// operation.
    #[error("invalid input buffer size")]
    InputSize,
    /// The output buffer is an invalid size.
    ///
    /// Depending on the operation, either a wrapped or unwrapped key is written to the output
    /// buffer. The output buffer's minimum size depends on the operation and corresponding input
    /// buffer size.
    #[error("invalid output buffer size")]
    OutputSize,
}

/// AES cipher.
struct AesKey {
    inner: AES_KEY,
}

impl AesKey {
    /// Constructs an `AES_KEY` configured for encryption with the given key.
    ///
    /// **The `AES_KEY` must only be used with encryption functions unless it is reconfigured. See
    /// the `aes_set_decrypt_key` and `aes_set_encrypt_key` functions.
    fn with_encrypt_key(key: &SizedKey) -> Self {
        let mut aes = AesKey::zeroed();
        // `SizedKey` only provides supported key sizes, so this should never panic.
        aes.aes_set_encrypt_key(key.as_ref()).unwrap();
        aes
    }

    /// Constructs an `AES_KEY` configured for decryption with the given key.
    ///
    /// **The `AES_KEY` must only be used with decryption functions unless it is reconfigured. See
    /// the `aes_set_decrypt_key` and `aes_set_encrypt_key` functions.
    fn with_decrypt_key(key: &SizedKey) -> Self {
        let mut aes = AesKey::zeroed();
        // `SizedKey` only provides supported key sizes, so this should never panic.
        aes.aes_set_decrypt_key(key.as_ref()).unwrap();
        aes
    }

    /// Constructs a zeroed `AES_KEY`.
    ///
    /// **Care must be taken to configure the `AES_KEY` for use with encryption or decryption
    /// functions before use.** See the `aes_set_decrypt_key` and `aes_set_encrypt_key` functions.
    fn zeroed() -> Self {
        AesKey { inner: AES_KEY { rd_key: [0u32; 60], rounds: 0 } }
    }

    fn as_ptr(&self) -> *const AES_KEY {
        &self.inner as *const AES_KEY
    }
}

/// `AES_KEY` FFI functions.
///
/// These functions interact directly with `boringssl_sys` items concerning `AES_KEY`. They provide
/// a minimal wrapper, but must still be used with care and should never be exported outside of
/// this module. Wrappers primarily expose more idiomatic parameter and output types and marshal
/// errors into `Result`s. Client code is responsible for maintaining invariants.
impl AesKey {
    /// The `AES_set_encrypt_key` function from BoringSSL.
    ///
    /// This is a safe wrapper.
    fn aes_set_encrypt_key(&mut self, key: &[u8]) -> Result<(), AesError> {
        let n = key.len();
        // This is safe, because the length `n` of `key` is managed by the compiler.
        unsafe {
            if 0 > AES_set_encrypt_key(key.as_ptr(), (n * 8) as u32, &mut self.inner) {
                Err(AesError::KeySize(n))
            } else {
                Ok(())
            }
        }
    }

    /// The `AES_set_decrypt_key` function from BoringSSL.
    ///
    /// This is a safe wrapper.
    fn aes_set_decrypt_key(&mut self, key: &[u8]) -> Result<(), AesError> {
        let n = key.len();
        // This is safe, because the length `n` of `key` is managed by the compiler.
        unsafe {
            if 0 > AES_set_decrypt_key(key.as_ptr(), (n * 8) as u32, &mut self.inner) {
                Err(AesError::KeySize(n))
            } else {
                Ok(())
            }
        }
    }

    /// The `AES_wrap_key` function from BoringSSL.
    ///
    /// If `iv` is `None`, then the default IV is used. Client code must maintain important
    /// invariants. See `aes::wrap_key`.
    ///
    /// # Errors
    ///
    /// `AES_wrap_key` does not distinguish between different error conditions, so this function
    /// does not provide detailed error information. Client code must derive more detail from
    /// context.
    ///
    /// # Safety
    ///
    /// The length of the `output` slice must be at least eight bytes (the block size) more than
    /// the length of the `input` slice. That is, if the length of the `input` slice is `i` bytes,
    /// then the `output` slice must be at least `i + 8` bytes long. **If `output` is too short,
    /// then this function will write beyond its bounds.**
    unsafe fn aes_wrap_key(
        &self,
        iv: Option<&[u8; KEY_WRAP_IV_LEN]>,
        output: &mut [u8],
        input: &[u8],
    ) -> Result<i32, ()> {
        let iv = if let Some(iv) = iv { iv as *const _ } else { ptr::null() };
        match AES_wrap_key(
            self.as_ptr(),
            iv,
            output.as_mut_ptr(), // Must have sufficient capacity.
            input.as_ptr(),
            input.len().try_into().expect("buffer length overflow"),
        ) {
            -1 => Err(()),
            n => Ok(n),
        }
    }

    /// The `AES_unwrap_key` function from BoringSSL.
    ///
    /// If `iv` is `None`, then the default IV is used. Client code must maintain important
    /// invariants. See `aes::unwrap_key`.
    ///
    /// # Errors
    ///
    /// `AES_unwrap_key` does not distinguish between different error conditions, so this function
    /// does not provide detailed error information. Client code must derive more detail from
    /// context.
    ///
    /// # Safety
    ///
    /// The length of the `output` slice must be longer than eight bytes (the block size) less than
    /// the length of the `input` slice. That is, if the length of the `input` slice is `i` bytes,
    /// then the `output` slice must be at least `i - 8` bytes long. **If `output` is too short,
    /// then this function will write beyond its bounds.**
    unsafe fn aes_unwrap_key(
        &self,
        iv: Option<&[u8; KEY_WRAP_IV_LEN]>,
        output: &mut [u8],
        input: &[u8],
    ) -> Result<i32, ()> {
        let iv = if let Some(iv) = iv { iv as *const _ } else { ptr::null() };
        match AES_unwrap_key(
            self.as_ptr(),
            iv,
            output.as_mut_ptr(), // Must have sufficient capacity.
            input.as_ptr(),
            input.len().try_into().expect("buffer length overflow"),
        ) {
            -1 => Err(()),
            n => Ok(n),
        }
    }
}

// TODO: This could use `Cow<[u8]>` to avoid copying key data. To prevent arbitrary mutations, an
//       additional opaque type must be introduced.
/// Sized AES key data.
///
/// AES keys must be 128, 192, or 256 bits (16, 24, or 32 bytes) in length.
/// `SizedKey` provides variants with a fixed-sized buffer for each key length.
/// Keys can be constructed from slices and arrays of a suitable length or size.
///
/// Note that slices and arrays specify their size in bytes, not bits. For
/// example, for a 128-bit key, a 16-byte slice or array is required.
pub enum SizedKey {
    /// 128-bit AES key.
    Key128Bit([u8; 16]),
    /// 192-bit AES key.
    Key192Bit([u8; 24]),
    /// 256-bit AES key.
    Key256Bit([u8; 32]),
}

impl SizedKey {
    /// Constructs an AES key from an appropriately sized byte slice.
    pub fn try_from_slice(key: &[u8]) -> Result<Self, AesError> {
        match key.len() {
            16 => {
                let mut key128 = [0; 16];
                key128[..].copy_from_slice(key);
                Ok(SizedKey::Key128Bit(key128))
            }
            24 => {
                let mut key192 = [0; 24];
                key192[..].copy_from_slice(key);
                Ok(SizedKey::Key192Bit(key192))
            }
            32 => {
                let mut key256 = [0; 32];
                key256[..].copy_from_slice(key);
                Ok(SizedKey::Key256Bit(key256))
            }
            n => Err(AesError::KeySize(n)),
        }
    }

    /// Gets the length of the key in **bytes**.
    pub fn byte_len(&self) -> usize {
        match self {
            SizedKey::Key128Bit(..) => 16,
            SizedKey::Key192Bit(..) => 24,
            SizedKey::Key256Bit(..) => 32,
        }
    }

    /// Gets the length of the key in **bits**.
    pub fn bit_len(&self) -> usize {
        match self {
            SizedKey::Key128Bit(..) => 128,
            SizedKey::Key192Bit(..) => 192,
            SizedKey::Key256Bit(..) => 256,
        }
    }
}

impl AsMut<[u8]> for SizedKey {
    fn as_mut(&mut self) -> &mut [u8] {
        match self {
            SizedKey::Key128Bit(ref mut key) => key.as_mut(),
            SizedKey::Key192Bit(ref mut key) => key.as_mut(),
            SizedKey::Key256Bit(ref mut key) => key.as_mut(),
        }
    }
}

impl AsRef<[u8]> for SizedKey {
    fn as_ref(&self) -> &[u8] {
        match self {
            SizedKey::Key128Bit(ref key) => key.as_ref(),
            SizedKey::Key192Bit(ref key) => key.as_ref(),
            SizedKey::Key256Bit(ref key) => key.as_ref(),
        }
    }
}

impl From<[u8; 16]> for SizedKey {
    fn from(key128: [u8; 16]) -> Self {
        SizedKey::Key128Bit(key128)
    }
}

impl From<[u8; 24]> for SizedKey {
    fn from(key192: [u8; 24]) -> Self {
        SizedKey::Key192Bit(key192)
    }
}

impl From<[u8; 32]> for SizedKey {
    fn from(key256: [u8; 32]) -> Self {
        SizedKey::Key256Bit(key256)
    }
}

/// I/O buffers that have interdependent requirements, such as length.
struct BufferIo<I, O>
where
    I: AsRef<[u8]>,
    O: AsMut<[u8]>,
{
    input: I,
    output: O,
}

/// RFC 3394 key wrapping I/O.
///
/// This type exposes buffers for key wrapping and ensures that those buffers are properly sized
/// before use.
pub struct KeyWrapIo<I, O>(BufferIo<I, O>)
where
    I: AsRef<[u8]>,
    O: AsMut<[u8]>;

impl<I, O> KeyWrapIo<I, O>
where
    I: AsRef<[u8]>,
    O: AsMut<[u8]>,
{
    /// Constructs key wrapping I/O from existing input and output buffers.
    pub fn try_from_io(input: I, mut output: O) -> Result<Self, AesError> {
        // See RFC 3394, 2.2.1. Section 2 requires at least two blocks of plaintext.
        let n = input.as_ref().len();
        let m = output.as_mut().len();
        match (
            n >= (KEY_WRAP_BLOCK_LEN * 2) && n % KEY_WRAP_BLOCK_LEN == 0,
            m >= (n + KEY_WRAP_BLOCK_LEN),
        ) {
            (true, true) => Ok(KeyWrapIo(BufferIo { input, output })),
            // Report input buffer size errors first.
            (false, _) => Err(KeyWrapError::InputSize.into()),
            (_, false) => Err(KeyWrapError::OutputSize.into()),
        }
    }

    /// Gets the input and output buffers.
    pub fn io(&mut self) -> (&[u8], &mut [u8]) {
        (self.0.input.as_ref(), self.0.output.as_mut())
    }

    /// Gets the input buffer.
    pub fn input(&self) -> &[u8] {
        self.0.input.as_ref()
    }

    /// Gets the output buffer.
    pub fn output(&mut self) -> &mut [u8] {
        self.0.output.as_mut()
    }

    /// Consumes the key wrapping I/O and releases its output buffer.
    pub fn into_output(self) -> O {
        self.0.output
    }
}

impl<I> KeyWrapIo<I, Vec<u8>>
where
    I: AsRef<[u8]>,
{
    /// Constructs key wrapping I/O from an existing input buffer.
    ///
    /// A sufficiently sized output buffer is allocated.
    pub fn try_from_input(input: I) -> Result<Self, AesError> {
        let n = input.as_ref().len();
        KeyWrapIo::try_from_io(input, vec![0; n + KEY_WRAP_BLOCK_LEN])
    }
}

/// RFC 3394 key unwrapping I/O.
///
/// This type exposes buffers for key unwrapping and ensures that those buffers are properly sized
/// before use.
pub struct KeyUnwrapIo<I, O>(BufferIo<I, O>)
where
    I: AsRef<[u8]>,
    O: AsMut<[u8]>;

impl<I, O> KeyUnwrapIo<I, O>
where
    I: AsRef<[u8]>,
    O: AsMut<[u8]>,
{
    /// Constructs key unwrapping I/O from existing input and output buffers.
    pub fn try_from_io(input: I, mut output: O) -> Result<Self, AesError> {
        // See RFC 3394, 2.2.2. Section 2 requires at least two blocks of plaintext, so there must
        // be at least three blocks of ciphertext.
        let n = input.as_ref().len();
        let m = output.as_mut().len();
        match (
            n >= (KEY_WRAP_BLOCK_LEN * 3) && n % KEY_WRAP_BLOCK_LEN == 0,
            m >= (n - KEY_WRAP_BLOCK_LEN),
        ) {
            (true, true) => Ok(KeyUnwrapIo(BufferIo { input, output })),
            // Report input buffer size errors first.
            (false, _) => Err(KeyWrapError::InputSize.into()),
            (_, false) => Err(KeyWrapError::OutputSize.into()),
        }
    }

    /// Gets the input and output buffers.
    pub fn io(&mut self) -> (&[u8], &mut [u8]) {
        (self.0.input.as_ref(), self.0.output.as_mut())
    }

    /// Gets the input buffer.
    pub fn input(&self) -> &[u8] {
        self.0.input.as_ref()
    }

    /// Gets the output buffer.
    pub fn output(&mut self) -> &mut [u8] {
        self.0.output.as_mut()
    }

    /// Consumes the key unwrapping I/O and releases its output buffer.
    pub fn into_output(self) -> O {
        self.0.output
    }
}

impl<I> KeyUnwrapIo<I, Vec<u8>>
where
    I: AsRef<[u8]>,
{
    /// Constructs key unwrapping I/O from an existing input buffer.
    ///
    /// A sufficiently sized output buffer is allocated.
    pub fn try_from_input(input: I) -> Result<Self, AesError> {
        let n = input.as_ref().len();
        if n < (KEY_WRAP_BLOCK_LEN * 3) {
            Err(KeyWrapError::InputSize.into()) // Avoid underflow when creating the output buffer.
        } else {
            KeyUnwrapIo::try_from_io(input, vec![0; n - KEY_WRAP_BLOCK_LEN])
        }
    }
}

/// RFC 4493 AES-CMAC.
pub fn cmac(key: &SizedKey, message: &[u8]) -> Result<[u8; CMAC_LEN], AesError> {
    // Keys must be 128 or 256 bits in length.
    let n = match key.byte_len() {
        16 => Ok(16),
        32 => Ok(32),
        n => Err(AesError::KeySize(n)),
    }?;
    let mut mac = [0u8; CMAC_LEN];
    unsafe {
        if 0 == AES_CMAC(
            mac.as_mut().as_mut_ptr(),
            key.as_ref().as_ptr(),
            n.try_into().expect("key length overflow"),
            message.as_ptr(),
            message.len().try_into().expect("message length overflow"),
        ) {
            // This should never occur; the key and MAC are prepared before `AES_CMAC` is used.
            // However, to remain robust and avoid incorrectness or UB, this branch panics.
            panic!("AES-CMAC failed: invalid parameters")
        } else {
            Ok(mac)
        }
    }
}

/// RFC 3394 AES key wrapping.
pub fn wrap_key<I, O>(
    key: &SizedKey,
    iv: Option<&[u8; KEY_WRAP_IV_LEN]>,
    mut kio: KeyWrapIo<I, O>,
) -> Result<O, AesError>
where
    I: AsRef<[u8]>,
    O: AsMut<[u8]>,
{
    let aes = AesKey::with_encrypt_key(key);
    let (input, output) = kio.io();
    // This is safe, because `KeyWrapIo` always exposes properly sized buffers.
    unsafe {
        // Errors should never occur here; the key and buffers are prepared beforehand.
        aes.aes_wrap_key(iv, output, input).expect("AES key wrap failed: invalid parameters");
    }
    Ok(kio.into_output())
}

/// RFC 3394 AES key unwrapping.
pub fn unwrap_key<I, O>(
    key: &SizedKey,
    iv: Option<&[u8; KEY_WRAP_IV_LEN]>,
    mut kio: KeyUnwrapIo<I, O>,
) -> Result<O, AesError>
where
    I: AsRef<[u8]>,
    O: AsMut<[u8]>,
{
    let aes = AesKey::with_decrypt_key(key);
    let (input, output) = kio.io();
    // This is safe, because `KeyUnwrapIo` always exposes properly sized buffers.
    unsafe {
        // The only error that can occur here is an unmatched AES key.
        aes.aes_unwrap_key(iv, output, input).map_err(|_| KeyWrapError::UnwrapKey)?;
    }
    Ok(kio.into_output())
}
