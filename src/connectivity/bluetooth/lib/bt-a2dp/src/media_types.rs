// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use bitflags::bitflags;
use bt_avdtp as avdtp;
use std::convert::TryFrom;

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

impl SbcSamplingFrequency {
    /// Select the "best" sampling frequency of the ones available.
    fn best(&self) -> Option<Self> {
        let ordered = [Self::FREQ48000HZ, Self::FREQ44100HZ, Self::FREQ32000HZ, Self::FREQ16000HZ];
        ordered.iter().find(|freq| self.contains(**freq)).copied()
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

impl SbcChannelMode {
    fn best(&self) -> Option<Self> {
        let ordered = [Self::JOINT_STEREO, Self::STEREO, Self::DUAL_CHANNEL, Self::MONO];
        ordered.iter().find(|mode| self.contains(**mode)).copied()
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

pub const SBC_CODEC_EXTRA_LEN: usize = 4;

bitfield! {
    /// SBC Codec Specific Information Elements (A2DP Sec. 4.3.2).
    /// Packet structure:
    ///     Octet0: Sampling Frequency (b4-7), Channel Mode (b0-3)
    ///     Octet1: Block Length (b4-7), Subbands (b2-3), Allocation Method (b0-1)
    ///     Octet2: Minimum Bitpool Value [2,250]
    ///     Octet3: Maximum Bitpool Value [2,250]
    /// Some fields are mandatory choose 1, and therefore do not have a mandatory parameter method.
    struct SbcCodecInfoBits(u32);
    impl Debug;
    u8;
    maxbitpoolval, set_maxbpv: 7, 0;
    minbitpoolval, set_minbpv: 15, 8;
    allocation_method, set_allocation_method: 17,16;
    sub_bands, set_sub_bands: 19, 18;
    block_count, set_block_count: 23, 20;
    channel_mode, set_channel_mode: 27, 24;
    sampling_frequency, set_sampling_frequency: 31, 28;
}

#[derive(Debug)]
pub struct SbcCodecInfo(SbcCodecInfoBits);

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
    ) -> avdtp::Result<Self> {
        if min_bpv > max_bpv {
            return Err(avdtp::Error::OutOfRange);
        }
        if min_bpv < Self::BITPOOL_MIN
            || min_bpv > Self::BITPOOL_MAX
            || max_bpv < Self::BITPOOL_MIN
            || max_bpv > Self::BITPOOL_MAX
        {
            return Err(avdtp::Error::OutOfRange);
        }

        let mut res = SbcCodecInfoBits(0);
        res.set_maxbpv(max_bpv);
        res.set_minbpv(min_bpv);
        res.set_allocation_method(allocation.bits());
        res.set_sub_bands(sub_bands.bits());
        res.set_block_count(block_count.bits());
        res.set_channel_mode(channel_mode.bits());
        res.set_sampling_frequency(sampling_frequency.bits());
        Ok(Self(res))
    }

    pub fn to_bytes(&self) -> [u8; 4] {
        (self.0).0.to_be_bytes()
    }

    pub fn sub_bands(&self) -> SbcSubBands {
        SbcSubBands::from_bits_truncate(self.0.sub_bands())
    }

    pub fn allocation_method(&self) -> SbcAllocation {
        SbcAllocation::from_bits_truncate(self.0.allocation_method())
    }

    pub fn block_count(&self) -> SbcBlockCount {
        SbcBlockCount::from_bits_truncate(self.0.block_count())
    }

    pub fn channel_mode(&self) -> SbcChannelMode {
        SbcChannelMode::from_bits_truncate(self.0.channel_mode())
    }

    /// Returns the number of channels selected.
    /// Returns Error::OutOfRange if both mono and stereo are selected.
    pub fn channel_count(&self) -> avdtp::Result<usize> {
        let chan_mode = self.channel_mode();
        if chan_mode == SbcChannelMode::MONO {
            return Ok(1);
        } else if !chan_mode.is_empty() && (chan_mode & SbcChannelMode::MONO).is_empty() {
            return Ok(2);
        }
        Err(avdtp::Error::OutOfRange)
    }

    pub fn max_bitpool(&self) -> u8 {
        self.0.maxbitpoolval()
    }

    /// Returns the sampling frequeency selected, in hz.
    /// Returns Error::OutOfRange if multiple frequencies are selected.
    pub fn sampling_frequency(&self) -> avdtp::Result<u32> {
        let hz = match SbcSamplingFrequency::from_bits_truncate(self.0.sampling_frequency()) {
            SbcSamplingFrequency::FREQ16000HZ => 16000,
            SbcSamplingFrequency::FREQ32000HZ => 32000,
            SbcSamplingFrequency::FREQ44100HZ => 44100,
            SbcSamplingFrequency::FREQ48000HZ => 48000,
            _ => return Err(avdtp::Error::OutOfRange),
        };
        Ok(hz)
    }

    /// Returns true if `other` is a compatible configuration of `self`
    pub fn supports(&self, other: &Self) -> bool {
        let allowed_frequencies =
            SbcSamplingFrequency::from_bits_truncate(self.0.sampling_frequency());
        let selected_frequencies =
            SbcSamplingFrequency::from_bits_truncate(other.0.sampling_frequency());
        if !(allowed_frequencies.intersects(selected_frequencies)) {
            return false;
        }
        if !(self.allocation_method().intersects(other.allocation_method())) {
            return false;
        }
        if !(self.channel_mode().intersects(other.channel_mode())) {
            return false;
        }
        if !(self.block_count().intersects(other.block_count())) {
            return false;
        }
        if !(self.sub_bands().intersects(other.sub_bands())) {
            return false;
        }
        if self.0.minbitpoolval() > other.0.minbitpoolval() {
            return false;
        }
        if self.0.maxbitpoolval() < other.0.maxbitpoolval() {
            return false;
        }
        return true;
    }

    /// Return the best intersection of a and b's capabilites, if one exists.
    pub fn negotiate(a: &Self, b: &Self) -> Option<Self> {
        let min_bitpool = std::cmp::max(a.0.minbitpoolval(), b.0.minbitpoolval());
        let max_bitpool = std::cmp::min(a.0.maxbitpoolval(), b.0.maxbitpoolval());
        if max_bitpool < min_bitpool {
            return None;
        }
        let available_frequencies = SbcSamplingFrequency::from_bits_truncate(
            a.0.sampling_frequency() & b.0.sampling_frequency(),
        );
        let frequency = match available_frequencies.best() {
            None => return None,
            Some(freq) => freq,
        };
        let available_mode = a.channel_mode() & b.channel_mode();
        let channel_mode = match available_mode.best() {
            None => return None,
            Some(mode) => mode,
        };
        // All sources and sinks must support these options. A2DP 1.3.2 Sec 4.3.2
        let allocation = SbcAllocation::LOUDNESS;
        let block_count = SbcBlockCount::SIXTEEN;
        let sub_bands = SbcSubBands::EIGHT;
        Some(
            SbcCodecInfo::new(
                frequency,
                channel_mode,
                block_count,
                sub_bands,
                allocation,
                min_bitpool,
                max_bitpool,
            )
            .expect("supported options"),
        )
    }
}

impl TryFrom<&[u8]> for SbcCodecInfo {
    type Error = avdtp::Error;

    fn try_from(value: &[u8]) -> Result<Self, Self::Error> {
        if value.len() != SBC_CODEC_EXTRA_LEN {
            return Err(avdtp::Error::OutOfRange);
        }

        let mut codec_info_bytes = [0_u8; SBC_CODEC_EXTRA_LEN];
        codec_info_bytes.copy_from_slice(&value);

        Ok(Self(SbcCodecInfoBits(u32::from_be_bytes(codec_info_bytes))))
    }
}

impl From<SbcCodecInfo> for avdtp::ServiceCapability {
    fn from(codec_info: SbcCodecInfo) -> Self {
        Self::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: codec_info.to_bytes().to_vec(),
        }
    }
}

bitflags! {
    /// Object Type field for MPEG-2,4 AAC (Octet 0; b0-7).
    /// Support for MPEG-2 AAC LC is mandatory in both A2DP Sink and Source.
    /// Bits 0 to 4 are RFA.
    /// A2DP Sec. 4.5.2.1
    pub struct AacObjectType:u8 {
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
    pub struct AacSamplingFrequency:u16 {
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

impl AacSamplingFrequency {
    fn best(&self) -> Option<Self> {
        // Since one of 48000 or 44100 is mandatory, this list should find one.
        let ordered = [Self::FREQ96000HZ, Self::FREQ48000HZ, Self::FREQ44100HZ];
        ordered.iter().find(|freq| self.contains(**freq)).copied()
    }
}

bitflags! {
    /// Channels field for MPEG-2,4 AAC (Octet 2; b2-3).
    /// Support for both 1 and 2 channels is mandatory in A2DP Sink.
    /// Support for either 1 or 2 channels is mandatory in A2DP Source.
    /// A2DP Sec 4.5.2.3
    pub struct AacChannels:u8 {
        const ONE = 0b10;
        const TWO = 0b01;
        const MANDATORY_SNK = Self::ONE.bits | Self::TWO.bits;
    }
}

impl AacChannels {
    fn best(&self) -> Option<Self> {
        let ordered = [Self::TWO, Self::ONE];
        ordered.iter().find(|channels| self.contains(**channels)).copied()
    }
}

bitflags! {
    /// Support of Variable Bit Rate (VBR) field for MPEG-2,4 AAC (Octet 3; b7).
    /// Support for VBR is mandatory in A2DP Sink.
    /// A2DP Sec 4.5.2.5
    struct AacVariableBitRate: u8 {
        const VBR_SUPPORTED = 0b1;
        const MANDATORY_SNK = Self::VBR_SUPPORTED.bits;
    }
}

pub const AAC_CODEC_EXTRA_LEN: usize = 6;

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
    struct AacCodecInfoBits(u64);
    impl Debug;
    u8;
    u32, bitrate, set_bitrate: 22, 0;
    vbr, set_vbr: 23, 23;
    // Bits 24-25 RFA.
    channels, set_channels: 27,26;
    u16, sampling_frequency, set_sampling_frequency: 39, 28;
    object_type, set_object_type: 47, 40;
    // Bits 48-63 Unused.
}

#[derive(Debug)]
pub struct AacCodecInfo(AacCodecInfoBits);

impl AacCodecInfo {
    pub fn new(
        object_type: AacObjectType,
        sampling_frequency: AacSamplingFrequency,
        channels: AacChannels,
        vbr: bool,
        bitrate: u32,
    ) -> avdtp::Result<Self> {
        // Bitrate is expressed as a 23bit UiMsbf, stored in a u32.
        if bitrate > 0x7fffff {
            return Err(avdtp::Error::OutOfRange);
        }
        let mut res = AacCodecInfoBits(0);
        res.set_bitrate(bitrate);
        if vbr {
            res.set_vbr(AacVariableBitRate::VBR_SUPPORTED.bits());
        }
        res.set_channels(channels.bits());
        res.set_sampling_frequency(sampling_frequency.bits());
        res.set_object_type(object_type.bits());
        Ok(Self(res))
    }

    /// `AacCodecInfoBytes` is represented as an u64, with upper 16 bits unused.
    /// Return a vector of the lower 6 bytes.
    pub fn to_bytes(&self) -> [u8; 6] {
        let codec_info = (self.0).0.to_be_bytes();
        let mut res = [0u8; 6];
        res.copy_from_slice(&codec_info[2..8]);
        res
    }

    pub fn variable_bit_rate(&self) -> bool {
        self.0.vbr() == 0b1
    }

    pub fn bitrate(&self) -> u32 {
        self.0.bitrate()
    }

    pub fn channels(&self) -> AacChannels {
        AacChannels::from_bits_truncate(self.0.channels())
    }

    /// Returns the number of channels selected.
    /// Returns Error::OutOfRange if both mono and stereo are selected.
    pub fn channel_count(&self) -> avdtp::Result<usize> {
        let count = match self.channels() {
            AacChannels::ONE => 1,
            AacChannels::TWO => 2,
            _ => return Err(avdtp::Error::OutOfRange),
        };
        Ok(count)
    }

    fn object_type(&self) -> AacObjectType {
        AacObjectType::from_bits_truncate(self.0.object_type())
    }

    /// Returns the sampling frequeency selected, in hz.
    /// Returns Error::OutOfRange if multiple frequencies are selected.
    pub fn sampling_frequency(&self) -> avdtp::Result<u32> {
        let hz = match AacSamplingFrequency::from_bits_truncate(self.0.sampling_frequency()) {
            AacSamplingFrequency::FREQ8000HZ => 8000,
            AacSamplingFrequency::FREQ11025HZ => 11025,
            AacSamplingFrequency::FREQ12000HZ => 12000,
            AacSamplingFrequency::FREQ16000HZ => 16000,
            AacSamplingFrequency::FREQ22050HZ => 22050,
            AacSamplingFrequency::FREQ24000HZ => 24000,
            AacSamplingFrequency::FREQ32000HZ => 32000,
            AacSamplingFrequency::FREQ44100HZ => 44100,
            AacSamplingFrequency::FREQ48000HZ => 48000,
            AacSamplingFrequency::FREQ64000HZ => 64000,
            AacSamplingFrequency::FREQ88200HZ => 88200,
            AacSamplingFrequency::FREQ96000HZ => 96000,
            _ => return Err(avdtp::Error::OutOfRange),
        };
        Ok(hz)
    }

    /// Returns true if `other` is a compatable configuration of `self`
    pub fn supports(&self, other: &Self) -> bool {
        let allowed_frequencies =
            AacSamplingFrequency::from_bits_truncate(self.0.sampling_frequency());
        let selected_frequencies =
            AacSamplingFrequency::from_bits_truncate(other.0.sampling_frequency());
        if !(allowed_frequencies.intersects(selected_frequencies)) {
            return false;
        }
        if !(self.channels().intersects(other.channels())) {
            return false;
        }
        if !(self.object_type().intersects(other.object_type())) {
            return false;
        }
        if !self.variable_bit_rate() && other.variable_bit_rate() {
            return false;
        }
        // Note: the "capabilities" bitrate field is unclear in the A2DP spec: 4.5.2.4.
        // Interpreting this as the max bitrate for either constant or vbr.
        // Zero is "not known", which we interpret to be "any bitrate is okay", but only
        // if we are interpreting our bitrate support
        if self.bitrate() != 0 && (self.bitrate() < other.bitrate()) {
            return false;
        }
        return true;
    }

    pub fn negotiate(a: &Self, b: &Self) -> Option<Self> {
        let available_frequencies = AacSamplingFrequency::from_bits_truncate(
            a.0.sampling_frequency() & b.0.sampling_frequency(),
        );
        let sampling_frequency = match available_frequencies.best() {
            None => return None,
            Some(freq) => freq,
        };
        let available_channels = a.channels() & b.channels();
        let channels = match available_channels.best() {
            None => return None,
            Some(channels) => channels,
        };
        let vbr = a.variable_bit_rate() && b.variable_bit_rate();
        // If either bitrate is unspecified, take the other one, otherwise, the minimum.
        let bitrate = match (a.bitrate(), b.bitrate()) {
            (0, b) => b,
            (a, 0) => a,
            (a, b) => std::cmp::min(a, b),
        };
        // This option is mandatory for source and sink and we prefer it. See A2DP 1.3.2 Sec 4.5.2
        let object_type = AacObjectType::MPEG2_AAC_LC;
        Some(AacCodecInfo::new(object_type, sampling_frequency, channels, vbr, bitrate).unwrap())
    }
}

impl TryFrom<&[u8]> for AacCodecInfo {
    type Error = avdtp::Error;

    /// Create `AacCodecInfo` from slice of length `AAC_CODEC_EXTRA_LEN`
    fn try_from(value: &[u8]) -> Result<Self, Self::Error> {
        if value.len() != AAC_CODEC_EXTRA_LEN {
            return Err(avdtp::Error::OutOfRange);
        }
        let mut codec_info_bytes = [0_u8; 8];
        let codec_info_slice = &mut codec_info_bytes[2..8];
        // AacCodecInfo is represented as 8 bytes, with lower 6 bytes containing
        // the codec extra data.
        codec_info_slice.copy_from_slice(&value);
        Ok(Self(AacCodecInfoBits(u64::from_be_bytes(codec_info_bytes))))
    }
}

impl From<AacCodecInfo> for avdtp::ServiceCapability {
    fn from(codec_info: AacCodecInfo) -> Self {
        Self::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_AAC,
            codec_extra: codec_info.to_bytes().to_vec(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    /// Unit test for the SBC media codec info generation.
    fn test_sbc_codec_info() {
        // Mandatory A2DP Sink support case.
        let sbc_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            2,
            250,
        )
        .expect("Couldn't create sbc media codec info.");
        let res = sbc_codec_info.to_bytes();
        let codec_extra: Vec<u8> = vec![0x3F, 0xFF, 2, 250];
        assert_eq!(codec_extra, res);

        // reverse parsing and check we match
        let res = SbcCodecInfo::try_from(&codec_extra[..]).expect("created codec info");
        assert_eq!((res.0).0, (sbc_codec_info.0).0);

        // Mandatory A2DP source support case. Some fields are choose 1 fields.
        let sbc_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::MONO | SbcChannelMode::DUAL_CHANNEL,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            2,
            250,
        )
        .expect("Couldn't create sbc media codec info.");
        let res = sbc_codec_info.to_bytes();
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
        assert_matches!(sbc_codec_info.channel_count(), Err(avdtp::Error::OutOfRange));
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
        assert_matches!(sbc_codec_info.channel_count(), Err(avdtp::Error::OutOfRange));
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

        let empty = vec![0, 0, 0, 0];
        let res = SbcCodecInfo::try_from(&empty[..]).expect("created codec info");
        assert_eq!((res.0).0, 0);

        let too_big = vec![0, 0, 0, 0, 0];
        assert_matches!(SbcCodecInfo::try_from(&too_big[..]), Err(avdtp::Error::OutOfRange));

        // Mono and Stereo
        let sbc_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::MONO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            2,
            250,
        )
        .expect("Couldn't create sbc media codec info.");
        assert_matches!(sbc_codec_info.channel_count(), Ok(1));

        let sbc_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            2,
            250,
        )
        .expect("Couldn't create sbc media codec info.");
        assert_matches!(sbc_codec_info.channel_count(), Ok(2));
    }

    #[test]
    fn test_sbc_codec_supports() {
        let sbc_codec_all = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");
        // Should support itself.
        assert!(sbc_codec_all.supports(&sbc_codec_all));

        let sbc_codec_441 = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::all(),
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            53,                        // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");

        let sbc_codec_48 = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::all(),
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            53,                        // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");

        assert!(sbc_codec_all.supports(&sbc_codec_441));
        assert!(sbc_codec_all.supports(&sbc_codec_48));

        // Frequency mismatches
        assert!(!sbc_codec_48.supports(&sbc_codec_441));
        assert!(!sbc_codec_441.supports(&sbc_codec_48));

        // Min / Max bitpools
        assert!(!sbc_codec_48.supports(&sbc_codec_all));
        assert!(!sbc_codec_441.supports(&sbc_codec_all));

        let sbc_codec_mono = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::MONO,
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");
        let sbc_codec_stereo = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::all(),
            SbcSubBands::all(),
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");

        assert!(sbc_codec_all.supports(&sbc_codec_mono));
        assert!(sbc_codec_all.supports(&sbc_codec_stereo));
        assert!(!sbc_codec_mono.supports(&sbc_codec_stereo));
        assert!(!sbc_codec_stereo.supports(&sbc_codec_mono));

        let sbc_codec_blocks_4 = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::FOUR,
            SbcSubBands::all(),
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");
        let sbc_codec_blocks_8 = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::EIGHT,
            SbcSubBands::all(),
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");

        assert!(sbc_codec_all.supports(&sbc_codec_blocks_4));
        assert!(sbc_codec_all.supports(&sbc_codec_blocks_8));
        assert!(!sbc_codec_blocks_4.supports(&sbc_codec_blocks_8));
        assert!(!sbc_codec_blocks_8.supports(&sbc_codec_blocks_4));

        let sbc_codec_bands_4 = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::FOUR,
            SbcSubBands::FOUR,
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");
        let sbc_codec_bands_8 = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::FOUR,
            SbcSubBands::EIGHT,
            SbcAllocation::all(),
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");

        assert!(sbc_codec_all.supports(&sbc_codec_bands_4));
        assert!(sbc_codec_all.supports(&sbc_codec_bands_8));
        assert!(!sbc_codec_bands_4.supports(&sbc_codec_bands_8));
        assert!(!sbc_codec_bands_8.supports(&sbc_codec_bands_4));

        let sbc_codec_snr = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::FOUR,
            SbcSubBands::FOUR,
            SbcAllocation::SNR,
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");
        let sbc_codec_loudness = SbcCodecInfo::new(
            SbcSamplingFrequency::all(),
            SbcChannelMode::all(),
            SbcBlockCount::FOUR,
            SbcSubBands::FOUR,
            SbcAllocation::LOUDNESS,
            SbcCodecInfo::BITPOOL_MIN, // Smallest bitpool value.
            SbcCodecInfo::BITPOOL_MAX, // Largest bitpool value.
        )
        .expect("Couldn't create sbc media codec info.");

        assert!(sbc_codec_all.supports(&sbc_codec_snr));
        assert!(sbc_codec_all.supports(&sbc_codec_loudness));
        assert!(!sbc_codec_snr.supports(&sbc_codec_loudness));
        assert!(!sbc_codec_loudness.supports(&sbc_codec_snr));
    }

    #[test]
    fn test_aac_codec_info() {
        // Empty case.
        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::empty(),
            AacSamplingFrequency::empty(),
            AacChannels::empty(),
            false,
            0,
        )
        .expect("Error creating aac media codec info.");
        let res = aac_codec_info.to_bytes();
        assert_eq!(vec![0, 0, 0, 0, 0, 0], res);

        // All codec info supported case.
        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::all(),
            AacSamplingFrequency::all(),
            AacChannels::all(),
            true,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");
        assert_matches!(aac_codec_info.channel_count(), Err(avdtp::Error::OutOfRange));
        let res = aac_codec_info.to_bytes();
        assert_eq!(vec![0xF0, 0xFF, 0xFC, 0xFF, 0xFF, 0xFF], res);

        // Only VBR specified.
        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::empty(),
            AacSamplingFrequency::empty(),
            AacChannels::empty(),
            true,
            0,
        )
        .expect("Error creating aac media codec info.");
        assert_matches!(aac_codec_info.channel_count(), Err(avdtp::Error::OutOfRange));
        let res = aac_codec_info.to_bytes();
        assert_eq!(vec![0x00, 0x00, 0x00, 0x80, 0x00, 0x00], res);

