// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ie::*,
    paste,
    std::{
        cmp::{max, min},
        collections::HashSet,
        ops::BitAnd,
    },
    zerocopy::LayoutVerified,
};

// TODO(fxbug.dev/42763): HT and VHT intersections defined here are best effort only.

// For example:
// struct Foo { a: u8, b: u8 };
// impl_intersect!(Foo, {
//   intersect: a,
//   min: b,
// }
// will produce a Foo { a: intersect(self.a, other.a), b: min(self.b, other.b) }

macro_rules! impl_intersect {
  ($struct_name:ident { $($op:ident: $field:ident),* $(,)?}) => {
    paste::paste! {
      impl Intersect for $struct_name {
        fn intersect(&self, other: &Self) -> Self {
          Self(0)
          $(
            .[<with_ $field>]($op(self.$field(), other.$field ()))
          )*
        }
      }
    }
  };
}

/// Intersect capabilities between two entities, such as a client and an AP.
/// Note: a.intersect(b) is not guaranteed to be the same as b.intersect(b). One such example is the
/// TX_STBC and RX_STBC fields in HtCapabilityInfo.
pub trait Intersect {
    fn intersect(&self, other: &Self) -> Self;
}

fn intersect<I: Intersect>(a: I, b: I) -> I {
    a.intersect(&b)
}

fn and<B: BitAnd>(a: B, b: B) -> B::Output {
    a & b
}

// IEEE Std. 802.11-2016, 11.2.6 mentioned this but did not provide interpretation for DISABLED.
// This is Fuchsia's interpretation.
impl Intersect for SmPowerSave {
    fn intersect(&self, other: &Self) -> Self {
        if *self == Self::DISABLED || *other == Self::DISABLED {
            Self::DISABLED
        } else {
            Self(min(self.0, other.0))
        }
    }
}

impl_intersect!(HtCapabilityInfo {
    and: ldpc_coding_cap,
    min: chan_width_set_raw,  // ChanWidthSet(u8)
    intersect: sm_power_save, // SmPowerSave(u8)
    and: greenfield,
    and: short_gi_20,
    and: short_gi_40,
    and: tx_stbc,
    min: rx_stbc,
    and: delayed_block_ack,
    and: max_amsdu_len_raw, // MaxAmsduLen(u8)
    and: dsss_in_40,
    and: intolerant_40,
    and: lsig_txop_protect,
});

impl_intersect!(AmpduParams {
    min: max_ampdu_exponent_raw, // MaxAmpduExponent(u8)
    max: min_start_spacing_raw,  // MinMpduStartSpacing(u8)
});

// TODO(fxbug.dev/29404): if tx_rx_diff is set, the intersection rule may be more complicated.
impl_intersect!(SupportedMcsSet {
    and: rx_mcs_raw, // RxMcsBitmask(u128)
    min: rx_highest_rate,
    and: tx_set_defined,
    and: tx_rx_diff,
    min: tx_max_ss_raw, // NumSpatialStreams(u8)
    and: tx_ueqm,
});

// IEEE Std. 802.11-2016, 11.17.3
// PcoTransitionTime can be dynamic so the best effort here is to use the slower transition time.
impl Intersect for PcoTransitionTime {
    fn intersect(&self, other: &Self) -> Self {
        if *self == Self::PCO_RESERVED || *other == Self::PCO_RESERVED {
            Self::PCO_RESERVED
        } else {
            Self(max(self.0, other.0))
        }
    }
}

impl_intersect!(HtExtCapabilities {
    and: pco,
    intersect: pco_transition, // PcoTransitionTime(u8)
    min: mcs_feedback_raw,     // McsFeedback(u8)
    and: htc_ht_support,
    and: rd_responder,
});

