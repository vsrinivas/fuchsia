// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    banjo_fuchsia_wlan_common as banjo_common,
    thiserror::{self, Error},
    wlan_common::ie,
    zerocopy::ByteSlice,
};

#[derive(Default)]
pub struct ChannelState {
    pub(crate) main_channel: Option<banjo_common::WlanChannel>,
}

#[derive(Debug, PartialEq)]
pub struct ChannelSwitch {
    pub channel_switch_count: u8,
    pub new_channel: banjo_common::WlanChannel,
    pub pause_transmission: bool,
    pub new_operating_class: Option<u8>,
    // TODO(fxbug.dev/97850): Support transmit power envelope.
    pub new_transmit_power_envelope_specified: bool,
}

// TODO(fxbug.dev/88026): Remove unused annotation.
#[allow(unused)]
impl ChannelSwitch {
    // TODO(fxbug.dev/97850): Support channel switch related feature queries.
    /// Determines whether this ChannelSwitch can be performed by the driver.
    fn compatible(&self) -> bool {
        self.new_operating_class.is_none() && !self.new_transmit_power_envelope_specified
    }
}

#[derive(Default)]
pub struct ChannelSwitchBuilder<B> {
    channel_switch: Option<ie::ChannelSwitchAnnouncement>,
    secondary_channel_offset: Option<ie::SecChanOffset>,
    extended_channel_switch: Option<ie::ExtendedChannelSwitchAnnouncement>,
    new_country: Option<ie::CountryView<B>>,
    wide_bandwidth_channel_switch: Option<ie::WideBandwidthChannelSwitch>,
    transmit_power_envelope: Option<ie::TransmitPowerEnvelopeView<B>>,
}

#[derive(Debug, Error)]
pub enum ChannelSwitchError {
    #[error("Channel switch is insufficiently specified.")]
    NotEnoughInfo,
    #[error("Frame contains multiple channel switch elements with conflicting information.")]
    ConflictingElements,
    #[error("Invalid channel switch mode {}", _0)]
    InvalidChannelSwitchMode(u8),
}

// TODO(fxbug.dev/88026): Remove unused annotation.
#[allow(unused)]
impl<B: ByteSlice> ChannelSwitchBuilder<B> {
    // Convert a set of received channel-switch-related IEs into the parameters
    // for a channel switch. Returns an error if the IEs received do not describe
    // a deterministic, valid channel switch.
    pub fn build(self) -> Result<ChannelSwitch, ChannelSwitchError> {
        // Extract shared information from the channel switch or extended channel switch elements
        // present. If both are present we check that they agree on the destination channel and then
        // use the CSA instead of the ECSA. This decision is to avoid specifying a
        // new_operating_class wherever possible, since operating class switches are unsupported.
        let (mode, new_channel_number, channel_switch_count, new_operating_class) =
            if let Some(csa) = self.channel_switch {
                if let Some(ecsa) = self.extended_channel_switch {
                    // If both CSA and ECSA elements are present, make sure they match.
                    if csa.new_channel_number != ecsa.new_channel_number {
                        return Err(ChannelSwitchError::ConflictingElements);
                    }
                }
                // IEEE Std 802.11-2016 11.9.8 describes the operation of a CSA.
                (csa.mode, csa.new_channel_number, csa.channel_switch_count, None)
            } else if let Some(ecsa) = self.extended_channel_switch {
                // IEEE Std 802.11-2016 11.10 describes the operation of an extended CSA.
                (
                    ecsa.mode,
                    ecsa.new_channel_number,
                    ecsa.channel_switch_count,
                    Some(ecsa.new_operating_class),
                )
            } else {
                return Err(ChannelSwitchError::NotEnoughInfo);
            };

        let pause_transmission = match mode {
            1 => true,
            0 => false,
            other => return Err(ChannelSwitchError::InvalidChannelSwitchMode(other)),
        };

        // IEEE Std 802.11-2016 9.4.2.159 Table 9-252 specifies that wide bandwidth channel switch
        // elements are treated identically to those in a VHT element.
        let vht_cbw_and_segs = self
            .wide_bandwidth_channel_switch
            .map(|wbcs| (wbcs.new_width, wbcs.new_center_freq_seg0, wbcs.new_center_freq_seg1));
        let sec_chan_offset =
            self.secondary_channel_offset.unwrap_or(ie::SecChanOffset::SECONDARY_NONE);
        let (cbw, secondary80) =
            wlan_common::channel::derive_wide_channel_bandwidth(vht_cbw_and_segs, sec_chan_offset)
                .to_banjo();

        Ok(ChannelSwitch {
            channel_switch_count: channel_switch_count,
            new_channel: banjo_common::WlanChannel {
                primary: new_channel_number,
                cbw: cbw.into(),
                secondary80,
            },
            pause_transmission,
            new_operating_class,
            new_transmit_power_envelope_specified: self.transmit_power_envelope.is_some(),
        })
    }

