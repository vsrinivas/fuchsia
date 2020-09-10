// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{constants::*, fields::*, id::Id, rsn::rsne, wpa, wsc},
    crate::{
        appendable::{Appendable, BufferTooSmall},
        error::FrameWriteError,
        mac::{MacAddr, ReasonCode},
        organization::Oui,
    },
    std::mem::size_of,
    zerocopy::AsBytes,
};

macro_rules! validate {
    ( $condition:expr, $fmt:expr $(, $args:expr)* ) => {
        if !$condition {
            return Err($crate::error::FrameWriteError::new_invalid_data(format!($fmt, $($args,)*)));
        }
    };
}

macro_rules! write_ie {
    ( $buf:expr, $id:expr, $( $part:expr ),* ) => {
        {
            let body_len = 0 $( + ::std::mem::size_of_val($part) )*;
            validate!(body_len <= crate::ie::IE_MAX_LEN,
                "Element body length {} exceeds max of 255", body_len);
            if !$buf.can_append(2 + body_len) {
                return Err(FrameWriteError::BufferTooSmall);
            }
            $buf.append_value(&$id)
                    .expect("expected enough room in the buffer for element id");
            $buf.append_byte(body_len as u8)
                    .expect("expected enough room in the buffer for element length");
            $(
                $buf.append_value($part)
                        .expect("expected enough room in the buffer for element fields");
            )*
            Ok(())
        }
    }
}

pub fn write_ssid<B: Appendable>(buf: &mut B, ssid: &[u8]) -> Result<(), FrameWriteError> {
    validate!(
        ssid.len() <= SSID_MAX_LEN,
        "SSID is too long (max: {} bytes, got: {})",
        SSID_MAX_LEN,
        ssid.len()
    );
    write_ie!(buf, Id::SSID, ssid)
}

pub fn write_supported_rates<B: Appendable>(
    buf: &mut B,
    rates: &[u8],
) -> Result<(), FrameWriteError> {
    validate!(!rates.is_empty(), "List of Supported Rates is empty");
    validate!(
        rates.len() <= SUPPORTED_RATES_MAX_LEN,
        "Too many Supported Rates (max {}, got {})",
        SUPPORTED_RATES_MAX_LEN,
        rates.len()
    );
    write_ie!(buf, Id::SUPPORTED_RATES, rates)
}

pub fn write_rsne<B: Appendable>(buf: &mut B, rsne: &rsne::Rsne) -> Result<(), FrameWriteError> {
    rsne.write_into(buf).map_err(|e| e.into())
}

pub fn write_ext_supported_rates<B: Appendable>(
    buf: &mut B,
    rates: &[u8],
) -> Result<(), FrameWriteError> {
    validate!(!rates.is_empty(), "List of Extended Supported Rates is empty");
    write_ie!(buf, Id::EXT_SUPPORTED_RATES, rates)
}

pub fn write_ht_capabilities<B: Appendable>(
    buf: &mut B,
    ht_cap: &HtCapabilities,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::HT_CAPABILITIES, ht_cap.as_bytes())
}

pub fn write_ht_operation<B: Appendable>(
    buf: &mut B,
    ht_op: &HtOperation,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::HT_OPERATION, ht_op.as_bytes())
}

pub fn write_dsss_param_set<B: Appendable>(
    buf: &mut B,
    dsss: &DsssParamSet,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::DSSS_PARAM_SET, dsss)
}

pub fn write_tim<B: Appendable>(
    buf: &mut B,
    header: &TimHeader,
    bitmap: &[u8],
) -> Result<(), FrameWriteError> {
    validate!(!bitmap.is_empty(), "Partial virtual bitmap in TIM is empty");
    validate!(
        bitmap.len() <= TIM_MAX_BITMAP_LEN,
        "Partial virtual bitmap in TIM too large (max: {} bytes, got {})",
        TIM_MAX_BITMAP_LEN,
        bitmap.len()
    );
    write_ie!(buf, Id::TIM, header, bitmap)
}

pub fn write_bss_max_idle_period<B: Appendable>(
    buf: &mut B,
    bss_max_idle_period: &BssMaxIdlePeriod,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::BSS_MAX_IDLE_PERIOD, bss_max_idle_period)
}

