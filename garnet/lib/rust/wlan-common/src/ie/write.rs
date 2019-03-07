// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{constants::*, fields::*, id},
    crate::{appendable::Appendable, error::FrameWriteError},
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
            validate!(body_len <= 255, "Element body exceeds max length of 255");
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

#[cfg(test)]
mod tests {
    use {super::*, crate::buffer_writer::BufferWriter};

    #[test]
    pub fn write_ie_body_too_long() {
        let mut buf = vec![];
        let mut f = || write_ie!(&mut buf, id::SSID, &[0u8; 256][..]);
        assert_eq!(
            Err(FrameWriteError::new_invalid_data(
                "Element body exceeds max length of 255".to_string()
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
}
