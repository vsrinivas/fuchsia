// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use elliptic_curve::sec1::FromEncodedPoint;
use p256::{EncodedPoint, FieldBytes, PublicKey, SecretKey};
use sha2::{Digest, Sha256};

use crate::types::{Error, SharedSecret};

// This private key is defined in the GFPS specification testcases and is used in component unit
// tests.
// https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#ecdh_key_exchange
#[cfg(test)]
pub(crate) const LOCAL_PRIVATE_KEY_BYTES: [u8; 32] = [
    0xD7, 0x5E, 0x54, 0xC7, 0x7D, 0x76, 0x24, 0x89, 0xE5, 0x7C, 0xFA, 0x92, 0x37, 0x43, 0xF1, 0x67,
    0x77, 0xA4, 0x28, 0x3D, 0x99, 0x80, 0x0B, 0xAC, 0x55, 0x58, 0x48, 0x38, 0x93, 0xE5, 0xB0, 0x6D,
];

/// Returns a PublicKey representing a point on the secp256r1 elliptic curve.
///
/// There is an implicit assumption that the key is encoded as an (x,y) point where the first 32
/// bytes is the X coordinate, and the latter 32 bytes is the Y coordinate.
pub fn public_key_from_bytes(bytes: Vec<u8>) -> Result<PublicKey, Error> {
    if bytes.len() != 64 {
        return Err(Error::internal("Invalid public key length"));
    }

    let x = FieldBytes::clone_from_slice(&bytes[..32]);
    let y = FieldBytes::clone_from_slice(&bytes[32..64]);

    let remote_public_point = EncodedPoint::from_affine_coordinates(&x, &y, false);
    let key: Option<PublicKey> = PublicKey::from_encoded_point(&remote_public_point).into();
    key.ok_or(Error::internal("Invalid remote public key point"))
}

/// Returns a SecretKey representing an integer [1, n-1] on the secp256r1 elliptic curve.
pub fn private_key_from_bytes(bytes: Vec<u8>) -> Result<SecretKey, Error> {
    SecretKey::from_be_bytes(&bytes[..])
        .map_err(|e| Error::internal(&format!("Invalid private key: {:?}", e)))
}

