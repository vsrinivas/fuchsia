// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use fuchsia_bluetooth::types::Address;
use packet_encoding::decodable_enum;
use std::convert::{TryFrom, TryInto};
use tracing::debug;

use crate::types::keys::public_key_from_bytes;
use crate::types::{AccountKey, Error, SharedSecret};

/// Default size of an encrypted/decrypted GATT request buffer.
const GATT_REQUEST_BUFFER_SIZE: usize = 16;

/// Default size of an encrypted/decrypted GATT response buffer.
const GATT_RESPONSE_BUFFER_SIZE: usize = 16;

/// Attempts to parse the provided key-based pairing `request`.
/// Returns the encrypted portion of the request and an optional Public Key on success.
pub fn parse_key_based_pairing_request(
    mut request: Vec<u8>,
) -> Result<([u8; GATT_REQUEST_BUFFER_SIZE], Option<p256::PublicKey>), Error> {
    // The first 16 bytes of the `request` contains the encrypted request. There can also
    // be an optional Public Key which is 64 bytes long. Therefore, the payload can either be
    // 16 or 80 bytes long.
    let public_key = match request.len() {
        80 => Some(public_key_from_bytes(request.split_off(16))?),
        16 => None,
        _ => return Err(Error::Packet),
    };
    let encrypted_request: [u8; GATT_REQUEST_BUFFER_SIZE] =
        request[..GATT_REQUEST_BUFFER_SIZE].try_into().map_err(|_| Error::Packet)?;
    Ok((encrypted_request, public_key))
}

decodable_enum! {
    /// The type of key-based pairing request.
    pub enum MessageType<u8, Error, Packet> {
        Pairing = 0x00,
        DeviceAction = 0x10,
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
    /// Seeker has requested to start Fast Pair. It will initiate the pairing flow. The
    /// `received_provider_address` can either be the Public or Random address.
    SeekerInitiatesPairing { received_provider_address: [u8; 6] },
    /// Seeker has requested to start Fast Pair. The Provider will initiate the pairing flow.
    ProviderInitiatesPairing { seeker_address: Address },
    /// Seeker has already paired to the Provider device and wants to retroactively save a Fast Pair
    /// Account Key for this Provider device.
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
    encrypted_request: &[u8; GATT_REQUEST_BUFFER_SIZE],
    key: &SharedSecret,
) -> Result<KeyBasedPairingRequest, Error> {
    let request = key.decrypt(encrypted_request);
    let message_type = MessageType::try_from(request[0])?;
    match message_type {
        MessageType::Pairing => {
            let flags = KeyBasedPairingFlags(request[1]);
            debug!(?flags, "Key-based pairing request");
            let mut received_provider_address = [0; 6];
            received_provider_address.copy_from_slice(&request[2..8]);
            // The received Provider address can be either the Public or Random address. It is
            // received in Big Endian. All BT addresses saved in the Sapphire stack are in Little
            // Endian.
            received_provider_address.reverse();

            if flags.provider_initiates_bonding() || flags.retroactive_write() {
                let mut seeker_address_bytes = [0; 6];
                seeker_address_bytes.copy_from_slice(&request[8..14]);
                // Received in Big Endian. All BT addresses saved in the Sapphire stack are in
                // Little Endian.
                seeker_address_bytes.reverse();
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
                action: KeyBasedPairingAction::SeekerInitiatesPairing { received_provider_address },
                _salt: request[8..].to_vec(),
            });
        }
        MessageType::DeviceAction => {
            let raw_flags = request[1];
            debug!(?raw_flags, "Device Action request");
            // TODO(fxbug.dev/99734): Support Device Action requests.
            return Err(Error::internal("Device Action requests not supported"));
        }
    }
}

decodable_enum! {
    pub enum PasskeyType<u8, Error, Packet> {
        Seeker = 0x02,
        Provider = 0x03,
    }
}

/// Attempts to decrypt and parse the provided `encrypted_request`.
/// Returns the integer passkey on success, Error otherwise.
pub fn decrypt_passkey_request(
    encrypted_request: Vec<u8>,
    key: &SharedSecret,
) -> Result<u32, Error> {
    if encrypted_request.len() != GATT_REQUEST_BUFFER_SIZE {
        return Err(Error::Packet);
    }

    let request = encrypted_request.try_into().map_err(|_| Error::Packet)?;
    let decrypted = key.decrypt(&request);

    let passkey_type = PasskeyType::try_from(decrypted[0])?;
    match passkey_type {
        PasskeyType::Seeker => {
            let mut passkey_bytes = [0; 4];
            passkey_bytes[1..4].copy_from_slice(&decrypted[1..4]);
            Ok(u32::from_be_bytes(passkey_bytes))
        }
        PasskeyType::Provider => Err(Error::internal("Unexpected Provider passkey")),
    }
}

