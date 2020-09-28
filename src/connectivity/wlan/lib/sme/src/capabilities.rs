// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    log::error,
    static_assertions::assert_eq_size,
    std::convert::TryInto,
    wlan_common::{
        channel::Channel,
        ie::{
            intersect::*, parse_ht_capabilities, parse_vht_capabilities, HtCapabilities,
            SupportedRate, VhtCapabilities,
        },
        mac::CapabilityInfo,
    },
    zerocopy::AsBytes,
};

/// Capabilities that takes the iface device's capabilities based on the channel a client is trying
/// to join, the PHY parameters that is overridden by user's command line input and the BSS the
/// client are is trying to join.
/// They are stored in the form of IEs because at some point they will be transmitted in
/// (Re)Association Request and (Re)Association Response frames.
#[derive(Debug, PartialEq)]
pub struct StaCapabilities {
    pub cap_info: CapabilityInfo,
    pub rates: Vec<SupportedRate>,
    pub ht_cap: Option<HtCapabilities>,
    pub vht_cap: Option<VhtCapabilities>,
}

#[derive(Debug, PartialEq)]
pub struct ClientCapabilities(pub StaCapabilities);
#[derive(Debug, PartialEq)]
pub struct ApCapabilities(pub StaCapabilities);

/// Performs capability negotiation with an AP assuming the Fuchsia device is a client.
pub fn intersect_with_ap_as_client(
    client: &ClientCapabilities,
    ap: &ApCapabilities,
) -> StaCapabilities {
    let rates = match intersect_rates(ApRates(&ap.0.rates[..]), ClientRates(&client.0.rates[..])) {
        Ok(rates) => rates,
        Err(e) => {
            error!("error intersecting rates: {:?}", e);
            vec![]
        }
    };
    let (cap_info, ht_cap, vht_cap) = intersect(&client.0, &ap.0);
    StaCapabilities { rates, cap_info, ht_cap, vht_cap }
}

/// Performs capability negotiation with a remote client assuming the Fuchsia device is an AP.
#[allow(unused)]
pub fn intersect_with_remote_client_as_ap(
    ap: &ApCapabilities,
    remote_client: &ClientCapabilities,
) -> StaCapabilities {
    // Safe to unwrap. Otherwise we would have rejected the association from this remote client.
    let rates = intersect_rates(ApRates(&ap.0.rates[..]), ClientRates(&remote_client.0.rates[..]))
        .unwrap_or(vec![]);
    let (cap_info, ht_cap, vht_cap) = intersect(&ap.0, &remote_client.0);
    StaCapabilities { rates, cap_info, ht_cap, vht_cap }
}

fn intersect(
    ours: &StaCapabilities,
    theirs: &StaCapabilities,
) -> (CapabilityInfo, Option<HtCapabilities>, Option<VhtCapabilities>) {
    // Every bit is a boolean so bit-wise and is sufficient
    let cap_info = CapabilityInfo(ours.cap_info.raw() & theirs.cap_info.raw());
    let ht_cap = match (ours.ht_cap, theirs.ht_cap) {
        // Intersect is NOT necessarily symmetrical. Our own capabilities prevails.
        (Some(ours), Some(theirs)) => Some(ours.intersect(&theirs)),
        _ => None,
    };
    let vht_cap = match (ours.vht_cap, theirs.vht_cap) {
        // Intersect is NOT necessarily symmetrical. Our own capabilities prevails.
        (Some(ours), Some(theirs)) => Some(ours.intersect(&theirs)),
        _ => None,
    };
    (cap_info, ht_cap, vht_cap)
}

impl From<fidl_mlme::AssociateConfirm> for ApCapabilities {
    fn from(ac: fidl_mlme::AssociateConfirm) -> Self {
        type HtCapArray = [u8; fidl_mlme::HT_CAP_LEN as usize];
        type VhtCapArray = [u8; fidl_mlme::VHT_CAP_LEN as usize];

        let cap_info = CapabilityInfo(ac.cap_info);
        let rates = ac.rates.iter().map(|&r| SupportedRate(r)).collect();

        let ht_cap = ac.ht_cap.map(|ht_cap| {
            let bytes: &HtCapArray = &ht_cap.bytes;
            assert_eq_size!(HtCapabilities, HtCapArray);
            let ht_caps: HtCapabilities = *parse_ht_capabilities(&bytes[..]).unwrap();
            ht_caps
        });
        let vht_cap = ac.vht_cap.map(|vht_cap| {
            let bytes: &VhtCapArray = &vht_cap.bytes;
            assert_eq_size!(VhtCapabilities, VhtCapArray);
            let vht_caps: VhtCapabilities = *parse_vht_capabilities(&bytes[..]).unwrap();
            vht_caps
        });
        Self(StaCapabilities { cap_info, rates, ht_cap, vht_cap })
    }
}

