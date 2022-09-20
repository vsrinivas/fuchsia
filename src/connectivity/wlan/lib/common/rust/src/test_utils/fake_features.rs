// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;

pub fn fake_discovery_support() -> fidl_common::DiscoverySupport {
    fidl_common::DiscoverySupport {
        scan_offload: fidl_common::ScanOffloadExtension {
            supported: true,
            scan_cancel_supported: false,
        },
        probe_response_offload: fidl_common::ProbeResponseOffloadExtension { supported: false },
    }
}

pub fn fake_mac_sublayer_support() -> fidl_common::MacSublayerSupport {
    fidl_common::MacSublayerSupport {
        rate_selection_offload: fidl_common::RateSelectionOffloadExtension { supported: false },
        data_plane: fidl_common::DataPlaneExtension {
            data_plane_type: fidl_common::DataPlaneType::EthernetDevice,
        },
        device: fidl_common::DeviceExtension {
            is_synthetic: false,
            mac_implementation_type: fidl_common::MacImplementationType::Softmac,
            tx_status_report_supported: false,
        },
    }
}

pub fn fake_security_support() -> fidl_common::SecuritySupport {
    let mut support = fake_security_support_empty();
    support.mfp.supported = true;
    support.sae.sme_handler_supported = true;
    support
}

pub fn fake_security_support_empty() -> fidl_common::SecuritySupport {
    fidl_common::SecuritySupport {
        mfp: fidl_common::MfpFeature { supported: false },
        sae: fidl_common::SaeFeature {
            driver_handler_supported: false,
            sme_handler_supported: false,
        },
    }
}

pub fn fake_spectrum_management_support_empty() -> fidl_common::SpectrumManagementSupport {
    fidl_common::SpectrumManagementSupport { dfs: fidl_common::DfsFeature { supported: false } }
}
