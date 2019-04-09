// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(test)]
#![feature(drain_filter)]
#![deny(warnings)]
// Remove once Cipher and AKM *_bits() were replaced with *_len() calls.
#![allow(deprecated)]

use failure::{self, Fail};

// TODO(hahnr): Limit exports and rearrange modules.

pub mod auth;
mod crypto_utils;
mod integrity;
pub mod key;
mod key_data;
mod keywrap;
pub mod rsna;
mod state_machine;

use crate::key::exchange::{
    self,
    handshake::fourway::{self, MessageNumber},
    handshake::group_key,
};
use crate::rsna::esssa::EssSa;
use crate::rsna::{Role, UpdateSink};
use std::sync::{Arc, Mutex};

pub use crate::auth::psk;
pub use crate::crypto_utils::nonce;
pub use crate::key::gtk;
pub use crate::key::gtk::GtkProvider;
pub use crate::rsna::NegotiatedRsne;
use wlan_common::ie::rsn::rsne;

#[derive(Debug, PartialEq)]
pub struct Supplicant {
    esssa: EssSa,
}

impl Supplicant {
    /// WPA2-PSK CCMP-128 Supplicant which supports 4-Way- and Group-Key Handshakes.
    pub fn new_wpa2psk_ccmp128(
        nonce_rdr: Arc<nonce::NonceReader>,
        psk: psk::Psk,
        s_addr: [u8; 6],
        s_rsne: rsne::Rsne,
        a_addr: [u8; 6],
        a_rsne: rsne::Rsne,
    ) -> Result<Supplicant, failure::Error> {
        let negotiated_rsne = NegotiatedRsne::from_rsne(&s_rsne)?;
        let akm = negotiated_rsne.akm.clone();
        let group_data = negotiated_rsne.group_data.clone();

        let esssa = EssSa::new(
            Role::Supplicant,
            negotiated_rsne,
            auth::Config::ComputedPsk(psk),
            exchange::Config::FourWayHandshake(fourway::Config::new(
                Role::Supplicant,
                s_addr,
                s_rsne,
                a_addr,
                a_rsne,
                nonce_rdr,
                None,
            )?),
            Some(exchange::Config::GroupKeyHandshake(group_key::Config {
                role: Role::Supplicant,
                akm,
                cipher: group_data,
            })),
        )?;

        Ok(Supplicant { esssa })
    }

    /// Starts the Supplicant. A Supplicant must be started after its creation and everytime it was
    /// reset.
    pub fn start(&mut self) -> Result<(), failure::Error> {
        // The Supplicant always waits for Authenticator to initiate and does not yet support EAPOL
        // request frames. Thus, all updates can be ignored.
        let mut dead_update_sink = vec![];
        self.esssa.initiate(&mut dead_update_sink)
    }

    /// Resets all established Security Associations and invalidates all derived keys.
    /// The Supplicant must be reset or destroyed when the underlying 802.11 association terminates.
    pub fn reset(&mut self) {
        self.esssa.reset();
    }

    /// Entry point for all incoming EAPOL frames. Incoming frames can be corrupted, invalid or of
    /// unsupported types; the Supplicant will filter and drop all unexpected frames.
    /// Outbound EAPOL frames, status and key updates will be pushed into the `update_sink`.
    /// The method will return an `Error` if the frame was invalid.
    pub fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: &eapol::Frame,
    ) -> Result<(), failure::Error> {
        self.esssa.on_eapol_frame(update_sink, frame)
    }
}

#[derive(Debug, PartialEq)]
pub struct Authenticator {
    esssa: EssSa,
}

impl Authenticator {
    /// WPA2-PSK CCMP-128 Authenticator which supports 4-Way Handshake.
    /// The Authenticator does not support GTK rotations.
    pub fn new_wpa2psk_ccmp128(
        nonce_rdr: Arc<nonce::NonceReader>,
        gtk_provider: Arc<Mutex<gtk::GtkProvider>>,
        psk: psk::Psk,
        s_addr: [u8; 6],
        s_rsne: rsne::Rsne,
        a_addr: [u8; 6],
        a_rsne: rsne::Rsne,
    ) -> Result<Authenticator, failure::Error> {
        let negotiated_rsne = NegotiatedRsne::from_rsne(&s_rsne)?;
        let esssa = EssSa::new(
            Role::Authenticator,
            negotiated_rsne,
            auth::Config::ComputedPsk(psk),
            exchange::Config::FourWayHandshake(fourway::Config::new(
                Role::Authenticator,
                s_addr,
                s_rsne,
                a_addr,
                a_rsne,
                nonce_rdr,
                Some(gtk_provider),
            )?),
            // Group-Key Handshake does not support Authenticator role yet.
            None,
        )?;

        Ok(Authenticator { esssa })
    }