    pub fn add_channel_switch_announcement(&mut self, csa: ie::ChannelSwitchAnnouncement) {
        self.channel_switch.replace(csa);
    }

    pub fn add_secondary_channel_offset(&mut self, sco: ie::SecChanOffset) {
        self.secondary_channel_offset.replace(sco);
    }

    pub fn add_extended_channel_switch_announcement(
        &mut self,
        ecsa: ie::ExtendedChannelSwitchAnnouncement,
    ) {
        self.extended_channel_switch.replace(ecsa);
    }

    pub fn add_wide_bandwidth_channel_switch(&mut self, wbcs: ie::WideBandwidthChannelSwitch) {
        self.wide_bandwidth_channel_switch.replace(wbcs);
    }

    pub fn add_transmit_power_envelope(&mut self, tpe: ie::TransmitPowerEnvelopeView<B>) {
        self.transmit_power_envelope.replace(tpe);
    }

    pub fn add_channel_switch_wrapper(&mut self, csw: ie::ChannelSwitchWrapperView<B>) {
        csw.new_country.map(|new_country| self.new_country.replace(new_country));
        csw.new_transmit_power_envelope.map(|tpe| self.add_transmit_power_envelope(tpe));
        csw.wide_bandwidth_channel_switch.map(|wbcs| self.add_wide_bandwidth_channel_switch(*wbcs));
    }
}

#[cfg(test)]
mod tests {
    use {super::*, test_case::test_case, wlan_common::assert_variant};

    const NEW_CHANNEL: u8 = 10;
    const NEW_OPERATING_CLASS: u8 = 20;
    const CHANNEL_SWITCH_COUNT: u8 = 30;

    fn csa(mode: u8) -> ie::ChannelSwitchAnnouncement {
        ie::ChannelSwitchAnnouncement {
            mode,
            new_channel_number: NEW_CHANNEL,
            channel_switch_count: CHANNEL_SWITCH_COUNT,
        }
    }

    fn ecsa(mode: u8) -> ie::ExtendedChannelSwitchAnnouncement {
        ie::ExtendedChannelSwitchAnnouncement {
            mode,
            new_operating_class: NEW_OPERATING_CLASS,
            new_channel_number: NEW_CHANNEL,
            channel_switch_count: CHANNEL_SWITCH_COUNT,
        }
    }

    fn wbcs(seg0: u8, seg1: u8) -> ie::WideBandwidthChannelSwitch {
        ie::WideBandwidthChannelSwitch {
            new_width: ie::VhtChannelBandwidth::CBW_80_160_80P80,
            new_center_freq_seg0: seg0,
            new_center_freq_seg1: seg1,
        }
    }

