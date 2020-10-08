// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::protection::Protection,
    anyhow::{bail, ensure, format_err},
    eapol,
    fidl_fuchsia_wlan_mlme::{BssDescription, DeviceInfo, SaeFrame},
    fidl_fuchsia_wlan_sme as fidl_sme,
    std::boxed::Box,
    wlan_common::ie::rsn::rsne,
    wlan_rsn::{
        self, auth, nonce::NonceReader, psk, rsna::UpdateSink, Error, NegotiatedProtection,
        ProtectionInfo,
    },
};

#[derive(Debug)]
pub struct Rsna {
    pub negotiated_protection: NegotiatedProtection,
    pub supplicant: Box<dyn Supplicant>,
}

impl PartialEq for Rsna {
    fn eq(&self, other: &Self) -> bool {
        self.negotiated_protection == other.negotiated_protection
    }
}

pub trait Supplicant: std::fmt::Debug + std::marker::Send {
    fn start(&mut self) -> Result<(), Error>;
    fn reset(&mut self);
    fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::Frame<&[u8]>,
    ) -> Result<(), Error>;
    fn on_sae_handshake_ind(&mut self, update_sink: &mut UpdateSink) -> Result<(), anyhow::Error>;
    fn on_sae_frame_rx(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: SaeFrame,
    ) -> Result<(), anyhow::Error>;
    fn on_sae_timeout(
        &mut self,
        update_sink: &mut UpdateSink,
        event_id: u64,
    ) -> Result<(), anyhow::Error>;
    fn get_auth_method(&self) -> auth::MethodName;
}

impl Supplicant for wlan_rsn::Supplicant {
    fn start(&mut self) -> Result<(), Error> {
        wlan_rsn::Supplicant::start(self)
    }

    fn reset(&mut self) {
        wlan_rsn::Supplicant::reset(self)
    }

    fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::Frame<&[u8]>,
    ) -> Result<(), Error> {
        wlan_rsn::Supplicant::on_eapol_frame(self, update_sink, frame)
    }

    fn on_sae_handshake_ind(&mut self, update_sink: &mut UpdateSink) -> Result<(), anyhow::Error> {
        wlan_rsn::Supplicant::on_sae_handshake_ind(self, update_sink)
    }

    fn on_sae_frame_rx(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: SaeFrame,
    ) -> Result<(), anyhow::Error> {
        wlan_rsn::Supplicant::on_sae_frame_rx(self, update_sink, frame)
    }

    fn on_sae_timeout(
        &mut self,
        update_sink: &mut UpdateSink,
        event_id: u64,
    ) -> Result<(), anyhow::Error> {
        wlan_rsn::Supplicant::on_sae_timeout(self, update_sink, event_id)
    }

    fn get_auth_method(&self) -> auth::MethodName {
        self.auth_method
    }
}

pub fn get_wpa2_rsna(
    device_info: &DeviceInfo,
    credential: &fidl_sme::Credential,
    bss: &BssDescription,
) -> Result<Protection, anyhow::Error> {
    let a_rsne_bytes = match bss.rsne.as_ref() {
        None => return Err(format_err!("RSNE not present in BSS")),
        Some(rsne) => &rsne[..],
    };

    // Credentials supplied and BSS is protected.
    let (_, a_rsne) = rsne::from_bytes(a_rsne_bytes)
        .map_err(|e| format_err!("invalid RSNE {:02x?}: {:?}", a_rsne_bytes, e))?;
    let s_rsne = a_rsne.derive_wpa2_s_rsne()?;
    let negotiated_protection = NegotiatedProtection::from_rsne(&s_rsne)?;

    let psk = compute_psk(credential, &bss.ssid[..])?;
    let supplicant = wlan_rsn::Supplicant::new_wpa_personal(
        // Note: There should be one Reader per device, not per SME.
        // Follow-up with improving on this.
        NonceReader::new(&device_info.mac_addr[..])?,
        psk,
        device_info.mac_addr,
        ProtectionInfo::Rsne(s_rsne),
        bss.bssid,
        ProtectionInfo::Rsne(a_rsne),
    )
    .map_err(|e| format_err!("failed to create ESS-SA: {:?}", e))?;
    Ok(Protection::Rsna(Rsna { negotiated_protection, supplicant: Box::new(supplicant) }))
}