impl_intersect!(TxBfCapability {
    and: implicit_rx,
    and: rx_stag_sounding,
    and: tx_stag_sounding,
    and: rx_ndp,
    and: tx_ndp,
    and: implicit,
    min: calibration_raw, // Calibration(u8)
    and: csi,
    and: noncomp_steering,
    and: comp_steering,

    // IEEE 802.11-2016 Table 9-166
    // xxx_feedback behaves like bitmask for delayed and immediate feedback
    and: csi_feedback_raw,     // Feedback(u8)
    and: noncomp_feedback_raw, // Feedback(u8)
    and: comp_feedback_raw,    // Feedback(u8)

    min: min_grouping_raw,          // MinGroup(u8)
    min: csi_antennas_raw,          // NumAntennas(u8)
    min: noncomp_steering_ants_raw, // NumAntennas(u8)
    min: comp_steering_ants_raw,    // NumAntennas(u8)
    min: csi_rows_raw,              // NumCsiRows(u8)
    min: chan_estimation_raw,       // NumSpaceTimeStreams(u8)
});

impl_intersect!(AselCapability {
    and: asel,
    and: csi_feedback_tx_asel,
    and: ant_idx_feedback_tx_asel,
    and: explicit_csi_feedback,
    and: antenna_idx_feedback,
    and: rx_asel,
    and: tx_sounding_ppdu,
});

impl Intersect for HtCapabilities {
    fn intersect(&self, other: &Self) -> Self {
        let mut out = Self {
            ht_cap_info: { self.ht_cap_info }.intersect(&{ other.ht_cap_info }),
            ampdu_params: { self.ampdu_params }.intersect(&{ other.ampdu_params }),
            mcs_set: { self.mcs_set }.intersect(&{ other.mcs_set }),
            ht_ext_cap: { self.ht_ext_cap }.intersect(&{ other.ht_ext_cap }),
            txbf_cap: { self.txbf_cap }.intersect(&{ other.txbf_cap }),
            asel_cap: { self.asel_cap }.intersect(&{ other.asel_cap }),
        };
        // IEEE Std. 802.11-2016, 10.17
        // An STA can use rx_stbc if its peer supports tx_stbc. Similarly, an STA can use tx_stbc if
        // its peer supports at least one(1) spatial stream for rx_stbc.
        // TODO(fxbug.dev/29131): Verify STBC behavior is correct.
        out.ht_cap_info = out
            .ht_cap_info
            .with_tx_stbc(if { other.ht_cap_info }.rx_stbc() != 0 {
                { self.ht_cap_info }.tx_stbc()
            } else {
                false
            })
            .with_rx_stbc(if { other.ht_cap_info }.tx_stbc() {
                { self.ht_cap_info }.rx_stbc()
            } else {
                0
            });
        out
    }
}

impl_intersect!(VhtCapabilitiesInfo {
    // TODO(fxbug.dev/29404): IEEE 802.11-2016 Table 9-250 - supported_cbw_set needs to consider ext_nss_bw
    min: max_mpdu_len_raw, // MaxMpduLen(u8)
    min: supported_cbw_set,
    and: rx_ldpc,
    and: sgi_cbw80,
    and: sgi_cbw160,
    and: tx_stbc,
    min: rx_stbc,
    and: su_bfer,
    and: su_bfee,
    min: bfee_sts,
    min: num_sounding,
    and: mu_bfer,
    and: mu_bfee,
    and: txop_ps,
    and: htc_vht,
    min: max_ampdu_exponent_raw, // MaxAmpduExponent(u8)
    min: link_adapt_raw,         // VhtLinkAdaptation(u8)
    and: rx_ant_pattern,
    and: tx_ant_pattern,
    min: ext_nss_bw,
});

impl Intersect for VhtMcsSet {
    fn intersect(&self, other: &Self) -> Self {
        if *self == Self::NONE || *other == Self::NONE {
            Self::NONE
        } else {
            Self(min(self.0, other.0))
        }
    }
}

impl_intersect!(VhtMcsNssMap {
    intersect: ss1, // VhtMcsSet(u8)
    intersect: ss2, // VhtMcsSet(u8)
    intersect: ss3, // VhtMcsSet(u8)
    intersect: ss4, // VhtMcsSet(u8)
    intersect: ss5, // VhtMcsSet(u8)
    intersect: ss6, // VhtMcsSet(u8)
    intersect: ss7, // VhtMcsSet(u8)
    intersect: ss8, // VhtMcsSet(u8)
});