pub fn decrypt_account_key_request(
    encrypted_request: Vec<u8>,
    key: &SharedSecret,
) -> Result<AccountKey, Error> {
    if encrypted_request.len() != GATT_REQUEST_BUFFER_SIZE {
        return Err(Error::Packet);
    }

    let request = encrypted_request.try_into().map_err(|_| Error::Packet)?;
    let decrypted = key.decrypt(&request);
    if decrypted[0] != 0x04 {
        return Err(Error::Packet);
    }

    Ok(AccountKey::new(decrypted))
}

/// Builds and returns an encrypted response to a Key-based Pairing write request. The response is
/// encrypted using the provided Account `key`.
/// Defined in Table 1.3 in the GFPS.
pub fn key_based_pairing_response(key: &SharedSecret, local_address: Address) -> Vec<u8> {
    let mut response = [0; GATT_RESPONSE_BUFFER_SIZE];
    // First byte indicates key-based pairing response.
    response[0] = 0x01;
    // Next 6 bytes is the local BR/EDR address in Big Endian.
    let mut local_address_bytes = local_address.bytes().clone();
    local_address_bytes.reverse();
    response[1..7].copy_from_slice(&local_address_bytes);
    // Final 9 bytes is a randomly generated salt value.
    fuchsia_zircon::cprng_draw(&mut response[7..16]);
    key.encrypt(&response).to_vec()
}

