// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme};
use crate::client::{ConnectPhyParams};
use log::{error};

use crate::DeviceInfo;


fn convert_chanwidth_to_cbw(chan_width_set: u8) -> fidl_mlme::Cbw {
    if chan_width_set == fidl_mlme::ChanWidthSet::TwentyOnly as u8 {
        fidl_mlme::Cbw::Cbw20
    } else {
        fidl_mlme::Cbw::Cbw40Below
    }
}

fn convert_secchan_offset_to_cbw(secchan_offset: u8) -> fidl_mlme::Cbw {
    if secchan_offset == fidl_mlme::SecChanOffset::SecondaryAbove as u8 {
        fidl_mlme::Cbw::Cbw40
    } else if secchan_offset == fidl_mlme::SecChanOffset::SecondaryBelow as u8 {
        fidl_mlme::Cbw::Cbw40Below
    } else {
        fidl_mlme::Cbw::Cbw20
    }
}

fn convert_vht_segments_to_cbw(seg0: u8, seg1:u8) -> fidl_mlme::Cbw {
    // See IEEE Std 802.11-2016, Table 9-253
    let gap = if seg0 >= seg1 { seg0 - seg1 } else { seg1 - seg0 };
    if seg1 == 0 {
        fidl_mlme::Cbw::Cbw80
    } else if gap == 8 {
        fidl_mlme::Cbw::Cbw160
    } else if gap > 16 {
        fidl_mlme::Cbw::Cbw80P80
    } else {
        fidl_mlme::Cbw::Cbw80
    }
}

fn derive_cbw_ht(client_ht_cap: &fidl_mlme::HtCapabilities,
                 bss_ht_op: &fidl_mlme::HtOperation)
    -> fidl_mlme::Cbw
{
    let ap_cbw = convert_secchan_offset_to_cbw(bss_ht_op.ht_op_info.secondary_chan_offset);
    let client_cbw = convert_chanwidth_to_cbw(client_ht_cap.ht_cap_info.chan_width_set);
    std::cmp::min(client_cbw, ap_cbw)
}

fn derive_cbw_vht(client_ht_cap: &fidl_mlme::HtCapabilities,
                  _client_vht_cap: &fidl_mlme::VhtCapabilities,
                  bss_ht_op: &fidl_mlme::HtOperation,
                  bss_vht_op: &fidl_mlme::VhtOperation)
    -> fidl_mlme::Cbw
{
    // Derive CBW from AP's VHT IEs
    let ap_cbw = if bss_vht_op.vht_cbw == fidl_mlme::VhtCbw::Cbw8016080P80 as u8 {
        convert_vht_segments_to_cbw(bss_vht_op.center_freq_seg0, bss_vht_op.center_freq_seg1)
    } else {
        derive_cbw_ht(client_ht_cap, bss_ht_op)
    };

    // TODO(NET-1575): Support CBW160 and CBW80P80
    // See IEEE Std 802.11-2016 table 9-250 for full decoding
    let client_cbw = fidl_mlme::Cbw::Cbw80;

    std::cmp::min(client_cbw, ap_cbw)
}

fn get_band_id(primary_chan: u8) -> fidl_mlme::Band {
    if primary_chan <= 14 {
        fidl_mlme::Band::WlanBand2Ghz
    } else {
        fidl_mlme::Band::WlanBand5Ghz
    }
}

pub fn get_device_band_info<'a>(device_info: &'a DeviceInfo, channel: u8)
    -> Option<&'a fidl_mlme::BandCapabilities>
{
    let target = get_band_id(channel);
    device_info.bands.iter().find(|b| b.band_id == target)
}

