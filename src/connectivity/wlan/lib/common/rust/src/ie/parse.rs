// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    crate::{
        buffer_reader::BufferReader,
        error::{FrameParseError, FrameParseResult},
        organization::Oui,
    },
    std::mem::size_of,
    zerocopy::{ByteSlice, LayoutVerified},
};

macro_rules! validate {
    ( $condition:expr, $debug_message:expr ) => {
        if !$condition {
            return Err($crate::error::FrameParseError::new($debug_message));
        }
    };
}

pub fn parse_ssid<B: ByteSlice>(raw_body: B) -> FrameParseResult<B> {
    validate!(raw_body.len() <= SSID_MAX_LEN, "SSID is too long");
    Ok(raw_body)
}

pub fn parse_supported_rates<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, [SupportedRate]>> {
    validate!(!raw_body.is_empty(), "Empty Supported Rates element");
    validate!(raw_body.len() <= SUPPORTED_RATES_MAX_LEN, "Too many Supported Rates");

    // unwrap() is OK because sizeof(SupportedRate) is 1, and any slice length is a multiple of 1
    Ok(LayoutVerified::new_slice_unaligned(raw_body).unwrap())
}

pub fn parse_dsss_param_set<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, DsssParamSet>> {
    LayoutVerified::new(raw_body)
        .ok_or(FrameParseError::new("Invalid length of DSSS Paramater set element"))
}

pub fn parse_tim<B: ByteSlice>(raw_body: B) -> FrameParseResult<TimView<B>> {
    let (header, bitmap) = LayoutVerified::<B, TimHeader>::new_unaligned_from_prefix(raw_body)
        .ok_or(FrameParseError::new("Element body is too short to include a TIM header"))?;
    validate!(!bitmap.is_empty(), "Bitmap in TIM is empty");
    validate!(bitmap.len() <= TIM_MAX_BITMAP_LEN, "Bitmap in TIM is too long");
    Ok(TimView { header, bitmap })
}

pub fn parse_ht_capabilities<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, HtCapabilities>> {
    LayoutVerified::new(raw_body)
        .ok_or(FrameParseError::new("Invalid length of HT Capabilities element"))
}

pub fn parse_ext_supported_rates<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, [SupportedRate]>> {
    validate!(!raw_body.is_empty(), "Empty Extended Supported Rates element");
    // unwrap() is OK because sizeof(SupportedRate) is 1, and any slice length is a multiple of 1
    Ok(LayoutVerified::new_slice_unaligned(raw_body).unwrap())
}

pub fn parse_ht_operation<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, HtOperation>> {
    LayoutVerified::new(raw_body)
        .ok_or(FrameParseError::new("Invalid length of HT Operation element"))
}

pub fn parse_mpm_open<B: ByteSlice>(raw_body: B) -> FrameParseResult<MpmOpenView<B>> {
    let mut reader = BufferReader::new(raw_body);
    let header = reader
        .read()
        .ok_or(FrameParseError::new("Element body is too short to include an MPM header"))?;
    let pmk = reader.read();
    if reader.bytes_remaining() > 0 {
        return Err(FrameParseError::new("Extra bytes at the end of the MPM Open element"));
    }
    Ok(MpmOpenView { header, pmk })
}

pub fn parse_mpm_confirm<B: ByteSlice>(raw_body: B) -> FrameParseResult<MpmConfirmView<B>> {
    let mut reader = BufferReader::new(raw_body);
    let header = reader
        .read()
        .ok_or(FrameParseError::new("Element body is too short to include an MPM header"))?;
    let peer_link_id = reader
        .read_unaligned()
        .ok_or(FrameParseError::new("Element body is too short to include a peer link ID"))?;
    let pmk = reader.read();
    if reader.bytes_remaining() > 0 {
        return Err(FrameParseError::new("Extra bytes at the end of the MPM Confirm element"));
    }
    Ok(MpmConfirmView { header, peer_link_id, pmk })
}

pub fn parse_mpm_close<B: ByteSlice>(raw_body: B) -> FrameParseResult<MpmCloseView<B>> {
    let mut reader = BufferReader::new(raw_body);
    let header = reader
        .read()
        .ok_or(FrameParseError::new("Element body is too short to include an MPM header"))?;

    let peer_link_id = if reader.bytes_remaining() % size_of::<MpmPmk>() == 4 {
        reader.read_unaligned()
    } else {
        None
    };

    let reason_code = reader
        .read_unaligned()
        .ok_or(FrameParseError::new("Element body is too short to include a reason code"))?;
    let pmk = reader.read();
    if reader.bytes_remaining() > 0 {
        return Err(FrameParseError::new("Extra bytes at the end of the MPM Close element"));
    }
    Ok(MpmCloseView { header, peer_link_id, reason_code, pmk })
}

