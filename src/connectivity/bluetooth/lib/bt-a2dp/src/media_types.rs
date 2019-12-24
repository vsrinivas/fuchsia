// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {bitfield::bitfield, bitflags::bitflags, thiserror::Error};

/// The error types for packet parsing.
#[derive(Error, Debug, PartialEq)]
pub enum Error {
    /// The value that was sent was out of range.
    #[error("Value was out of range.")]
    OutOfRange,

    /// The value that was provided is invalid.
    #[error("Invalid value.")]
    InvalidValue,

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

bitflags! {
    /// Sampling Frequency field for SBC (Octet 0; b4-7).
    /// 44100Hz and 48000Hz are mandatory for A2DP sink.
    /// A2DP source must support at least one of 44100Hz and 48000Hz.
    /// A2DP Sec. 4.3.2.1
    pub struct SbcSamplingFrequency:u8 {
        const FREQ16000HZ   = 0b1000;
        const FREQ32000HZ   = 0b0100;
        const FREQ44100HZ   = 0b0010;
        const FREQ48000HZ   = 0b0001;
        const MANDATORY_SNK = Self::FREQ44100HZ.bits | Self::FREQ48000HZ.bits;
    }
}

bitflags! {
    /// Channel Mode field for SBC (Octet 0; b0-3).
    /// Support for all modes is mandatory in A2DP sink.
    /// Mono and at least one of Dual Channel, Stereo, and Joint Stereo must be
    /// supported by A2DP source.
    /// A2DP Sec. 4.3.2.2
    pub struct SbcChannelMode:u8 {
        const MONO          = 0b1000;
        const DUAL_CHANNEL  = 0b0100;
        const STEREO        = 0b0010;
        const JOINT_STEREO  = 0b0001;
        const MANDATORY_SNK = Self::MONO.bits
            | Self::DUAL_CHANNEL.bits
            | Self::STEREO.bits
            | Self::JOINT_STEREO.bits;
    }
}

bitflags! {
    /// The Block Length field for SBC (Octet 1; b4-7).
    /// Support for all block lengths is mandatory in both A2DP Sink and Source.
    /// A2DP Sec. 4.3.2.3
    pub struct SbcBlockCount:u8 {
        const FOUR          = 0b1000;
        const EIGHT         = 0b0100;
        const TWELVE        = 0b0010;
        const SIXTEEN       = 0b0001;
        const MANDATORY_SNK = Self::FOUR.bits
            | Self::EIGHT.bits
            | Self::TWELVE.bits
            | Self::SIXTEEN.bits;
        const MANDATORY_SRC = Self::FOUR.bits
            | Self::EIGHT.bits
            | Self::TWELVE.bits
            | Self::SIXTEEN.bits;
    }
}

bitflags! {
    /// The Number of Subbands field for SBC (Octet 1; b2-3).
    /// Support for both 4 and 8 subbands is mandatory in A2DP Sink.
    /// Support for only 8 subbands is mandatory in A2DP Source.
    /// 4 subbands is optional.
    /// A2DP Sec. 4.3.2.4
    pub struct SbcSubBands:u8 {
        const FOUR            = 0b0010;
        const EIGHT            = 0b0001;
        const MANDATORY_SNK = Self::FOUR.bits | Self::EIGHT.bits;
        const MANDATORY_SRC = Self::EIGHT.bits;
    }
}

bitflags! {
    /// Allocation Method field for SBC (Octet 1; b0-1).
    /// Support for both SNR and Loudness is mandatory in A2DP Sink.
    /// Support for at least Loudness is mandatory in A2DP Source. SNR is optional.
    /// A2DP Sec. 4.3.2.5
    pub struct SbcAllocation:u8 {
        const SNR           = 0b0010;
        const LOUDNESS      = 0b0001;
        const MANDATORY_SNK = Self::SNR.bits | Self::LOUDNESS.bits;
        const MANDATORY_SRC = Self::LOUDNESS.bits;
    }
}

bitfield! {
    /// SBC Codec Specific Information Elements (A2DP Sec. 4.3.2).
    /// Packet structure:
    ///     Octet0: Sampling Frequency (b4-7), Channel Mode (b0-3)
    ///     Octet1: Block Length (b4-7), Subbands (b2-3), Allocation Method (b0-1)
    ///     Octet2: Minimum Bitpool Value [2,250]
    ///     Octet3: Maximum Bitpool Value [2,250]
    /// Some fields are mandatory choose 1, and therefore do not have a mandatory parameter method.
    pub struct SbcCodecInfo(u32);
    impl Debug;
    u8;
    maxbitpoolval, set_maxbpv: 7, 0;
    minbitpoolval, set_minbpv: 15, 8;
    allocation_method, set_allocation_method: 17,16;
    subbands, set_sub_bands: 19, 18;
    block_count, set_block_count: 23, 20;
    channel_mode, set_channel_mode: 27, 24;
    pub sampling_frequency, set_sampling_frequency: 31, 28;
}

impl SbcCodecInfo {
    // Bitpool values can range from [2,250].
    pub const BITPOOL_MIN: u8 = 2;
    pub const BITPOOL_MAX: u8 = 250;
    pub fn new(
        sampling_frequency: SbcSamplingFrequency,
        channel_mode: SbcChannelMode,
        block_count: SbcBlockCount,
        sub_bands: SbcSubBands,
        allocation: SbcAllocation,
        min_bpv: u8,
        max_bpv: u8,
    ) -> Result<Self, Error> {
        if min_bpv > max_bpv {
            return Err(Error::InvalidValue);
        }
        if min_bpv < Self::BITPOOL_MIN
            || min_bpv > Self::BITPOOL_MAX
            || max_bpv < Self::BITPOOL_MIN
            || max_bpv > Self::BITPOOL_MAX
        {
            return Err(Error::OutOfRange);
        }

        let mut res = SbcCodecInfo(0);
        res.set_maxbpv(max_bpv);
        res.set_minbpv(min_bpv);
        res.set_allocation_method(allocation.bits());
        res.set_sub_bands(sub_bands.bits());
        res.set_block_count(block_count.bits());
        res.set_channel_mode(channel_mode.bits());
        res.set_sampling_frequency(sampling_frequency.bits());
        Ok(res)
    }
    pub fn to_bytes(&self) -> Vec<u8> {
        self.0.to_be_bytes().to_vec()
    }
}

bitflags! {
    /// Object Type field for MPEG-2,4 AAC (Octet 0; b0-7).
    /// Support for MPEG-2 AAC LC is mandatory in both A2DP Sink and Source.
    /// Bits 0 to 4 are RFA.
    /// A2DP Sec. 4.5.2.1
    pub struct AACObjectType:u8 {
        const MPEG2_AAC_LC       = 0b10000000;
        const MPEG4_AAC_LC       = 0b01000000;
        const MPEG4_AAC_LTP      = 0b00100000;
        const MPEG4_AAC_SCALABLE = 0b00010000;
        const MANDATORY_SNK = Self::MPEG2_AAC_LC.bits;
        const MANDATORY_SRC = Self::MPEG2_AAC_LC.bits;
    }
}

bitflags! {
    /// Sampling Frequency field for MPEG-2,4 AAC (Octet 1; b0-7, Octet 2; b4-7)
    /// Support for 44.1KHz & 48.0KHz is mandatory in A2DP Sink.
    /// Support for either 44.1KHz or 48.0KHz is mandatory in A2DP Source.
    /// A2DP Sec. 4.5.2.2
    pub struct AACSamplingFrequency:u16 {
        const FREQ8000HZ  = 0b100000000000;
        const FREQ11025HZ = 0b010000000000;
        const FREQ12000HZ = 0b001000000000;
        const FREQ16000HZ = 0b000100000000;
        const FREQ22050HZ = 0b000010000000;
        const FREQ24000HZ = 0b000001000000;
        const FREQ32000HZ = 0b000000100000;
        const FREQ44100HZ = 0b000000010000;
        const FREQ48000HZ = 0b000000001000;
        const FREQ64000HZ = 0b000000000100;
        const FREQ88200HZ = 0b000000000010;
        const FREQ96000HZ = 0b000000000001;
        const MANDATORY_SNK = Self::FREQ44100HZ.bits | Self::FREQ48000HZ.bits;
    }
}

bitflags! {
    /// Channels field for MPEG-2,4 AAC (Octet 2; b2-3).
    /// Support for both 1 and 2 channels is mandatory in A2DP Sink.
    /// Support for either 1 or 2 channels is mandatory in A2DP Source.
    /// A2DP Sec 4.5.2.3
    pub struct AACChannels:u8 {
        const ONE = 0b10;
        const TWO = 0b01;
        const MANDATORY_SNK = Self::ONE.bits | Self::TWO.bits;
    }
}

bitflags! {
    /// Support of Variable Bit Rate (VBR) field for MPEG-2,4 AAC (Octet 3; b7).
    /// Support for VBR is mandatory in A2DP Sink.
    /// A2DP Sec 4.5.2.5
    pub struct AACVariableBitRate: u8 {
        const VBR_SUPPORTED = 0b1;
        const MANDATORY_SNK = Self::VBR_SUPPORTED.bits;
    }
}

bitfield! {
    /// MPEG-2 AAC Codec Specific Information Elements (A2DP Sec 4.5.2)
    /// Structure:
    ///     Octet0: Object Type (b 40-47)
    ///     Octet1: Sampling Frequency (b 32-39)
    ///     Octet2: Sampling Frequency (b 28-31), Channels (b 26-27), RFA (b 24-25)
    ///     Octet3: VBR (b 23), Bit Rate (b 16-22)
    ///     Octet4: Bit Rate (b 8-15)
    ///     Octet5: Bit Rate (b 0-7)
    /// Some fields are mandatory choose 1, and therefore do not have a mandatory parameter method.
    pub struct AACMediaCodecInfo(u64);
    impl Debug;
    u8;
    u32, bitrate, set_bitrate: 22, 0;
    vbr, set_vbr: 23, 23;
    // Bits 24-25 RFA.
    channels, set_channels: 27,26;
    pub u16, sampling_frequency, set_sampling_frequency: 39, 28;
    object_type, set_object_type: 47, 40;
    // Bits 48-63 Unused.
}

impl AACMediaCodecInfo {
    pub fn new(
        object_type: AACObjectType,
        sampling_frequency: AACSamplingFrequency,
        channels: AACChannels,
        vbr: AACVariableBitRate,
        bitrate: u32,
    ) -> Result<Self, Error> {
        // Bitrate is expressed as a 23bit UiMsbf, stored in a u32.
        if bitrate > 0x7fffff {
            return Err(Error::OutOfRange);
        }
        let mut res = AACMediaCodecInfo(0);
        res.set_bitrate(bitrate);
        res.set_vbr(vbr.bits());
        res.set_channels(channels.bits());
        res.set_sampling_frequency(sampling_frequency.bits());
        res.set_object_type(object_type.bits());
        Ok(res)
    }

