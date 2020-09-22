// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use log::error;
use wlan_common::{
    channel::{Cbw, Channel, Phy},
    ie::*,
    RadioConfig,
};

fn convert_chanwidth_to_cbw(chan_width_set: ChanWidthSet) -> fidl_common::Cbw {
    if chan_width_set == ChanWidthSet::TWENTY_ONLY {
        fidl_common::Cbw::Cbw20
    } else {
        fidl_common::Cbw::Cbw40Below
    }
}

fn convert_secchan_offset_to_cbw(secchan_offset: SecChanOffset) -> fidl_common::Cbw {
    if secchan_offset == SecChanOffset::SECONDARY_ABOVE {
        fidl_common::Cbw::Cbw40
    } else if secchan_offset == SecChanOffset::SECONDARY_BELOW {
        fidl_common::Cbw::Cbw40Below
    } else {
        fidl_common::Cbw::Cbw20
    }
}

fn convert_vht_segments_to_cbw(seg0: u8, seg1: u8) -> fidl_common::Cbw {
    // See IEEE Std 802.11-2016, Table 9-253
    let gap = if seg0 >= seg1 { seg0 - seg1 } else { seg1 - seg0 };
    if seg1 == 0 {
        fidl_common::Cbw::Cbw80
    } else if gap == 8 {
        fidl_common::Cbw::Cbw160
    } else if gap > 16 {
        fidl_common::Cbw::Cbw80P80
    } else {
        fidl_common::Cbw::Cbw80
    }
}

fn derive_cbw_ht(client_ht_cap: &HtCapabilities, bss_ht_op: &HtOperation) -> fidl_common::Cbw {
    let client_ht_cap_info = client_ht_cap.ht_cap_info;
    let client_cbw = convert_chanwidth_to_cbw(client_ht_cap_info.chan_width_set());
    let ap_cbw =
        convert_secchan_offset_to_cbw({ bss_ht_op.ht_op_info_head }.secondary_chan_offset());
    std::cmp::min(client_cbw, ap_cbw)
}

fn derive_cbw_vht(
    client_ht_cap: &HtCapabilities,
    _client_vht_cap: &VhtCapabilities,
    bss_ht_op: &HtOperation,
    bss_vht_op: &VhtOperation,
) -> fidl_common::Cbw {
    // Derive CBW from AP's VHT IEs
    let ap_cbw = if bss_vht_op.vht_cbw == VhtChannelBandwidth::CBW_80_160_80P80 {
        convert_vht_segments_to_cbw(bss_vht_op.center_freq_seg0, bss_vht_op.center_freq_seg1)
    } else {
        derive_cbw_ht(client_ht_cap, bss_ht_op)
    };

    // TODO(fxbug.dev/29000): Support CBW160 and CBW80P80
    // See IEEE Std 802.11-2016 table 9-250 for full decoding
    let client_cbw = fidl_common::Cbw::Cbw80;

    std::cmp::min(client_cbw, ap_cbw)
}