impl_intersect!(VhtMcsNssSet {
    intersect: rx_max_mcs, // VhtMcsNssMap(u16)
    min: rx_max_data_rate,
    min: max_nsts,
    intersect: tx_max_mcs, // VhtMcsNssMap(u16)
    min: tx_max_data_rate,
    and: ext_nss_bw,
});

impl Intersect for VhtCapabilities {
    fn intersect(&self, other: &Self) -> Self {
        Self {
            vht_cap_info: { self.vht_cap_info }.intersect(&{ other.vht_cap_info }),
            vht_mcs_nss: { self.vht_mcs_nss }.intersect(&{ other.vht_mcs_nss }),
        }
    }
}

pub struct ApRates<'a>(pub &'a [SupportedRate]);
pub struct ClientRates<'a>(pub &'a [SupportedRate]);

impl<'a> From<&'a [u8]> for ApRates<'a> {
    fn from(rates: &'a [u8]) -> Self {
        // This is always safe, as SupportedRate is a newtype of u8.
        Self(LayoutVerified::new_slice(rates).unwrap().into_slice())
    }
}

impl<'a> From<&'a [u8]> for ClientRates<'a> {
    fn from(rates: &'a [u8]) -> Self {
        // This is always safe, as SupportedRate is a newtype of u8.
        Self(LayoutVerified::new_slice(rates).unwrap().into_slice())
    }
}

#[derive(Eq, PartialEq, Debug)]
pub enum IntersectRatesError {
    BasicRatesMismatch,
    NoApRatesSupported,
}