    /// `AACMediaCodecInfo` is represented as an u64, with upper 16 bits unused.
    /// Return a vector of the lower 6 bytes.
    pub fn to_bytes(&self) -> Vec<u8> {
        let codec_info = self.0.to_be_bytes();
        let mut res = [0u8; 6];
        res.copy_from_slice(&codec_info[2..8]);
        res.to_vec()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    /// Unit test for the SBC media codec info generation.
    fn test_sbc_media_codec_info() {
        // Mandatory A2DP Sink support case.
        let sbc_media_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            2,
            250,
        )
        .expect("Couldn't create sbc media codec info.");
        let res = sbc_media_codec_info.to_bytes();
        assert_eq!(vec![0x3F, 0xFF, 2, 250], res);

        // Mandatory A2DP source support case. Some fields are choose 1 fields.
        let sbc_media_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::MONO | SbcChannelMode::DUAL_CHANNEL,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            2,
            250,
        )
        .expect("Couldn't create sbc media codec info.");
        let res = sbc_media_codec_info.to_bytes();
        assert_eq!(vec![0x2C, 0xF5, 2, 250], res);

        // No supported codec information
        let sbc_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::empty(),
            SbcChannelMode::empty(),
            SbcBlockCount::empty(),
            SbcSubBands::empty(),
            SbcAllocation::empty(),
            2,
            250,
        )
        .expect("Couldn't create sbc media codec info.");
        let res = sbc_codec_info.to_bytes();
        assert_eq!(vec![0x00, 0x00, 2, 250], res);

