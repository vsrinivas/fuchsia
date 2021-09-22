// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg_attr(feature = "benchmarks", feature(test))]
// Remove once Cipher and AKM *_bits() were replaced with *_len() calls.
#![allow(deprecated)]
// This crate doesn't comply with all 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use thiserror::{self, Error};

// TODO(hahnr): Limit exports and rearrange modules.

mod aes;
pub mod auth;
mod integrity;
pub mod key;
mod key_data;
mod keywrap;
pub mod nonce;
mod prf;
pub mod rsna;

use {
    crate::{
        aes::AesError,
        key::exchange::{
            self,
            handshake::{fourway, group_key, HandshakeMessageNumber},
        },
        rsna::{esssa::EssSa, Role, UpdateSink},
    },
    eapol,
    fidl_fuchsia_wlan_mlme::{EapolResultCode, SaeFrame},
    log::warn,
    std::sync::{Arc, Mutex},
    wlan_common::ie::{
        rsn::{
            cipher::Cipher,
            rsne::{self, Rsne},
        },
        wpa::WpaIe,
    },
    zerocopy::ByteSlice,
};

pub use crate::{
    auth::psk,
    key::gtk::{self, GtkProvider},
    key::igtk::{self, IgtkProvider},
    rsna::NegotiatedProtection,
};

#[derive(Debug)]
pub struct Supplicant {
    auth_method: auth::Method,
    esssa: EssSa,
    pub auth_cfg: auth::Config,
}

/// Any information (i.e. info elements) used to negotiate protection on an RSN.
#[derive(Debug, Clone, PartialEq)]
pub enum ProtectionInfo {
    Rsne(Rsne),
    LegacyWpa(WpaIe),
}

fn extract_sae_key_helper(update_sink: &UpdateSink) -> Option<Vec<u8>> {
    for update in &update_sink[..] {
        if let rsna::SecAssocUpdate::Key(key::exchange::Key::Pmk(pmk)) = update {
            return Some(pmk.clone());
        }
    }
    None
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

        let auth_method = auth::Method::from_config(auth_cfg.clone())?;
        let pmk = match auth_cfg.clone() {
            auth::Config::ComputedPsk(psk) => Some(psk.to_vec()),
            _ => None,
        };
        let esssa = EssSa::new(
            Role::Supplicant,
            pmk,
            negotiated_protection,
            exchange::Config::FourWayHandshake(fourway::Config::new(
                Role::Supplicant,
                s_addr,
                s_protection,
                a_addr,
                a_protection,
                nonce_rdr,
                None,
                None,
            )?),
            gtk_exch_cfg,
        )?;

        Ok(Supplicant { auth_method, esssa, auth_cfg })
    }

    /// Starts the Supplicant. A Supplicant must be started after its creation and everytime it was
    /// reset.
    pub fn start(&mut self) -> Result<(), Error> {
        // The Supplicant always waits for Authenticator to initiate and does not yet support EAPOL
        // request frames. Thus, all updates can be ignored.
        let mut dead_update_sink = vec![];
        self.esssa.initiate(&mut dead_update_sink)
    }

    /// Resets all established Security Associations and invalidates all derived keys in this ESSSA.
    /// The Supplicant must be reset or destroyed when the underlying 802.11 association terminates.
    /// The replay counter is also reset.
    pub fn reset(&mut self) {
        // The replay counter must be reset so subsequent associations are not ignored.
        self.esssa.reset_replay_counter();
        self.esssa.reset_security_associations();
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

    pub fn on_eapol_conf(
        &mut self,
        update_sink: &mut UpdateSink,
        result: EapolResultCode,
    ) -> Result<(), Error> {
        self.esssa.on_eapol_conf(update_sink, result)
    }

    pub fn on_eapol_key_frame_timeout(
        &mut self,
        update_sink: &mut UpdateSink,
    ) -> Result<(), Error> {
        self.esssa.on_key_frame_timeout(update_sink)
    }

    fn extract_sae_key(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        if let Some(pmk) = extract_sae_key_helper(&update_sink) {
            self.esssa.on_pmk_available(update_sink, pmk)?;
        }
        Ok(())
    }

    pub fn on_pmk_available(
        &mut self,
        update_sink: &mut UpdateSink,
        pmk: &[u8],
        pmkid: &[u8],
    ) -> Result<(), Error> {
        let mut updates = UpdateSink::new();
        self.auth_method.on_pmk_available(pmk, pmkid, &mut updates)?;
        self.extract_sae_key(update_sink)
    }

    pub fn on_sae_handshake_ind(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        self.auth_method.on_sae_handshake_ind(update_sink).map_err(Error::AuthError)
    }

    pub fn on_sae_frame_rx(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: SaeFrame,
    ) -> Result<(), Error> {
        self.auth_method.on_sae_frame_rx(update_sink, frame).map_err(Error::AuthError)?;
        self.extract_sae_key(update_sink)
    }

    pub fn on_sae_timeout(
        &mut self,
        update_sink: &mut UpdateSink,
        event_id: u64,
    ) -> Result<(), Error> {
        self.auth_method.on_sae_timeout(update_sink, event_id).map_err(Error::AuthError)
    }
}

