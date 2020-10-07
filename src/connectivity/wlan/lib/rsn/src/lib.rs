// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg_attr(feature = "benchmarks", feature(test))]
// Remove once Cipher and AKM *_bits() were replaced with *_len() calls.
#![allow(deprecated)]
// This crate doesn't comply with all 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use thiserror::{self, Error};

// TODO(hahnr): Limit exports and rearrange modules.

pub mod auth;
mod crypto_utils;
mod integrity;
pub mod key;
mod key_data;
mod keywrap;
pub mod rsna;

use {
    crate::{
        key::exchange::{
            self,
            handshake::{fourway, group_key, HandshakeMessageNumber},
        },
        rsna::{esssa::EssSa, Role, UpdateSink},
    },
    eapol,
    fidl_fuchsia_wlan_mlme::SaeFrame,
    std::sync::{Arc, Mutex},
    wlan_common::ie::{rsn::rsne::Rsne, wpa::WpaIe},
    zerocopy::ByteSlice,
};

pub use crate::{
    auth::psk,
    crypto_utils::nonce,
    key::gtk::{self, GtkProvider},
    rsna::NegotiatedProtection,
};

#[derive(Debug)]
pub struct Supplicant {
    esssa: EssSa,
}

/// Any information (i.e. info elements) used to negotiate protection on an RSN.
#[derive(Debug, Clone, PartialEq)]
pub enum ProtectionInfo {
    Rsne(Rsne),
    LegacyWpa(WpaIe),
}

impl Supplicant {
    /// WPA personal supplicant which supports 4-Way- and Group-Key Handshakes.
    pub fn new_wpa_personal(
        nonce_rdr: Arc<nonce::NonceReader>,
        auth_cfg: auth::Config,
        s_addr: [u8; 6],
        s_protection: ProtectionInfo,
        a_addr: [u8; 6],
        a_protection: ProtectionInfo,
    ) -> Result<Supplicant, anyhow::Error> {
        let negotiated_protection = NegotiatedProtection::from_protection(&s_protection)?;
        let gtk_exch_cfg = Some(exchange::Config::GroupKeyHandshake(group_key::Config {
            role: Role::Supplicant,
            protection: negotiated_protection.clone(),
        }));
        let esssa = EssSa::new(
            Role::Supplicant,
            negotiated_protection,
            auth_cfg,
            exchange::Config::FourWayHandshake(fourway::Config::new(
                Role::Supplicant,
                s_addr,
                s_protection,
                a_addr,
                a_protection,
                nonce_rdr,
                None,
            )?),
            gtk_exch_cfg,
        )?;

        Ok(Supplicant { esssa })
    }

    /// Starts the Supplicant. A Supplicant must be started after its creation and everytime it was
    /// reset.
    pub fn start(&mut self) -> Result<(), Error> {
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
    pub fn on_eapol_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::Frame<B>,
    ) -> Result<(), Error> {
        self.esssa.on_eapol_frame(update_sink, frame)
    }

    pub fn on_sae_handshake_ind(
        &mut self,
        update_sink: &mut UpdateSink,
    ) -> Result<(), anyhow::Error> {
        self.esssa.on_sae_handshake_ind(update_sink).map_err(|e| e.into())
    }

    pub fn on_sae_frame_rx(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: SaeFrame,
    ) -> Result<(), anyhow::Error> {
        self.esssa.on_sae_frame_rx(update_sink, frame).map_err(|e| e.into())
    }

    pub fn on_sae_timeout(
        &mut self,
        update_sink: &mut UpdateSink,
        event_id: u64,
    ) -> Result<(), anyhow::Error> {
        self.esssa.on_sae_timeout(update_sink, event_id).map_err(|e| e.into())
    }
}

