// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {bitfield::bitfield, thiserror::Error};

/// The error types for rtp header parsing.
#[derive(Error, Debug, PartialEq)]
pub enum RtpError {
    /// The buffer used to create the header was too short
    #[error("The buffer is too short.")]
    BufferTooShort,

    /// The RTP version of this packet is not supported
    #[error("Unsupported RTP Version.")]
    UnsupportedVersion,

    /// The value that was provided is invalid.
    #[error("Unsupported flags or fields were found in the header.")]
    UnsupportedFeature,

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

bitfield! {
    pub struct RtpHeaderInner(MSB0 [u8]);
    impl Debug;
    pub u8, version, _: 1, 0;
    pub bool, padding, _: 2;
    pub bool, extension, _: 3;
    pub u8, csrc_count, _: 7, 4;
    pub bool, marker, _: 8;
    pub u8, payload_type, _: 15, 9;
    pub u16, sequence_number, _: 31, 16;
    pub u32, timestamp, _: 63, 32;
    pub u32, ssrc, _: 95, 64;
}

/// RTP Packet header as described in https://tools.ietf.org/html/rfc1889 Section 5.1
///
/// RTP, the real-time transport protocol, provides end-to-end network transport functions
/// suitable for applications transmitting real-time data, such as audio, video or
/// simulation data, over multicast or unicast network services.
#[cfg_attr(test, derive(Debug))]
pub struct RtpHeader(RtpHeaderInner<[u8; Self::LENGTH]>);

impl RtpHeader {
    /// The minimum length of an RTP Header in bytes. This is the length of the header,
    /// if there are no extension fields or csrc fields.
    pub const LENGTH: usize = 12;

    /// Parse and validate an RTP Packet from a buffer of bytes. Only packets that do
    /// not contain extension fields or csrc fields will be parsed. This is done because
    /// we do not currently use the aformentioned fields and a fixed size header is guaranteed
    /// when headers to not contain them.
    pub fn new(buf: &[u8]) -> Result<Self, RtpError> {
        if buf.len() < Self::LENGTH {
            return Err(RtpError::BufferTooShort);
        }
        let mut b = [0; Self::LENGTH];
        b.copy_from_slice(&buf[..Self::LENGTH]);
        let r = RtpHeaderInner(b);
        if r.version() != 2 {
            return Err(RtpError::UnsupportedVersion);
        }
        if r.extension() {
            return Err(RtpError::UnsupportedFeature);
        }
        if r.csrc_count() > 0 {
            return Err(RtpError::UnsupportedFeature);
        }
        Ok(Self(r))
    }

    /// The sequence number increments by one for each RTP data packet
    /// sent, and may be used by the receiver to detect packet loss and
    /// to restore packet sequence. The initial value of the sequence
    /// number is random (unpredictable) to make known-plaintext attacks
    /// on encryption more difficult, even if the source itself does not
    /// encrypt, because the packets may flow through a translator that
    /// does.
    pub fn sequence_number(&self) -> u16 {
        self.0.sequence_number()
    }

    /// The timestamp reflects the sampling instant of the first octet
    /// in the RTP data packet. The sampling instant must be derived
    /// from a clock that increments monotonically and linearly in time
    /// to allow synchronization and jitter calculations (see Section
    /// 6.3.1).  The resolution of the clock must be sufficient for the
    /// desired synchronization accuracy and for measuring packet
    /// arrival jitter (one tick per video frame is typically not
    /// sufficient).  The clock frequency is dependent on the format of
    /// data carried as payload and is specified statically in the
    /// profile or payload format specification that defines the format,
    /// or may be specified dynamically for payload formats defined
    /// through non-RTP means. If RTP packets are generated
    /// periodically, the nominal sampling instant as determined from
    /// the sampling clock is to be used, not a reading of the system
    /// clock. As an example, for fixed-rate audio the timestamp clock
    /// would likely increment by one for each sampling period.  If an
    /// audio application reads blocks covering 160 sampling periods
    /// from the input device, the timestamp would be increased by 160
    /// for each such block, regardless of whether the block is
    /// transmitted in a packet or dropped as silent.
    ///
    /// The initial value of the timestamp is random, as for the sequence
    /// number. Several consecutive RTP packets may have equal timestamps if
    /// they are (logically) generated at once, e.g., belong to the same
    /// video frame. Consecutive RTP packets may contain timestamps that are
    /// not monotonic if the data is not transmitted in the order it was
    /// sampled, as in the case of MPEG interpolated video frames. (The
    /// sequence numbers of the packets as transmitted will still be
    /// monotonic.)
    pub fn timestamp(&self) -> u32 {
        self.0.timestamp()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_valid_rtp_headers() {
        let raw = [128, 96, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 5];
        let header = RtpHeader::new(&raw).expect("valid header");
        assert_eq!(header.sequence_number(), 1);
        assert_eq!(header.timestamp(), 0);

        let raw_with_payload = [128, 96, 0, 2, 0, 0, 2, 128, 0, 0, 0, 0, 5, 0, 1, 2, 3, 4];
        let header = RtpHeader::new(&raw_with_payload).expect("valid header");
        assert_eq!(header.sequence_number(), 2);
        assert_eq!(header.timestamp(), 640);
    }

    #[test]
    fn test_invalid_rtp_headers() {
        let raw_short = [128, 96];
        let err = RtpHeader::new(&raw_short).expect_err("invalid header");
        assert_eq!(err, RtpError::BufferTooShort);

        let raw_unsupported_version =
            [0b0100_0000, 96, 0, 2, 0, 0, 2, 128, 0, 0, 0, 0, 5, 0, 1, 2, 3, 4];
        let err = RtpHeader::new(&raw_unsupported_version).expect_err("invalid header");
        assert_eq!(err, RtpError::UnsupportedVersion);

        let raw_unsupported_extension =
            [0b1001_0000, 96, 0, 2, 0, 0, 2, 128, 0, 0, 0, 0, 5, 0, 1, 2, 3, 4];
        let err = RtpHeader::new(&raw_unsupported_extension).expect_err("invalid header");
        assert_eq!(err, RtpError::UnsupportedFeature);

        let raw_includes_csrc = [0b1000_1000, 96, 0, 2, 0, 0, 2, 128, 0, 0, 0, 0, 5, 0, 1, 2, 3, 4];
        let err = RtpHeader::new(&raw_includes_csrc).expect_err("invalid header");
        assert_eq!(err, RtpError::UnsupportedFeature);
    }
}
