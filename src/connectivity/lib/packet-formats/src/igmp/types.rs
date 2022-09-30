// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IGMP parsing and serialization helper types.

use core::convert::{From, TryFrom, TryInto};
use core::time::Duration;

use super::IgmpMaxRespCode;

/// IGMP-specific errors.
#[derive(Debug, PartialEq)]
pub enum IgmpError {
    /// Error converting to IGMP v3 floating point format.
    FloatFormatError,
}

impl From<core::num::TryFromIntError> for IgmpError {
    fn from(_: core::num::TryFromIntError) -> Self {
        IgmpError::FloatFormatError
    }
}

/// Thin wrapper around `u8` that provides maximum response time parsing
/// for IGMP v2.
///
/// Provides conversions to and from `Duration` for parsing and
/// and serializing in the correct format, following that the underlying `u8`
/// is the maximum response time in tenths of seconds.
#[derive(Debug, PartialEq)]
pub struct IgmpResponseTimeV2(u8);

impl IgmpMaxRespCode for IgmpResponseTimeV2 {
    fn as_code(&self) -> u8 {
        self.0
    }

    fn from_code(code: u8) -> Self {
        Self(code)
    }
}

impl TryFrom<Duration> for IgmpResponseTimeV2 {
    type Error = IgmpError;

    fn try_from(value: Duration) -> Result<Self, Self::Error> {
        let tenths = value.as_millis() / 100;
        Ok(Self(tenths.try_into()?))
    }
}

impl From<IgmpResponseTimeV2> for Duration {
    fn from(value: IgmpResponseTimeV2) -> Duration {
        let v: u64 = value.0.into();
        Self::from_millis(v * 100)
    }
}

/// Thin wrapper around u8 that provides maximum response time parsing
/// for IGMP v3.
///
/// Provides conversions to and from `Duration` for parsing and
/// and serializing in the correct format.
#[derive(Debug, PartialEq)]
pub struct IgmpResponseTimeV3(u8);

impl IgmpMaxRespCode for IgmpResponseTimeV3 {
    fn as_code(&self) -> u8 {
        self.0
    }

    fn from_code(code: u8) -> Self {
        Self(code)
    }
}

const FLOATING_POINT_SWITCH_POINT: u8 = 128;
const FLOATING_POINT_MAX_VALUE: u32 = (0x0F | 0x10) << (7 + 3);

/// Parses a code that may be representing a floating point value.
///
/// Defined in RFC 3376 and used in IGMP v3 membership queries and
/// membership responses.
///
/// The floating point format is as follows:
///
///   If Code < 128, Value = Code
//
//   If Code >= 128, Value represents a floating-point
//   value as follows:
//
//       0 1 2 3 4 5 6 7
//      +-+-+-+-+-+-+-+-+
//      |1| exp | mant  |
//      +-+-+-+-+-+-+-+-+
//
//   Value = (mant | 0x10) << (exp + 3)
//
// Which yields the maximum representable value as:
//  31744 = (0x0F | 0x10) << (7 + 3)
//
pub fn parse_v3_possible_floating_point(code: u8) -> u32 {
    if code < FLOATING_POINT_SWITCH_POINT {
        code.into()
    } else {
        let code: u32 = code.into();
        ((code & 0x0F) | 0x10) << (3 + ((code >> 4) & 0x07))
    }
}

/// Makes an `u8` code out of an `u32` value that can be represented as
/// a floating point.
///
/// See definition in `parse_v3_possible_floating_point`
pub fn make_v3_possible_floating_point(val: u32) -> Option<u8> {
    if val > FLOATING_POINT_MAX_VALUE {
        None
    } else if val < FLOATING_POINT_SWITCH_POINT.into() {
        // value is < 128, unwrapping here is safe:
        Some(val.try_into().unwrap())
    } else {
        let msb = (32 - val.leading_zeros()) - 1;
        let exp = msb - 4;
        let mant = (val >> (exp)) & 0x0F;
        // unwrap guaranteed by the structure of the built int:
        Some((0x80 | ((exp - 3) << 4) | mant).try_into().unwrap())
    }
}

impl TryFrom<Duration> for IgmpResponseTimeV3 {
    type Error = IgmpError;

    fn try_from(value: Duration) -> Result<Self, Self::Error> {
        // ResponseTime v3 is represented in tenths of seconds and coded
        // with specific floating point schema.
        let millis: u32 = value.as_millis().try_into()?;
        Ok(Self(make_v3_possible_floating_point(millis / 100).ok_or(IgmpError::FloatFormatError)?))
    }
}

impl From<IgmpResponseTimeV3> for Duration {
    fn from(value: IgmpResponseTimeV3) -> Duration {
        // ResponseTime v3 is represented in tenths of seconds and coded
        // with specific floating point schema.
        let tenths: u64 = parse_v3_possible_floating_point(value.0).into();
        Self::from_millis(tenths * 100)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn parse_and_serialize_code_v2() {
        for code in 0..=255 {
            let response = IgmpResponseTimeV2::from_code(code);
            assert_eq!(response.as_code(), code);
            let dur = Duration::from(response);
            let back = IgmpResponseTimeV2::try_from(dur).unwrap();
            assert_eq!(dur.as_millis(), u128::from(code) * 100);
            assert_eq!(code, back.as_code());
        }

        // test that anything larger than max u8 tenths of seconds will cause
        // try_from to fail:
        assert_eq!(
            IgmpResponseTimeV2::try_from(Duration::from_millis(
                (u64::from(core::u8::MAX) + 1) * 100,
            )),
            Err(IgmpError::FloatFormatError)
        );
    }

    #[test]
    pub fn parse_and_serialize_code_v3() {
        let r = Duration::from(IgmpResponseTimeV3::from_code(0x80 | 0x01));
        assert_eq!(r.as_millis(), 13600);
        let t = IgmpResponseTimeV3::try_from(Duration::from_millis((128 + 8) * 100)).unwrap();
        assert_eq!(t.as_code(), (0x80 | 0x01));
        for code in 0..=255 {
            let response = IgmpResponseTimeV3::from_code(code);
            assert_eq!(response.as_code(), code);
            let dur = Duration::from(response);
            let back = IgmpResponseTimeV3::try_from(dur).unwrap();
            assert_eq!(code, back.as_code());
        }

        // test that anything larger than max u8 tenths of seconds will cause
        // try_from to fail:
        assert_eq!(
            IgmpResponseTimeV3::try_from(Duration::from_millis(
                (FLOATING_POINT_MAX_VALUE as u64 + 1) * 100,
            )),
            Err(IgmpError::FloatFormatError)
        );
    }
}
