// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::integrity::integrity_algorithm;
use crate::key::exchange::Key;
use crate::keywrap::keywrap_algorithm;
use crate::Error;
use eapol;
use failure::{self, bail, ensure};
use wlan_common::ie::rsn::{
    akm::Akm,
    cipher::{Cipher, GROUP_CIPHER_SUITE, TKIP},
    rsne::{RsnCapabilities, Rsne},
};

pub mod esssa;
#[cfg(test)]
pub mod test_util;

#[derive(Debug, Clone, PartialEq)]
pub struct NegotiatedRsne {
    pub group_data: Cipher,
    pub pairwise: Cipher,
    pub akm: Akm,
    pub mic_size: u16,
    // Some networks carry RSN capabilities.
    // To construct a valid RSNE, these capabilities must be tracked.
    caps: Option<RsnCapabilities>,
}

impl NegotiatedRsne {
    pub fn from_rsne(rsne: &Rsne) -> Result<NegotiatedRsne, failure::Error> {
        ensure!(rsne.group_data_cipher_suite.is_some(), Error::InvalidNegotiatedRsne);
        let group_data = rsne.group_data_cipher_suite.as_ref().unwrap();

        ensure!(rsne.pairwise_cipher_suites.len() == 1, Error::InvalidNegotiatedRsne);
        let pairwise = &rsne.pairwise_cipher_suites[0];

        ensure!(rsne.akm_suites.len() == 1, Error::InvalidNegotiatedRsne);
        let akm = &rsne.akm_suites[0];

        let mic_size = akm.mic_bytes();
        ensure!(mic_size.is_some(), Error::InvalidNegotiatedRsne);
        let mic_size = mic_size.unwrap();

        Ok(NegotiatedRsne {
            group_data: group_data.clone(),
            pairwise: pairwise.clone(),
            akm: akm.clone(),
            mic_size,
            caps: rsne.rsn_capabilities.clone(),
        })
    }

    pub fn to_full_rsne(&self) -> Rsne {
        let mut s_rsne = Rsne::new();
        s_rsne.group_data_cipher_suite = Some(self.group_data.clone());
        s_rsne.pairwise_cipher_suites = vec![self.pairwise.clone()];
        s_rsne.akm_suites = vec![self.akm.clone()];
        s_rsne.rsn_capabilities = self.caps.clone();
        s_rsne
    }
}

pub struct EncryptedKeyData<'a> {
    key_data: &'a [u8],
}

impl<'a> EncryptedKeyData<'a> {
    pub fn decrypt(&self, kek: &[u8], akm: &Akm) -> Result<Vec<u8>, failure::Error> {
        Ok(keywrap_algorithm(akm).ok_or(Error::UnsupportedAkmSuite)?.unwrap(kek, self.key_data)?)
    }
}

pub struct UnverifiedMic<'a> {
    frame: &'a eapol::KeyFrame,
}

impl<'a> UnverifiedMic<'a> {
    pub fn verify_mic(&self, kck: &[u8], akm: &Akm) -> Result<&'a eapol::KeyFrame, failure::Error> {
        // IEEE Std 802.11-2016, 12.7.2 h)
        // IEEE Std 802.11-2016, 12.7.2 b.6)
        let mic_bytes = akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)?;
        ensure!(self.frame.key_mic.len() == mic_bytes as usize, Error::InvalidMicSize);

        // If a MIC is set but the PTK was not yet derived, the MIC cannot be verified.
        let mut buf = Vec::with_capacity(self.frame.len());
        self.frame.as_bytes(true, &mut buf);
        let valid_mic = integrity_algorithm(akm).ok_or(Error::UnsupportedAkmSuite)?.verify(
            kck,
            &buf[..],
            &self.frame.key_mic[..],
        );
        ensure!(valid_mic, Error::InvalidMic);

        Ok(self.frame)
    }
}