    #[test_case(Some(NEW_OPERATING_CLASS), false, false ; "when operating class present")]
    #[test_case(None, true, false ; "when new TPE present")]
    #[test_case(Some(NEW_OPERATING_CLASS), true, false ; "when operating class and new TPE present")]
    #[test_case(None, false, true ; "when operating class and new TPE absent")]
    #[fuchsia::test]
    fn channel_switch_compatible(
        new_operating_class: Option<u8>,
        new_transmit_power_envelope_specified: bool,
        expected_compatible: bool,
    ) {
        let channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW20,
                secondary80: 0,
            },
            pause_transmission: false,
            new_operating_class,
            new_transmit_power_envelope_specified,
        };
        assert_eq!(channel_switch.compatible(), expected_compatible);
    }

    #[test]
    fn empty_builder_fails() {
        let builder = ChannelSwitchBuilder::<&[u8]>::default();
        let result = builder.build();
        let err = result.expect_err("Empty channel switch build should fail.");
        assert_variant!(err, ChannelSwitchError::NotEnoughInfo);
    }

    #[test_case(0, false ; "when transmission is not paused")]
    #[test_case(1, true ; "when transmission is paused")]
    #[fuchsia::test]
    fn basic_csa_20mhz(mode: u8, pause_transmission: bool) {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(mode));
        let channel_switch = builder.build().expect("Valid CSA should build.");
        let expected_channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW20,
                secondary80: 0,
            },
            pause_transmission,
            new_operating_class: None,
            new_transmit_power_envelope_specified: false,
        };
        assert_eq!(channel_switch, expected_channel_switch);
    }

    #[test_case(0, false ; "when transmission is not paused")]
    #[test_case(1, true ; "when transmission is paused")]
    #[fuchsia::test]
    fn basic_ecsa_20mhz(mode: u8, pause_transmission: bool) {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_extended_channel_switch_announcement(ecsa(mode));
        let channel_switch = builder.build().expect("Valid CSA should build.");
        let expected_channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW20,
                secondary80: 0,
            },
            pause_transmission,
            new_operating_class: Some(NEW_OPERATING_CLASS),
            new_transmit_power_envelope_specified: false,
        };
        assert_eq!(channel_switch, expected_channel_switch);
    }

    #[test]
    fn basic_csa_40mhz() {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(0));
        builder.add_secondary_channel_offset(ie::SecChanOffset::SECONDARY_ABOVE);
        let channel_switch = builder.build().expect("Valid CSA should build.");
        let expected_channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW40,
                secondary80: 0,
            },
            pause_transmission: false,
            new_operating_class: None,
            new_transmit_power_envelope_specified: false,
        };
        assert_eq!(channel_switch, expected_channel_switch);
    }

    #[test]
    fn basic_csa_80mhz() {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(0));
        builder.add_secondary_channel_offset(ie::SecChanOffset::SECONDARY_ABOVE);
        builder.add_wide_bandwidth_channel_switch(wbcs(NEW_CHANNEL + 8, 0));
        let channel_switch = builder.build().expect("Valid CSA should build.");
        let expected_channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW80,
                secondary80: 0,
            },
            pause_transmission: false,
            new_operating_class: None,
            new_transmit_power_envelope_specified: false,
        };
        assert_eq!(channel_switch, expected_channel_switch);
    }

    #[test]
    fn basic_csa_160mhz() {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(0));
        builder.add_secondary_channel_offset(ie::SecChanOffset::SECONDARY_ABOVE);
        builder.add_wide_bandwidth_channel_switch(wbcs(NEW_CHANNEL + 8, NEW_CHANNEL + 16));
        let channel_switch = builder.build().expect("Valid CSA should build.");
        let expected_channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW160,
                secondary80: 0,
            },
            pause_transmission: false,
            new_operating_class: None,
            new_transmit_power_envelope_specified: false,
        };
        assert_eq!(channel_switch, expected_channel_switch);
    }

    #[test]
    fn basic_csa_80p80mhz() {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(0));
        builder.add_secondary_channel_offset(ie::SecChanOffset::SECONDARY_ABOVE);
        builder.add_wide_bandwidth_channel_switch(wbcs(NEW_CHANNEL + 8, NEW_CHANNEL + 100));
        let channel_switch = builder.build().expect("Valid CSA should build.");
        let expected_channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW80P80,
                secondary80: NEW_CHANNEL + 100,
            },
            pause_transmission: false,
            new_operating_class: None,
            new_transmit_power_envelope_specified: false,
        };
        assert_eq!(channel_switch, expected_channel_switch);
    }

    #[test_case(0, false ; "when transmission is not paused")]
    #[test_case(1, true ; "when transmission is paused")]
    #[fuchsia::test]
    fn mixed_csa_ecsa_20mhz(mode: u8, pause_transmission: bool) {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(mode));
        builder.add_extended_channel_switch_announcement(ecsa(mode));
        let channel_switch = builder.build().expect("Valid CSA should build.");
        let expected_channel_switch = ChannelSwitch {
            channel_switch_count: CHANNEL_SWITCH_COUNT,
            new_channel: banjo_common::WlanChannel {
                primary: NEW_CHANNEL,
                cbw: banjo_common::ChannelBandwidth::CBW20,
                secondary80: 0,
            },
            pause_transmission,
            new_operating_class: None,
            new_transmit_power_envelope_specified: false,
        };
        assert_eq!(channel_switch, expected_channel_switch);
    }

    #[test]
    fn mixed_csa_ecsa_mismatch_20mhz() {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(0));
        let mut ecsa = ecsa(0);
        ecsa.new_channel_number += 1;
        builder.add_extended_channel_switch_announcement(ecsa);
        let err = builder.build().expect_err("Invalid CSA should not build.");
        assert_variant!(err, ChannelSwitchError::ConflictingElements);
    }

    #[test]
    fn basic_csa_invalid_mode_20mhz() {
        let mut builder = ChannelSwitchBuilder::<&[u8]>::default();
        builder.add_channel_switch_announcement(csa(123));
        let err = builder.build().expect_err("Invalid CSA should not build.");
        assert_variant!(err, ChannelSwitchError::InvalidChannelSwitchMode(123));
    }
}