#[derive(Debug)]
pub struct Authenticator {
    auth_method: auth::Method,
    esssa: EssSa,
    pub auth_cfg: auth::Config,
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
        let auth_cfg = auth::Config::ComputedPsk(psk.clone());
        let auth_method = auth::Method::from_config(auth_cfg.clone())?;
        let esssa = EssSa::new(
            Role::Authenticator,
            Some(psk.to_vec()),
            negotiated_protection,
            exchange::Config::FourWayHandshake(fourway::Config::new(
                Role::Authenticator,
                s_addr,
                s_protection,
                a_addr,
                a_protection,
                nonce_rdr,
                Some(gtk_provider),
                None,
            )?),
            // Group-Key Handshake does not support Authenticator role yet.
            None,
        )?;

        Ok(Authenticator { auth_method, esssa, auth_cfg })
    }

    /// WPA3 Authenticator which supports 4-Way Handshake.
    /// The Authenticator does not support GTK rotations.
    pub fn new_wpa3(
        nonce_rdr: Arc<nonce::NonceReader>,
        gtk_provider: Arc<Mutex<gtk::GtkProvider>>,
        igtk_provider: Arc<Mutex<igtk::IgtkProvider>>,
        password: Vec<u8>,
        s_addr: [u8; 6],
        s_protection: ProtectionInfo,
        a_addr: [u8; 6],
        a_protection: ProtectionInfo,
    ) -> Result<Authenticator, anyhow::Error> {
        let negotiated_protection = NegotiatedProtection::from_protection(&s_protection)?;
        let auth_cfg =
            auth::Config::Sae { password, mac: a_addr.clone(), peer_mac: s_addr.clone() };
        let auth_method = auth::Method::from_config(auth_cfg.clone())?;

        let esssa = EssSa::new(
            Role::Authenticator,
            None,
            negotiated_protection,
            exchange::Config::FourWayHandshake(fourway::Config::new(
                Role::Authenticator,
                s_addr,
                s_protection,
                a_addr,
                a_protection,
                nonce_rdr,
                Some(gtk_provider),
                Some(igtk_provider),
            )?),
            // Group-Key Handshake does not support Authenticator role yet.
            None,
        )?;

        Ok(Authenticator { auth_cfg, esssa, auth_method })
    }

    pub fn get_negotiated_protection(&self) -> &NegotiatedProtection {
        &self.esssa.negotiated_protection
    }

    /// Resets all established Security Associations and invalidates all derived keys in this ESSSA.
    /// The Authenticator must be reset or destroyed when the underlying 802.11 association
    /// terminates. The replay counter is also reset.
    pub fn reset(&mut self) {
        self.esssa.reset_replay_counter();
        self.esssa.reset_security_associations();

        // Recreate auth_method to reset its state
        match auth::Method::from_config(self.auth_cfg.clone()) {
            Ok(auth_method) => self.auth_method = auth_method,
            Err(e) => warn!("Unable to recreate auth::Method: {}", e),
        }
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

    pub fn on_eapol_conf(
        &mut self,
        update_sink: &mut UpdateSink,
        result: EapolResultCode,
    ) -> Result<(), Error> {
        self.esssa.on_eapol_conf(update_sink, result)
    }

    fn extract_sae_key(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        if let Some(pmk) = extract_sae_key_helper(&update_sink) {
            self.esssa.on_pmk_available(update_sink, pmk)?;
        }
        Ok(())
    }

    pub fn on_sae_handshake_ind(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        self.auth_method.on_sae_handshake_ind(update_sink).map_err(Error::AuthError)
    }

    pub fn on_sae_frame_rx(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: SaeFrame,
    ) -> Result<(), Error> {
        self.auth_method.on_sae_frame_rx(update_sink, frame).map_err(Error::AuthError)?;
        self.extract_sae_key(update_sink)
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
    #[error("error deriving IGTK; unsupported cipher suite")]
    IgtkHierarchyUnsupportedCipherError,
    #[error("no GtkProvider for Authenticator")]
    MissingGtkProvider,
    #[error("no IgtkProvider for Authenticator with Management Frame Protection support")]
    MissingIgtkProvider,
    #[error("invalid supplicant protection: {}", _0)]
    InvalidSupplicantProtection(String),
    #[error("required group mgmt cipher does not match IgtkProvider cipher: {:?} != {:?}", _0, _1)]
    WrongIgtkProviderCipher(Cipher, Cipher),
    #[error("error determining group mgmt cipher: {:?} != {:?}", _0, _1)]
    GroupMgmtCipherMismatch(Cipher, Cipher),
    #[error("client requires management frame protection and ap is not capable")]
    MgmtFrameProtectionRequiredByClient,
    #[error("ap requires management frame protection and client is not capable")]
    MgmtFrameProtectionRequiredByAp,
    #[error("client set MFP required bit without setting MFP capability bit")]
    InvalidClientMgmtFrameProtectionCapabilityBit,
    #[error("ap set MFP required bit without setting MFP capability bit")]
    InvalidApMgmtFrameProtectionCapabilityBit,
    #[error("AES operation failed: {}", _0)]
    Aes(AesError),
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
    #[error("cannot initiate Supplicant in current EssSa state")]
    UnexpectedEsssaInitiation,
    #[error("too many key frame transmission retries")]
    TooManyKeyFrameRetries,
    #[error("key frame transmission failed")]
    KeyFrameTransmissionFailed,
    #[error("no key frame transmission confirm received; dropped {} pending updates", _0)]
    NoKeyFrameTransmissionConfirm(usize),
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
    #[error("rsne error, {}", _0)]
    RsneError(rsne::Error),
    #[error("rsne invalid subset, supplicant: {:?}, authenticator: {:?}", _0, _1)]
    RsneInvalidSubset(rsne::Rsne, rsne::Rsne),
    #[error("error, {}", _0)]
    GenericError(String),
}

impl PartialEq for Error {
    fn eq(&self, other: &Self) -> bool {
        format!("{:?}", self) == format!("{:?}", other)
    }
}
impl Eq for Error {}

impl From<AesError> for Error {
    fn from(error: AesError) -> Self {
        Error::Aes(error)
    }
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

impl From<rsne::Error> for Error {
    fn from(e: rsne::Error) -> Self {
        Error::RsneError(e)
    }
}

#[cfg(test)]
mod tests {
    use crate::key::exchange::Key;
    use crate::rsna::{test_util, SecAssocStatus, SecAssocUpdate};
    use wlan_common::assert_variant;

    #[test]
    fn supplicant_extract_sae_key() {
        let mut supplicant = test_util::get_wpa3_supplicant();
        let mut dummy_update_sink = vec![
            SecAssocUpdate::ScheduleSaeTimeout(123),
            SecAssocUpdate::Key(Key::Pmk(vec![1, 2, 3, 4, 5, 6, 7, 8])),
        ];
        supplicant.extract_sae_key(&mut dummy_update_sink).expect("Failed to extract key");
        // ESSSA should register the new PMK and report this.
        assert_eq!(
            dummy_update_sink,
            vec![
                SecAssocUpdate::ScheduleSaeTimeout(123),
                SecAssocUpdate::Key(Key::Pmk(vec![1, 2, 3, 4, 5, 6, 7, 8])),
                SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished),
            ]
        );
    }

    #[test]
    fn supplicant_extract_sae_key_no_key() {
        let mut supplicant = test_util::get_wpa3_supplicant();
        let mut dummy_update_sink = vec![SecAssocUpdate::ScheduleSaeTimeout(123)];
        supplicant.extract_sae_key(&mut dummy_update_sink).expect("Failed to extract key");
        // No PMK means no new update.
        assert_eq!(dummy_update_sink, vec![SecAssocUpdate::ScheduleSaeTimeout(123)]);
    }

    #[test]
    fn authenticator_extract_sae_key() {
        let mut authenticator = test_util::get_wpa3_authenticator();
        let mut dummy_update_sink = vec![
            SecAssocUpdate::ScheduleSaeTimeout(123),
            SecAssocUpdate::Key(Key::Pmk(vec![1, 2, 3, 4, 5, 6, 7, 8])),
        ];
        authenticator.extract_sae_key(&mut dummy_update_sink).expect("Failed to extract key");
        // ESSSA should register the new PMK and report this.
        assert_eq!(
            &dummy_update_sink[0..3],
            vec![
                SecAssocUpdate::ScheduleSaeTimeout(123),
                SecAssocUpdate::Key(Key::Pmk(vec![1, 2, 3, 4, 5, 6, 7, 8])),
                SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished),
            ]
            .as_slice(),
        );

        // ESSSA should also transmit an EAPOL frame since this is the Authenticator.
        assert_variant!(&dummy_update_sink[3], &SecAssocUpdate::TxEapolKeyFrame { .. });
    }

    #[test]
    fn authenticator_extract_sae_key_no_key() {
        let mut authenticator = test_util::get_wpa3_authenticator();
        let mut dummy_update_sink = vec![SecAssocUpdate::ScheduleSaeTimeout(123)];
        authenticator.extract_sae_key(&mut dummy_update_sink).expect("Failed to extract key");
        // No PMK means no new update.
        assert_eq!(dummy_update_sink, vec![SecAssocUpdate::ScheduleSaeTimeout(123)]);
    }
}
