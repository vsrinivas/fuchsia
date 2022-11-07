// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use fuchsia_bluetooth::types::Address;
use hmac::{Hmac, Mac, NewMac};
use packet_encoding::decodable_enum;
use sha2::Sha256;
use std::convert::{TryFrom, TryInto};
use tracing::debug;

use crate::types::keys::public_key_from_bytes;
use crate::types::{AccountKey, Error, SharedSecret};

type HmacSha256 = Hmac<Sha256>;

/// Default size of an encrypted/decrypted GATT request buffer.
const GATT_REQUEST_BUFFER_SIZE: usize = 16;

/// Default size of an encrypted/decrypted GATT response buffer.
const GATT_RESPONSE_BUFFER_SIZE: usize = 16;

/// The ID associated with a personalized name write request.
/// Defined in https://developers.google.com/nearby/fast-pair/specifications/extensions/personalizedname
const PERSONALIZED_NAME_DATA_ID: u8 = 0x01;

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
    pub struct KeyBasedPairingFlags(u8);
    impl Debug;
    // Bits 0-3 are reserved.
    pub bool, retroactive_write, _: 4;
    pub bool, notify_name, _: 5;
    pub bool, provider_initiates_bonding, _: 6;
    // Bit 7 (MSB) is deprecated and ignored.
}

bitfield! {
    /// The flags associated with a device action request.
    /// Defined in Table 1.2.2 in
    /// https://developers.google.com/nearby/fast-pair/specifications/characteristics#table1.2.2
    /// Note: `bitfields` uses the opposite bit endianness from Table 1.2.2 - e.g. Bit 0 in the
    /// table corresponds to Bit 7 in the `bitfields` declaration.
    pub struct DeviceActionFlags(u8);
    impl Debug;
    // Bits 0-5 are reserved.
    pub bool, has_additional_data, _: 6;
    pub bool, device_action, _: 7;
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
    /// Seeker wants to write a personalized name for this device.
    PersonalizedNameWrite { received_provider_address: [u8; 6] },
    // TODO(fxbug.dev/99734): Add Device Action requests.
}