        // All codec field values are supported
        let sbc_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");
        let res = sbc_codec_info.to_bytes();
        assert_eq!(vec![0xFF, 0xFF, 2, 250], res);

        // Out of range bitpool value
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            20,
            252, // Too large.
        );
        assert!(sbc_codec_info.is_err());

        // Out of range bitpool value
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            0, // Too small
            240,
        );
        assert!(sbc_codec_info.is_err());

        // Invalid bitpool value
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            100,
            50,
        );
        assert!(sbc_codec_info.is_err());
    }

    #[test]
    fn test_aac_media_codec_info() {
        // Empty case.
        let aac_media_codec_info: AACMediaCodecInfo = AACMediaCodecInfo::new(
            AACObjectType::empty(),
            AACSamplingFrequency::empty(),
            AACChannels::empty(),
            AACVariableBitRate::empty(),
            0,
        )
        .expect("Error creating aac media codec info.");
        let res = aac_media_codec_info.to_bytes();
        assert_eq!(vec![0, 0, 0, 0, 0, 0], res);

        // All codec info supported case.
        let aac_media_codec_info: AACMediaCodecInfo = AACMediaCodecInfo::new(
            AACObjectType::all(),
            AACSamplingFrequency::all(),
            AACChannels::all(),
            AACVariableBitRate::all(),
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");
        let res = aac_media_codec_info.to_bytes();
        assert_eq!(vec![0xF0, 0xFF, 0xFC, 0xFF, 0xFF, 0xFF], res);

        // Only VBR specified.
        let aac_media_codec_info: AACMediaCodecInfo = AACMediaCodecInfo::new(
            AACObjectType::empty(),
            AACSamplingFrequency::empty(),
            AACChannels::empty(),
            AACVariableBitRate::VBR_SUPPORTED,
            0,
        )
        .expect("Error creating aac media codec info.");
        let res = aac_media_codec_info.to_bytes();
        assert_eq!(vec![0x00, 0x00, 0x00, 0x80, 0x00, 0x00], res);

        // A2DP Sink mandatory fields supported.
        let aac_media_codec_info: AACMediaCodecInfo = AACMediaCodecInfo::new(
            AACObjectType::MANDATORY_SNK,
            AACSamplingFrequency::MANDATORY_SNK,
            AACChannels::MANDATORY_SNK,
            AACVariableBitRate::MANDATORY_SNK,
            0xAAFF, // Arbitrary bit rate.
        )
        .expect("Error creating aac media codec info.");
        let res = aac_media_codec_info.to_bytes();
        assert_eq!(vec![0x80, 0x01, 0x8C, 0x80, 0xAA, 0xFF], res);

        // A2DP Source mandatory fields supported.
        let aac_media_codec_info: AACMediaCodecInfo = AACMediaCodecInfo::new(
            AACObjectType::MANDATORY_SRC,
            AACSamplingFrequency::FREQ44100HZ,
            AACChannels::ONE,
            AACVariableBitRate::empty(), // VBR is optional in SRC.
            0xAAFF,                      // Arbitrary
        )
        .expect("Error creating aac media codec info.");
        let res = aac_media_codec_info.to_bytes();
        assert_eq!(vec![0x80, 0x01, 0x08, 0x00, 0xAA, 0xFF], res);

        // Out of range bit rate.
        let aac_media_codec_info = AACMediaCodecInfo::new(
            AACObjectType::MANDATORY_SRC,
            AACSamplingFrequency::FREQ44100HZ,
            AACChannels::ONE,
            AACVariableBitRate::empty(),
            0xFFFFFF, // Too large
        );
        assert!(aac_media_codec_info.is_err());
    }
}