pub enum KeyFrameKeyDataState<'a> {
    Encrypted(EncryptedKeyData<'a>),
    Unencrypted(&'a [u8]),
}

impl<'a> KeyFrameKeyDataState<'a> {
    pub fn from_frame(frame: &'a eapol::KeyFrame) -> KeyFrameKeyDataState<'a> {
        if frame.key_info.encrypted_key_data() {
            KeyFrameKeyDataState::Encrypted(EncryptedKeyData { key_data: &frame.key_data[..] })
        } else {
            KeyFrameKeyDataState::Unencrypted(&frame.key_data[..])
        }
    }
}

pub enum KeyFrameState<'a> {
    UnverifiedMic(UnverifiedMic<'a>),
    NoMic(&'a eapol::KeyFrame),
}

impl<'a> KeyFrameState<'a> {
    pub fn from_frame(frame: &'a eapol::KeyFrame) -> KeyFrameState<'a> {
        if frame.key_info.key_mic() {
            KeyFrameState::UnverifiedMic(UnverifiedMic { frame })
        } else {
            KeyFrameState::NoMic(frame)
        }
    }

    /// CAUTION: Returns the underlying frame without verifying its MIC if one is present.
    /// Only use this if you know what you are doing.
    pub fn unsafe_get_raw(&self) -> &'a eapol::KeyFrame {
        match self {
            KeyFrameState::UnverifiedMic(UnverifiedMic { frame }) => frame,
            KeyFrameState::NoMic(frame) => frame,
        }
    }
}

// EAPOL Key frames carried in this struct comply with IEEE Std 802.11-2016, 12.7.2.
#[derive(Debug, Clone, PartialEq)]
pub struct VerifiedKeyFrame<'a> {
    frame: &'a eapol::KeyFrame,
}

impl<'a> VerifiedKeyFrame<'a> {
    pub fn from_key_frame(
        frame: &'a eapol::KeyFrame,
        role: &Role,
        rsne: &NegotiatedRsne,
        key_replay_counter: u64,
    ) -> Result<VerifiedKeyFrame<'a>, failure::Error> {
        let sender = match role {
            Role::Supplicant => Role::Authenticator,
            Role::Authenticator => Role::Supplicant,
        };

        // IEEE Std 802.11-2016, 12.7.2 a)
        // IEEE Std 802.1X-2010, 11.9
        let key_descriptor = match eapol::KeyDescriptor::from_u8(frame.descriptor_type) {
            Some(eapol::KeyDescriptor::Ieee802dot11) => eapol::KeyDescriptor::Ieee802dot11,
            // Use of RC4 is deprecated.
            Some(_) => bail!(Error::InvalidKeyDescriptor(
                frame.descriptor_type,
                eapol::KeyDescriptor::Ieee802dot11,
            )),
            // Invalid value.
            None => bail!(Error::UnsupportedKeyDescriptor(frame.descriptor_type)),
        };

        // IEEE Std 802.11-2016, 12.7.2 b.1)
        let expected_version = derive_key_descriptor_version(key_descriptor, rsne);
        ensure!(
            frame.key_info.key_descriptor_version() == expected_version,
            Error::UnsupportedKeyDescriptorVersion(frame.key_info.key_descriptor_version())
        );

        // IEEE Std 802.11-2016, 12.7.2 b.2)
        // IEEE Std 802.11-2016, 12.7.2 b.4)
        match frame.key_info.key_type() {
            eapol::KEY_TYPE_PAIRWISE => {}
            eapol::KEY_TYPE_GROUP_SMK => {
                // IEEE Std 802.11-2016, 12.7.2 b.4 ii)
                ensure!(!frame.key_info.install(), Error::InvalidInstallBitGroupSmkHandshake);
            }
            _ => bail!(Error::UnsupportedKeyDerivation),
        };

        // IEEE Std 802.11-2016, 12.7.2 b.5)
        if let Role::Supplicant = sender {
            ensure!(!frame.key_info.key_ack(), Error::InvalidKeyAckBitSupplicant);
        }

        // IEEE Std 802.11-2016, 12.7.2 b.6)
        // IEEE Std 802.11-2016, 12.7.2 b.7)
        // MIC and Secure bit depend on specific key-exchange methods and can not be verified now.
        // More specifically, there are frames which can carry a MIC or secure bit but are required
        // to compute the PTK and/or GTK and thus cannot be verified up-front.

        // IEEE Std 802.11-2016, 12.7.2 b.8)
        if let Role::Authenticator = sender {
            ensure!(!frame.key_info.error(), Error::InvalidErrorBitAuthenticator);
        }

        // IEEE Std 802.11-2016, 12.7.2 b.9)
        if let Role::Authenticator = sender {
            ensure!(!frame.key_info.request(), Error::InvalidRequestBitAuthenticator);
        }

        // IEEE Std 802.11-2016, 12.7.2 b.10)
        // Encrypted key data is validated at the end once all other validations succeeded.

        // IEEE Std 802.11-2016, 12.7.2 b.11)
        ensure!(!frame.key_info.smk_message(), Error::SmkHandshakeNotSupported);

        // IEEE Std 802.11-2016, 12.7.2 c)
        match frame.key_info.key_type() {
            eapol::KEY_TYPE_PAIRWISE => match sender {
                // IEEE is somewhat vague on what is expected from the frame's key_len field.
                // IEEE Std 802.11-2016, 12.7.2 c) requires the key_len to match the PTK's
                // length, while all handshakes defined in IEEE such as
                // 4-Way Handshake (12.7.6.3) and Group Key Handshake (12.7.7.3) explicitly require
                // a value of 0 for frames sent by the Supplicant.
                // Not all vendors follow the latter requirement, such as Apple with iOS.
                // To improve interoperability, a value of 0 or the pairwise temporal key length is
                // allowed for frames sent by the Supplicant.
                Role::Supplicant if frame.key_len != 0 => {
                    let tk_bits = rsne.pairwise.tk_bits().ok_or(Error::UnsupportedCipherSuite)?;
                    let tk_len = tk_bits / 8;
                    ensure!(
                        frame.key_len == tk_len,
                        Error::InvalidKeyLength(frame.key_len, tk_len)
                    );
                }
                // Authenticator must use the pairwise cipher's key length.
                Role::Authenticator => {
                    let tk_bits = rsne.pairwise.tk_bits().ok_or(Error::UnsupportedCipherSuite)?;
                    let tk_len = tk_bits / 8;
                    ensure!(
                        frame.key_len == tk_len,
                        Error::InvalidKeyLength(frame.key_len, tk_len)
                    );
                }
                _ => {}
            },
            // IEEE Std 802.11-2016, 12.7.2 c) does not specify the expected value for frames
            // involved in exchanging the GTK. Thus, leave validation and enforcement of this
            // requirement to the selected key exchange method.
            eapol::KEY_TYPE_GROUP_SMK => {}
            _ => bail!(Error::UnsupportedKeyDerivation),
        };

        // IEEE Std 802.11-2016, 12.7.2, d)
        if key_replay_counter > 0 {
            match sender {
                // Supplicant responds to messages from the Authenticator with the same
                // key replay counter.
                Role::Supplicant => {
                    ensure!(
                        frame.key_replay_counter >= key_replay_counter,
                        Error::InvalidKeyReplayCounter(
                            frame.key_replay_counter,
                            key_replay_counter
                        )
                    );
                }
                // Authenticator must send messages with a strictly larger key replay counter.
                Role::Authenticator => {
                    ensure!(
                        frame.key_replay_counter > key_replay_counter,
                        Error::InvalidKeyReplayCounter(
                            frame.key_replay_counter,
                            key_replay_counter
                        )
                    );
                }
            }
        }

        // IEEE Std 802.11-2016, 12.7.2, e)
        // Validation is specific for the selected key exchange method.

        // IEEE Std 802.11-2016, 12.7.2, f)
        // Validation is specific for the selected key exchange method.

        // IEEE Std 802.11-2016, 12.7.2, g)
        // Validation is specific for the selected key exchange method.

        // IEEE Std 802.11-2016, 12.7.2 h)
        // IEEE Std 802.11-2016, 12.7.2 b.6)
        // See explanation for IEEE Std 802.11-2016, 12.7.2 b.7) why the MIC cannot be verified
        // here.

        // IEEE Std 802.11-2016, 12.7.2 i) & j)
        // IEEE Std 802.11-2016, 12.7.2 b.10)
        ensure!(frame.key_data_len as usize == frame.key_data.len(), Error::InvalidKeyDataLength);

        Ok(VerifiedKeyFrame { frame })
    }

    pub fn get(&self) -> KeyFrameState<'a> {
        KeyFrameState::from_frame(self.frame)
    }

    pub fn get_key_data(&self) -> KeyFrameKeyDataState<'a> {
        KeyFrameKeyDataState::from_frame(self.frame)
    }
}

