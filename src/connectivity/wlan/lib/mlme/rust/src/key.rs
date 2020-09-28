// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_wlan_mlme as fidl_mlme, wlan_common::mac::MacAddr};

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KeyType(u8);

// LINT.IfChange
impl KeyType {
    pub const PAIRWISE: Self = Self(1);
    pub const GROUP: Self = Self(2);
    pub const IGTK: Self = Self(3);
    pub const PEER: Self = Self(4);
}
// LINT.ThenChange(//zircon/system/banjo/ddk.protocol.wlan.info/info.banjo)

// LINT.IfChange
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Protection(u8);

impl Protection {
    pub const NONE: Self = Self(0);
    pub const RX: Self = Self(1);
    pub const TX: Self = Self(2);
    pub const RX_TX: Self = Self(3);
}

/// This is wlan_key_config_t.
///
/// It should match the same ABI as garnet/lib/wlan/protocol/include/wlan/protocol/mac.h.
#[repr(C)]
#[derive(Clone, Debug, PartialEq)]
pub struct KeyConfig {
    pub bssid: u8,
    pub protection: Protection,
    pub cipher_oui: [u8; 3],
    pub cipher_type: u8,
    pub key_type: KeyType,
    pub peer_addr: MacAddr,
    pub key_idx: u8,
    pub key_len: u8,
    pub key: [u8; 32],
    pub rsc: u64,
}
// LINT.ThenChange(//garnet/lib/wlan/protocol/include/wlan/protocol/mac.h)

impl From<&fidl_mlme::SetKeyDescriptor> for KeyConfig {
    fn from(key_desc: &fidl_mlme::SetKeyDescriptor) -> Self {
        let mut key = [0; 32];
        key[..key_desc.key.len()].copy_from_slice(&key_desc.key[..]);

        Self {
            // TODO(fxbug.dev/39764): This value was default initialized to 0 in the original code:
            // we need to figure out if the driver still needs this.
            bssid: 0,
            protection: Protection::RX_TX,
            cipher_oui: key_desc.cipher_suite_oui,
            cipher_type: key_desc.cipher_suite_type,
            key_type: match key_desc.key_type {
                fidl_mlme::KeyType::Pairwise => KeyType::PAIRWISE,
                fidl_mlme::KeyType::PeerKey => KeyType::PEER,
                fidl_mlme::KeyType::Igtk => KeyType::IGTK,
                fidl_mlme::KeyType::Group => KeyType::GROUP,
            },
            peer_addr: key_desc.address,
            key_idx: key_desc.key_id as u8,
            key_len: key_desc.key.len() as u8,
            key: key,
            rsc: key_desc.rsc,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn from_set_key_descriptor() {
        assert_eq!(
            KeyConfig::from(&fidl_mlme::SetKeyDescriptor {
                cipher_suite_oui: [1, 2, 3],
                cipher_suite_type: 4,
                key_type: fidl_mlme::KeyType::Pairwise,
                address: [5; 6],
                key_id: 6,
                key: vec![1, 2, 3, 4, 5, 6, 7],
                rsc: 8,
            }),
            KeyConfig {
                bssid: 0,
                protection: Protection::RX_TX,
                cipher_oui: [1, 2, 3],
                cipher_type: 4,
                key_type: KeyType::PAIRWISE,
                peer_addr: [5; 6],
                key_idx: 6,
                key_len: 7,
                key: [
                    1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0,
                ],
                rsc: 8,
            }
        )
    }
}