pub fn parse_preq<B: ByteSlice>(raw_body: B) -> FrameParseResult<PreqView<B>> {
    let mut reader = BufferReader::new(raw_body);
    let header = reader
        .read::<PreqHeader>()
        .ok_or(FrameParseError::new("Element body is too short to include a PREQ header"))?;
    let originator_external_addr = if header.flags.addr_ext() {
        let addr = reader.read().ok_or(FrameParseError::new(
            "Element body is too short to include an external address",
        ))?;
        Some(addr)
    } else {
        None
    };
    let middle = reader
        .read::<PreqMiddle>()
        .ok_or(FrameParseError::new("Element body is too short to include middle PREQ fields"))?;
    let targets = reader
        .read_array(middle.target_count as usize)
        .ok_or(FrameParseError::new("Element body is too short to include all PREQ targets"))?;
    if reader.bytes_remaining() > 0 {
        return Err(FrameParseError::new("Extra bytes at the end of the PREQ element"));
    }
    Ok(PreqView { header, originator_external_addr, middle, targets })
}

pub fn parse_prep<B: ByteSlice>(raw_body: B) -> FrameParseResult<PrepView<B>> {
    let mut reader = BufferReader::new(raw_body);
    let header = reader
        .read::<PrepHeader>()
        .ok_or(FrameParseError::new("Element body is too short to include a PREP header"))?;
    let target_external_addr = if header.flags.addr_ext() {
        let addr = reader.read().ok_or(FrameParseError::new(
            "Element body is too short to include an external address",
        ))?;
        Some(addr)
    } else {
        None
    };
    let tail = reader
        .read()
        .ok_or(FrameParseError::new("Element body is too short to include a PREP tail"))?;
    if reader.bytes_remaining() > 0 {
        return Err(FrameParseError::new("Extra bytes at the end of the PREP element"));
    }
    Ok(PrepView { header, target_external_addr, tail })
}

pub fn parse_perr<B: ByteSlice>(raw_body: B) -> FrameParseResult<PerrView<B>> {
    let mut reader = BufferReader::new(raw_body);
    let header = reader
        .read::<PerrHeader>()
        .ok_or(FrameParseError::new("Element body is too short to include a PERR header"))?;
    let destinations = PerrDestinationListView(reader.into_remaining());
    Ok(PerrView { header, destinations })
}

pub fn parse_vht_capabilities<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, VhtCapabilities>> {
    LayoutVerified::new(raw_body)
        .ok_or(FrameParseError::new("Invalid length of VHT Capabilities element"))
}

pub fn parse_vht_operation<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, VhtOperation>> {
    LayoutVerified::new(raw_body)
        .ok_or(FrameParseError::new("Invalid length of VHT Operation element"))
}

pub fn parse_wpa_ie<B: ByteSlice>(raw_body: B) -> FrameParseResult<wpa::WpaIe> {
    wpa::from_bytes(&raw_body[..])
        .map(|(_, r)| r)
        .map_err(|_| FrameParseError::new("Failed to parse WPA IE"))
}