// IEEE Std 802.11-2016, 12.7.2 b.1)
// Key Descriptor Version is based on the negotiated AKM, Pairwise- and Group Cipher suite.
pub fn derive_key_descriptor_version(
    key_descriptor_type: eapol::KeyDescriptor,
    rsne: &NegotiatedRsne,
) -> u16 {
    let akm = &rsne.akm;
    let pairwise = &rsne.pairwise;

    if !akm.has_known_algorithm() || !pairwise.has_known_usage() {
        return 0;
    }

    match akm.suite_type {
        1 | 2 => match key_descriptor_type {
            eapol::KeyDescriptor::Rc4 => match pairwise.suite_type {
                TKIP | GROUP_CIPHER_SUITE => 1,
                _ => 0,
            },
            eapol::KeyDescriptor::Ieee802dot11
                if pairwise.is_enhanced() || rsne.group_data.is_enhanced() =>
            {
                2
            }
            _ => 0,
        },
        // Interestingly, IEEE 802.11 does not specify any pairwise- or group cipher
        // requirements for these AKMs.
        3...6 => 3,
        _ => 0,
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum Role {
    Authenticator,
    Supplicant,
}

#[derive(Debug, PartialEq)]
pub enum SecAssocStatus {
    // TODO(hahnr): Rather than reporting wrong password as a status, report it as an error.
    WrongPassword,
    EssSaEstablished,
}

#[derive(Debug, PartialEq)]
pub enum SecAssocUpdate {
    TxEapolKeyFrame(eapol::KeyFrame),
    Key(Key),
    Status(SecAssocStatus),
}

pub type UpdateSink = Vec<SecAssocUpdate>;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rsna::{test_util, NegotiatedRsne, Role};
    use bytes::Bytes;
    use wlan_common::ie::rsn::{akm, cipher, rsne::Rsne, suite_selector::OUI};

    #[test]
    fn test_negotiated_rsne_from_rsne() {
        let rsne = make_rsne(Some(cipher::GCMP_256), vec![cipher::CCMP_128], vec![akm::PSK]);
        NegotiatedRsne::from_rsne(&rsne).expect("error, could not create negotiated RSNE");

        let rsne = make_rsne(None, vec![cipher::CCMP_128], vec![akm::PSK]);
        NegotiatedRsne::from_rsne(&rsne).expect_err("error, created negotiated RSNE");

        let rsne = make_rsne(Some(cipher::CCMP_128), vec![], vec![akm::PSK]);
        NegotiatedRsne::from_rsne(&rsne).expect_err("error, created negotiated RSNE");

        let rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![]);
        NegotiatedRsne::from_rsne(&rsne).expect_err("error, created negotiated RSNE");
    }

    // IEEE requires the key length to be zeroed in the 4-Way Handshake but some vendors send the
    // pairwise cipher's key length instead. The requirement was relaxed to improve
    // interoperability,
    #[test]
    fn test_supplicant_sends_zeroed_and_non_zeroed_key_length() {
        let rsne = NegotiatedRsne::from_rsne(&test_util::get_s_rsne())
            .expect("could not derive negotiated RSNE");
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        let (mut msg2, ptk) = env.send_msg1_to_supplicant(msg1, 12);

        // Use CCMP-128 key length.
        msg2.key_len = 16;
        msg2 = test_util::finalize_key_frame(msg2, Some(ptk.kck()));
        let result = VerifiedKeyFrame::from_key_frame(&msg2, &Role::Authenticator, &rsne, 12);
        assert!(result.is_ok(), "failed verifying message: {}", result.unwrap_err());

        msg2.key_len = 0;
        msg2 = test_util::finalize_key_frame(msg2, Some(ptk.kck()));
        let result = VerifiedKeyFrame::from_key_frame(&msg2, &Role::Authenticator, &rsne, 12);
        assert!(result.is_ok(), "failed verifying message: {}", result.unwrap_err());
    }

    // Fuchsia requires EAPOL frames sent from the Supplicant to contain a key length of either 0 or
    // the PTK's length.
    #[test]
    fn test_supplicant_sends_random_key_length() {
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        let (mut msg2, ptk) = env.send_msg1_to_supplicant(msg1, 12);
        msg2.key_len = 29;
        msg2 = test_util::finalize_key_frame(msg2, Some(ptk.kck()));

        let rsne = NegotiatedRsne::from_rsne(&test_util::get_s_rsne())
            .expect("could not derive negotiated RSNE");
        let result = VerifiedKeyFrame::from_key_frame(&msg2, &Role::Authenticator, &rsne, 12);
        assert!(result.is_err(), "successfully verified illegal message");
    }

    #[test]
    fn test_to_rsne() {
        let rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let negotiated_rsne = NegotiatedRsne::from_rsne(&rsne)
            .expect("error, could not create negotiated RSNE")
            .to_full_rsne();
        assert_eq!(negotiated_rsne, rsne);
    }

    fn make_cipher(suite_type: u8) -> cipher::Cipher {
        cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type }
    }

    fn make_akm(suite_type: u8) -> akm::Akm {
        akm::Akm { oui: Bytes::from(&OUI[..]), suite_type }
    }

    fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Rsne {
        let mut rsne = Rsne::new();
        rsne.group_data_cipher_suite = data.map(make_cipher);
        rsne.pairwise_cipher_suites = pairwise.into_iter().map(make_cipher).collect();
        rsne.akm_suites = akms.into_iter().map(make_akm).collect();
        rsne
    }

}
