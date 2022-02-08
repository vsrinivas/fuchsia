// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme as fidl_mlme;

// TODO(fxbug.dev/91038): Using channel number to determine band is incorrect.
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

#[cfg(test)]
mod tests {
    use {super::*, crate::test_utils::*, wlan_common::ie::ChanWidthSet};

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
}