pub fn parse_vendor_ie<B: ByteSlice>(raw_body: B) -> FrameParseResult<VendorIe<B>> {
    let mut reader = BufferReader::new(raw_body);
    let oui = *reader.read::<Oui>().ok_or(FrameParseError::new("Failed to read vendor OUI"))?;
    let vendor_ie = match oui {
        Oui::MSFT => {
            let ie_type = reader.peek_byte();
            match ie_type {
                Some(wpa::VENDOR_SPECIFIC_TYPE) => {
                    // We already know from our peek_byte that at least one byte remains, so this
                    // split will not panic.
                    let (_type, body) = reader.into_remaining().split_at(1);
                    VendorIe::MsftLegacyWpa(body)
                }
                _ => VendorIe::Unknown { oui, body: reader.into_remaining() },
            }
        }
        _ => VendorIe::Unknown { oui, body: reader.into_remaining() },
    };
    Ok(vendor_ie)
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::assert_variant, fidl_fuchsia_wlan_mlme as fidl_mlme,
        std::convert::TryInto, zerocopy::AsBytes,
    };

    #[test]
    pub fn ssid_ok() {
        assert_eq!(Ok(&[][..]), parse_ssid(&[][..]));
        assert_eq!(Ok(&[1, 2, 3][..]), parse_ssid(&[1, 2, 3][..]));
    }

    #[test]
    pub fn ssid_too_long() {
        assert_eq!(Err(FrameParseError::new("SSID is too long")), parse_ssid(&[0u8; 33][..]));
    }

    #[test]
    pub fn supported_rates_ok() {
        let r = parse_supported_rates(&[1, 2, 3][..]).expect("expected Ok");
        assert_eq!(&[SupportedRate(1), SupportedRate(2), SupportedRate(3)][..], &r[..]);
    }

    #[test]
    pub fn supported_rates_empty() {
        let err = parse_supported_rates(&[][..]).expect_err("expected Err");
        assert_eq!("Empty Supported Rates element", err.debug_message());
    }

    #[test]
    pub fn supported_rates_too_long() {
        let err = parse_supported_rates(&[0u8; 9][..]).expect_err("expected Err");
        assert_eq!("Too many Supported Rates", err.debug_message());
    }

    #[test]
    pub fn dsss_param_set_ok() {
        let r = parse_dsss_param_set(&[6u8][..]).expect("expected Ok");
        assert_eq!(6, r.current_chan);
    }

    #[test]
    pub fn dsss_param_set_wrong_size() {
        let err = parse_dsss_param_set(&[][..]).err().expect("expected Err for empty slice");
        assert_eq!("Invalid length of DSSS Paramater set element", err.debug_message());
        let err = parse_dsss_param_set(&[1, 2][..]).err().expect("expected Err for long slice");
        assert_eq!("Invalid length of DSSS Paramater set element", err.debug_message());
    }

    #[test]
    pub fn tim_ok() {
        let r = parse_tim(&[1, 2, 3, 4, 5][..]).expect("expected Ok");
        assert_eq!(2, r.header.dtim_period);
        assert_eq!(&[4, 5][..], r.bitmap);
    }

    #[test]
    pub fn tim_too_short_for_header() {
        let err = parse_tim(&[1, 2][..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include a TIM header", err.debug_message());
    }

    #[test]
    pub fn tim_empty_bitmap() {
        let err = parse_tim(&[1, 2, 3][..]).err().expect("expected Err");
        assert_eq!("Bitmap in TIM is empty", err.debug_message());
    }

    #[test]
    pub fn tim_bitmap_too_long() {
        let err = parse_tim(&[0u8; 255][..]).err().expect("expected Err");
        assert_eq!("Bitmap in TIM is too long", err.debug_message());
    }

    #[test]
    fn ht_capabilities_wrong_size() {
        let err = parse_ht_capabilities(&[0u8; 25][..]).err().expect("expected Err");
        assert_eq!("Invalid length of HT Capabilities element", err.debug_message());
        let err = parse_ht_capabilities(&[0u8; 27][..]).err().expect("expected Err");
        assert_eq!("Invalid length of HT Capabilities element", err.debug_message());
    }

    #[test]
    fn ht_capabilities_ok() {
        // HtCapabilities element without Element Id and length
        #[rustfmt::skip]
        let raw_body = [
            0x4e, 0x11, // HtCapabilitiInfo(u16)
            0x1b, // AmpduParams(u8)
            0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xab, 0xcd, 0x00, 0x00, 0x00, 0x00, // SupportedMcsSet(u128)
            0x06, 0x03, // HtExtCapabilities(u16)
            0xc0, 0xb0, 0xcb, 0x13, // TxBfCapability(u32)
            0x00, // AselCapability(u8)
        ];
        let ht_cap = parse_ht_capabilities(&raw_body[..]).expect("valid frame should result in OK");

        let ht_cap_info = ht_cap.ht_cap_info;
        assert_eq!(ht_cap_info.0, 0x114e);
        assert_eq!(ht_cap_info.chan_width_set(), ChanWidthSet::TWENTY_FORTY);
        assert_eq!(ht_cap_info.sm_power_save(), SmPowerSave::DISABLED);
        assert_eq!(ht_cap_info.max_amsdu_len(), MaxAmsduLen::OCTETS_3839);

        let ampdu_params = ht_cap.ampdu_params;
        assert_eq!(ampdu_params.0, 0x1b);
        assert_eq!(ampdu_params.max_ampdu_exponent().to_len(), 65535);
        assert_eq!(ampdu_params.min_start_spacing(), MinMpduStartSpacing::EIGHT_USEC);

        let mcs_set = ht_cap.mcs_set;
        assert_eq!(mcs_set.0, 0x00000000_cdab0000_00000000_000000ff);
        assert_eq!(mcs_set.rx_mcs().0, 0xff);
        assert_eq!(mcs_set.rx_mcs().support(7), true);
        assert_eq!(mcs_set.rx_mcs().support(8), false);
        assert_eq!(mcs_set.rx_highest_rate(), 0x01ab);

        let ht_ext_cap = ht_cap.ht_ext_cap;
        let raw_value = ht_ext_cap.0;
        assert_eq!(raw_value, 0x0306);
        assert_eq!(ht_ext_cap.pco_transition(), PcoTransitionTime::PCO_5000_USEC);
        assert_eq!(ht_ext_cap.mcs_feedback(), McsFeedback::BOTH);

        let txbf_cap = ht_cap.txbf_cap;
        let raw_value = txbf_cap.0;
        assert_eq!(raw_value, 0x13cbb0c0);
        assert_eq!(txbf_cap.calibration(), Calibration::RESPOND_INITIATE);
        assert_eq!(txbf_cap.csi_feedback(), Feedback::IMMEDIATE);
        assert_eq!(txbf_cap.noncomp_feedback(), Feedback::DELAYED);
        assert_eq!(txbf_cap.min_grouping(), MinGroup::TWO);

        // human-readable representation
        assert_eq!(txbf_cap.csi_antennas().to_human(), 2);
        assert_eq!(txbf_cap.noncomp_steering_ants().to_human(), 3);
        assert_eq!(txbf_cap.comp_steering_ants().to_human(), 4);
        assert_eq!(txbf_cap.csi_rows().to_human(), 2);
        assert_eq!(txbf_cap.chan_estimation().to_human(), 3);

        let asel_cap = ht_cap.asel_cap;
        assert_eq!(asel_cap.0, 0);
    }

    #[test]
    pub fn ext_supported_rates_ok() {
        let r = parse_ext_supported_rates(&[1, 2, 3][..]).expect("expected Ok");
        assert_eq!(&[SupportedRate(1), SupportedRate(2), SupportedRate(3)][..], &r[..]);
    }

    #[test]
    pub fn ext_supported_rates_empty() {
        let err = parse_ext_supported_rates(&[][..]).expect_err("expected Err");
        assert_eq!("Empty Extended Supported Rates element", err.debug_message());
    }

    #[test]
    fn ht_operation_wrong_size() {
        let err = parse_ht_operation(&[0u8; 21][..]).err().expect("expected Err");
        assert_eq!("Invalid length of HT Operation element", err.debug_message());
        let err = parse_ht_operation(&[0u8; 23][..]).err().expect("expected Err");
        assert_eq!("Invalid length of HT Operation element", err.debug_message());
    }

    #[test]
    fn ht_operation_ok() {
        // HtOperation element without Element Id and length
        #[rustfmt::skip]
        let raw_body = [
            99, // primary_chan(u8)
            0xff, // ht_op_info_head(HtOpInfoHead(u8))
            0xfe, 0xff, 0xff, 0xff, // ht_op_info_tail(HtOpInfoTail(u8),
            0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xab, 0xcd, 0x00, 0x00, 0x00, 0x00,
            // basic_ht_mcs_set(SupportedMcsSet(u128))
        ];
        let ht_op = parse_ht_operation(&raw_body[..]).expect("valid frame should result in OK");

        assert_eq!(ht_op.primary_chan, 99);

        let ht_op_info_head = ht_op.ht_op_info_head;
        assert_eq!(ht_op_info_head.secondary_chan_offset(), SecChanOffset::SECONDARY_BELOW);
        assert_eq!(ht_op_info_head.sta_chan_width(), StaChanWidth::ANY);
        assert_eq!(ht_op_info_head.ht_protection(), HtProtection::TWENTY_MHZ);

        let ht_op_info_tail = ht_op.ht_op_info_tail;
        assert_eq!(ht_op_info_tail.pco_phase(), PcoPhase::FORTY_MHZ);

        let basic_mcs_set = ht_op.basic_ht_mcs_set;
        assert_eq!(basic_mcs_set.0, 0x00000000_cdab0000_00000000_000000ff);
    }

    #[test]
    fn mpm_open_ok_no_pmk() {
        let r = parse_mpm_open(&[0x11, 0x22, 0x33, 0x44][..]).expect("expected Ok");
        assert_eq!(0x2211, { r.header.protocol.0 });
        assert_eq!(0x4433, { r.header.local_link_id });
        assert!(r.pmk.is_none());
    }

    #[test]
    fn mpm_open_ok_with_pmk() {
        #[rustfmt::skip]
        let data = [0x11, 0x22, 0x33, 0x44,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let r = parse_mpm_open(&data[..]).expect("expected Ok");
        assert_eq!(0x2211, { r.header.protocol.0 });
        assert_eq!(0x4433, { r.header.local_link_id });
        let pmk = r.pmk.expect("expected pmk to be present");
        assert_eq!([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16], pmk.0);
    }

    #[test]
    fn mpm_open_too_short() {
        let err = parse_mpm_open(&[0x11, 0x22, 0x33][..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include an MPM header", err.debug_message());
    }

    #[test]
    fn mpm_open_weird_length() {
        let err = parse_mpm_open(&[0x11, 0x22, 0x33, 0x44, 0x55][..]).err().expect("expected Err");
        assert_eq!("Extra bytes at the end of the MPM Open element", err.debug_message());
    }

    #[test]
    fn mpm_open_too_long() {
        #[rustfmt::skip]
        let data = [0x11, 0x22, 0x33, 0x44,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17];
        let err = parse_mpm_open(&data[..]).err().expect("expected Err");
        assert_eq!("Extra bytes at the end of the MPM Open element", err.debug_message());
    }

    #[test]
    fn mpm_confirm_ok_no_pmk() {
        let r = parse_mpm_confirm(&[0x11, 0x22, 0x33, 0x44, 0x55, 0x66][..]).expect("expected Ok");
        assert_eq!(0x2211, { r.header.protocol.0 });
        assert_eq!(0x4433, { r.header.local_link_id });
        assert_eq!(0x6655, r.peer_link_id.get());
        assert!(r.pmk.is_none());
    }

    #[test]
    fn mpm_confirm_ok_with_pmk() {
        #[rustfmt::skip]
        let data = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let r = parse_mpm_confirm(&data[..]).expect("expected Ok");
        assert_eq!(0x2211, { r.header.protocol.0 });
        assert_eq!(0x4433, { r.header.local_link_id });
        assert_eq!(0x6655, r.peer_link_id.get());
        let pmk = r.pmk.expect("expected pmk to be present");
        assert_eq!([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16], pmk.0);
    }

    #[test]
    fn mpm_confirm_too_short_for_header() {
        let err = parse_mpm_confirm(&[0x11, 0x22, 0x33][..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include an MPM header", err.debug_message());
    }

    #[test]
    fn mpm_confirm_too_short_for_peer_link_id() {
        let err =
            parse_mpm_confirm(&[0x11, 0x22, 0x33, 0x44, 0x55][..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include a peer link ID", err.debug_message());
    }

    #[test]
    fn mpm_confirm_weird_length() {
        let err = parse_mpm_confirm(&[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77][..])
            .err()
            .expect("expected Err");
        assert_eq!("Extra bytes at the end of the MPM Confirm element", err.debug_message());
    }

    #[test]
    fn mpm_confirm_too_long() {
        #[rustfmt::skip]
        let data = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17];
        let err = parse_mpm_confirm(&data[..]).err().expect("expected Err");
        assert_eq!("Extra bytes at the end of the MPM Confirm element", err.debug_message());
    }

    #[test]
    fn mpm_close_ok_no_link_id_no_pmk() {
        let data = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
        let r = parse_mpm_close(&data[..]).expect("expected Ok");
        assert_eq!(0x2211, { r.header.protocol.0 });
        assert_eq!(0x4433, { r.header.local_link_id });
        assert!(r.peer_link_id.is_none());
        assert_eq!(0x6655, r.reason_code.get().0);
        assert!(r.pmk.is_none());
    }

    #[test]
    fn mpm_close_ok_with_link_id_no_pmk() {
        let data = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88];
        let r = parse_mpm_close(&data[..]).expect("expected Ok");
        assert_eq!(0x4433, { r.header.local_link_id });
        let peer_link_id = r.peer_link_id.expect("expected peer link id to be present");
        assert_eq!(0x6655, peer_link_id.get());
        assert_eq!(0x8877, r.reason_code.get().0);
        assert!(r.pmk.is_none());
    }

    #[test]
    fn mpm_close_ok_no_link_id_with_pmk() {
        #[rustfmt::skip]
        let data = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let r = parse_mpm_close(&data[..]).expect("expected Ok");
        assert_eq!(0x2211, { r.header.protocol.0 });
        assert_eq!(0x4433, { r.header.local_link_id });
        assert!(r.peer_link_id.is_none());
        assert_eq!(0x6655, r.reason_code.get().0);
        let pmk = r.pmk.expect("expected pmk to be present");
        assert_eq!([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16], pmk.0);
    }

    #[test]
    fn mpm_close_ok_with_link_id_with_pmk() {
        #[rustfmt::skip]
        let data = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let r = parse_mpm_close(&data[..]).expect("expected Ok");
        assert_eq!(0x4433, { r.header.local_link_id });
        let peer_link_id = r.peer_link_id.expect("expected peer link id to be present");
        assert_eq!(0x6655, peer_link_id.get());
        assert_eq!(0x8877, r.reason_code.get().0);
        let pmk = r.pmk.expect("expected pmk to be present");
        assert_eq!([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16], pmk.0);
    }

    #[test]
    fn mpm_close_too_short_for_header() {
        let err = parse_mpm_close(&[0x11, 0x22, 0x33][..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include an MPM header", err.debug_message());
    }

    #[test]
    fn mpm_close_too_short_for_reason_code() {
        let err = parse_mpm_close(&[0x11, 0x22, 0x33, 0x44, 0x55][..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include a reason code", err.debug_message());
    }

    #[test]
    fn mpm_close_weird_length_1() {
        let err = parse_mpm_close(&[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77][..])
            .err()
            .expect("expected Err");
        assert_eq!("Extra bytes at the end of the MPM Close element", err.debug_message());
    }

    #[test]
    fn mpm_close_weird_length_2() {
        let err = parse_mpm_close(&[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99][..])
            .err()
            .expect("expected Err");
        assert_eq!("Extra bytes at the end of the MPM Close element", err.debug_message());
    }

    #[test]
    fn mpm_close_too_long() {
        #[rustfmt::skip]
        let data = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17];
        let err = parse_mpm_close(&data[..]).err().expect("expected Err");
        assert_eq!("Extra bytes at the end of the MPM Close element", err.debug_message());
    }

    #[test]
    fn preq_ok_minimal() {
        #[rustfmt::skip]
        let data = [
            0x00, // flags
            0x02, // hop count
            0x03, // element ttl
            0x04, 0x05, 0x06, 0x07, // path discovery ID
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
            0x18, 0x19, 0x1a, 0x1b, // lifetime
            0x1c, 0x1d, 0x1e, 0x1f, // metric
            // Target count. Having no targets probably doesn't make sense,
            // but we test this code path anyway.
            0,
        ];
        let r = parse_preq(&data[..]).expect("expected Ok");
        assert_eq!(0x02, r.header.hop_count);
        assert!(r.originator_external_addr.is_none());
        assert_eq!(0x1b1a1918, { r.middle.lifetime });
        assert_eq!(0, r.targets.len());
    }

    #[test]
    fn preq_ok_full() {
        #[rustfmt::skip]
        let data = [
            0x40, // flags: address extension = true
            0x02, // hop count
            0x03, // element ttl
            0x04, 0x05, 0x06, 0x07, // path discovery ID
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
            0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, // originator external address
            0x18, 0x19, 0x1a, 0x1b, // lifetime
            0x1c, 0x1d, 0x1e, 0x1f, // metric
            2, // target count
            // Target 1
            0x00, // target flags
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // target address
            0xa1, 0xa2, 0xa3, 0xa4, // target hwmp seqno
            // Target 2
            0x00, // target flags
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, // target address
            0xb1, 0xb2, 0xb3, 0xb4, // target hwmp seqno
        ];
        let r = parse_preq(&data[..]).expect("expected Ok");
        assert_eq!(0x02, r.header.hop_count);
        let ext_addr = r.originator_external_addr.expect("expected ext addr to be present");
        assert_eq!([0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b], *ext_addr);
        assert_eq!(0x1b1a1918, { r.middle.lifetime });
        assert_eq!(2, r.targets.len());
        assert_eq!([0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb], r.targets[1].target_addr);
    }

    #[test]
    fn preq_too_short_for_header() {
        #[rustfmt::skip]
        let data = [
            0x00, // flags
            0x02, // hop count
            0x03, // element ttl
            0x04, 0x05, 0x06, 0x07, // path discovery ID
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x0e, 0x0f, 0x10, // one byte missing from originator hwmp seqno
        ];
        let err = parse_preq(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include a PREQ header", err.debug_message());
    }

    #[test]
    fn preq_too_short_for_ext_addr() {
        #[rustfmt::skip]
        let data = [
            0x40, // flags: address extension = true
            0x02, // hop count
            0x03, // element ttl
            0x04, 0x05, 0x06, 0x07, // path discovery ID
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
            0x16, 0x17, 0x18, 0x19, 0x1a, // one byte missing from originator external address
        ];
        let err = parse_preq(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include an external address", err.debug_message());
    }

    #[test]
    fn preq_too_short_for_middle() {
        #[rustfmt::skip]
        let data = [
            0x00, // flags
            0x02, // hop count
            0x03, // element ttl
            0x04, 0x05, 0x06, 0x07, // path discovery ID
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
            0x18, 0x19, 0x1a, 0x1b, // lifetime
            0x1c, 0x1d, 0x1e, 0x1f, // metric
            // Target count missing
        ];
        let err = parse_preq(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include middle PREQ fields", err.debug_message());
    }

    #[test]
    fn preq_too_short_for_targets() {
        #[rustfmt::skip]
        let data = [
            0x40, // flags: address extension = true
            0x02, // hop count
            0x03, // element ttl
            0x04, 0x05, 0x06, 0x07, // path discovery ID
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
            0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, // originator external address
            0x18, 0x19, 0x1a, 0x1b, // lifetime
            0x1c, 0x1d, 0x1e, 0x1f, // metric
            2, // target count
            // Target 1
            0x00, // target flags
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // target address
            0xa1, 0xa2, 0xa3, 0xa4, // target hwmp seqno
            // Target 2
            0x00, // target flags
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, // target address
            0xb1, 0xb2, 0xb3, // one byte missing from target hwmp seqno
        ];
        let err = parse_preq(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include all PREQ targets", err.debug_message());
    }

    #[test]
    fn preq_too_long() {
        #[rustfmt::skip]
        let data = [
            0x00, // flags
            0x02, // hop count
            0x03, // element ttl
            0x04, 0x05, 0x06, 0x07, // path discovery ID
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
            0x18, 0x19, 0x1a, 0x1b, // lifetime
            0x1c, 0x1d, 0x1e, 0x1f, // metric
            0, // target count
            1 // extra byte
        ];
        let err = parse_preq(&data[..]).err().expect("expected Err");
        assert_eq!("Extra bytes at the end of the PREQ element", err.debug_message());
    }

    #[test]
    fn prep_ok_no_ext() {
        #[rustfmt::skip]
        let data = [
            0x00, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
            0x0d, 0x0e, 0x0f, 0x10, // lifetime
            0x11, 0x12, 0x13, 0x14, // metric
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
            0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
        ];
        let r = parse_prep(&data[..]).expect("expected Ok");
        assert_eq!(0x0c0b0a09, { r.header.target_hwmp_seqno });
        assert!(r.target_external_addr.is_none());
        assert_eq!(0x14131211, { r.tail.metric });
    }

    #[test]
    fn prep_ok_with_ext() {
        #[rustfmt::skip]
        let data = [
            0x40, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
            0x44, 0x55, 0x66, 0x77, 0x88, 0x99, // target external addr
            0x0d, 0x0e, 0x0f, 0x10, // lifetime
            0x11, 0x12, 0x13, 0x14, // metric
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
            0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
        ];
        let r = parse_prep(&data[..]).expect("expected Ok");
        assert_eq!(0x0c0b0a09, { r.header.target_hwmp_seqno });
        let ext_addr = r.target_external_addr.expect("expected an external address");
        assert_eq!([0x44, 0x55, 0x66, 0x77, 0x88, 0x99], *ext_addr);
        assert_eq!(0x14131211, { r.tail.metric });
    }

    #[test]
    fn prep_too_short_for_header() {
        #[rustfmt::skip]
        let data = [
            0x00, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, // one byte missing from target hwmp seqno
        ];
        let err = parse_prep(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include a PREP header", err.debug_message());
    }

    #[test]
    fn prep_too_short_for_ext_addr() {
        #[rustfmt::skip]
        let data = [
            0x40, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
            0x44, 0x55, 0x66, 0x77, 0x88, // one byte missing from target external addr
        ];
        let err = parse_prep(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include an external address", err.debug_message());
    }

    #[test]
    fn prep_too_short_for_tail() {
        #[rustfmt::skip]
        let data = [
            0x00, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
            0x0d, 0x0e, 0x0f, 0x10, // lifetime
            0x11, 0x12, 0x13, 0x14, // metric
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
            0x1b, 0x1c, 0x1d, // one byte missing from originator hwmp seqno
        ];
        let err = parse_prep(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include a PREP tail", err.debug_message());
    }

    #[test]
    fn prep_too_long() {
        #[rustfmt::skip]
        let data = [
            0x00, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
            0x0d, 0x0e, 0x0f, 0x10, // lifetime
            0x11, 0x12, 0x13, 0x14, // metric
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
            0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
            0, // extra byte
        ];
        let err = parse_prep(&data[..]).err().expect("expected Err");
        assert_eq!("Extra bytes at the end of the PREP element", err.debug_message());
    }

    #[test]
    fn perr_ok() {
        #[rustfmt::skip]
        let data = [
            11, // TTL
            1, // number of destinations
            // Destination 1
            0, // flags
            0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, // dest addr
            0x77, 0x88, 0x99, 0xaa, // HWMP seqno
            0xbb, 0xcc, // reason code
        ];
        let r = parse_perr(&data[..]).expect("expected Ok");
        assert_eq!(11, r.header.element_ttl);

        let destinations = r.destinations.iter().collect::<Vec<_>>();
        assert_eq!(1, destinations.len());
        assert_eq!(0xaa998877, { destinations[0].header.hwmp_seqno });
    }

    #[test]
    fn perr_too_short_for_header() {
        let data = [11];
        let err = parse_perr(&data[..]).err().expect("expected Err");
        assert_eq!("Element body is too short to include a PERR header", err.debug_message());
    }

    #[test]
    fn vht_capabilities_wrong_size() {
        let err = parse_vht_capabilities(&[0u8; 11][..]).err().expect("expected Err");
        assert_eq!("Invalid length of VHT Capabilities element", err.debug_message());
        let err = parse_vht_capabilities(&[0u8; 13][..]).err().expect("expected Err");
        assert_eq!("Invalid length of VHT Capabilities element", err.debug_message());
    }

    #[test]
    fn vht_capabilities_ok() {
        // VhtCapabilities element without Element Id and length
        #[rustfmt::skip]
        let raw_body = [
            0xfe, 0xff, 0xff, 0xff, // VhtCapabilitiesInfo(u32)
            0xff, 0xaa, 0x00, 0x00, 0x55, 0xff, 0x00, 0x00, // VhtMcsNssSet(u64)
        ];
        let vht_cap = parse_vht_capabilities(&raw_body[..]).expect("expected OK from valid frames");

        let cap_info = vht_cap.vht_cap_info;
        assert_eq!(cap_info.max_mpdu_len(), MaxMpduLen::OCTECTS_11454);
        assert_eq!(cap_info.link_adapt(), VhtLinkAdaptation::BOTH);
        let max_ampdu_component = cap_info.max_ampdu_exponent();
        assert_eq!(max_ampdu_component.to_len(), 1048575);

        let mcs_nss = vht_cap.vht_mcs_nss;
        assert_eq!(mcs_nss.rx_max_mcs().ss1(), VhtMcsSet::NONE);
        assert_eq!(mcs_nss.rx_max_mcs().ss7(), VhtMcsSet::UPTO_9);
        assert_eq!(mcs_nss.tx_max_mcs().ss1(), VhtMcsSet::UPTO_8);
        assert_eq!(mcs_nss.tx_max_mcs().ss7(), VhtMcsSet::NONE);

        assert_eq!(mcs_nss.rx_max_mcs().ss(2), Ok(VhtMcsSet::NONE));
        assert_eq!(mcs_nss.rx_max_mcs().ss(6), Ok(VhtMcsSet::UPTO_9));
        assert_eq!(mcs_nss.tx_max_mcs().ss(2), Ok(VhtMcsSet::UPTO_8));
        assert_eq!(mcs_nss.tx_max_mcs().ss(6), Ok(VhtMcsSet::NONE));
    }

    #[test]
    fn vht_operation_wrong_size() {
        let err = parse_vht_operation(&[0u8; 4][..]).err().expect("expected Err");
        assert_eq!("Invalid length of VHT Operation element", err.debug_message());
        let err = parse_vht_operation(&[0u8; 6][..]).err().expect("expected Err");
        assert_eq!("Invalid length of VHT Operation element", err.debug_message());
    }

    #[test]
    fn vht_operation_ok() {
        // VhtOperation element without Element Id and length
        #[rustfmt::skip]
        let raw_body = [
            231, // vht_cbw(u8)
            232, // center_freq_seg0(u8)
            233, // center_freq_seg1(u8)
            0xff, 0x66, // basic_mcs_nss(VhtMcsNssMap(u16))
        ];
        let vht_op = parse_vht_operation(&raw_body[..]).expect("expected OK from valid frames");
        assert_eq!(231, vht_op.vht_cbw.0);
        assert_eq!(232, vht_op.center_freq_seg0);
        assert_eq!(233, vht_op.center_freq_seg1);
    }

    #[test]
    fn parse_wpa_ie_ok() {
        let raw_body: Vec<u8> = vec![
            0x00, 0x50, 0xf2, // MSFT OUI
            0x01, 0x01, 0x00, // WPA IE header
            0x00, 0x50, 0xf2, 0x02, // multicast cipher: AKM
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher: TKIP
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
        ];
        let wpa_ie = parse_vendor_ie(&raw_body[..]).expect("failed to parse wpa vendor ie");
        assert_variant!(wpa_ie, VendorIe::MsftLegacyWpa(wpa_body) => {
            parse_wpa_ie(&wpa_body[..]).expect("failed to parse wpa vendor ie")
        });
    }

    #[test]
    fn parse_bad_wpa_ie() {
        let raw_body: Vec<u8> = vec![
            0x00, 0x50, 0xf2, // MSFT OUI
            0x01, 0x01, 0x00, // WPA IE header
            0x00, 0x50, 0xf2, 0x02, // multicast cipher: AKM
                  // truncated
        ];
        // parse_vendor_ie does not validate the actual wpa ie body, so this
        // succeeds.
        let wpa_ie = parse_vendor_ie(&raw_body[..]).expect("failed to parse wpa vendor ie");
        assert_variant!(wpa_ie, VendorIe::MsftLegacyWpa(wpa_body) => {
            parse_wpa_ie(&wpa_body[..]).expect_err("parsed truncated wpa ie")
        });
    }

    #[test]
    fn parse_unknown_msft_ie() {
        let raw_body: Vec<u8> = vec![
            0x00, 0x50, 0xf2, // MSFT OUI
            0xff, 0x01, 0x00, // header with unknown vendor specific IE type
            0x00, 0x50, 0xf2, 0x02, // multicast cipher: AKM
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher: TKIP
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
        ];
        let ie = parse_vendor_ie(&raw_body[..]).expect("failed to parse ie");
        assert_variant!(ie, VendorIe::Unknown { .. });
    }

    #[test]
    fn parse_unknown_vendor_ie() {
        let raw_body: Vec<u8> = vec![0x00, 0x12, 0x34]; // Made up OUI
        let ie = parse_vendor_ie(&raw_body[..]).expect("failed to parse wpa vendor ie");
        assert_variant!(ie, VendorIe::Unknown { .. });
    }

    #[test]
    fn to_and_from_fidl_ht_cap() {
        fidl_mlme::HtCapabilities {
            bytes: fake_ht_capabilities().as_bytes().try_into().expect("HT Cap to FIDL"),
        };
        let fidl = fidl_mlme::HtCapabilities { bytes: [0; fidl_mlme::HT_CAP_LEN as usize] };
        assert!(parse_ht_capabilities(&fidl.bytes[..]).is_ok());
    }

    #[test]
    fn to_and_from_fidl_vht_cap() {
        fidl_mlme::VhtCapabilities {
            bytes: fake_vht_capabilities().as_bytes().try_into().expect("VHT Cap to FIDL"),
        };
        let fidl = fidl_mlme::VhtCapabilities { bytes: [0; fidl_mlme::VHT_CAP_LEN as usize] };
        assert!(parse_vht_capabilities(&fidl.bytes[..]).is_ok());
    }

    #[test]
    fn to_and_from_fidl_ht_op() {
        fidl_mlme::HtOperation {
            bytes: fake_ht_operation().as_bytes().try_into().expect("HT Op to FIDL"),
        };
        let fidl = fidl_mlme::HtOperation { bytes: [0; fidl_mlme::HT_OP_LEN as usize] };
        assert!(parse_ht_operation(&fidl.bytes[..]).is_ok());
    }

    #[test]
    fn to_and_from_fidl_vht_op() {
        fidl_mlme::VhtOperation {
            bytes: fake_vht_operation().as_bytes().try_into().expect("VHT Op to FIDL"),
        };
        let fidl = fidl_mlme::VhtOperation { bytes: [0; fidl_mlme::VHT_OP_LEN as usize] };
        assert!(parse_vht_operation(&fidl.bytes[..]).is_ok());
    }
}