fn get_band_id(primary_chan: u8) -> fidl_common::Band {
    if primary_chan <= 14 {
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

/// Derive PHY and CBW for Client role
pub fn derive_phy_cbw(
    bss: &fidl_mlme::BssDescription,
    device_info: &fidl_mlme::DeviceInfo,
    radio_cfg: &RadioConfig,
) -> (fidl_common::Phy, fidl_common::Cbw) {
    let band_cap = match get_device_band_info(device_info, bss.chan.primary) {
        None => {
            error!(
                "Could not find the device capability corresponding to the \
                 channel {} of the selected AP {:?} \
                 Falling back to ERP with 20 MHz bandwidth",
                bss.chan.primary, bss.bssid
            );
            // Fallback to a common ground of Fuchsia
            return (fidl_common::Phy::Erp, fidl_common::Cbw::Cbw20);
        }
        Some(bc) => bc,
    };

    let supported_phy = if band_cap.ht_cap.is_none() || bss.ht_cap.is_none() || bss.ht_op.is_none()
    {
        fidl_common::Phy::Erp
    } else if band_cap.vht_cap.is_none() || bss.vht_cap.is_none() || bss.vht_op.is_none() {
        fidl_common::Phy::Ht
    } else {
        fidl_common::Phy::Vht
    };

    let phy_to_use = match radio_cfg.phy {
        None => supported_phy,
        Some(override_phy) => std::cmp::min(override_phy.to_fidl(), supported_phy),
    };

    // Safe to unwrap below because phy_to_use guarantees that IEs exist.
    // TODO(38205): Clean up this part to remove all the expect(...).
    let best_cbw = match phy_to_use {
        fidl_common::Phy::Hr => fidl_common::Cbw::Cbw20,
        fidl_common::Phy::Erp => fidl_common::Cbw::Cbw20,
        fidl_common::Phy::Ht => derive_cbw_ht(
            &parse_ht_capabilities(&band_cap.ht_cap.as_ref().unwrap().bytes[..])
                .expect("band capability needs ht_cap"),
            &parse_ht_operation(&bss.ht_op.as_ref().unwrap().bytes[..])
                .expect("bss is expected to have ht_op"),
        ),
        fidl_common::Phy::Vht | fidl_common::Phy::Hew => derive_cbw_vht(
            &parse_ht_capabilities(&band_cap.ht_cap.as_ref().unwrap().bytes[..])
                .expect("band capability needs ht_cap"),
            &parse_vht_capabilities(&band_cap.vht_cap.as_ref().unwrap().bytes[..])
                .expect("band capability needs vht_cap"),
            &parse_ht_operation(&bss.ht_op.as_ref().unwrap().bytes[..]).expect("bss needs ht_op"),
            &parse_vht_operation(&bss.vht_op.as_ref().unwrap().bytes[..])
                .expect("bss needs vht_op"),
        ),
    };

    let cbw_to_use = match radio_cfg.cbw {
        None => best_cbw,
        Some(override_cbw) => {
            let (cbw, _) = override_cbw.to_fidl();
            std::cmp::min(best_cbw, cbw)
        }
    };
    (phy_to_use, cbw_to_use)
}

/// Derive PHY to use for AP or Mesh role. Input config_phy and chan are required to be valid.
pub fn derive_phy_cbw_for_ap(
    device_info: &fidl_mlme::DeviceInfo,
    config_phy: &Phy,
    chan: &Channel,
) -> (Phy, Cbw) {
    let band_cap = match get_device_band_info(device_info, chan.primary) {
        None => {
            error!(
                "Could not find the device capability corresponding to the \
                 channel {} Falling back to HT with 20 MHz bandwidth",
                chan.primary
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
            // Consider the input chan of this function can be Channel
            // { primary: 48, cbw: Cbw80 }, which is valid. If phy_to_use is HT, however,
            // Cbw80 becomes infeasible, and a next feasible CBW needs to be found.
            let ht_cap = parse_ht_capabilities(&band_cap.ht_cap.as_ref().unwrap().bytes[..])
                .expect("band capability needs ht_cap");
            let ht_cap_info = ht_cap.ht_cap_info;
            if ht_cap_info.chan_width_set() == ChanWidthSet::TWENTY_ONLY {
                Cbw::Cbw20
            } else {
                // Only one is feasible.
                let c = Channel::new(chan.primary, Cbw::Cbw40);
                if c.is_valid() {
                    Cbw::Cbw40
                } else {
                    Cbw::Cbw40Below
                }
            }
        }
        // TODO(porce): CBW160, CBW80P80, HEW support
        Phy::Vht | Phy::Hew => Cbw::Cbw80,
    };
    let cbw_to_use = std::cmp::min(chan.cbw, best_cbw);

    (phy_to_use, cbw_to_use)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{client::test_utils::fake_vht_bss_description, test_utils::*},
        wlan_common::{
            channel::{Cbw, Channel, Phy},
            RadioConfig,
        },
    };

    #[test]
    fn band_id() {
        assert_eq!(fidl_common::Band::WlanBand2Ghz, get_band_id(1));
        assert_eq!(fidl_common::Band::WlanBand2Ghz, get_band_id(14));
        assert_eq!(fidl_common::Band::WlanBand5Ghz, get_band_id(36));
        assert_eq!(fidl_common::Band::WlanBand5Ghz, get_band_id(165));
    }

    #[test]
    fn test_convert_chanwidth_to_cbw() {
        assert_eq!(fidl_common::Cbw::Cbw20, convert_chanwidth_to_cbw(ChanWidthSet::TWENTY_ONLY));
        assert_eq!(
            fidl_common::Cbw::Cbw40Below,
            convert_chanwidth_to_cbw(ChanWidthSet::TWENTY_FORTY)
        );
    }

    #[test]
    fn test_convert_secchan_offset_to_cbw() {
        assert_eq!(
            fidl_common::Cbw::Cbw20,
            convert_secchan_offset_to_cbw(SecChanOffset::SECONDARY_NONE)
        );
        assert_eq!(
            fidl_common::Cbw::Cbw40,
            convert_secchan_offset_to_cbw(SecChanOffset::SECONDARY_ABOVE)
        );
        assert_eq!(
            fidl_common::Cbw::Cbw40Below,
            convert_secchan_offset_to_cbw(SecChanOffset::SECONDARY_BELOW)
        );
    }

    #[test]
    fn test_convert_vht_segments_to_cbw() {
        assert_eq!(fidl_common::Cbw::Cbw80, convert_vht_segments_to_cbw(255, 0));
        assert_eq!(fidl_common::Cbw::Cbw160, convert_vht_segments_to_cbw(255, 247));
        assert_eq!(fidl_common::Cbw::Cbw80P80, convert_vht_segments_to_cbw(255, 200));
        assert_eq!(fidl_common::Cbw::Cbw80, convert_vht_segments_to_cbw(255, 250));
    }

    #[test]
    fn test_derive_cbw_ht() {
        {
            let want = fidl_common::Cbw::Cbw20;
            let got = derive_cbw_ht(
                &fake_ht_cap_chanwidth(ChanWidthSet::TWENTY_FORTY),
                &fake_ht_op_sec_offset(SecChanOffset::SECONDARY_NONE),
            );
            assert_eq!(want, got);
        }
        {
            let want = fidl_common::Cbw::Cbw20;
            let got = derive_cbw_ht(
                &fake_ht_cap_chanwidth(ChanWidthSet::TWENTY_ONLY),
                &fake_ht_op_sec_offset(SecChanOffset::SECONDARY_ABOVE),
            );
            assert_eq!(want, got);
        }
        {
            let want = fidl_common::Cbw::Cbw40;
            let got = derive_cbw_ht(
                &fake_ht_cap_chanwidth(ChanWidthSet::TWENTY_FORTY),
                &fake_ht_op_sec_offset(SecChanOffset::SECONDARY_ABOVE),
            );
            assert_eq!(want, got);
        }
        {
            let want = fidl_common::Cbw::Cbw40Below;
            let got = derive_cbw_ht(
                &fake_ht_cap_chanwidth(ChanWidthSet::TWENTY_FORTY),
                &fake_ht_op_sec_offset(SecChanOffset::SECONDARY_BELOW),
            );
            assert_eq!(want, got);
        }
    }

    #[test]
    fn test_derive_cbw_vht() {
        {
            let want = fidl_common::Cbw::Cbw80;
            let got = derive_cbw_vht(
                &fake_ht_cap_chanwidth(ChanWidthSet::TWENTY_FORTY),
                &fake_vht_capabilities(),
                &fake_ht_op_sec_offset(SecChanOffset::SECONDARY_ABOVE),
                &fake_vht_op_cbw(VhtChannelBandwidth::CBW_80_160_80P80),
            );
            assert_eq!(want, got);
        }
        {
            let want = fidl_common::Cbw::Cbw40;
            let got = derive_cbw_vht(
                &fake_ht_cap_chanwidth(ChanWidthSet::TWENTY_FORTY),
                &fake_vht_capabilities(),
                &fake_ht_op_sec_offset(SecChanOffset::SECONDARY_ABOVE),
                &fake_vht_op_cbw(VhtChannelBandwidth::CBW_20_40),
            );
            assert_eq!(want, got);
        }
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

    #[test]
    fn test_derive_phy_cbw() {
        {
            let want = (fidl_common::Phy::Vht, fidl_common::Cbw::Cbw80);
            let got = derive_phy_cbw(
                &fake_vht_bss_description(),
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &RadioConfig::default(),
            );
            assert_eq!(want, got);
        }
        {
            let want = (fidl_common::Phy::Ht, fidl_common::Cbw::Cbw40);
            let got = derive_phy_cbw(
                &fake_vht_bss_description(),
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &fake_overrider(fidl_common::Phy::Ht, fidl_common::Cbw::Cbw80),
            );
            assert_eq!(want, got);
        }
        {
            let want = (fidl_common::Phy::Ht, fidl_common::Cbw::Cbw20);
            let got = derive_phy_cbw(
                &fake_vht_bss_description(),
                &fake_device_info_ht(ChanWidthSet::TWENTY_ONLY),
                &fake_overrider(fidl_common::Phy::Vht, fidl_common::Cbw::Cbw80),
            );
            assert_eq!(want, got);
        }
    }

    struct UserCfg {
        phy: Phy,
        chan: Channel,
    }

    impl UserCfg {
        fn new(phy: Phy, primary_chan: u8, cbw: Cbw) -> Self {
            UserCfg { phy, chan: Channel::new(primary_chan, cbw) }
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
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw40);
            let want = (Phy::Vht, Cbw::Cbw40);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw20);
            let want = (Phy::Vht, Cbw::Cbw20);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 40, Cbw::Cbw40Below);
            let want = (Phy::Vht, Cbw::Cbw40Below);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.chan,
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
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Ht, 40, Cbw::Cbw40Below);
            let want = (Phy::Ht, Cbw::Cbw40Below);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_vht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Ht, 36, Cbw::Cbw40);
            let want = (Phy::Ht, Cbw::Cbw40);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Ht, 36, Cbw::Cbw20);
            let want = (Phy::Ht, Cbw::Cbw20);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.chan,
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
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 40, Cbw::Cbw80);
            let want = (Phy::Ht, Cbw::Cbw40Below);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_FORTY),
                &usr_cfg.phy,
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
        {
            let usr_cfg = UserCfg::new(Phy::Vht, 36, Cbw::Cbw80);
            let want = (Phy::Ht, Cbw::Cbw20);
            let got = derive_phy_cbw_for_ap(
                &fake_device_info_ht(ChanWidthSet::TWENTY_ONLY),
                &usr_cfg.phy,
                &usr_cfg.chan,
            );
            assert_eq!(want, got);
        }
    }
}