pub fn write_mpm_open<B: Appendable>(
    buf: &mut B,
    header: &MpmHeader,
    pmk: Option<&MpmPmk>,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::MESH_PEERING_MGMT, header, option_as_bytes(pmk))
}

pub fn write_mpm_confirm<B: Appendable>(
    buf: &mut B,
    header: &MpmHeader,
    peer_link_id: u16,
    pmk: Option<&MpmPmk>,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::MESH_PEERING_MGMT, header, &peer_link_id, option_as_bytes(pmk))
}

pub fn write_mpm_close<B: Appendable>(
    buf: &mut B,
    header: &MpmHeader,
    peer_link_id: Option<u16>,
    reason_code: ReasonCode,
    pmk: Option<&MpmPmk>,
) -> Result<(), FrameWriteError> {
    write_ie!(
        buf,
        Id::MESH_PEERING_MGMT,
        header,
        option_as_bytes(peer_link_id.as_ref()),
        &reason_code,
        option_as_bytes(pmk)
    )
}

pub fn write_preq<B: Appendable>(
    buf: &mut B,
    header: &PreqHeader,
    originator_external_addr: Option<&MacAddr>,
    middle: &PreqMiddle,
    targets: &[PreqPerTarget],
) -> Result<(), FrameWriteError> {
    if header.flags.addr_ext() {
        validate!(
            originator_external_addr.is_some(),
            "Address extension flag is set in PREQ but no external address supplied"
        );
    } else {
        validate!(
            originator_external_addr.is_none(),
            "External address is present but address extension flag is not set in PREQ"
        );
    }
    validate!(
        middle.target_count as usize == targets.len(),
        "target_count in PREQ ({}) does not match the number of supplied targets ({})",
        middle.target_count,
        targets.len()
    );
    write_ie!(buf, Id::PREQ, header, option_as_bytes(originator_external_addr), middle, targets)
}

pub fn write_prep<B: Appendable>(
    buf: &mut B,
    header: &PrepHeader,
    target_external_addr: Option<&MacAddr>,
    tail: &PrepTail,
) -> Result<(), FrameWriteError> {
    if header.flags.addr_ext() {
        validate!(
            target_external_addr.is_some(),
            "Address extension flag is set in PREP but no external address supplied"
        );
    } else {
        validate!(
            target_external_addr.is_none(),
            "External address is present but address extension flag is not set in PREP"
        );
    }
    write_ie!(buf, Id::PREP, header, option_as_bytes(target_external_addr), tail)
}

/// Note that this does not write a full PERR IE, but only a single destination.
/// The idea is to first use this function to write destinations to a separate buffer,
/// and then pass that buffer to `write_perr()`:
///
/// ```
/// let mut destinations_buf = [0u8; PERR_MAX_DESTINATIONS * PERR_MAX_DESTINATION_SIZE];
/// let mut destinations = BufferWriter::new(&mut destinations_buf[..]);
/// let mut num_destinations = 0;
///
/// for each destination {
///     let dest_header = PerrDestinationHeader { ... };
///     write_perr_destination(&mut destinations, &dest_header,
///                            ReasonCode::MESH_PATH_ERROR_NO_FORWARDING_INFORMATION)?;
///     num_destinations += 1;
/// }
///
/// let perr_header = PerrHeader { element_ttl: ..., num_destinations };
/// write_perr(&mut buf, &perr_header, destinations.into_written())?;
/// ```
///
pub fn write_perr_destination<B: Appendable>(
    buf: &mut B,
    header: &PerrDestinationHeader,
    ext_addr: Option<&MacAddr>,
    reason_code: ReasonCode,
) -> Result<(), FrameWriteError> {
    if header.flags.addr_ext() {
        validate!(
            ext_addr.is_some(),
            "Address extension flag is set in PERR destination but no external address supplied"
        );
    } else {
        validate!(
            ext_addr.is_none(),
            "External address is present but address extension flag is not set in PERR destination"
        );
    }
    let len = size_of::<PerrDestinationHeader>()
        + option_as_bytes(ext_addr).len()
        + size_of::<ReasonCode>();
    if !buf.can_append(len) {
        return Err(FrameWriteError::BufferTooSmall);
    }
    buf.append_value(header).expect("expected enough room for PERR destination header");
    if let Some(addr) = ext_addr {
        buf.append_value(addr).expect("expected enough room for PERR external address");
    }
    buf.append_value(&reason_code).expect("expected enough room for PERR reason code");
    Ok(())
}

