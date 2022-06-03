// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use fuchsia_bluetooth::types::Address;
use packet_encoding::pub_decodable_enum;
use std::convert::{TryFrom, TryInto};
use tracing::debug;

use crate::types::keys::public_key_from_bytes;
use crate::types::{AccountKey, Error};

/// Attempts to parse the provided key-based pairing `request`.
/// Returns the encrypted portion of the request and an optional Public Key on success.
pub fn parse_key_based_pairing_request(
    mut request: Vec<u8>,
) -> Result<([u8; 16], Option<p256::PublicKey>), Error> {
    // The first 16 bytes of the `request` contains the encrypted request. There can also
    // be an optional Public Key which is 64 bytes long. Therefore, the payload can either be
    // 16 or 80 bytes long.
    let public_key = match request.len() {
        80 => Some(public_key_from_bytes(request.split_off(16))?),
        16 => None,
        _ => return Err(Error::Packet),
    };
    let encrypted_request: [u8; 16] = request[..16].try_into().map_err(|_| Error::Packet)?;
    Ok((encrypted_request, public_key))
}

pub_decodable_enum! {
    /// The type of key-based pairing request.
    MessageType<u8, Error, Packet> {
        Pairing => 0x00,
        DeviceAction => 0x10,
    }
}

bitfield! {
    /// The flags associated with a key-based pairing request.
    /// Defined in Table 1.2.1 in
    /// https://developers.google.com/nearby/fast-pair/specifications/characteristics
    /// Note: `bitfields` uses the opposite bit endianness from Table 1.2.1 - e.g. Bit 0 in the
    /// table corresponds to Bit 7 in the `bitfields` declaration.
    struct KeyBasedPairingFlags(u8);
    impl Debug;
    // Bits 0-3 are reserved.
    pub bool, retroactive_write, _: 4;
    pub bool, notify_name, _: 5;
    pub bool, provider_initiates_bonding, _: 6;
    // Bit 7 (MSB) is deprecated and ignored.
}

/// Actions that the Provider should take after a GATT write to the key-based pairing
/// characteristic.
#[derive(Debug, PartialEq)]
pub enum KeyBasedPairingAction {
    SeekerInitiatesPairing,
    ProviderInitiatesPairing { seeker_address: Address },
    RetroactiveWrite { seeker_address: Address },
    // TODO(fxbug.dev/99734): Add Device Action requests.
}

/// A parsed and validated request.
#[derive(Debug, PartialEq)]
pub struct KeyBasedPairingRequest {
    pub action: KeyBasedPairingAction,
    _salt: Vec<u8>,
}

