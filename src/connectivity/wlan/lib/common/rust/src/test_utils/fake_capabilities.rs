// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capabilities::{ClientCapabilities, StaCapabilities},
        ie,
        mac::CapabilityInfo,
    },
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    std::convert::TryInto,
    zerocopy::AsBytes,
};

pub fn fake_5ghz_band_capability_ht_cbw(chanwidth: ie::ChanWidthSet) -> fidl_mlme::BandCapability {
    let bc = fake_5ghz_band_capability();
    fidl_mlme::BandCapability {
        ht_cap: Some(Box::new(fidl_internal::HtCapabilities {
            bytes: fake_ht_capabilities_cbw(chanwidth).as_bytes().try_into().unwrap(),
        })),
        ..bc
    }
}

pub fn fake_5ghz_band_capability_vht(chanwidth: ie::ChanWidthSet) -> fidl_mlme::BandCapability {
    let bc = fake_5ghz_band_capability();
    fidl_mlme::BandCapability {
        ht_cap: Some(Box::new(fidl_internal::HtCapabilities {
            bytes: fake_ht_capabilities_cbw(chanwidth).as_bytes().try_into().unwrap(),
        })),
        vht_cap: Some(Box::new(fidl_internal::VhtCapabilities {
            bytes: ie::fake_vht_capabilities().as_bytes().try_into().unwrap(),
        })),
        ..bc
    }
}

pub fn fake_ht_capabilities_cbw(chanwidth: ie::ChanWidthSet) -> ie::HtCapabilities {
    let mut ht_cap = ie::fake_ht_capabilities();
    ht_cap.ht_cap_info = ht_cap.ht_cap_info.with_chan_width_set(chanwidth);
    ht_cap
}

pub fn fake_capability_info() -> CapabilityInfo {
    CapabilityInfo(0)
        .with_ess(false)
        .with_ibss(false)
        .with_cf_pollable(false)
        .with_cf_poll_req(false)
        .with_privacy(false)
        .with_short_preamble(true)
        .with_spectrum_mgmt(false)
        .with_qos(false)
        .with_short_slot_time(false)
        .with_apsd(false)
        .with_radio_measurement(false)
        .with_delayed_block_ack(false)
        .with_immediate_block_ack(false)
}

pub fn fake_5ghz_band_capability() -> fidl_mlme::BandCapability {
    fidl_mlme::BandCapability {
        band: fidl_common::WlanBand::FiveGhz,
        basic_rates: vec![0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        operating_channels: vec![],
        ht_cap: None,
        vht_cap: None,
    }
}

pub fn fake_2ghz_band_capability_vht() -> fidl_mlme::BandCapability {
    fidl_mlme::BandCapability {
        ht_cap: Some(Box::new(fidl_internal::HtCapabilities {
            bytes: fake_ht_capabilities_cbw(ie::ChanWidthSet::TWENTY_FORTY)
                .as_bytes()
                .try_into()
                .unwrap(),
        })),
        vht_cap: Some(Box::new(fidl_internal::VhtCapabilities {
            bytes: ie::fake_vht_capabilities().as_bytes().try_into().unwrap(),
        })),
        ..fake_2ghz_band_capability()
    }
}

pub fn fake_2ghz_band_capability() -> fidl_mlme::BandCapability {
    fidl_mlme::BandCapability {
        band: fidl_common::WlanBand::TwoGhz,
        basic_rates: vec![0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        operating_channels: vec![],
        ht_cap: None,
        vht_cap: None,
    }
}

pub fn fake_client_capabilities() -> ClientCapabilities {
    let rates = vec![0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c];
    ClientCapabilities(StaCapabilities {
        capability_info: fake_capability_info(),
        rates: rates.into_iter().map(ie::SupportedRate).collect(),
        ht_cap: Some(ie::fake_ht_capabilities()),
        vht_cap: Some(ie::fake_vht_capabilities()),
    })
}
