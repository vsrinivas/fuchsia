// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    crate::error::{FrameParseError, FrameParseResult},
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

pub fn parse_ext_supported_rates<B: ByteSlice>(
    raw_body: B,
) -> FrameParseResult<LayoutVerified<B, [SupportedRate]>> {
    validate!(!raw_body.is_empty(), "Empty Extended Supported Rates element");
    // unwrap() is OK because sizeof(SupportedRate) is 1, and any slice length is a multiple of 1
    Ok(LayoutVerified::new_slice_unaligned(raw_body).unwrap())
}

#[cfg(test)]
mod tests {
    use super::*;

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
    pub fn ext_supported_rates_ok() {
        let r = parse_ext_supported_rates(&[1, 2, 3][..]).expect("expected Ok");
        assert_eq!(&[SupportedRate(1), SupportedRate(2), SupportedRate(3)][..], &r[..]);
    }

    #[test]
    pub fn ext_supported_rates_empty() {
        let err = parse_ext_supported_rates(&[][..]).expect_err("expected Err");
        assert_eq!("Empty Extended Supported Rates element", err.debug_message());
    }
}