pub fn write_perr<B: Appendable>(
    buf: &mut B,
    header: &PerrHeader,
    destinations: &[u8],
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::PERR, header, destinations)
}

pub fn write_vht_capabilities<B: Appendable>(
    buf: &mut B,
    vht_cap: &VhtCapabilities,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::VHT_CAPABILITIES, vht_cap.as_bytes())
}

pub fn write_vht_operation<B: Appendable>(
    buf: &mut B,
    vht_op: &VhtOperation,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, Id::VHT_OPERATION, vht_op.as_bytes())
}

/// Writes the entire WPA1 IE into the given buffer, including the vendor IE header.
pub fn write_wpa1_ie<B: Appendable>(
    buf: &mut B,
    wpa_ie: &wpa::WpaIe,
) -> Result<(), BufferTooSmall> {
    let len = std::mem::size_of::<Oui>() + 1 + wpa_ie.len();
    if !buf.can_append(len + 2) {
        return Err(BufferTooSmall);
    }
    buf.append_value(&Id::VENDOR_SPECIFIC)?;
    buf.append_byte(len as u8)?;
    buf.append_value(&Oui::MSFT)?;
    buf.append_byte(wpa::VENDOR_SPECIFIC_TYPE)?;
    wpa_ie.write_into(buf)
}

/// Writes the entire WSC IE into the given buffer, including the vendor IE header.
pub fn write_wsc_ie<B: Appendable>(buf: &mut B, wsc: &[u8]) -> Result<(), BufferTooSmall> {
    let len = std::mem::size_of::<Oui>() + 1 + wsc.len();
    if !buf.can_append(len + 2) {
        return Err(BufferTooSmall);
    }
    buf.append_value(&Id::VENDOR_SPECIFIC)?;
    buf.append_byte(len as u8)?;
    buf.append_value(&Oui::MSFT)?;
    buf.append_byte(wsc::VENDOR_SPECIFIC_TYPE)?;
    buf.append_bytes(wsc)
}

