// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    // TODO(porce): Rename the aliases as fidl_*
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_device as fidl_device,
    fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_tap as wlantap,
    std::convert::TryInto,
    wlan_common::ie::*,
    zerocopy::AsBytes,
};

pub(crate) fn create_wlantap_config(
    name: String,
    hw_mac_address: [u8; 6],
    mac_role: fidl_device::MacRole,
) -> wlantap::WlantapPhyConfig {
    use fidl_fuchsia_wlan_common::DriverFeature;
    use fidl_fuchsia_wlan_device::SupportedPhy;
    wlantap::WlantapPhyConfig {
        phy_info: fidl_device::PhyInfo {
            id: 0,
            dev_path: None,
            hw_mac_address,
            supported_phys: vec![
                SupportedPhy::Dsss,
                SupportedPhy::Cck,
                SupportedPhy::Ofdm,
                SupportedPhy::Ht,
            ],
            driver_features: vec![DriverFeature::Synth, DriverFeature::TxStatusReport],
            mac_roles: vec![mac_role],
            caps: vec![],
            bands: vec![create_2_4_ghz_band_info()],
        },
        name,
        quiet: false,
    }
}

fn create_2_4_ghz_band_info() -> fidl_device::BandInfo {
    fidl_device::BandInfo {
        band_id: fidl_common::Band::WlanBand2Ghz,
        ht_caps: Some(Box::new(fidl_internal::HtCapabilities {
            bytes: fake_ht_capabilities().as_bytes().try_into().unwrap(),
        })),
        vht_caps: None,
        rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
        supported_channels: fidl_device::ChannelList {
            base_freq: 2407,
            channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
        },
    }
}