    pub fn get_negotiated_rsne(&self) -> &NegotiatedRsne {
        &self.esssa.negotiated_rsne
    }

    /// Resets all established Security Associations and invalidates all derived keys.
    /// The Authenticator must be reset or destroyed when the underlying 802.11 association
    /// terminates.
    pub fn reset(&mut self) {
        self.esssa.reset();
    }

    /// `initiate(...)` must be called when the Authenticator should start establishing a
    /// security association with a client.
    /// The Authenticator must always initiate the security association in the current system as
    /// EAPOL request frames from clients are not yet supported.
    /// This method can be called multiple times to re-initiate the security association, however,
    /// calling this method will invalidate all established security associations and their derived
    /// keys.
    pub fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), failure::Error> {
        self.esssa.initiate(update_sink)
    }

    /// Entry point for all incoming EAPOL frames. Incoming frames can be corrupted, invalid or of
    /// unsupported types; the Authenticator will filter and drop all unexpected frames.
    /// Outbound EAPOL frames, status and key updates will be pushed into the `update_sink`.
    /// The method will return an `Error` if the frame was invalid.
    pub fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: &eapol::Frame,
    ) -> Result<(), failure::Error> {
        self.esssa.on_eapol_frame(update_sink, frame)
    }
}

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "unexpected IO error: {}", _0)]
    UnexpectedIoError(#[cause] std::io::Error),
    #[fail(display = "invalid OUI length; expected 3 bytes but received {}", _0)]
    InvalidOuiLength(usize),
    #[fail(display = "invalid PMKID length; expected 16 bytes but received {}", _0)]
    InvalidPmkidLength(usize),
    #[fail(display = "invalid ssid length: {}", _0)]
    InvalidSsidLen(usize),
    #[fail(display = "invalid passphrase length: {}", _0)]
    InvalidPassphraseLen(usize),
    #[fail(display = "passphrase contains invalid character: {:x}", _0)]
    InvalidPassphraseChar(u8),
    #[fail(display = "the config `{:?}` is incompatible with the auth method `{:?}`", _0, _1)]
    IncompatibleConfig(auth::Config, String),
    #[fail(display = "invalid bit size; must be a multiple of 8 but was {}", _0)]
    InvalidBitSize(usize),
    #[fail(display = "nonce could not be generated")]
    NonceError,
    #[fail(display = "error deriving PTK; invalid PMK")]
    PtkHierarchyInvalidPmkError,
    #[fail(display = "error deriving PTK; unsupported AKM suite")]
    PtkHierarchyUnsupportedAkmError,
    #[fail(display = "error deriving PTK; unsupported cipher suite")]
    PtkHierarchyUnsupportedCipherError,
    #[fail(display = "error deriving GTK; unsupported cipher suite")]
    GtkHierarchyUnsupportedCipherError,
    #[fail(display = "error invalid key size for AES keywrap: {}", _0)]
    InvalidAesKeywrapKeySize(usize),
    #[fail(
        display = "error data must be a multiple of 64-bit blocks and at least 128 bits: {}",
        _0
    )]
    InvalidAesKeywrapDataLength(usize),
    #[fail(display = "error wrong key for AES Keywrap unwrapping")]
    WrongAesKeywrapKey,
    #[fail(
        display = "invalid key data length; must be at least 16 bytes and a multiple of 8: {}",
        _0
    )]
    InvaidKeyDataLength(usize),
    #[fail(display = "invalid key data; error code: {:?}", _0)]
    InvalidKeyData(nom::IError),
    #[fail(display = "unknown authentication method")]
    UnknownAuthenticationMethod,
    #[fail(display = "no AKM negotiated")]
    InvalidNegotiatedAkm,
    #[fail(display = "unknown key exchange method")]
    UnknownKeyExchange,
    #[fail(display = "cannot initiate Fourway Handshake as Supplicant")]
    UnexpectedInitiationRequest,
    #[fail(display = "unsupported Key Descriptor Type: {:?}", _0)]
    UnsupportedKeyDescriptor(u8),
    #[fail(display = "unexpected Key Descriptor Type {:?}; expected {:?}", _0, _1)]
    InvalidKeyDescriptor(u8, eapol::KeyDescriptor),
    #[fail(display = "unsupported Key Descriptor Version: {:?}", _0)]
    UnsupportedKeyDescriptorVersion(u16),
    #[fail(display = "only PTK and GTK derivation is supported")]
    UnsupportedKeyDerivation,
    #[fail(display = "unexpected message: {:?}", _0)]
    Unexpected4WayHandshakeMessage(MessageNumber),
    #[fail(display = "invalid install bit value; message: {:?}", _0)]
    InvalidInstallBitValue(MessageNumber),
    #[fail(display = "error, install bit set for Group-/SMK-Handshake")]
    InvalidInstallBitGroupSmkHandshake,
    #[fail(display = "invalid key_ack bit value; message: {:?}", _0)]
    InvalidKeyAckBitValue(MessageNumber),
    #[fail(display = "invalid key_mic bit value; message: {:?}", _0)]
    InvalidKeyMicBitValue(MessageNumber),
    #[fail(display = "invalid key_mic bit value; message: {:?}", _0)]
    InvalidSecureBitValue(MessageNumber),
    #[fail(display = "error, secure bit set by Authenticator before PTK is known")]
    SecureBitWithUnknownPtk,
    #[fail(display = "error, secure bit set must be set by Supplicant once PTK and GTK are known")]
    SecureBitNotSetWithKnownPtkGtk,
    #[fail(display = "invalid error bit value; message: {:?}", _0)]
    InvalidErrorBitValue(MessageNumber),
    #[fail(display = "invalid request bit value; message: {:?}", _0)]
    InvalidRequestBitValue(MessageNumber),
    #[fail(display = "error, Authenticator set request bit")]
    InvalidRequestBitAuthenticator,
    #[fail(display = "error, Authenticator set error bit")]
    InvalidErrorBitAuthenticator,
    #[fail(display = "error, Supplicant set key_ack bit")]
    InvalidKeyAckBitSupplicant,
    #[fail(display = "invalid encrypted_key_data bit value")]
    InvalidEncryptedKeyDataBitValue(MessageNumber),
    #[fail(display = "invalid key length {:?}; expected {:?}", _0, _1)]
    InvalidKeyLength(u16, u16),
    #[fail(display = "unsupported cipher suite")]
    UnsupportedCipherSuite,
    #[fail(display = "unsupported AKM suite")]
    UnsupportedAkmSuite,
    #[fail(display = "invalid MIC size")]
    InvalidMicSize,
    #[fail(display = "invalid Nonce; expected to be non-zero")]
    InvalidNonce(MessageNumber),
    #[fail(display = "invalid RSC; expected to be zero")]
    InvalidRsc(MessageNumber),
    #[fail(display = "invalid key data; must not be zero")]
    EmptyKeyData(MessageNumber),
    #[fail(display = "invalid key data")]
    InvalidKeyDataContent,
    #[fail(display = "invalid key data length; doesn't match with key data")]
    InvalidKeyDataLength,
    #[fail(display = "cannot validate MIC; PTK not yet derived")]
    UnexpectedMic,
    #[fail(display = "invalid MIC")]
    InvalidMic,
    #[fail(display = "cannot decrypt key data; PTK not yet derived")]
    UnexpectedEncryptedKeyData,
    #[fail(display = "invalid key replay counter {:?}; expected counter to be > {:?}", _0, _1)]
    InvalidKeyReplayCounter(u64, u64),
    #[fail(display = "invalid nonce; nonce must match nonce from 1st message")]
    ErrorNonceDoesntMatch,
    #[fail(display = "invalid IV; EAPOL protocol version: {:?}; message: {:?}", _0, _1)]
    InvalidIv(u8, MessageNumber),
    #[fail(display = "PMKSA was not yet established")]
    PmksaNotEstablished,
    #[fail(display = "invalid nonce size; expected 32 bytes, found: {:?}", _0)]
    InvalidNonceSize(usize),
    #[fail(display = "invalid key data; expected negotiated RSNE")]
    InvalidKeyDataRsne,
    #[fail(display = "buffer too small; required: {}, available: {}", _0, _1)]
    BufferTooSmall(usize, usize),
    #[fail(display = "error, SMK-Handshake is not supported")]
    SmkHandshakeNotSupported,
    #[fail(display = "error, negotiated RSNE is invalid")]
    InvalidNegotiatedRsne,
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error::UnexpectedIoError(e)
    }
}