/// Attempts to decrypt, parse, and validate the provided `encrypted` key-based pairing request.
/// Returns the parsed request on success, Error otherwise.
pub fn decrypt_key_based_pairing_request(
    encrypted_request: &[u8; 16],
    key: &AccountKey,
    local_address: &Address,
) -> Result<KeyBasedPairingRequest, Error> {
    let request = key.decrypt(encrypted_request);
    let message_type = MessageType::try_from(request[0])?;
    match message_type {
        MessageType::Pairing => {
            let flags = KeyBasedPairingFlags(request[1]);
            debug!("Key-based pairing request: {:?}", flags);
            let mut received_local_address_bytes = [0; 6];
            received_local_address_bytes.copy_from_slice(&request[2..8]);

            if local_address.bytes() != &received_local_address_bytes {
                return Err(Error::internal("Invalid local address"));
            }

            if flags.provider_initiates_bonding() || flags.retroactive_write() {
                let mut seeker_address_bytes = [0; 6];
                seeker_address_bytes.copy_from_slice(&request[8..14]);
                let seeker_address = Address::Public(seeker_address_bytes);
                let action = if flags.provider_initiates_bonding() {
                    KeyBasedPairingAction::ProviderInitiatesPairing { seeker_address }
                } else {
                    KeyBasedPairingAction::RetroactiveWrite { seeker_address }
                };
                return Ok(KeyBasedPairingRequest { action, _salt: request[14..].to_vec() });
            }

            // Otherwise, this is a standard request to start key-based pairing.
            return Ok(KeyBasedPairingRequest {
                action: KeyBasedPairingAction::SeekerInitiatesPairing,
                _salt: request[8..].to_vec(),
            });
        }
        MessageType::DeviceAction => {
            debug!("Device Action request: Flags({})", request[1]);
            // TODO(fxbug.dev/99734): Support Device Action requests.
            return Err(Error::internal("Device Action requests not supported"));
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use crate::types::keys::tests::{bob_public_key_bytes, example_aes_key};
    use assert_matches::assert_matches;

    #[test]
    fn parse_empty_request() {
        let result = parse_key_based_pairing_request(vec![]);
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_request_too_small() {
        let result = parse_key_based_pairing_request(vec![1, 2, 3]);
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_request_invalid_size() {
        let result = parse_key_based_pairing_request(vec![
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        ]);
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_request_no_public_key() {
        let buf = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let (parsed, key) = parse_key_based_pairing_request(buf.clone()).expect("can parse");
        assert_eq!(parsed, &buf[..16]);
        assert_matches!(key, None);
    }

    #[test]
    fn parse_request_with_invalid_public_key() {
        // The public key point is valid in size but is not a point on the secp256r1 curve.
        let buf = vec![1u8; 80];
        let result = parse_key_based_pairing_request(buf.clone());
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[test]
    fn parse_request_with_valid_public_key() {
        let mut buf = vec![1u8; 80];
        buf[16..].copy_from_slice(&bob_public_key_bytes()[..]);
        let (parsed, key) = parse_key_based_pairing_request(buf.clone()).expect("can parse");
        assert_eq!(parsed, &buf[..16]);
        assert_matches!(key, Some(_));
    }

    #[test]
    fn decrypt_invalid_request() {
        let key = example_aes_key();
        // Invalidly formatted request.
        let random_request = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let encrypted_request = key.encrypt(&random_request);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key, &LOCAL_ADDRESS);
        assert_matches!(request, Err(Error::Packet));
    }

    #[test]
    fn decrypt_device_action_request_is_error() {
        let key = example_aes_key();
        // The request is formatted correctly, but Device Action is not supported.
        let device_action = [
            0x10, 0x11, // Device Action, Random flags
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Random Provider Address
            0x01, 0x01, 0x01, // Random Message Group, Code, Additional Flags
            0xaa, 0xbb, 0xcc, 0xdd, 0xee, // Salt
        ];
        let encrypted_request = key.encrypt(&device_action);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key, &LOCAL_ADDRESS);
        assert_matches!(request, Err(Error::InternalError(_)));
    }

    /// Example Key-based pairing request with a fixed address of 0x123456.
    pub(crate) const KEY_BASED_PAIRING_REQUEST: [u8; 16] = [
        0x00, 0x00, // Key Pairing request, Flags are empty
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Provider address
        0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd, // Salt
    ];

    const LOCAL_ADDRESS: Address = Address::Public([0x1, 0x2, 0x3, 0x4, 0x5, 0x6]);

    #[test]
    fn decrypt_request_invalid_address() {
        let key = example_aes_key();
        let random_address = Address::Public([0x1, 0x2, 0x1, 0x2, 0x1, 0x2]);
        // The address in `KEY_BASED_PAIRING_REQUEST` differs from `random_address`.
        let encrypted_request = key.encrypt(&KEY_BASED_PAIRING_REQUEST);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key, &random_address);
        assert_matches!(request, Err(Error::InternalError(_)));
    }

    #[test]
    fn decrypt_request_not_encrypted() {
        let request = decrypt_key_based_pairing_request(
            &KEY_BASED_PAIRING_REQUEST,
            &example_aes_key(),
            &LOCAL_ADDRESS,
        );
        assert_matches!(request, Err(_));
    }

    #[test]
    fn decrypt_seeker_initiates_pairing_request() {
        let key = example_aes_key();
        // The request is formatted and encrypted OK.
        let encrypted_request = key.encrypt(&KEY_BASED_PAIRING_REQUEST);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key, &LOCAL_ADDRESS)
            .expect("successful decryption");
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::SeekerInitiatesPairing,
            _salt: vec![0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd],
        };
        assert_eq!(request, expected_request);
    }

    #[test]
    fn decrypt_retroactive_pairing_request() {
        let key = example_aes_key();
        // The request is formatted and encrypted OK.
        let mut retroactive_pairing_request = KEY_BASED_PAIRING_REQUEST;
        retroactive_pairing_request[1] = 0x10; // Retroactive pairing flags
        let encrypted_request = key.encrypt(&retroactive_pairing_request);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key, &LOCAL_ADDRESS)
            .expect("successful decryption");
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::RetroactiveWrite {
                // Seeker address is the first 6 bytes of the salt in `KEY_BASED_PAIRING_REQUEST.
                seeker_address: Address::Public([0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc]),
            },
            _salt: vec![0xdd, 0xdd],
        };
        assert_eq!(request, expected_request);
    }

    #[test]
    fn decrypt_provider_initiates_pairing_request() {
        let key = example_aes_key();
        // The request is formatted and encrypted OK.
        let mut provider_pairing_request = KEY_BASED_PAIRING_REQUEST;
        provider_pairing_request[1] = 0x40; // Provider initiates pairing flags
        let encrypted_request = key.encrypt(&provider_pairing_request);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key, &LOCAL_ADDRESS)
            .expect("successful decryption");
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::ProviderInitiatesPairing {
                // Seeker address is the first 6 bytes of the salt in `KEY_BASED_PAIRING_REQUEST.
                seeker_address: Address::Public([0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc]),
            },
            _salt: vec![0xdd, 0xdd],
        };
        assert_eq!(request, expected_request);
    }
}