impl StaCapabilities {
    pub fn to_fidl_negotiated_capabilities(
        &self,
        channel: &Channel,
    ) -> fidl_mlme::NegotiatedCapabilities {
        type HtCapArray = [u8; fidl_mlme::HT_CAP_LEN as usize];
        type VhtCapArray = [u8; fidl_mlme::VHT_CAP_LEN as usize];

        let ht_cap = self.ht_cap.map(|ht_cap| {
            assert_eq_size!(HtCapabilities, HtCapArray);
            let bytes: HtCapArray = ht_cap.as_bytes().try_into().unwrap();
            fidl_mlme::HtCapabilities { bytes }
        });
        let vht_cap = self.vht_cap.map(|vht_cap| {
            assert_eq_size!(VhtCapabilities, VhtCapArray);
            let bytes: VhtCapArray = vht_cap.as_bytes().try_into().unwrap();
            fidl_mlme::VhtCapabilities { bytes }
        });
        fidl_mlme::NegotiatedCapabilities {
            channel: channel.to_fidl(),
            cap_info: self.cap_info.raw(),
            rates: self.rates.as_bytes().to_vec(),
            // TODO(fxbug.dev/43938): populate WMM param with actual value
            wmm_param: None,
            ht_cap: ht_cap.map(Box::new),
            vht_cap: vht_cap.map(Box::new),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_wlan_common as fidl_common,
        wlan_common::{ie, mac},
    };

    fn fake_client_join_cap() -> ClientCapabilities {
        ClientCapabilities(StaCapabilities {
            cap_info: mac::CapabilityInfo(0x1234),
            rates: [101, 102, 103, 104].iter().cloned().map(SupportedRate).collect(),
            ht_cap: Some(HtCapabilities {
                ht_cap_info: ie::HtCapabilityInfo(0).with_rx_stbc(2).with_tx_stbc(false),
                ..ie::fake_ht_capabilities()
            }),
            vht_cap: Some(ie::fake_vht_capabilities()),
        })
    }

    fn fake_ap_join_cap() -> ApCapabilities {
        ApCapabilities(StaCapabilities {
            cap_info: mac::CapabilityInfo(0x4321),
            // 101 + 128 turns it into a basic rate
            rates: [101 + 128, 102, 9].iter().cloned().map(SupportedRate).collect(),
            ht_cap: Some(HtCapabilities {
                ht_cap_info: ie::HtCapabilityInfo(0).with_rx_stbc(1).with_tx_stbc(true),
                ..ie::fake_ht_capabilities()
            }),
            vht_cap: Some(ie::fake_vht_capabilities()),
        })
    }

    #[test]
    fn client_intersect_with_ap() {
        assert_eq!(
            intersect_with_ap_as_client(&fake_client_join_cap(), &fake_ap_join_cap()),
            StaCapabilities {
                cap_info: mac::CapabilityInfo(0x0220),
                rates: [229, 102].iter().cloned().map(SupportedRate).collect(),
                ht_cap: Some(HtCapabilities {
                    ht_cap_info: ie::HtCapabilityInfo(0).with_rx_stbc(2).with_tx_stbc(false),
                    ..ie::fake_ht_capabilities()
                }),
                ..fake_client_join_cap().0
            }
        );
    }

    #[test]
    fn ap_intersect_with_remote_client() {
        assert_eq!(
            intersect_with_remote_client_as_ap(&fake_ap_join_cap(), &fake_client_join_cap()),
            StaCapabilities {
                cap_info: mac::CapabilityInfo(0x0220),
                rates: [229, 102].iter().cloned().map(SupportedRate).collect(),
                ht_cap: Some(HtCapabilities {
                    ht_cap_info: ie::HtCapabilityInfo(0).with_rx_stbc(0).with_tx_stbc(true),
                    ..ie::fake_ht_capabilities()
                }),
                ..fake_ap_join_cap().0
            }
        );
    }

    #[test]
    fn fidl_assoc_conf_to_cap() {
        let ac = fidl_mlme::AssociateConfirm {
            result_code: fidl_mlme::AssociateResultCodes::Success,
            association_id: 123,
            cap_info: 0x1234,
            rates: vec![125, 126, 127, 128, 129],
            wmm_param: None,
            ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                bytes: ie::fake_ht_capabilities().as_bytes().try_into().unwrap(),
            })),
            vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                bytes: ie::fake_vht_capabilities().as_bytes().try_into().unwrap(),
            })),
        };
        let cap: ApCapabilities = ac.into();
        assert_eq!(
            cap.0,
            StaCapabilities {
                cap_info: mac::CapabilityInfo(0x1234),
                rates: [125u8, 126, 127, 128, 129].iter().cloned().map(ie::SupportedRate).collect(),
                ht_cap: Some(ie::fake_ht_capabilities()),
                vht_cap: Some(ie::fake_vht_capabilities()),
            }
        );
    }

    #[test]
    fn cap_to_fidl_negotiated_cap() {
        let cap = StaCapabilities {
            cap_info: mac::CapabilityInfo(0x1234),
            rates: [125u8, 126, 127, 128, 129].iter().cloned().map(ie::SupportedRate).collect(),
            ht_cap: Some(ie::fake_ht_capabilities()),
            vht_cap: Some(ie::fake_vht_capabilities()),
        };
        let fidl_cap = cap.to_fidl_negotiated_capabilities(&Channel {
            primary: 123,
            cbw: wlan_common::channel::Cbw::Cbw80P80 { secondary80: 42 },
        });
        assert_eq!(
            fidl_cap,
            fidl_mlme::NegotiatedCapabilities {
                channel: fidl_fuchsia_wlan_common::WlanChan {
                    primary: 123,
                    cbw: fidl_common::Cbw::Cbw80P80,
                    secondary80: 42,
                },
                cap_info: 0x1234,
                rates: vec![125, 126, 127, 128, 129],
                wmm_param: None,
                ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                    bytes: ie::fake_ht_capabilities().as_bytes().try_into().unwrap()
                })),
                vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                    bytes: ie::fake_vht_capabilities().as_bytes().try_into().unwrap()
                })),
            }
        );
    }
}