/// Builds and returns an encrypted response to a Passkey write request. The response is encrypted
/// using the provided Account `key`.
/// Defined in Table 2.2 in the GFPS.
pub fn passkey_response(key: &SharedSecret, passkey: u32) -> Vec<u8> {
    let mut response = [0; GATT_RESPONSE_BUFFER_SIZE];
    // First byte indicates Provider passkey.
    response[0] = u8::from(&PasskeyType::Provider);
    // Next 3 bytes is the passkey in Big Endian.
    response[1..4].copy_from_slice(&passkey.to_be_bytes()[1..4]);
    // Final 12 bytes is a randomly generated salt value.
    fuchsia_zircon::cprng_draw(&mut response[4..16]);
    key.encrypt(&response).to_vec()
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use crate::types::keys::tests::{bob_public_key_bytes, encrypt_message, example_aes_key};
    use assert_matches::assert_matches;

    #[test]
    fn parse_empty_key_pairing_request() {
        let result = parse_key_based_pairing_request(vec![]);
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_key_pairing_request_too_small() {
        let result = parse_key_based_pairing_request(vec![1, 2, 3]);
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_key_pairing_request_invalid_size() {
        let result = parse_key_based_pairing_request(vec![
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        ]);
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_key_pairing_request_no_public_key() {
        let buf = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let (parsed, key) = parse_key_based_pairing_request(buf.clone()).expect("can parse");
        assert_eq!(parsed, &buf[..16]);
        assert_matches!(key, None);
    }

    #[test]
    fn parse_key_pairing_request_with_invalid_public_key() {
        // The public key point is valid in size but is not a point on the secp256r1 curve.
        let buf = vec![1u8; 80];
        let result = parse_key_based_pairing_request(buf.clone());
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[test]
    fn parse_key_pairing_request_with_valid_public_key() {
        let mut buf = vec![1u8; 80];
        buf[16..].copy_from_slice(&bob_public_key_bytes()[..]);
        let (parsed, key) = parse_key_based_pairing_request(buf.clone()).expect("can parse");
        assert_eq!(parsed, &buf[..16]);
        assert_matches!(key, Some(_));
    }

    #[test]
    fn decrypt_invalid_key_pairing_request() {
        let key = example_aes_key();
        // Invalidly formatted request.
        let random_request = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let encrypted_request = key.encrypt(&random_request);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key);
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

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key);
        assert_matches!(request, Err(Error::InternalError(_)));
    }

    /// Example Key-based pairing request with a fixed address of 0x123456.
    pub(crate) const KEY_BASED_PAIRING_REQUEST: [u8; 16] = [
        0x00, 0x00, // Key Pairing request, Flags are empty
        0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // Provider address (sent in big-endian)
        0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd, // Salt
    ];

    #[test]
    fn decrypt_request_not_encrypted() {
        let request =
            decrypt_key_based_pairing_request(&KEY_BASED_PAIRING_REQUEST, &example_aes_key());
        assert_matches!(request, Err(_));
    }

    #[test]
    fn decrypt_seeker_initiates_pairing_request() {
        let key = example_aes_key();
        // The request is formatted and encrypted OK.
        let encrypted_request = key.encrypt(&KEY_BASED_PAIRING_REQUEST);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key)
            .expect("successful decryption");
        // Received provider address should be saved in Little Endian.
        let received_provider_address = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06];
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::SeekerInitiatesPairing { received_provider_address },
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

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key)
            .expect("successful decryption");
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::RetroactiveWrite {
                // Seeker address is the first 6 bytes of the salt in `KEY_BASED_PAIRING_REQUEST.
                // Received in BE but stored as LE per Sapphire stack.
                seeker_address: Address::Public([0xcc, 0xcc, 0xbb, 0xbb, 0xaa, 0xaa]),
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

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key)
            .expect("successful decryption");
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::ProviderInitiatesPairing {
                // Seeker address is the first 6 bytes of the salt in `KEY_BASED_PAIRING_REQUEST.
                // Received in BE but stored as LE per Sapphire stack.
                seeker_address: Address::Public([0xcc, 0xcc, 0xbb, 0xbb, 0xaa, 0xaa]),
            },
            _salt: vec![0xdd, 0xdd],
        };
        assert_eq!(request, expected_request);
    }

    pub(crate) const PASSKEY_REQUEST: [u8; 16] = [
        0x02, 0x12, 0x34, 0x56, // Seeker Passkey, Passkey = 0x123456
        0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b, 0x0b, 0x0b, 0x0c, 0x0c, 0x0c, 0x0c, // Salt
    ];

    #[test]
    fn decrypt_passkey_request_too_small() {
        let encrypted_buf = vec![1; 10];
        let result = decrypt_passkey_request(encrypted_buf, &example_aes_key());
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn decrypt_passkey_request_too_large() {
        let encrypted_buf = vec![1; 20];
        let result = decrypt_passkey_request(encrypted_buf, &example_aes_key());
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn decrypt_passkey_request_not_encrypted() {
        let result = decrypt_passkey_request(PASSKEY_REQUEST.to_vec(), &example_aes_key());
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn decrypt_passkey_request_invalid_type() {
        let mut buf = PASSKEY_REQUEST;
        buf[0] = 0x04; // Update a standard passkey request with an invalid type.
        let encrypted_buf = encrypt_message(&buf);
        let result = decrypt_passkey_request(encrypted_buf.to_vec(), &example_aes_key());
        assert_matches!(result, Err(Error::Packet));

        // We only expect to parse the Seeker passkey.
        buf[0] = u8::from(&PasskeyType::Provider);
        let encrypted_buf = encrypt_message(&buf);
        let result = decrypt_passkey_request(encrypted_buf.to_vec(), &example_aes_key());
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[test]
    fn decrypt_valid_passkey_request() {
        let encrypted_buf = encrypt_message(&PASSKEY_REQUEST);
        let parsed_passkey =
            decrypt_passkey_request(encrypted_buf.to_vec(), &example_aes_key()).expect("can parse");
        assert_eq!(parsed_passkey, 0x123456);
    }

    pub const ACCOUNT_KEY_REQUEST: [u8; 16] = [
        0x04, // All Account Keys start with 0x04.
        0x01, 0x02, 0x03, 0x04, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05,
    ];

    #[test]
    fn decrypt_account_key_request_too_small() {
        let encrypted_buf = vec![1; 10];
        let result = decrypt_account_key_request(encrypted_buf, &example_aes_key());
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn decrypt_account_key_request_too_large() {
        let encrypted_buf = vec![1; 20];
        let result = decrypt_account_key_request(encrypted_buf, &example_aes_key());
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_invalid_account_key_request() {
        let mut buf = ACCOUNT_KEY_REQUEST;
        buf[0] = 0x00;
        let encrypted_buf = encrypt_message(&buf);
        let result = decrypt_account_key_request(encrypted_buf.to_vec(), &example_aes_key());
        assert_matches!(result, Err(Error::Packet));
    }

    #[test]
    fn parse_valid_account_key_request() {
        let encrypted_buf = encrypt_message(&ACCOUNT_KEY_REQUEST);
        let result = decrypt_account_key_request(encrypted_buf.to_vec(), &example_aes_key());
        assert_matches!(result, Ok(_));
    }

    #[test]
    fn key_based_pairing_notification_data() {
        let key = example_aes_key();
        let address = Address::Public([1, 2, 3, 4, 5, 6]);

        // The response is encrypted.
        let response = key_based_pairing_response(&key, address);
        let mut response_buf: [u8; 16] = Default::default();
        response_buf.copy_from_slice(&response[..]);
        let decrypted_response = key.decrypt(&response_buf);

        // Only compare the first 7 bytes since the remaining is a randomly generated salt.
        let expected = [
            0x01, // Key-based pairing response
            0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // Address in Big Endian
        ];
        assert_eq!(decrypted_response[..7], expected);
    }

    #[test]
    fn passkey_notification_data() {
        let key = example_aes_key();
        let passkey = 0x123456;

        // The response is encrypted.
        let response = passkey_response(&key, passkey);
        let mut response_buf: [u8; 16] = Default::default();
        response_buf.copy_from_slice(&response[..]);
        let decrypted_response = key.decrypt(&response_buf);

        // Only compare the first 4 bytes since the remaining is a randomly generated salt.
        let expected = [
            0x03, // Provider's passkey
            0x12, 0x34, 0x56, // Passkey in Big Endian bytes
        ];
        assert_eq!(decrypted_response[..4], expected);
    }
}