/// Generates an AES encryption key from the local Anti-Spoofing Private key and remote Public key.
///
/// `local_secret_key` is the 32-byte Anti-Spoofing Private Key owned by the local device.
/// `remote_public_key` is the 64-byte Public Key owned by the remote device. This key is a point on
/// the secp256r1 elliptic curve.
///
/// Returns the calculated Account Key on success, or Error otherwise.
///
/// Note: While the `elliptic_curve` crate recommends "[w]henever possible, [use] the high-level
/// ECDH ephemeral API provided by EphemeralSecret" instead of the lower-level `diffie_hellman`
/// function, it makes an exception for "implementing a protocol which requires a static scalar
/// value as part of an ECDH exchange". While the remote public key from the Seeker will vary, the
/// `local_secret_key` will always be the FastPair Anti-Spoofing Private key assigned at model
/// registration.
pub fn aes_from_anti_spoofing_and_public(
    local_secret_key: &SecretKey,
    remote_public_key: &PublicKey,
) -> Result<SharedSecret, Error> {
    // The shared secret is computed via the DH algorithm. This uses the local Secret Key and the
    // remote Public key.
    let shared_secret = elliptic_curve::ecdh::diffie_hellman(
        local_secret_key.to_nonzero_scalar(),
        remote_public_key.as_affine(),
    );

    // The first 16 bytes of the 32-byte hashed key is the Anti-Spoofing AES key.
    let mut hasher = Sha256::new();
    hasher.update(shared_secret.as_bytes());
    let calculated_hash: [u8; 32] = hasher.finalize().into();

    let mut anti_spoofing_key: [u8; 16] = [0; 16];
    anti_spoofing_key.copy_from_slice(&calculated_hash[..16]);

    Ok(SharedSecret::new(anti_spoofing_key))
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use assert_matches::assert_matches;

    /// Example Public key defined in the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#ecdh_key_exchange
    pub(crate) fn bob_public_key_bytes() -> Vec<u8> {
        vec![
            0xF7, 0xD4, 0x96, 0xA6, 0x2E, 0xCA, 0x41, 0x63, 0x51, 0x54, 0x0A, 0xA3, 0x43, 0xBC,
            0x69, 0x0A, 0x61, 0x09, 0xF5, 0x51, 0x50, 0x06, 0x66, 0xB8, 0x3B, 0x12, 0x51, 0xFB,
            0x84, 0xFA, 0x28, 0x60, 0x79, 0x5E, 0xBD, 0x63, 0xD3, 0xB8, 0x83, 0x6F, 0x44, 0xA9,
            0xA3, 0xE2, 0x8B, 0xB3, 0x40, 0x17, 0xE0, 0x15, 0xF5, 0x97, 0x93, 0x05, 0xD8, 0x49,
            0xFD, 0xF8, 0xDE, 0x10, 0x12, 0x3B, 0x61, 0xD2,
        ]
    }

    /// Expected AES secret key defined in the GFPS specification. This key is the result of
    /// applying ECDH to a Public and Private key.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#ecdh_key_exchange
    pub(crate) fn example_aes_key() -> SharedSecret {
        SharedSecret::new([
            0xB0, 0x7F, 0x1F, 0x17, 0xC2, 0x36, 0xCB, 0xD3, 0x35, 0x23, 0xC5, 0x15, 0xF3, 0x50,
            0xAE, 0x57,
        ])
    }

    /// Encrypts the provided `message` with the example AES key that is defined in the GFPS spec.
    pub(crate) fn encrypt_message(message: &[u8; 16]) -> Vec<u8> {
        example_aes_key().encrypt(message).to_vec()
    }

    /// Encrypts the provided `message` with the example AES key. Returns the buffer and the example
    /// public key defined in the GFPS.
    pub(crate) fn encrypt_message_include_public_key(message: &[u8; 16]) -> Vec<u8> {
        let mut encrypted_message = example_aes_key().encrypt(message).to_vec();
        encrypted_message.append(&mut bob_public_key_bytes());
        encrypted_message
    }

    /// This test verifies the ECDH key exchange algorithm.
    /// The contents of this test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#ecdh_key_exchange
    #[fuchsia::test]
    fn ecdh_key_exchange() {
        let remote_public_key =
            public_key_from_bytes(bob_public_key_bytes()).expect("valid public key");

        let local_private_key =
            private_key_from_bytes(LOCAL_PRIVATE_KEY_BYTES.to_vec()).expect("valid private key");

        let shared_secret = elliptic_curve::ecdh::diffie_hellman(
            local_private_key.to_nonzero_scalar(),
            remote_public_key.as_affine(),
        );
        let expected_shared_secret = [
            0x9D, 0xAD, 0xE4, 0xF8, 0x6A, 0xC3, 0x48, 0x8B, 0xBA, 0xC2, 0xAC, 0x34, 0xB5, 0xFE,
            0x68, 0xA0, 0xEE, 0x5A, 0x67, 0x06, 0xF5, 0x43, 0xD9, 0x06, 0x1A, 0xD5, 0x78, 0x89,
            0x49, 0x8A, 0xE6, 0xBA,
        ];
        assert_eq!(shared_secret.as_bytes()[..], expected_shared_secret[..]);
    }

    /// This test verifies the calculation of the Anti-Spoofing AES key.
    /// The contents of this test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#aes_key_from_ecdh_shared_secret
    #[fuchsia::test]
    fn aes_key_generation_from_public_private_keys() {
        let remote_public_key =
            public_key_from_bytes(bob_public_key_bytes()).expect("valid public key");

        let local_private_key =
            private_key_from_bytes(LOCAL_PRIVATE_KEY_BYTES.to_vec()).expect("valid private key");

        let calculated_aes_key =
            aes_from_anti_spoofing_and_public(&local_private_key, &remote_public_key)
                .expect("valid calculation");

        assert_eq!(calculated_aes_key, example_aes_key());
    }

    #[fuchsia::test]
    fn invalid_private_key() {
        let result = private_key_from_bytes(vec![]);
        assert_matches!(result, Err(Error::InternalError(_)));

        let result = private_key_from_bytes(vec![1; 40]);
        assert_matches!(result, Err(Error::InternalError(_)));

        // Correct size, but the key must be a natural number.
        let result = private_key_from_bytes(vec![0; 32]);
        assert_matches!(result, Err(Error::InternalError(_)));

        // Correct size, but the value is too large. The private key value is bounded by the order
        // of the elliptic curve (number of all its points).
        let result = private_key_from_bytes(vec![0xff; 32]);
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    fn invalid_public_key() {
        let result = public_key_from_bytes(vec![]);
        assert_matches!(result, Err(Error::InternalError(_)));

        let result = public_key_from_bytes([1; 65].to_vec());
        assert_matches!(result, Err(Error::InternalError(_)));

        // Correct size, but the point must be nonzero.
        let result = public_key_from_bytes([0; 64].to_vec());
        assert_matches!(result, Err(Error::InternalError(_)));

        // Correct size, but this random point is not on the secp256r1 curve.
        let mut test_x = vec![7; 32];
        let mut test_y = vec![4; 32];
        test_x.append(&mut test_y);
        let result = public_key_from_bytes(test_x);
        assert_matches!(result, Err(Error::InternalError(_)));
    }
}