pub fn compute_psk(
    credential: &fidl_sme::Credential,
    ssid: &[u8],
) -> Result<auth::Config, anyhow::Error> {
    match credential {
        fidl_sme::Credential::Password(password) => {
            psk::compute(&password[..], ssid).map(auth::Config::ComputedPsk)
        }
        fidl_sme::Credential::Psk(psk) => {
            ensure!(psk.len() == 32, "PSK must be 32 octets but was {}", psk.len());
            Ok(auth::Config::ComputedPsk(psk.clone().into_boxed_slice()))
        }
        fidl_sme::Credential::None(..) => bail!("expected credentials but none provided"),
        _ => bail!("unsupported credentials configuration for computing PSK"),
    }
}

pub fn get_wpa3_rsna(
    device_info: &DeviceInfo,
    credential: &fidl_sme::Credential,
    bss: &BssDescription,
) -> Result<Protection, anyhow::Error> {
    let password = match credential {
        fidl_sme::Credential::Password(pwd) => pwd.to_vec(),
        _ => bail!("Unexpected credential type"),
    };
    let a_rsne_bytes = match bss.rsne.as_ref() {
        None => return Err(format_err!("RSNE not present in BSS")),
        Some(rsne) => &rsne[..],
    };

    let (_, a_rsne) = rsne::from_bytes(a_rsne_bytes)
        .map_err(|e| format_err!("invalid RSNE {:02x?}: {:?}", a_rsne_bytes, e))?;
    let s_rsne = a_rsne.derive_wpa3_s_rsne()?;
    let negotiated_protection = NegotiatedProtection::from_rsne(&s_rsne)?;
    let supplicant = wlan_rsn::Supplicant::new_wpa_personal(
        NonceReader::new(&device_info.mac_addr[..])?,
        auth::Config::Sae {
            password,
            mac: device_info.mac_addr.clone(),
            peer_mac: bss.bssid.clone(),
        },
        device_info.mac_addr,
        ProtectionInfo::Rsne(s_rsne),
        bss.bssid,
        ProtectionInfo::Rsne(a_rsne),
    )
    .map_err(|e| format_err!("failed to create ESS-SA: {:?}", e))?;
    Ok(Protection::Rsna(Rsna { negotiated_protection, supplicant: Box::new(supplicant) }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        client::test_utils::{
            fake_protected_bss_description, fake_unprotected_bss_description,
            fake_wpa3_bss_description,
        },
        test_utils::fake_device_info,
    };
    use wlan_common::assert_variant;

    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];

    #[test]
    fn test_get_rsna_password_for_unprotected_network() {
        let bss = fake_unprotected_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Password("somepass".as_bytes().to_vec());
        let rsna = get_wpa2_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss);
        assert!(rsna.is_err(), "expect error when password is supplied for unprotected network")
    }

    #[test]
    fn test_get_rsna_no_password_for_protected_network() {
        let bss = fake_protected_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let rsna = get_wpa2_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss);
        assert!(rsna.is_err(), "expect error when no password is supplied for protected network")
    }

    #[test]
    fn test_get_rsna_psk() {
        let bss = fake_protected_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Psk(vec![0xAA; 32]);
        get_wpa2_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect("expected successful RSNA with valid PSK");
    }

    #[test]
    fn test_wpa2_get_auth_method() {
        let bss = fake_protected_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Psk(vec![0xAA; 32]);
        let protection = get_wpa2_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect("expected successful RSNA with valid PSK");
        assert_variant!(protection, Protection::Rsna(_));
        if let Protection::Rsna(rsna) = protection {
            assert_eq!(rsna.supplicant.get_auth_method(), auth::MethodName::Psk);
        }
    }

    #[test]
    fn test_get_rsna_invalid_psk() {
        let bss = fake_protected_bss_description(b"foo_bss".to_vec());
        // PSK too short
        let credential = fidl_sme::Credential::Psk(vec![0xAA; 31]);
        get_wpa2_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect_err("expected RSNA failure with invalid PSK");
    }

    #[test]
    fn test_get_rsna_wpa3() {
        let bss = fake_wpa3_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Password(vec![0xBB; 8]);
        get_wpa3_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect("expected successful SAE RSNA with valid credential");
    }

    #[test]
    fn test_wpa3_get_auth_method() {
        let bss = fake_wpa3_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Password(vec![0xBB; 8]);
        let protection = get_wpa3_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect("expected successful SAE RSNA with valid credential");
        assert_variant!(protection, Protection::Rsna(_));
        if let Protection::Rsna(rsna) = protection {
            assert_eq!(rsna.supplicant.get_auth_method(), auth::MethodName::Sae);
        }
    }

    #[test]
    fn test_get_rsna_wpa3_psk_fails() {
        let bss = fake_wpa3_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Psk(vec![0xAA; 32]);
        get_wpa3_rsna(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect_err("expected WPA3 RSNA failure with PSK");
    }
}