        // A2DP Sink mandatory fields supported.
        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::MANDATORY_SNK,
            AacSamplingFrequency::MANDATORY_SNK,
            AacChannels::MANDATORY_SNK,
            true,
            0xAAFF, // Arbitrary bit rate.
        )
        .expect("Error creating aac media codec info.");
        let res = aac_codec_info.to_bytes();
        let codec_extra: Vec<u8> = vec![0x80, 0x01, 0x8C, 0x80, 0xAA, 0xFF];
        assert_eq!(codec_extra, res);

        // reverse parsing and check we match
        let res = AacCodecInfo::try_from(&codec_extra[..]).expect("created codec info");
        assert_eq!((res.0).0, (aac_codec_info.0).0);

        // A2DP Source mandatory fields supported.
        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::MANDATORY_SRC,
            AacSamplingFrequency::FREQ44100HZ,
            AacChannels::ONE,
            false,  // VBR is optional in SRC.
            0xAAFF, // Arbitrary
        )
        .expect("Error creating aac media codec info.");
        assert_matches!(aac_codec_info.channel_count(), Ok(1));
        let res = aac_codec_info.to_bytes();
        assert_eq!(vec![0x80, 0x01, 0x08, 0x00, 0xAA, 0xFF], res);

        // A2DP Source with two channels
        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::MANDATORY_SRC,
            AacSamplingFrequency::FREQ44100HZ,
            AacChannels::TWO,
            false,  // VBR is optional in SRC.
            0xAAFF, // Arbitrary
        )
        .expect("Error creating aac media codec info.");
        assert_matches!(aac_codec_info.channel_count(), Ok(2));

        // Out of range bit rate.
        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::MANDATORY_SRC,
            AacSamplingFrequency::FREQ44100HZ,
            AacChannels::ONE,
            false,
            0xFFFFFF, // Too large
        );
        assert!(aac_codec_info.is_err());

        let empty = vec![0, 0, 0, 0, 0, 0];
        let res = AacCodecInfo::try_from(&empty[..]).expect("created codec info");
        assert_eq!((res.0).0, 0);

        let too_big = vec![0, 0, 0, 0, 0, 0, 0];
        assert_matches!(AacCodecInfo::try_from(&too_big[..]), Err(avdtp::Error::OutOfRange));
    }

    #[test]
    fn test_aac_codec_supports() {
        // All codec info supported case.
        let aac_codec_all = AacCodecInfo::new(
            AacObjectType::all(),
            AacSamplingFrequency::all(),
            AacChannels::all(),
            true,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");

        assert!(aac_codec_all.supports(&aac_codec_all));

        let aac_codec_441 = AacCodecInfo::new(
            AacObjectType::all(),
            AacSamplingFrequency::FREQ44100HZ,
            AacChannels::all(),
            true,
            50, // Lower than largest.
        )
        .expect("Error creating aac media codec info.");

        let aac_codec_48 = AacCodecInfo::new(
            AacObjectType::all(),
            AacSamplingFrequency::FREQ48000HZ,
            AacChannels::all(),
            true,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");

        assert!(aac_codec_all.supports(&aac_codec_441));
        assert!(aac_codec_all.supports(&aac_codec_48));

        // Frequencies
        assert!(!aac_codec_441.supports(&aac_codec_48));
        assert!(!aac_codec_48.supports(&aac_codec_441));

        // Max bitrate.
        assert!(!aac_codec_441.supports(&aac_codec_all));

        // Channels
        let aac_codec_chan_one = AacCodecInfo::new(
            AacObjectType::all(),
            AacSamplingFrequency::all(),
            AacChannels::ONE,
            true,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");

        let aac_codec_chan_two = AacCodecInfo::new(
            AacObjectType::all(),
            AacSamplingFrequency::all(),
            AacChannels::TWO,
            true,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");

        assert!(aac_codec_all.supports(&aac_codec_chan_one));
        assert!(aac_codec_all.supports(&aac_codec_chan_two));
        assert!(!aac_codec_chan_one.supports(&aac_codec_chan_two));
        assert!(!aac_codec_chan_two.supports(&aac_codec_chan_one));

        // object type
        let aac_codec_mpeg2 = AacCodecInfo::new(
            AacObjectType::MPEG2_AAC_LC,
            AacSamplingFrequency::all(),
            AacChannels::ONE,
            true,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");

        let aac_codec_mpeg4 = AacCodecInfo::new(
            AacObjectType::MPEG4_AAC_LC,
            AacSamplingFrequency::all(),
            AacChannels::ONE,
            true,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");

        assert!(aac_codec_all.supports(&aac_codec_mpeg2));
        assert!(aac_codec_all.supports(&aac_codec_mpeg4));
        assert!(!aac_codec_mpeg2.supports(&aac_codec_mpeg4));
        assert!(!aac_codec_mpeg4.supports(&aac_codec_mpeg2));

        let aac_codec_not_vbr = AacCodecInfo::new(
            AacObjectType::MPEG4_AAC_LC,
            AacSamplingFrequency::all(),
            AacChannels::ONE,
            false,
            8388607, // Largest 23-bit bit rate.
        )
        .expect("Error creating aac media codec info.");

        assert!(!aac_codec_not_vbr.supports(&aac_codec_mpeg4));

        let aac_codec_bitrate_not_known = AacCodecInfo::new(
            AacObjectType::MPEG4_AAC_LC,
            AacSamplingFrequency::all(),
            AacChannels::ONE,
            true,
            0, // Bitrate "Not known"
        )
        .expect("Error creating aac media codec info.");

        assert!(aac_codec_bitrate_not_known.supports(&aac_codec_not_vbr));
    }
}