#[derive(Debug)]
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
        s_protection: ProtectionInfo,
        a_addr: [u8; 6],
        a_protection: ProtectionInfo,
    ) -> Result<Authenticator, anyhow::Error> {
        let negotiated_protection = NegotiatedProtection::from_protection(&s_protection)?;
        let esssa = EssSa::new(
            Role::Authenticator,
            negotiated_protection,
            auth::Config::ComputedPsk(psk),
            exchange::Config::FourWayHandshake(fourway::Config::new(
                Role::Authenticator,
                s_addr,
                s_protection,
                a_addr,
                a_protection,
                nonce_rdr,
                Some(gtk_provider),
            )?),
            // Group-Key Handshake does not support Authenticator role yet.
            None,
        )?;

        Ok(Authenticator { esssa })
    }

    pub fn get_negotiated_protection(&self) -> &NegotiatedProtection {
        &self.esssa.negotiated_protection
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
    pub fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        self.esssa.initiate(update_sink)
    }

    /// Entry point for all incoming EAPOL frames. Incoming frames can be corrupted, invalid or of
    /// unsupported types; the Authenticator will filter and drop all unexpected frames.
    /// Outbound EAPOL frames, status and key updates will be pushed into the `update_sink`.
    /// The method will return an `Error` if the frame was invalid.
    pub fn on_eapol_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::Frame<B>,
    ) -> Result<(), Error> {
        self.esssa.on_eapol_frame(update_sink, frame)
    }
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("unexpected IO error: {}", _0)]
    UnexpectedIoError(std::io::Error),
    #[error("invalid OUI length; expected 3 bytes but received {}", _0)]
    InvalidOuiLength(usize),
    #[error("invalid PMKID length; expected 16 bytes but received {}", _0)]
    InvalidPmkidLength(usize),
    #[error("invalid ssid length: {}", _0)]
    InvalidSsidLen(usize),
    #[error("invalid passphrase length: {}", _0)]
    InvalidPassphraseLen(usize),
    #[error("passphrase is not valid UTF-8; failed to parse after byte at index: {:x}", _0)]
    InvalidPassphraseEncoding(usize),
    #[error("the config `{:?}` is incompatible with the auth method `{:?}`", _0, _1)]
    IncompatibleConfig(auth::Config, String),
    #[error("invalid bit size; must be a multiple of 8 but was {}", _0)]
    InvalidBitSize(usize),
    #[error("nonce could not be generated")]
    NonceError,
    #[error("error deriving PTK; invalid PMK")]
    PtkHierarchyInvalidPmkError,
    #[error("error deriving PTK; unsupported AKM suite")]
    PtkHierarchyUnsupportedAkmError,
    #[error("error deriving PTK; unsupported cipher suite")]
    PtkHierarchyUnsupportedCipherError,
    #[error("error deriving GTK; unsupported cipher suite")]
    GtkHierarchyUnsupportedCipherError,
    #[error("error invalid key size for AES keywrap: {}", _0)]
    InvalidAesKeywrapKeySize(usize),
    #[error("error data must be a multiple of 64-bit blocks and at least 128 bits: {}", _0)]
    InvalidAesKeywrapDataLength(usize),
    #[error("error wrong key for AES Keywrap unwrapping")]
    WrongAesKeywrapKey,
    #[error("invalid key data length; must be at least 16 bytes and a multiple of 8: {}", _0)]
    InvaidKeyDataLength(usize),
    #[error("invalid key data; error code: {:?}", _0)]
    InvalidKeyData(nom::error::ErrorKind),
    #[error("unknown authentication method")]
    UnknownAuthenticationMethod,
    #[error("no AKM negotiated")]
    InvalidNegotiatedAkm,
    #[error("unknown key exchange method")]
    UnknownKeyExchange,
    #[error("cannot initiate Fourway Handshake as Supplicant")]
    UnexpectedInitiationRequest,
    #[error("unsupported Key Descriptor Type: {:?}", _0)]
    UnsupportedKeyDescriptor(eapol::KeyDescriptor),
    #[error("unexpected Key Descriptor Type {:?}; expected {:?}", _0, _1)]
    InvalidKeyDescriptor(eapol::KeyDescriptor, eapol::KeyDescriptor),
    #[error("unsupported Key Descriptor Version: {:?}", _0)]
    UnsupportedKeyDescriptorVersion(u16),
    #[error("only PTK and GTK derivation is supported")]
    UnsupportedKeyDerivation,
    #[error("unexpected message: {:?}", _0)]
    UnexpectedHandshakeMessage(HandshakeMessageNumber),
    #[error("invalid install bit value; message: {:?}", _0)]
    InvalidInstallBitValue(HandshakeMessageNumber),
    #[error("error, install bit set for Group-/SMK-Handshake")]
    InvalidInstallBitGroupSmkHandshake,
    #[error("invalid key_ack bit value; message: {:?}", _0)]
    InvalidKeyAckBitValue(HandshakeMessageNumber),
    #[error("invalid key_mic bit value; message: {:?}", _0)]
    InvalidKeyMicBitValue(HandshakeMessageNumber),
    #[error("invalid secure bit value; message: {:?}", _0)]
    InvalidSecureBitValue(HandshakeMessageNumber),
    #[error("error, secure bit set by Authenticator before PTK is known")]
    SecureBitWithUnknownPtk,
    #[error("error, secure bit set must be set by Supplicant once PTK and GTK are known")]
    SecureBitNotSetWithKnownPtkGtk,
    #[error("invalid error bit value; message: {:?}", _0)]
    InvalidErrorBitValue(HandshakeMessageNumber),
    #[error("invalid request bit value; message: {:?}", _0)]
    InvalidRequestBitValue(HandshakeMessageNumber),
    #[error("error, Authenticator set request bit")]
    InvalidRequestBitAuthenticator,
    #[error("error, Authenticator set error bit")]
    InvalidErrorBitAuthenticator,
    #[error("error, Supplicant set key_ack bit")]
    InvalidKeyAckBitSupplicant,
    #[error("invalid encrypted_key_data bit value")]
    InvalidEncryptedKeyDataBitValue(HandshakeMessageNumber),
    #[error("encrypted_key_data bit requires MIC bit to be set")]
    InvalidMicBitForEncryptedKeyData,
    #[error("invalid key length {:?}; expected {:?}", _0, _1)]
    InvalidKeyLength(u16, u16),
    #[error("unsupported cipher suite")]
    UnsupportedCipherSuite,
    #[error("unsupported AKM suite")]
    UnsupportedAkmSuite,
    #[error("cannot compute MIC for key frames which haven't set their MIC bit")]
    ComputingMicForUnprotectedFrame,
    #[error("cannot compute MIC; error while encrypting")]
    ComputingMicEncryptionError,
    #[error("the key frame's MIC size ({}) differes from the expected size: {}", _0, _1)]
    MicSizesDiffer(usize, usize),
    #[error("invalid MIC size")]
    InvalidMicSize,
    #[error("invalid Nonce; expected to be non-zero")]
    InvalidNonce(HandshakeMessageNumber),
    #[error("invalid RSC; expected to be zero")]
    InvalidRsc(HandshakeMessageNumber),
    #[error("invalid key data; must not be zero")]
    EmptyKeyData(HandshakeMessageNumber),
    #[error("invalid key data")]
    InvalidKeyDataContent,
    #[error("invalid key data length; doesn't match with key data")]
    InvalidKeyDataLength,
    #[error("cannot validate MIC; PTK not yet derived")]
    UnexpectedMic,
    #[error("invalid MIC")]
    InvalidMic,
    #[error("cannot decrypt key data; PTK not yet derived")]
    UnexpectedEncryptedKeyData,
    #[error("invalid key replay counter {:?}; expected counter to be > {:?}", _0, _1)]
    InvalidKeyReplayCounter(u64, u64),
    #[error("invalid nonce; nonce must match nonce from 1st message")]
    ErrorNonceDoesntMatch,
    #[error("invalid IV; EAPOL protocol version: {:?}; message: {:?}", _0, _1)]
    InvalidIv(eapol::ProtocolVersion, HandshakeMessageNumber),
    #[error("PMKSA was not yet established")]
    PmksaNotEstablished,
    #[error("invalid nonce size; expected 32 bytes, found: {:?}", _0)]
    InvalidNonceSize(usize),
    #[error("invalid key data; expected negotiated protection")]
    InvalidKeyDataProtection,
    #[error("buffer too small; required: {}, available: {}", _0, _1)]
    BufferTooSmall(usize, usize),
    #[error("error, SMK-Handshake is not supported")]
    SmkHandshakeNotSupported,
    #[error("error, negotiated protection is invalid")]
    InvalidNegotiatedProtection,
    #[error("unknown integrity algorithm for negotiated protection")]
    UnknownIntegrityAlgorithm,
    #[error("unknown keywrap algorithm for negotiated protection")]
    UnknownKeywrapAlgorithm,
    #[error("eapol error, {}", _0)]
    EapolError(eapol::Error),
    #[error("auth error, {}", _0)]
    AuthError(auth::AuthError),
    #[error("error, {}", _0)]
    GenericError(String),
}

#[macro_export]
macro_rules! rsn_ensure {
    ($cond:expr, $err:literal) => {
        if !$cond {
            return std::result::Result::Err(Error::GenericError($err.to_string()));
        }
    };
    ($cond:expr, $err:expr $(,)?) => {
        if !$cond {
            return std::result::Result::Err($err);
        }
    };
}

#[macro_export]
macro_rules! format_rsn_err {
    ($msg:literal $(,)?) => {
        // Handle $:literal as a special case to make cargo-expanded code more
        // concise in the common case.
        Error::GenericError($msg.to_string())
    };
    ($err:expr $(,)?) => ({
        Error::GenericError($err)
    });
    ($fmt:expr, $($arg:tt)*) => {
        Error::GenericError(format!($fmt, $($arg)*))
    };
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error::UnexpectedIoError(e)
    }
}

impl From<eapol::Error> for Error {
    fn from(e: eapol::Error) -> Self {
        Error::EapolError(e)
    }
}

impl From<auth::AuthError> for Error {
    fn from(e: auth::AuthError) -> Self {
        Error::AuthError(e)
    }
}
