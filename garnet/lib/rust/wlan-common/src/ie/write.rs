// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{constants::*, fields::*, id},
    crate::{
        appendable::Appendable,
        error::FrameWriteError,
        mac::{MacAddr, ReasonCode},
    },
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
            validate!(body_len <= 255, "Element body length {} exceeds max of 255", body_len);
            if !$buf.can_append(2 + body_len) {
                return Err(FrameWriteError::BufferTooSmall);
            }
            $buf.append_byte($id.raw())
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
    write_ie!(buf, id::SSID, ssid)
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
    write_ie!(buf, id::SUPPORTED_RATES, rates)
}

pub fn write_ext_supported_rates<B: Appendable>(
    buf: &mut B,
    rates: &[u8],
) -> Result<(), FrameWriteError> {
    validate!(!rates.is_empty(), "List of Extended Supported Rates is empty");
    write_ie!(buf, id::EXT_SUPPORTED_RATES, rates)
}

pub fn write_dsss_param_set<B: Appendable>(
    buf: &mut B,
    dsss: &DsssParamSet,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, id::DSSS_PARAM_SET, dsss)
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
    write_ie!(buf, id::TIM, header, bitmap)
}

pub fn write_mpm_open<B: Appendable>(
    buf: &mut B,
    header: &MpmHeader,
    pmk: Option<&MpmPmk>,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, id::MESH_PEERING_MGMT, header, option_as_bytes(pmk))
}

pub fn write_mpm_confirm<B: Appendable>(
    buf: &mut B,
    header: &MpmHeader,
    peer_link_id: u16,
    pmk: Option<&MpmPmk>,
) -> Result<(), FrameWriteError> {
    write_ie!(buf, id::MESH_PEERING_MGMT, header, &peer_link_id, option_as_bytes(pmk))
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
        id::MESH_PEERING_MGMT,
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
    write_ie!(buf, id::PREQ, header, option_as_bytes(originator_external_addr), middle, targets)
}

fn option_as_bytes<T: AsBytes>(opt: Option<&T>) -> &[u8] {
    opt.map_or(&[], T::as_bytes)
}

#[cfg(test)]
mod tests {
    use {super::*, crate::buffer_writer::BufferWriter};

    #[test]
    pub fn write_ie_body_too_long() {
        let mut buf = vec![];
        let mut f = || write_ie!(&mut buf, id::SSID, &[0u8; 256][..]);
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Element body length 256 exceeds max of 255".to_string()
            )),
            f()
        );
    }

    #[test]
    pub fn write_ie_buffer_too_small() {
        let mut buf = [7u8; 5];
        let mut writer = BufferWriter::new(&mut buf[..]);
        let mut f = || write_ie!(&mut writer, id::SSID, &[1u8, 2, 3, 4][..]);
        assert_eq!(Err(FrameWriteError::BufferTooSmall), f());
        // Expect the buffer to be left untouched
        assert_eq!(&[7, 7, 7, 7, 7], &buf[..]);
    }

    #[test]
    pub fn write_ie_buffer_exactly_long_enough() {
        let mut buf = [0u8; 5];
        let mut writer = BufferWriter::new(&mut buf[..]);
        let mut f = || write_ie!(&mut writer, id::SSID, &[1u8, 2, 3][..]);
        assert_eq!(Ok(()), f());
        assert_eq!(&[0, 3, 1, 2, 3], &buf[..]);
    }

    #[test]
    pub fn ssid_ok() {
        let mut buf = vec![];
        write_ssid(&mut buf, &[1, 2, 3]).expect("expected Ok");
        assert_eq!(&[0, 3, 1, 2, 3], &buf[..]);
    }

    #[test]
    pub fn ssid_ok_empty() {
        let mut buf = vec![];
        write_ssid(&mut buf, &[]).expect("expected Ok");
        assert_eq!(&[0, 0], &buf[..]);
    }

    #[test]
    pub fn ssid_too_long() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "SSID is too long (max: 32 bytes, got: 33)".to_string()
            )),
            write_ssid(&mut buf, &[0u8; 33])
        );
    }

    #[test]
    pub fn supported_rates_ok() {
        let mut buf = vec![];
        write_supported_rates(&mut buf, &[1, 2, 3, 4, 5, 6, 7, 8]).expect("expected Ok");
        assert_eq!(&[1, 8, 1, 2, 3, 4, 5, 6, 7, 8], &buf[..]);
    }

    #[test]
    pub fn supported_rates_empty() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data("List of Supported Rates is empty".to_string())),
            write_supported_rates(&mut buf, &[])
        );
    }

    #[test]
    pub fn supported_rates_too_long() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Too many Supported Rates (max 8, got 9)".to_string()
            )),
            write_supported_rates(&mut buf, &[0u8; 9])
        );
    }

    #[test]
    pub fn ext_supported_rates_ok() {
        let mut buf = vec![];
        write_ext_supported_rates(&mut buf, &[1, 2, 3, 4, 5, 6, 7, 8]).expect("expected Ok");
        assert_eq!(&[50, 8, 1, 2, 3, 4, 5, 6, 7, 8], &buf[..]);
    }

    #[test]
    pub fn ext_supported_rates_empty() {
        let mut buf = vec![];
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "List of Extended Supported Rates is empty".to_string()
            )),
            write_ext_supported_rates(&mut buf, &[])
        );
    }

    #[test]
    pub fn dsss_param_set() {
        let mut buf = vec![];
        write_dsss_param_set(&mut buf, &DsssParamSet { current_chan: 6 }).expect("expected Ok");
        assert_eq!(&[3, 1, 6], &buf[..]);
    }

    #[test]
    pub fn tim_ok() {
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
    pub fn tim_empty_bitmap() {
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
    pub fn tim_bitmap_too_long() {
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
    pub fn mpm_open_no_pmk() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let mut buf = vec![];
        write_mpm_open(&mut buf, &header, None).expect("expected Ok");
        assert_eq!(&[117, 4, 0x11, 0x22, 0x33, 0x44], &buf[..]);
    }

    #[test]
    pub fn mpm_open_with_pmk() {
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
    pub fn mpm_confirm_no_pmk() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let mut buf = vec![];
        write_mpm_confirm(&mut buf, &header, 0x6655, None).expect("expected Ok");
        assert_eq!(&[117, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66], &buf[..]);
    }

    #[test]
    pub fn mpm_confirm_with_pmk() {
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
    pub fn mpm_close_minimal() {
        let header = MpmHeader { protocol: MpmProtocol(0x2211), local_link_id: 0x4433 };
        let mut buf = vec![];
        write_mpm_close(&mut buf, &header, None, ReasonCode(0x6655), None).expect("expected Ok");
        assert_eq!(&[117, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66], &buf[..]);
    }

    #[test]
    pub fn mpm_close_full() {
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
    pub fn preq_minimal() {
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
    pub fn preq_full() {
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
    pub fn preq_addr_ext_flag_set_but_no_addr_given() {
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
    pub fn preq_ext_addr_given_but_no_flag_set() {
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
    pub fn preq_target_count_mismatch() {
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
}