fn option_as_bytes<T: AsBytes>(opt: Option<&T>) -> &[u8] {
    opt.map_or(&[], T::as_bytes)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::buffer_writer::BufferWriter,
        crate::ie::rsn::{akm, cipher},
        crate::organization::Oui,
    };

    #[test]
    fn write_ie_body_too_long() {
        let mut buf = vec![];
        let mut f = || write_ie!(&mut buf, Id::SSID, &[0u8; 256][..]);
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Element body length 256 exceeds max of 255".to_string()
            )),
            f()
        );
    }

    #[test]
    fn write_ie_buffer_too_small() {
        let mut buf = [7u8; 5];
        let mut writer = BufferWriter::new(&mut buf[..]);
        let mut f = || write_ie!(&mut writer, Id::SSID, &[1u8, 2, 3, 4][..]);
        assert_eq!(Err(FrameWriteError::BufferTooSmall), f());
        // Expect the buffer to be left untouched
        assert_eq!(&[7, 7, 7, 7, 7], &buf[..]);
    }

    #[test]
    fn write_ie_buffer_exactly_long_enough() {
        let mut buf = [0u8; 5];
        let mut writer = BufferWriter::new(&mut buf[..]);
        let mut f = || write_ie!(&mut writer, Id::SSID, &[1u8, 2, 3][..]);
        assert_eq!(Ok(()), f());
        assert_eq!(&[0, 3, 1, 2, 3], &buf[..]);
    }

    #[test]
    fn ssid_ok() {
        let mut buf = vec![];
        write_ssid(&mut buf, &[1, 2, 3]).expect("expected Ok");
        assert_eq!(&[0, 3, 1, 2, 3], &buf[..]);
    }

    #[test]
    fn ssid_ok_empty() {
        let mut buf = vec![];
        write_ssid(&mut buf, &[]).expect("expected Ok");
        assert_eq!(&[0, 0], &buf[..]);
    }

    #[test]
    fn ssid_too_long() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "SSID is too long (max: 32 bytes, got: 33)".to_string()
            )),
            write_ssid(&mut buf, &[0u8; 33])
        );
    }

    #[test]
    fn supported_rates_ok() {
        let mut buf = vec![];
        write_supported_rates(&mut buf, &[1, 2, 3, 4, 5, 6, 7, 8]).expect("expected Ok");
        assert_eq!(&[1, 8, 1, 2, 3, 4, 5, 6, 7, 8], &buf[..]);
    }

    #[test]
    fn supported_rates_empty() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data("List of Supported Rates is empty".to_string())),
            write_supported_rates(&mut buf, &[])
        );
    }

    #[test]
    fn supported_rates_too_long() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Too many Supported Rates (max 8, got 9)".to_string()
            )),
            write_supported_rates(&mut buf, &[0u8; 9])
        );
    }

    #[test]
    fn ext_supported_rates_ok() {
        let mut buf = vec![];
        write_ext_supported_rates(&mut buf, &[1, 2, 3, 4, 5, 6, 7, 8]).expect("expected Ok");
        assert_eq!(&[50, 8, 1, 2, 3, 4, 5, 6, 7, 8], &buf[..]);
    }

    #[test]
    fn ext_supported_rates_empty() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "List of Extended Supported Rates is empty".to_string()
            )),
            write_ext_supported_rates(&mut buf, &[])
        );
    }

    #[test]
    fn dsss_param_set() {
        let mut buf = vec![];
        write_dsss_param_set(&mut buf, &DsssParamSet { current_chan: 6 }).expect("expected Ok");
        assert_eq!(&[3, 1, 6], &buf[..]);
    }

    #[test]
    fn tim_ok() {
        let mut buf = vec![];
        write_tim(
            &mut buf,
            &TimHeader { dtim_count: 1, dtim_period: 2, bmp_ctrl: BitmapControl(3) },
            &[4, 5, 6],
        )
        .expect("expected Ok");
        assert_eq!(&[5, 6, 1, 2, 3, 4, 5, 6], &buf[..]);
    }

    #[test]
    fn tim_empty_bitmap() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Partial virtual bitmap in TIM is empty".to_string()
            )),
            write_tim(
                &mut buf,
                &TimHeader { dtim_count: 1, dtim_period: 2, bmp_ctrl: BitmapControl(3) },
                &[]
            )
        );
    }

    #[test]
    fn tim_bitmap_too_long() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Partial virtual bitmap in TIM too large (max: 251 bytes, got 252)".to_string()
            )),
            write_tim(
                &mut buf,
                &TimHeader { dtim_count: 1, dtim_period: 2, bmp_ctrl: BitmapControl(3) },
                &[0u8; 252][..]
            )
        );
    }

    #[test]
    fn mpm_open_no_pmk() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let mut buf = vec![];
        write_mpm_open(&mut buf, &header, None).expect("expected Ok");
        assert_eq!(&[117, 4, 0x11, 0x22, 0x33, 0x44], &buf[..]);
    }

    #[test]
    fn mpm_open_with_pmk() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let pmk = MpmPmk([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
        let mut buf = vec![];
        write_mpm_open(&mut buf, &header, Some(&pmk)).expect("expected Ok");

        #[rustfmt::skip]
        let expected = &[
            117, 20,
            0x11, 0x22, 0x33, 0x44,
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
        ];
        assert_eq!(expected, &buf[..]);
    }

    #[test]
    fn mpm_confirm_no_pmk() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let mut buf = vec![];
        write_mpm_confirm(&mut buf, &header, 0x6655, None).expect("expected Ok");
        assert_eq!(&[117, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66], &buf[..]);
    }

    #[test]
    fn mpm_confirm_with_pmk() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let pmk = MpmPmk([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
        let mut buf = vec![];
        write_mpm_confirm(&mut buf, &header, 0x6655, Some(&pmk)).expect("expected Ok");

        #[rustfmt::skip]
        let expected = &[
            117, 22,
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
        ];
        assert_eq!(expected, &buf[..]);
    }

    #[test]
    fn mpm_close_minimal() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let mut buf = vec![];
        write_mpm_close(&mut buf, &header, None, ReasonCode(0x6655), None).expect("expected Ok");
        assert_eq!(&[117, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66], &buf[..]);
    }

    #[test]
    fn mpm_close_full() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let pmk = MpmPmk([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
        let mut buf = vec![];
        write_mpm_close(&mut buf, &header, Some(0x6655), ReasonCode(0x8877), Some(&pmk))
            .expect("expected Ok");

        #[rustfmt::skip]
        let expected = &[
            117, 24,
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
        ];
        assert_eq!(expected, &buf[..]);
    }

    #[test]
    fn preq_minimal() {
        let header = PreqHeader {
            flags: PreqFlags(0),
            hop_count: 0x01,
            element_ttl: 0x02,
            path_discovery_id: 0x06050403,
            originator_addr: [0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c],
            originator_hwmp_seqno: 0x100f0e0d,
        };
        let middle = PreqMiddle { lifetime: 0x1a191817, metric: 0x1e1d1c1b, target_count: 0 };
        let mut buf = vec![];
        write_preq(&mut buf, &header, None, &middle, &[]).expect("expected Ok");

        #[rustfmt::skip]
        let expected = [
            130, 17 + 9,
            0x00, // flags
            0x01, // hop count
            0x02, // element ttl
            0x03, 0x04, 0x05, 0x06, // path discovery ID
            0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, // originator addr
            0x0d, 0x0e, 0x0f, 0x10, // originator hwmp seqno
            0x17, 0x18, 0x19, 0x1a, // lifetime
            0x1b, 0x1c, 0x1d, 0x1e, // metric
            // Target count
            0,
        ];
        assert_eq!(expected, &buf[..]);
    }

    #[test]
    fn preq_full() {
        let header = PreqHeader {
            flags: PreqFlags(0).with_addr_ext(true),
            hop_count: 0x01,
            element_ttl: 0x02,
            path_discovery_id: 0x06050403,
            originator_addr: [0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c],
            originator_hwmp_seqno: 0x100f0e0d,
        };
        let ext_addr = [0x11, 0x12, 0x13, 0x14, 0x15, 0x16];
        let middle = PreqMiddle { lifetime: 0x1a191817, metric: 0x1e1d1c1b, target_count: 1 };
        let target = PreqPerTarget {
            flags: PreqPerTargetFlags(0),
            target_addr: [0x21, 0x22, 0x23, 0x24, 0x25, 0x26],
            target_hwmp_seqno: 0x2a292827,
        };
        let mut buf = vec![];
        write_preq(&mut buf, &header, Some(&ext_addr), &middle, &[target]).expect("expected Ok");

        #[rustfmt::skip]
        let expected = [
            130, 17 + 6 + 9 + 11,
            0x40, // flags: ext addr present
            0x01, // hop count
            0x02, // element ttl
            0x03, 0x04, 0x05, 0x06, // path discovery ID
            0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, // originator addr
            0x0d, 0x0e, 0x0f, 0x10, // originator hwmp seqno
            0x11, 0x12, 0x13, 0x14, 0x15, 0x16, // ext addr
            0x17, 0x18, 0x19, 0x1a, // lifetime
            0x1b, 0x1c, 0x1d, 0x1e, // metric
            // Target count
            1,
            0x00, // target 1 flags
            0x21, 0x22, 0x23, 0x24, 0x25, 0x26, // target 1 address
            0x27, 0x28, 0x29, 0x2a, // target 1 hwmp seqno
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn preq_addr_ext_flag_set_but_no_addr_given() {
        let header = PreqHeader {
            flags: PreqFlags(0).with_addr_ext(true),
            hop_count: 0x01,
            element_ttl: 0x02,
            path_discovery_id: 0x06050403,
            originator_addr: [0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c],
            originator_hwmp_seqno: 0x100f0e0d,
        };
        let middle = PreqMiddle { lifetime: 0x1a191817, metric: 0x1e1d1c1b, target_count: 0 };
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Address extension flag is set in PREQ but no external address supplied"
                    .to_string()
            )),
            write_preq(&mut vec![], &header, None, &middle, &[])
        );
    }

    #[test]
    fn preq_ext_addr_given_but_no_flag_set() {
        let header = PreqHeader {
            flags: PreqFlags(0),
            hop_count: 0x01,
            element_ttl: 0x02,
            path_discovery_id: 0x06050403,
            originator_addr: [0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c],
            originator_hwmp_seqno: 0x100f0e0d,
        };
        let ext_addr = [0x11, 0x12, 0x13, 0x14, 0x15, 0x16];
        let middle = PreqMiddle { lifetime: 0x1a191817, metric: 0x1e1d1c1b, target_count: 0 };
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "External address is present but address extension flag is not set in PREQ"
                    .to_string()
            )),
            write_preq(&mut vec![], &header, Some(&ext_addr), &middle, &[])
        );
    }

    #[test]
    fn preq_target_count_mismatch() {
        let header = PreqHeader {
            flags: PreqFlags(0),
            hop_count: 0x01,
            element_ttl: 0x02,
            path_discovery_id: 0x06050403,
            originator_addr: [0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c],
            originator_hwmp_seqno: 0x100f0e0d,
        };
        let middle = PreqMiddle { lifetime: 0x1a191817, metric: 0x1e1d1c1b, target_count: 1 };
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "target_count in PREQ (1) does not match the number of supplied targets (0)"
                    .to_string()
            )),
            write_preq(&mut vec![], &header, None, &middle, &[])
        );
    }

    #[test]
    fn prep_no_ext() {
        let mut buf = vec![];
        let header = PrepHeader {
            flags: PrepFlags(0),
            hop_count: 1,
            element_ttl: 2,
            target_addr: [3, 4, 5, 6, 7, 8],
            target_hwmp_seqno: 0x0c0b0a09,
        };
        let tail = PrepTail {
            lifetime: 0x100f0e0d,
            metric: 0x14131211,
            originator_addr: [0x15, 0x16, 0x17, 0x18, 0x19, 0x1a],
            originator_hwmp_seqno: 0x1e1d1c1b,
        };
        write_prep(&mut buf, &header, None, &tail).expect("expected Ok");

        #[rustfmt::skip]
        let expected = [
            131, 31,
            0x00, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
            0x0d, 0x0e, 0x0f, 0x10, // lifetime
            0x11, 0x12, 0x13, 0x14, // metric
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
            0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn prep_with_ext() {
        let mut buf = vec![];
        let header = PrepHeader {
            flags: PrepFlags(0).with_addr_ext(true),
            hop_count: 1,
            element_ttl: 2,
            target_addr: [3, 4, 5, 6, 7, 8],
            target_hwmp_seqno: 0x0c0b0a09,
        };
        let ext_addr = [0x44, 0x55, 0x66, 0x77, 0x88, 0x99];
        let tail = PrepTail {
            lifetime: 0x100f0e0d,
            metric: 0x14131211,
            originator_addr: [0x15, 0x16, 0x17, 0x18, 0x19, 0x1a],
            originator_hwmp_seqno: 0x1e1d1c1b,
        };
        write_prep(&mut buf, &header, Some(&ext_addr), &tail).expect("expected Ok");

        #[rustfmt::skip]
        let expected = [
            131, 37,
            0x40, 0x01, 0x02, // flags, hop count, elem ttl
            0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
            0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
            0x44, 0x55, 0x66, 0x77, 0x88, 0x99, // target external addr
            0x0d, 0x0e, 0x0f, 0x10, // lifetime
            0x11, 0x12, 0x13, 0x14, // metric
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
            0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn prep_addr_ext_flag_set_but_no_addr_given() {
        let header = PrepHeader {
            flags: PrepFlags(0).with_addr_ext(true),
            hop_count: 1,
            element_ttl: 2,
            target_addr: [3, 4, 5, 6, 7, 8],
            target_hwmp_seqno: 0x0c0b0a09,
        };
        let tail = PrepTail {
            lifetime: 0x100f0e0d,
            metric: 0x14131211,
            originator_addr: [0x15, 0x16, 0x17, 0x18, 0x19, 0x1a],
            originator_hwmp_seqno: 0x1e1d1c1b,
        };
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Address extension flag is set in PREP but no external address supplied"
                    .to_string()
            )),
            write_prep(&mut vec![], &header, None, &tail)
        );
    }

    #[test]
    fn prep_ext_addr_given_but_no_flag_set() {
        let header = PrepHeader {
            flags: PrepFlags(0),
            hop_count: 1,
            element_ttl: 2,
            target_addr: [3, 4, 5, 6, 7, 8],
            target_hwmp_seqno: 0x0c0b0a09,
        };
        let ext_addr = [0x44, 0x55, 0x66, 0x77, 0x88, 0x99];
        let tail = PrepTail {
            lifetime: 0x100f0e0d,
            metric: 0x14131211,
            originator_addr: [0x15, 0x16, 0x17, 0x18, 0x19, 0x1a],
            originator_hwmp_seqno: 0x1e1d1c1b,
        };
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "External address is present but address extension flag is not set in PREP"
                    .to_string()
            )),
            write_prep(&mut vec![], &header, Some(&ext_addr), &tail)
        );
    }

    #[test]
    fn perr_destination_ok_no_ext() {
        let mut buf = vec![];
        let header = PerrDestinationHeader {
            flags: PerrDestinationFlags(0),
            dest_addr: [1, 2, 3, 4, 5, 6],
            hwmp_seqno: 0x0a090807,
        };
        write_perr_destination(&mut buf, &header, None, ReasonCode(0x0c0b)).expect("expected Ok");
        #[rustfmt::skip]
        let expected = [
            0, // flags
            1, 2, 3, 4, 5, 6, // dest addr
            0x7, 0x8, 0x9, 0xa, // hwmp seqno
            0xb, 0xc, // reason code
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn perr_destination_ok_with_ext() {
        let mut buf = vec![];
        let header = PerrDestinationHeader {
            flags: PerrDestinationFlags(0).with_addr_ext(true),
            dest_addr: [1, 2, 3, 4, 5, 6],
            hwmp_seqno: 0x0a090807,
        };
        let ext_addr = [0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10];
        write_perr_destination(&mut buf, &header, Some(&ext_addr), ReasonCode(0x1211))
            .expect("expected Ok");
        #[rustfmt::skip]
        let expected = [
            0x40, // flags
            1, 2, 3, 4, 5, 6, // dest addr
            0x7, 0x8, 0x9, 0xa, // hwmp seqno
            0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, // ext addr
            0x11, 0x12, // reason code
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn perr_destination_addr_ext_flag_set_but_no_addr_given() {
        let header = PerrDestinationHeader {
            flags: PerrDestinationFlags(0).with_addr_ext(true),
            dest_addr: [1, 2, 3, 4, 5, 6],
            hwmp_seqno: 0x0a090807,
        };
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Address extension flag is set in PERR destination but no external address supplied"
                    .to_string()
            )),
            write_perr_destination(&mut vec![], &header, None, ReasonCode(0x1211))
        );
    }

    #[test]
    fn perr_destination_ext_addr_given_but_no_flag_set() {
        let header = PerrDestinationHeader {
            flags: PerrDestinationFlags(0),
            dest_addr: [1, 2, 3, 4, 5, 6],
            hwmp_seqno: 0x0a090807,
        };
        let ext_addr = [0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "External address is present but address extension flag \
                 is not set in PERR destination"
                    .to_string()
            )),
            write_perr_destination(&mut vec![], &header, Some(&ext_addr), ReasonCode(0x1211))
        );
    }

    #[test]
    fn perr_destination_buffer_too_small() {
        let mut buf = [0u8; 12]; // 1 byte short
        let mut writer = BufferWriter::new(&mut buf[..]);
        let header = PerrDestinationHeader {
            flags: PerrDestinationFlags(0),
            dest_addr: [1, 2, 3, 4, 5, 6],
            hwmp_seqno: 0x0a090807,
        };
        assert_eq!(
            Err(FrameWriteError::BufferTooSmall),
            write_perr_destination(&mut writer, &header, None, ReasonCode(0x0c0b))
        );
        // Assert that nothing has been written
        assert_eq!(0, writer.bytes_written());
    }

    #[test]
    fn perr() {
        let header = PerrHeader { element_ttl: 11, num_destinations: 7 };
        let mut buf = vec![];
        write_perr(&mut buf, &header, &[1, 2, 3]).expect("expected Ok");
        let expected = [132, 5, 11, 7, 1, 2, 3];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn test_write_wpa1_ie() {
        let wpa_ie = wpa::WpaIe {
            multicast_cipher: cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP },
            unicast_cipher_list: vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }],
            akm_list: vec![akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }],
        };
        let expected: Vec<u8> = vec![
            0xdd, 0x16, // Vendor IE header
            0x00, 0x50, 0xf2, // MSFT OUI
            0x01, 0x01, 0x00, // WPA IE header
            0x00, 0x50, 0xf2, 0x02, // multicast cipher: AKM
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher: TKIP
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
        ];
        let mut buf = vec![];
        write_wpa1_ie(&mut buf, &wpa_ie).expect("WPA1 write to a Vec should never fail");
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn test_write_wpa1_ie_buffer_too_small() {
        let wpa_ie = wpa::WpaIe {
            multicast_cipher: cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP },
            unicast_cipher_list: vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }],
            akm_list: vec![akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }],
        };

        let mut buf = [0u8; 10];
        let mut writer = BufferWriter::new(&mut buf[..]);
        write_wpa1_ie(&mut writer, &wpa_ie).expect_err("WPA1 write to short buf should fail");
        // The buffer is not long enough, so no bytes should be written.
        assert_eq!(writer.into_written().len(), 0);
    }

    #[test]
    fn ht_capabilities_ok() {
        let mut buf = vec![];
        let ht_cap = crate::ie::fake_ies::fake_ht_capabilities();
        write_ht_capabilities(&mut buf, &ht_cap).expect("writing ht cap");
        assert_eq!(
            &buf[..],
            &[
                45, 26, // HT Cap id and length
                254, 1, 0, 255, 0, 0, 0, 1, // byte 0-7
                0, 0, 0, 0, 0, 0, 0, 1, // byte 8-15
                0, 0, 0, 0, 0, 0, 0, 0, // byte 16-23
                0, 0, // byte 24-25
            ]
        );
    }

    #[test]
    fn ht_operation_ok() {
        let mut buf = vec![];
        let ht_op = crate::ie::fake_ies::fake_ht_operation();
        write_ht_operation(&mut buf, &ht_op).expect("writing ht op");
        assert_eq!(
            &buf[..],
            &[
                61, 22, // HT Op id and length
                36, 5, 20, 0, 0, 0, 255, 0, // byte 0-7
                0, 0, 1, 0, 0, 0, 0, 0, // byte 8-15
                0, 0, 1, 0, 0, 0, // byte 16-21
            ]
        );
    }

    #[test]
    fn vht_capabilities_ok() {
        let mut buf = vec![];
        let vht_cap = crate::ie::fake_ies::fake_vht_capabilities();
        write_vht_capabilities(&mut buf, &vht_cap).expect("writing vht cap");
        assert_eq!(
            &buf[..],
            &[
                191, 12, // VHT Cap id and length
                177, 2, 0, 177, 3, 2, 99, 67, // byte 0-7
                3, 2, 99, 3, // byte 8-11
            ]
        );
    }

    #[test]
    fn vht_operation_ok() {
        let mut buf = vec![];
        let vht_op = crate::ie::fake_ies::fake_vht_operation();
        write_vht_operation(&mut buf, &vht_op).expect("writing vht op");
        assert_eq!(
            &buf[..],
            &[
                192, 5, // VHT Op id and length
                1, 42, 0, 27, 27, // byte 0-4
            ]
        );
    }

    #[test]
    fn rsne_ok() {
        let mut buf = vec![];
        let rsne = rsne::from_bytes(&crate::test_utils::fake_frames::fake_wpa2_rsne()[..])
            .expect("creating rsne")
            .1;
        write_rsne(&mut buf, &rsne).expect("writing rsne");
        assert_eq!(
            &buf[..],
            &[
                48, 18, // RSNE id and length
                1, 0, 0, 15, 172, 4, 1, 0, // byte 0-7
                0, 15, 172, 4, 1, 0, 0, 15, // byte 8-15
                172, 2, // byte 16-17
            ]
        );
    }

    #[test]
    fn bss_max_idle_period_ok() {
        let mut buf = vec![];
        write_bss_max_idle_period(
            &mut buf,
            &BssMaxIdlePeriod {
                max_idle_period: 99,
                idle_options: IdleOptions(0).with_protected_keep_alive_required(true),
            },
        )
        .expect("writing bss max idle period");
        assert_eq!(&buf[..], &[90, 3, 99, 0, 1]);
    }
}