/// A parsed and validated key-based pairing request.
#[derive(Debug, PartialEq)]
pub struct KeyBasedPairingRequest {
    pub action: KeyBasedPairingAction,
    pub notify_name: bool,
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
            // Whether the Provider needs to notify the Seeker of the current device name.
            let notify_name = flags.notify_name();

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
                return Ok(KeyBasedPairingRequest {
                    action,
                    notify_name,
                    _salt: request[14..].to_vec(),
                });
            }

            // Otherwise, this is a standard request to start key-based pairing.
            return Ok(KeyBasedPairingRequest {
                action: KeyBasedPairingAction::SeekerInitiatesPairing { received_provider_address },
                notify_name,
                _salt: request[8..].to_vec(),
            });
        }
        MessageType::DeviceAction => {
            let flags = DeviceActionFlags(request[1]);
            debug!(?flags, "Device Action request");
            let mut received_provider_address = [0; 6];
            received_provider_address.copy_from_slice(&request[2..8]);
            received_provider_address.reverse();
            if flags.has_additional_data() {
                // The only additional data flow defined in the spec is the personalized name write.
                if request[10] == PERSONALIZED_NAME_DATA_ID {
                    return Ok(KeyBasedPairingRequest {
                        action: KeyBasedPairingAction::PersonalizedNameWrite {
                            received_provider_address,
                        },
                        notify_name: false,
                        _salt: request[11..].to_vec(),
                    });
                }
                let err = format!("Additional Data request with invalid ID ({})", request[10]);
                return Err(Error::internal(&err));
            }
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

/// Builds and returns an encrypted response for notifying the device's personalized name.
/// Defined in https://developers.google.com/nearby/fast-pair/specifications/characteristics#AdditionalData
pub fn personalized_name_response(key: &SharedSecret, name: String) -> Vec<u8> {
    let mut nonce = [0; 8];
    fuchsia_zircon::cprng_draw(&mut nonce[..]);
    personalized_name_response_internal(key, name, nonce)
}

fn personalized_name_response_internal(
    key: &SharedSecret,
    name: String,
    nonce: [u8; 8],
) -> Vec<u8> {
    // The encrypted data is constructed (Steps 2-3 of the encryption procedure).
    let mut encrypted_blocks = encrypt_personalized_name(key, name, &nonce);

    // The HMAC-SHA256 is calculated using the encrypted data (Step 4).
    let mut encrypted_data_with_nonce = nonce.to_vec();
    encrypted_data_with_nonce.extend_from_slice(&encrypted_blocks);
    let mut hmac = HmacSha256::new_from_slice(key.as_bytes()).expect("valid key");
    hmac.update(&encrypted_data_with_nonce[..]);
    let hashed_encrypted_data: [u8; 32] = hmac.finalize().into_bytes().into();

    // The encrypted output consists of:
    // 1) First 8-bytes of the HMAC-SHA256 calculation.
    // 2) 8-byte nonce
    // 3) Encrypted data
    let mut output = vec![0; 16];
    output[..8].copy_from_slice(&hashed_encrypted_data[..8]);
    output[8..16].copy_from_slice(&nonce[..8]);
    output.append(&mut encrypted_blocks);
    output
}

/// Encrypts the provided `name` and returns a byte buffer of the encrypted output.
/// Corresponds to steps 2-3 in the personalized name encryption algorithm.
fn encrypt_personalized_name(key: &SharedSecret, name: String, nonce: &[u8; 8]) -> Vec<u8> {
    let name_bytes = name.into_bytes();

    // The provided `name_bytes` is divided into 16-byte blocks. The last block can be less than 16
    // bytes.
    // Each block is combined with a temporary buffer that depends on the index of the block.
    // The temporary buffer has format: [i, 0x00000000000000, nonce] where:
    // i = the index of the 16-byte block.
    // nonce = randomly generated 8-byte value.
    // Therefore, each block is encrypted as:
    //   encryptedBlock[i] = block[i] ^ AES(key, concat((uint8) i, 0x00000000000000, nonce))
    let mut block_mask = [0; 16];
    block_mask[8..].copy_from_slice(nonce);
    let mut encrypted_blocks = vec![];
    for (i, block) in name_bytes.chunks(16).enumerate() {
        block_mask[0] = i as u8;
        let encrypted_block = key.encrypt(&block_mask);
        let mut combined_encrypted_block =
            block.iter().zip(encrypted_block.iter()).map(|(&x1, &x2)| x1 ^ x2).collect();
        encrypted_blocks.append(&mut combined_encrypted_block);
    }
    encrypted_blocks
}

/// Returns the personalized name or Error if the encrypted `request` could not be parsed.
pub fn decrypt_personalized_name_request(
    key: &SharedSecret,
    request: Vec<u8>,
) -> Result<String, Error> {
    // First 8 bytes is the HMAC-SHA256 and is ignored.
    // Next 8 bytes is the nonce.
    let mut block_mask = [0; 16];
    block_mask[8..].copy_from_slice(&request[8..16]);
    let mut decrypted_blocks = vec![];
    for (i, block) in request[16..].chunks(16).enumerate() {
        block_mask[0] = i as u8;
        let decrypted_block = key.encrypt(&block_mask);
        let mut combined_decrypted_block =
            block.iter().zip(decrypted_block.iter()).map(|(&x1, &x2)| x1 ^ x2).collect();
        decrypted_blocks.append(&mut combined_decrypted_block);
    }
    String::from_utf8(decrypted_blocks).map_err(|_| Error::internal("invalid personalized name"))
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use crate::types::keys::tests::{bob_public_key_bytes, encrypt_message, example_aes_key};
    use assert_matches::assert_matches;

    /// Returns a key-based pairing request with the provided `flags`. A fixed address of
    /// 0x010203040506 is used.
    pub(crate) fn key_based_pairing_request(flags: u8) -> [u8; 16] {
        [
            0x00, flags, // Key Pairing request with flags
            0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // Provider address (sent in big-endian)
            0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd, // Salt
        ]
    }

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

    /// Example device action request with a fixed address of 0x123456.
    pub(crate) const DEVICE_ACTION_PERSONALIZED_NAME_REQUEST: [u8; 16] = [
        0x10, 0x40, // Device Action request, Flags indicating additional data characteristic
        0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // Provider address (sent in big-endian)
        0x00, 0x00, // Message group & code are ignored
        0x01, // Data ID = 0x01 indicating personalized name
        0xbb, 0xcc, 0xcc, 0xdd, 0xdd, // Salt
    ];

    #[test]
    fn decrypt_request_not_encrypted() {
        let request =
            decrypt_key_based_pairing_request(&key_based_pairing_request(0x00), &example_aes_key());
        assert_matches!(request, Err(_));
    }

    #[test]
    fn decrypt_seeker_initiates_pairing_request() {
        let key = example_aes_key();
        // The request is formatted and encrypted OK.
        let encrypted_request = key.encrypt(&key_based_pairing_request(0x00));

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key)
            .expect("successful decryption");
        // Received provider address should be saved in Little Endian.
        let received_provider_address = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06];
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::SeekerInitiatesPairing { received_provider_address },
            notify_name: false, // The Flags are empty so we don't expect to notify name.
            _salt: vec![0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd],
        };
        assert_eq!(request, expected_request);
    }

    #[test]
    fn decrypt_retroactive_pairing_request() {
        let key = example_aes_key();
        // `flags` = Only retroactive pairing.
        let encrypted_request = key.encrypt(&key_based_pairing_request(0x10));

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key)
            .expect("successful decryption");
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::RetroactiveWrite {
                // Seeker address is the first 6 bytes of the salt in `KEY_BASED_PAIRING_REQUEST.
                // Received in BE but stored as LE per Sapphire stack.
                seeker_address: Address::Public([0xcc, 0xcc, 0xbb, 0xbb, 0xaa, 0xaa]),
            },
            notify_name: false,
            _salt: vec![0xdd, 0xdd],
        };
        assert_eq!(request, expected_request);
    }

    #[test]
    fn decrypt_provider_initiates_pairing_request() {
        let key = example_aes_key();
        // `flags` = Provider initiates pairing & notify name
        let encrypted_request = key.encrypt(&key_based_pairing_request(0x60));

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key)
            .expect("successful decryption");
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::ProviderInitiatesPairing {
                // Seeker address is the first 6 bytes of the salt in `KEY_BASED_PAIRING_REQUEST.
                // Received in BE but stored as LE per Sapphire stack.
                seeker_address: Address::Public([0xcc, 0xcc, 0xbb, 0xbb, 0xaa, 0xaa]),
            },
            notify_name: true,
            _salt: vec![0xdd, 0xdd],
        };
        assert_eq!(request, expected_request);
    }

    #[test]
    fn decrypt_personalized_name_pairing_request() {
        let key = example_aes_key();
        let encrypted_request = key.encrypt(&DEVICE_ACTION_PERSONALIZED_NAME_REQUEST);

        let request = decrypt_key_based_pairing_request(&encrypted_request, &key)
            .expect("successful decryption");
        let expected_provider_address = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06];
        let expected_request = KeyBasedPairingRequest {
            action: KeyBasedPairingAction::PersonalizedNameWrite {
                received_provider_address: expected_provider_address,
            },
            notify_name: false,
            _salt: vec![0xbb, 0xcc, 0xcc, 0xdd, 0xdd],
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

    /// Shared secret that is used in the Personalized Name test cases in the GFPS.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#aes-ctr_encryption
    fn personalized_name_key() -> SharedSecret {
        SharedSecret::new([
            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB,
            0xCD, 0xEF,
        ])
    }

    /// This test verifies the encryption of the personalized name.
    /// The contents of this test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#aes-ctr_encryption
    #[test]
    fn personalized_name_aes_encrpytion() {
        let name = String::from_utf8(vec![
            0x53, 0x6F, 0x6D, 0x65, 0x6F, 0x6E, 0x65, 0x27, 0x73, 0x20, 0x47, 0x6F, 0x6F, 0x67,
            0x6C, 0x65, 0x20, 0x48, 0x65, 0x61, 0x64, 0x70, 0x68, 0x6F, 0x6E, 0x65,
        ])
        .expect("valid utf8 string");
        let nonce = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07];

        let result = encrypt_personalized_name(&personalized_name_key(), name, &nonce);
        let expected_encryption_result = [
            0xEE, 0x4A, 0x24, 0x83, 0x73, 0x80, 0x52, 0xE4, 0x4E, 0x9B, 0x2A, 0x14, 0x5E, 0x5D,
            0xDF, 0xAA, 0x44, 0xB9, 0xE5, 0x53, 0x6A, 0xF4, 0x38, 0xE1, 0xE5, 0xC6,
        ];
        assert_eq!(result[..], expected_encryption_result);
    }

    /// This test verifies the creation of the personalized name response.
    /// The contents of this test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#encode_personalized_name_to_additional_data_packet
    #[test]
    fn personalized_name_response() {
        let name = String::from_utf8(vec![
            0x53, 0x6F, 0x6D, 0x65, 0x6F, 0x6E, 0x65, 0x27, 0x73, 0x20, 0x47, 0x6F, 0x6F, 0x67,
            0x6C, 0x65, 0x20, 0x48, 0x65, 0x61, 0x64, 0x70, 0x68, 0x6F, 0x6E, 0x65,
        ])
        .expect("valid utf8 string");
        let nonce = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07];

        let response = personalized_name_response_internal(&personalized_name_key(), name, nonce);
        let expected_encrypted_response = [
            0x55, 0xEC, 0x5E, 0x60, 0x55, 0xAF, 0x6E, 0x92, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
            0x06, 0x07, 0xEE, 0x4A, 0x24, 0x83, 0x73, 0x80, 0x52, 0xE4, 0x4E, 0x9B, 0x2A, 0x14,
            0x5E, 0x5D, 0xDF, 0xAA, 0x44, 0xB9, 0xE5, 0x53, 0x6A, 0xF4, 0x38, 0xE1, 0xE5, 0xC6,
        ];
        assert_eq!(response[..], expected_encrypted_response);
    }

    /// This test verifies the parsing and decryption of the personalized name request.
    /// The contents of this test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#decode_additional_data_packet_to_get_personalized_name
    #[test]
    fn personalized_name_request() {
        let encrypted_request = vec![
            0x55, 0xEC, 0x5E, 0x60, 0x55, 0xAF, 0x6E, 0x92, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
            0x06, 0x07, 0xEE, 0x4A, 0x24, 0x83, 0x73, 0x80, 0x52, 0xE4, 0x4E, 0x9B, 0x2A, 0x14,
            0x5E, 0x5D, 0xDF, 0xAA, 0x44, 0xB9, 0xE5, 0x53, 0x6A, 0xF4, 0x38, 0xE1, 0xE5, 0xC6,
        ];

        let parsed_name =
            decrypt_personalized_name_request(&personalized_name_key(), encrypted_request)
                .expect("valid request");
        let expected_name = String::from_utf8(vec![
            0x53, 0x6F, 0x6D, 0x65, 0x6F, 0x6E, 0x65, 0x27, 0x73, 0x20, 0x47, 0x6F, 0x6F, 0x67,
            0x6C, 0x65, 0x20, 0x48, 0x65, 0x61, 0x64, 0x70, 0x68, 0x6F, 0x6E, 0x65,
        ])
        .expect("valid utf8 string");
        assert_eq!(parsed_name, expected_name);
    }
}
