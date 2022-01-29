// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use log::error;
use wlan_common::{
    channel::{Cbw, Channel, Phy},
    ie::*,
};

fn get_band_id(primary_channel: u8) -> fidl_common::Band {
    if primary_channel <= 14 {
        fidl_common::Band::WlanBand2Ghz
    } else {
        fidl_common::Band::WlanBand5Ghz
    }
}

pub fn get_device_band_info(
    device_info: &fidl_mlme::DeviceInfo,
    channel: u8,
) -> Option<&fidl_mlme::BandCapabilities> {
    let target = get_band_id(channel);
    device_info.bands.iter().find(|b| b.band_id == target)
}

/// Derive PHY to use for AP or Mesh role. Input config_phy and channel are required to be valid.
pub fn derive_phy_cbw_for_ap(
    device_info: &fidl_mlme::DeviceInfo,
    config_phy: &Phy,
    channel: &Channel,
) -> (Phy, Cbw) {
    let band_cap = match get_device_band_info(device_info, channel.primary) {
        None => {
            error!(
                "Could not find the device capability corresponding to the \
                 channel. Falling back to HT with 20 MHz bandwidth",
            );
            return (Phy::Ht, Cbw::Cbw20);
        }
        Some(bc) => bc,
    };

    let supported_phy = if band_cap.ht_cap.is_none() {
        Phy::Erp
    } else if band_cap.vht_cap.is_none() {
        Phy::Ht
    } else {
        Phy::Vht
    };

    let phy_to_use = std::cmp::min(*config_phy, supported_phy);

    let best_cbw = match phy_to_use {
        Phy::Hr | Phy::Erp => Cbw::Cbw20,
        Phy::Ht => {
            // Consider the input channel of this function can be Channel
            // { primary: 48, cbw: Cbw80 }, which is valid. If phy_to_use is HT, however,
            // Cbw80 becomes infeasible, and a next feasible CBW needs to be found.
            let ht_cap = parse_ht_capabilities(&band_cap.ht_cap.as_ref().unwrap().bytes[..])
                .expect("band capability needs ht_cap");
            let ht_cap_info = ht_cap.ht_cap_info;
            if ht_cap_info.chan_width_set() == ChanWidthSet::TWENTY_ONLY {
                Cbw::Cbw20
            } else {
                // Only one is feasible.
                let c = Channel::new(channel.primary, Cbw::Cbw40);
                if c.is_valid_in_us() {
                    Cbw::Cbw40
                } else {
                    Cbw::Cbw40Below
                }
            }
        }
        // TODO(porce): CBW160, CBW80P80, HEW support
        Phy::Vht | Phy::Hew => Cbw::Cbw80,
    };
    let cbw_to_use = std::cmp::min(channel.cbw, best_cbw);

    (phy_to_use, cbw_to_use)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils::*,
        wlan_common::channel::{Cbw, Channel, Phy},
    };

    #[test]
    fn band_id() {
        assert_eq!(fidl_common::Band::WlanBand2Ghz, get_band_id(1));
        assert_eq!(fidl_common::Band::WlanBand2Ghz, get_band_id(14));
        assert_eq!(fidl_common::Band::WlanBand5Ghz, get_band_id(36));
        assert_eq!(fidl_common::Band::WlanBand5Ghz, get_band_id(165));
    }

    #[test]
    fn test_get_band_id() {
        assert_eq!(fidl_common::Band::WlanBand2Ghz, get_band_id(14));
        assert_eq!(fidl_common::Band::WlanBand5Ghz, get_band_id(36));
    }

    #[test]
    fn test_get_device_band_info() {
        assert_eq!(
            fidl_common::Band::WlanBand5Ghz,
            get_device_band_info(&fake_device_info_ht(ChanWidthSet::TWENTY_FORTY), 36)
                .unwrap()
                .band_id
        );
    }

    struct UserCfg {
        phy: Phy,
        channel: Channel,
    }

    impl UserCfg {
        fn new(phy: Phy, primary_channel: u8, cbw: Cbw) -> Self {
            UserCfg { phy, channel: Channel::new(primary_channel, cbw) }
        }
    }

    #[test]
    fn test_derive_phy_cbw_for_ap() {
        // VHT config, VHT device
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw80);
            let want = (Phy::Vht, Cbw::Cbw80);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw40);
            let want = (Phy::Vht, Cbw::Cbw40);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw20);
            let want = (Phy::Vht, Cbw::Cbw20);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 40, Cbw::Cbw40Below);
            let want = (Phy::Vht, Cbw::Cbw40Below);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }

        // HT config, VHT device
        {
            let usr_cfg = UserCfg::new(Phy::Ht, 36, Cbw::Cbw40);
            let want = (Phy::Ht, Cbw::Cbw40);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Ht, 40, Cbw::Cbw40Below);
            let want = (Phy::Ht, Cbw::Cbw40Below);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Ht, 36, Cbw::Cbw40);
            let want = (Phy::Ht, Cbw::Cbw40);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Ht, 36, Cbw::Cbw20);
            let want = (Phy::Ht, Cbw::Cbw20);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }

        // VHT config, HT device
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw80);
            let want = (Phy::Ht, Cbw::Cbw40);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 40, Cbw::Cbw80);
            let want = (Phy::Ht, Cbw::Cbw40Below);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw80);
            let want = (Phy::Ht, Cbw::Cbw20);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_ONLY),
                &usr_cfg.phy,
                &usr_cfg.channel,
            );
            assert_eq!(want, got);
        }
    }
}