pub fn derive_phy_cbw(bss: &fidl_mlme::BssDescription,
                      device_info: &DeviceInfo,
                      params: &ConnectPhyParams)
    -> (fidl_mlme::Phy, fidl_mlme::Cbw)
{
    let band_cap = match get_device_band_info(device_info, bss.chan.primary) {
        None => {
            error!("Could not find the device capability corresponding to the \
                   channel {} of the selected AP {:?} \
                   Falling back to ERP with 20 MHz bandwidth", bss.chan.primary, bss.bssid);
            // Fallback to a common ground of Fuchsia
            return (fidl_mlme::Phy::Erp, fidl_mlme::Cbw::Cbw20);
        }
        Some(bc) => bc,
    };

    let supported_phy =
        if band_cap.ht_cap.is_none() || bss.ht_cap.is_none() || bss.ht_op.is_none() {
            fidl_mlme::Phy::Erp
        } else if band_cap.vht_cap.is_none() || bss.vht_cap.is_none() || bss.vht_op.is_none() {
            fidl_mlme::Phy::Ht
        } else {
            fidl_mlme::Phy::Vht
        };

    let phy_to_use = match params.phy {
        None => supported_phy,
        Some(override_phy) => std::cmp::min(override_phy, supported_phy),
    };

    let best_cbw = match phy_to_use {
        fidl_mlme::Phy::Hr => fidl_mlme::Cbw::Cbw20,
        fidl_mlme::Phy::Erp => fidl_mlme::Cbw::Cbw20,
        fidl_mlme::Phy::Ht => { 
            derive_cbw_ht(&band_cap.ht_cap.as_ref().expect("band capability needs ht_cap"),
                          &bss.ht_op.as_ref().expect("bss is expected to have ht_op"))
        },
        fidl_mlme::Phy::Vht | fidl_mlme::Phy::Hew => {
            derive_cbw_vht(&band_cap.ht_cap.as_ref().expect("band capability needs ht_cap"),
                           &band_cap.vht_cap.as_ref().expect("band capability needs vht_cap"),
                           &bss.ht_op.as_ref().expect("bss needs ht_op"),
                           &bss.vht_op.as_ref().expect("bss needs vht_op"))
        },
    };

    let cbw_to_use = match params.cbw {
        None => best_cbw,
        Some(override_cbw) => std::cmp::min(best_cbw, override_cbw),
    };
    (phy_to_use, cbw_to_use)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_wlan_mlme as fidl_mlme;
    use crate::client::test_utils::{fake_5ghz_band_capabilities, fake_ht_capabilities,
                                    fake_ht_operation, fake_vht_bss_description,
                                    fake_vht_capabilities, fake_vht_operation};
    use crate::client::{ConnectPhyParams};

    #[test]
    fn band_id() {
        assert_eq!(fidl_mlme::Band::WlanBand2Ghz, get_band_id(1));
        assert_eq!(fidl_mlme::Band::WlanBand2Ghz, get_band_id(14));
        assert_eq!(fidl_mlme::Band::WlanBand5Ghz, get_band_id(36));
        assert_eq!(fidl_mlme::Band::WlanBand5Ghz, get_band_id(165));
    }

    #[test]
    fn test_convert_chanwidth_to_cbw() {
        assert_eq!(fidl_mlme::Cbw::Cbw20,
                   convert_chanwidth_to_cbw(fidl_mlme::ChanWidthSet::TwentyOnly as u8));
        assert_eq!(fidl_mlme::Cbw::Cbw40Below,
                   convert_chanwidth_to_cbw(fidl_mlme::ChanWidthSet::TwentyForty as u8));
    }

    #[test]
    fn test_convert_secchan_offset_to_cbw() {
        assert_eq!(fidl_mlme::Cbw::Cbw20,
                   convert_secchan_offset_to_cbw(fidl_mlme::SecChanOffset::SecondaryNone as u8));
        assert_eq!(fidl_mlme::Cbw::Cbw40,
                   convert_secchan_offset_to_cbw(fidl_mlme::SecChanOffset::SecondaryAbove as u8));
        assert_eq!(fidl_mlme::Cbw::Cbw40Below,
                   convert_secchan_offset_to_cbw(fidl_mlme::SecChanOffset::SecondaryBelow as u8));
    }

    #[test]
    fn test_convert_vht_segments_to_cbw() {
        assert_eq!(fidl_mlme::Cbw::Cbw80, convert_vht_segments_to_cbw(255, 0));
        assert_eq!(fidl_mlme::Cbw::Cbw160, convert_vht_segments_to_cbw(255, 247));
        assert_eq!(fidl_mlme::Cbw::Cbw80P80, convert_vht_segments_to_cbw(255, 200));
        assert_eq!(fidl_mlme::Cbw::Cbw80, convert_vht_segments_to_cbw(255, 250));
    }

    #[test]
    fn test_derive_cbw_ht() {
        {
            let want = fidl_mlme::Cbw::Cbw20;
            let got =
                derive_cbw_ht(&fake_ht_cap_chanwidth(fidl_mlme::ChanWidthSet::TwentyForty),
                              &fake_ht_op_sec_offset(fidl_mlme::SecChanOffset::SecondaryNone));
            assert_eq!(want, got);
        }
        {
            let want = fidl_mlme::Cbw::Cbw20;
            let got =
                derive_cbw_ht(&fake_ht_cap_chanwidth(fidl_mlme::ChanWidthSet::TwentyOnly),
                              &fake_ht_op_sec_offset(fidl_mlme::SecChanOffset::SecondaryAbove));
            assert_eq!(want, got);
        }
        {
            let want = fidl_mlme::Cbw::Cbw40;
            let got =
                derive_cbw_ht(&fake_ht_cap_chanwidth(fidl_mlme::ChanWidthSet::TwentyForty),
                              &fake_ht_op_sec_offset(fidl_mlme::SecChanOffset::SecondaryAbove));
            assert_eq!(want, got);
        }
        {
            let want = fidl_mlme::Cbw::Cbw40Below;
            let got =
                derive_cbw_ht(&fake_ht_cap_chanwidth(fidl_mlme::ChanWidthSet::TwentyForty),
                              &fake_ht_op_sec_offset(fidl_mlme::SecChanOffset::SecondaryBelow));
            assert_eq!(want, got);
        }
    }

    #[test]
    fn test_derive_cbw_vht() {
        {
            let want = fidl_mlme::Cbw::Cbw80;
            let got =
                derive_cbw_vht(&fake_ht_cap_chanwidth(fidl_mlme::ChanWidthSet::TwentyForty),
                               &fake_vht_capabilities(),
                               &fake_ht_op_sec_offset(fidl_mlme::SecChanOffset::SecondaryAbove),
                               &fake_vht_op_cbw(fidl_mlme::VhtCbw::Cbw8016080P80));
            assert_eq!(want, got);
        }
        {
            let want = fidl_mlme::Cbw::Cbw40;
            let got =
                derive_cbw_vht(&fake_ht_cap_chanwidth(fidl_mlme::ChanWidthSet::TwentyForty),
                               &fake_vht_capabilities(),
                               &fake_ht_op_sec_offset(fidl_mlme::SecChanOffset::SecondaryAbove),
                               &fake_vht_op_cbw(fidl_mlme::VhtCbw::Cbw2040));
            assert_eq!(want, got);
        }
    }

    #[test]
    fn test_get_band_id() {
        assert_eq!(fidl_mlme::Band::WlanBand2Ghz, get_band_id(14));
        assert_eq!(fidl_mlme::Band::WlanBand5Ghz, get_band_id(36));
    }

    #[test]
    fn test_get_device_band_info() {
        assert_eq!(fidl_mlme::Band::WlanBand5Ghz,
            get_device_band_info(&fake_device_info_ht(fidl_mlme::ChanWidthSet::TwentyForty),
                                 36).unwrap().band_id);
    }

    #[test]
    fn test_derive_phy_cbw() {
        {
            let want = (fidl_mlme::Phy::Vht, fidl_mlme::Cbw::Cbw80);
            let got =
                derive_phy_cbw(&fake_vht_bss_description(),
                               &fake_device_info_vht(fidl_mlme::ChanWidthSet::TwentyForty),
                               &fake_overrider_empty());
            assert_eq!(want, got);
        }
        {
            let want = (fidl_mlme::Phy::Ht, fidl_mlme::Cbw::Cbw40);
            let got =
                derive_phy_cbw(&fake_vht_bss_description(),
                               &fake_device_info_vht(fidl_mlme::ChanWidthSet::TwentyForty),
                               &fake_overrider(fidl_mlme::Phy::Ht, fidl_mlme::Cbw::Cbw80));
            assert_eq!(want, got);
        }
        {
            let want = (fidl_mlme::Phy::Ht, fidl_mlme::Cbw::Cbw20);
            let got =
                derive_phy_cbw(&fake_vht_bss_description(),
                               &fake_device_info_ht(fidl_mlme::ChanWidthSet::TwentyOnly),
                               &fake_overrider(fidl_mlme::Phy::Vht, fidl_mlme::Cbw::Cbw80));
            assert_eq!(want, got);
        }
    }

    fn fake_ht_cap_chanwidth(chanwidth: fidl_mlme::ChanWidthSet) -> fidl_mlme::HtCapabilities {
        let mut ht_cap = fake_ht_capabilities();
        ht_cap.ht_cap_info.chan_width_set = chanwidth as u8;
        ht_cap
    }

    fn fake_ht_op_sec_offset(secondary_offset: fidl_mlme::SecChanOffset) -> fidl_mlme::HtOperation {
        let mut ht_op = fake_ht_operation();
        ht_op.ht_op_info.secondary_chan_offset = secondary_offset as u8;
        ht_op
    }

    fn fake_vht_op_cbw(cbw: fidl_mlme::VhtCbw) -> fidl_mlme::VhtOperation {
        fidl_mlme::VhtOperation {
            vht_cbw: cbw as u8,
            .. fake_vht_operation()
        }
    }

    pub fn fake_device_info_ht(chanwidth: fidl_mlme::ChanWidthSet) -> DeviceInfo {
        DeviceInfo {
            addr: [0; 6],
            bands: vec![fake_5ghz_band_capabilities_ht_cbw(chanwidth)],
        }
    }

    pub fn fake_device_info_vht(chanwidth: fidl_mlme::ChanWidthSet) -> DeviceInfo {
        DeviceInfo {
            addr: [0; 6],
            bands: vec![fake_band_capabilities_5ghz_vht(chanwidth)],
        }
    }

    fn fake_5ghz_band_capabilities_ht_cbw(chanwidth: fidl_mlme::ChanWidthSet)
        -> fidl_mlme::BandCapabilities {
        let bc = fake_5ghz_band_capabilities();
        fidl_mlme::BandCapabilities {
            ht_cap: Some(Box::new(fake_ht_capabilities_cbw(chanwidth))),
            .. bc
        }
    }

    fn fake_band_capabilities_5ghz_vht(chanwidth: fidl_mlme::ChanWidthSet)
        -> fidl_mlme::BandCapabilities {
        let bc = fake_5ghz_band_capabilities();
        fidl_mlme::BandCapabilities {
            ht_cap: Some(Box::new(fake_ht_capabilities_cbw(chanwidth))),
            vht_cap: Some(Box::new(fake_vht_capabilities())),
            .. bc
        }
    }

    fn fake_overrider(phy: fidl_mlme::Phy, cbw: fidl_mlme::Cbw) -> ConnectPhyParams {
        ConnectPhyParams {
            phy: Some(phy),
            cbw: Some(cbw),
        }
    }

    fn fake_overrider_empty() -> ConnectPhyParams {
        ConnectPhyParams {
            phy: None,
            cbw: None,
        }
    }

    fn fake_ht_capabilities_cbw(chanwidth: fidl_mlme::ChanWidthSet)
        -> fidl_mlme::HtCapabilities {
        let mut ht_cap = fake_ht_capabilities();
        ht_cap.ht_cap_info.chan_width_set = chanwidth as u8;
        ht_cap
    }
}