/// Returns the rates specified by the AP that are also supported by the client, with basic bits
/// following their values in the AP.
/// Returns Error if intersection fails.
/// Note: The client MUST support ALL the basic rates specified by the AP or the intersection fails.
pub fn intersect_rates(
    ap_rates: ApRates,
    client_rates: ClientRates,
) -> Result<Vec<SupportedRate>, IntersectRatesError> {
    let mut rates = ap_rates.0.to_vec();
    let client_rates = client_rates.0.iter().map(|r| r.rate()).collect::<HashSet<_>>();
    // The client MUST support ALL basic rates specified by the AP.
    if rates.iter().any(|ra| ra.basic() && !client_rates.contains(&ra.rate())) {
        return Err(IntersectRatesError::BasicRatesMismatch);
    }

    // Remove rates that are not supported by the client.
    rates.retain(|ra| client_rates.contains(&ra.rate()));
    if rates.is_empty() {
        Err(IntersectRatesError::NoApRatesSupported)
    } else {
        Ok(rates)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    impl SupportedRate {
        fn new_basic(rate: u8) -> Self {
            Self(rate).with_basic(true)
        }
    }

    #[test]
    fn some_basic_rate_missing() {
        // AP basic rate 120 is not supported, resulting in an Error
        let error = intersect_rates(
            ApRates(&[SupportedRate::new_basic(120), SupportedRate::new_basic(111)][..]),
            ClientRates(&[SupportedRate(111)][..]),
        )
        .unwrap_err();
        assert_eq!(error, IntersectRatesError::BasicRatesMismatch);
    }

    #[test]
    fn all_basic_rates_supported() {
        assert_eq!(
            vec![SupportedRate::new_basic(120)],
            intersect_rates(
                ApRates(&[SupportedRate::new_basic(120), SupportedRate(111)][..]),
                ClientRates(&[SupportedRate(120)][..])
            )
            .unwrap()
        );
    }

    #[test]
    fn all_basic_and_non_basic_rates_supported() {
        assert_eq!(
            vec![SupportedRate::new_basic(120)],
            intersect_rates(
                ApRates(&[SupportedRate::new_basic(120), SupportedRate(111)][..]),
                ClientRates(&[SupportedRate(120)][..])
            )
            .unwrap()
        );
    }

    #[test]
    fn no_rates_are_supported() {
        let error =
            intersect_rates(ApRates(&[SupportedRate(120)][..]), ClientRates(&[][..])).unwrap_err();
        assert_eq!(error, IntersectRatesError::NoApRatesSupported);
    }

    #[test]
    fn preserve_ap_rates_basicness() {
        // AP side 120 is not basic so the result should be non-basic.
        assert_eq!(
            vec![SupportedRate(120)],
            intersect_rates(
                ApRates(&[SupportedRate(120), SupportedRate(111)][..]),
                ClientRates(&[SupportedRate::new_basic(120)][..])
            )
            .unwrap()
        );
    }

    // TODO(fxbug.dev/42763): Currently, MCS set and channel bandwidth are the most important ones. Revisit
    // other fields when the use cases arise or we have more understanding.
    #[test]
    fn intersect_ht_cap_info_chan_width_set() {
        let a = HtCapabilityInfo(0).with_chan_width_set(ChanWidthSet::TWENTY_ONLY);
        let b = HtCapabilityInfo(0).with_chan_width_set(ChanWidthSet::TWENTY_FORTY);
        assert_eq!(ChanWidthSet::TWENTY_ONLY, a.intersect(&b).chan_width_set());

        let a = HtCapabilityInfo(0).with_chan_width_set(ChanWidthSet::TWENTY_FORTY);
        let b = HtCapabilityInfo(0).with_chan_width_set(ChanWidthSet::TWENTY_FORTY);
        assert_eq!(ChanWidthSet::TWENTY_FORTY, a.intersect(&b).chan_width_set());
    }

    #[test]
    fn intersect_supported_mcs_set() {
        let a = SupportedMcsSet(0).with_rx_mcs_raw(0xffff);
        let b = SupportedMcsSet(0).with_rx_mcs_raw(0x0304);
        assert_eq!(RxMcsBitmask(0x0304), a.intersect(&b).rx_mcs());
    }

    #[test]
    fn intersect_sm_power_save() {
        assert_eq!(SmPowerSave::DISABLED, SmPowerSave::DISABLED.intersect(&SmPowerSave::DISABLED));
        assert_eq!(SmPowerSave::DISABLED, SmPowerSave::STATIC.intersect(&SmPowerSave::DISABLED));
        assert_eq!(SmPowerSave::DISABLED, SmPowerSave::DYNAMIC.intersect(&SmPowerSave::DISABLED));
        assert_eq!(SmPowerSave::DISABLED, SmPowerSave::DISABLED.intersect(&SmPowerSave::STATIC));
        assert_eq!(SmPowerSave::DISABLED, SmPowerSave::DISABLED.intersect(&SmPowerSave::DYNAMIC));

        assert_eq!(SmPowerSave::STATIC, SmPowerSave::STATIC.intersect(&SmPowerSave::DYNAMIC));
        assert_eq!(SmPowerSave::STATIC, SmPowerSave::DYNAMIC.intersect(&SmPowerSave::STATIC));

        assert_eq!(SmPowerSave::DYNAMIC, SmPowerSave::DYNAMIC.intersect(&SmPowerSave::DYNAMIC));
    }

    #[test]
    fn intersect_pco_transition() {
        type PTT = PcoTransitionTime;
        assert_eq!(PTT::PCO_RESERVED, PTT::PCO_RESERVED.intersect(&PTT::PCO_RESERVED));
        assert_eq!(PTT::PCO_RESERVED, PTT::PCO_RESERVED.intersect(&PTT::PCO_400_USEC));
        assert_eq!(PTT::PCO_RESERVED, PTT::PCO_RESERVED.intersect(&PTT::PCO_1500_USEC));
        assert_eq!(PTT::PCO_RESERVED, PTT::PCO_RESERVED.intersect(&PTT::PCO_5000_USEC));

        assert_eq!(PTT::PCO_RESERVED, PTT::PCO_400_USEC.intersect(&PTT::PCO_RESERVED));
        assert_eq!(PTT::PCO_RESERVED, PTT::PCO_1500_USEC.intersect(&PTT::PCO_RESERVED));
        assert_eq!(PTT::PCO_RESERVED, PTT::PCO_5000_USEC.intersect(&PTT::PCO_RESERVED));

        assert_eq!(PTT::PCO_5000_USEC, PTT::PCO_400_USEC.intersect(&PTT::PCO_5000_USEC));
        assert_eq!(PTT::PCO_5000_USEC, PTT::PCO_1500_USEC.intersect(&PTT::PCO_5000_USEC));
        assert_eq!(PTT::PCO_5000_USEC, PTT::PCO_5000_USEC.intersect(&PTT::PCO_5000_USEC));

        assert_eq!(PTT::PCO_5000_USEC, PTT::PCO_5000_USEC.intersect(&PTT::PCO_400_USEC));
        assert_eq!(PTT::PCO_5000_USEC, PTT::PCO_5000_USEC.intersect(&PTT::PCO_1500_USEC));

        assert_eq!(PTT::PCO_1500_USEC, PTT::PCO_400_USEC.intersect(&PTT::PCO_1500_USEC));
        assert_eq!(PTT::PCO_1500_USEC, PTT::PCO_1500_USEC.intersect(&PTT::PCO_400_USEC));

        assert_eq!(PTT::PCO_400_USEC, PTT::PCO_400_USEC.intersect(&PTT::PCO_400_USEC));
    }

    #[test]
    // Check TX_STBC and RX_STBC too because they involve multiple fields.
    fn intersect_ht_cap_info_stbc() {
        let mut ht_cap_a = fake_ht_capabilities();
        let mut ht_cap_b = fake_ht_capabilities();

        ht_cap_a.ht_cap_info = HtCapabilityInfo(0).with_tx_stbc(true).with_rx_stbc(2);
        ht_cap_b.ht_cap_info = HtCapabilityInfo(0).with_tx_stbc(false).with_rx_stbc(1);

        let intersected_ht_cap_info = ht_cap_a.intersect(&ht_cap_b).ht_cap_info;
        assert_eq!(true, intersected_ht_cap_info.tx_stbc());
        assert_eq!(0, intersected_ht_cap_info.rx_stbc());

        let intersected_ht_cap_info = ht_cap_b.intersect(&ht_cap_a).ht_cap_info;
        assert_eq!(false, intersected_ht_cap_info.tx_stbc());
        assert_eq!(1, intersected_ht_cap_info.rx_stbc())
    }

    #[test]
    fn intersect_vht_mcs_set() {
        assert_eq!(VhtMcsSet::NONE, VhtMcsSet::NONE.intersect(&VhtMcsSet::UP_TO_7));
        assert_eq!(VhtMcsSet::NONE, VhtMcsSet::NONE.intersect(&VhtMcsSet::UP_TO_8));
        assert_eq!(VhtMcsSet::NONE, VhtMcsSet::NONE.intersect(&VhtMcsSet::UP_TO_9));
        assert_eq!(VhtMcsSet::NONE, VhtMcsSet::NONE.intersect(&VhtMcsSet::NONE));
        assert_eq!(VhtMcsSet::NONE, VhtMcsSet::UP_TO_7.intersect(&VhtMcsSet::NONE));
        assert_eq!(VhtMcsSet::NONE, VhtMcsSet::UP_TO_8.intersect(&VhtMcsSet::NONE));
        assert_eq!(VhtMcsSet::NONE, VhtMcsSet::UP_TO_9.intersect(&VhtMcsSet::NONE));

        assert_eq!(VhtMcsSet::UP_TO_7, VhtMcsSet::UP_TO_7.intersect(&VhtMcsSet::UP_TO_8));
        assert_eq!(VhtMcsSet::UP_TO_7, VhtMcsSet::UP_TO_8.intersect(&VhtMcsSet::UP_TO_7));

        assert_eq!(VhtMcsSet::UP_TO_8, VhtMcsSet::UP_TO_8.intersect(&VhtMcsSet::UP_TO_9));
        assert_eq!(VhtMcsSet::UP_TO_8, VhtMcsSet::UP_TO_9.intersect(&VhtMcsSet::UP_TO_8));

        assert_eq!(VhtMcsSet::UP_TO_9, VhtMcsSet::UP_TO_9.intersect(&VhtMcsSet::UP_TO_9));
    }
}
