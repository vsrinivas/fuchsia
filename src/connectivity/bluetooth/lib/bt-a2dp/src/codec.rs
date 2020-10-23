// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use bt_avdtp::{self as avdtp, MediaCodecType, ServiceCapability, StreamEndpointId};
use fidl_fuchsia_media as media;
use log::trace;
use std::convert::TryFrom;

use crate::media_types::{
    AacChannels, AacCodecInfo, SbcAllocation, SbcBlockCount, SbcChannelMode, SbcCodecInfo,
    SbcSamplingFrequency, SbcSubBands,
};
use crate::rtp::{AacRtpPacketBuilder, RtpPacketBuilder, SbcRtpPacketBuilder};

/// Stores the media codec configuration for an A2DP stream, and provides utility for integration
/// with Fuchsia media.
#[derive(Clone, Debug, PartialEq)]
pub struct MediaCodecConfig {
    codec_type: avdtp::MediaCodecType,
    codec_extra: Vec<u8>,
}

/// The number of frames in each SBC packet when encoding.
/// 5 is chosen by default as it represents a low amount of latency and fits within the default
/// L2CAP MTU.
/// RTP Header (12 bytes) + 1 byte (SBC header) + 5 * SBC Frame (119 bytes) = 608 bytes < 672
/// TODO(40986, 41449): Update this based on the input format and the codec settings.
const ENCODED_FRAMES_PER_SBC_PACKET: u8 = 5;
const PCM_FRAMES_PER_SBC_FRAME: u32 = 640;

/// The number of frames in each AAC packet when encoding.
/// Only one encoded AudioMuxElement is sent per RTP frame. 1024 is the most our current
/// AAC encoder will put in a single AudioMuxElement.
const ENCODED_FRAMES_PER_AAC_PACKET: u8 = 1;
const PCM_FRAMES_PER_AAC_FRAME: u32 = 1024;

impl MediaCodecConfig {
    /// Try to build a codec config given a codec type and the codec specific information elements
    /// for the codec specified in `extra`.  Fails with OutOfRange if the codec is not supported.
    pub fn build(codec_type: MediaCodecType, extra: &[u8]) -> avdtp::Result<Self> {
        match codec_type {
            MediaCodecType::AUDIO_SBC => {
                let _ = SbcCodecInfo::try_from(extra)?;
            }
            MediaCodecType::AUDIO_AAC => {
                let _ = AacCodecInfo::try_from(extra)?;
            }
            _ => return Err(avdtp::Error::OutOfRange),
        };
        Ok(Self { codec_type, codec_extra: extra.to_vec() })
    }

    /// Build an SBC configuration with minimum defaults for configuration
    /// (48000 Hz Mono, 16 Blocks, 8 SubBands, Loudness Allocation, 2-29 bitpool)
    /// This is the minimunm configuration requried by both Sink and Source as defined in the
    /// A2DP Specificaiton 1.2 Section 4.3.2.
    pub fn min_sbc() -> Self {
        let codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::MONO,
            SbcBlockCount::SIXTEEN,
            SbcSubBands::EIGHT,
            SbcAllocation::LOUDNESS,
            2,
            29,
        )
        .expect("Minimum Codec Info should build");
        Self::build(MediaCodecType::AUDIO_SBC, &codec_info.to_bytes()).expect("builds")
    }

    pub fn codec_type(&self) -> &MediaCodecType {
        &self.codec_type
    }

    pub fn codec_extra(&self) -> &[u8] {
        self.codec_extra.as_slice()
    }

    pub fn make_packet_builder(
        &self,
        max_packet_size: usize,
    ) -> avdtp::Result<Box<dyn RtpPacketBuilder>> {
        match self.codec_type {
            MediaCodecType::AUDIO_AAC => {
                let builder = AacRtpPacketBuilder::new(max_packet_size);
                return Ok(Box::new(builder));
            }
            MediaCodecType::AUDIO_SBC => {
                let builder = SbcRtpPacketBuilder::new(max_packet_size);
                return Ok(Box::new(builder));
            }
            _ => unreachable!(),
        }
    }

    /// Returns true if the given MediaCodecConfig is a compatible subset of this configuration.
    pub fn supports(&self, other: &MediaCodecConfig) -> bool {
        if &self.codec_type != other.codec_type() {
            return false;
        }
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => {
                let codec_info = SbcCodecInfo::try_from(self.codec_extra()).expect("should parse");
                let other_info = SbcCodecInfo::try_from(other.codec_extra()).expect("should parse");
                codec_info.supports(&other_info)
            }
            MediaCodecType::AUDIO_AAC => {
                let codec_info = AacCodecInfo::try_from(self.codec_extra()).expect("should parse");
                let other_info = AacCodecInfo::try_from(other.codec_extra()).expect("should parse");
                codec_info.supports(&other_info)
            }
            _ => false,
        }
    }

    /// Negotiate the best supported configuration, given another configuration.
    /// This can be seen as a kind of intersection of the capabilities of the two configs.
    /// IF this returns Some(result), then a.supports(result) and b.supports(result) will both be
    /// true.
    pub fn negotiate(a: &MediaCodecConfig, b: &MediaCodecConfig) -> Option<MediaCodecConfig> {
        if a.codec_type != b.codec_type {
            return None;
        }
        match a.codec_type {
            MediaCodecType::AUDIO_AAC => {
                let a = AacCodecInfo::try_from(a.codec_extra()).expect("should parse");
                let b = AacCodecInfo::try_from(b.codec_extra()).expect("should parse");
                AacCodecInfo::negotiate(&a, &b).map(|matched| MediaCodecConfig {
                    codec_type: MediaCodecType::AUDIO_AAC,
                    codec_extra: matched.to_bytes().to_vec(),
                })
            }
            MediaCodecType::AUDIO_SBC => {
                let a = SbcCodecInfo::try_from(a.codec_extra()).expect("should parse");
                let b = SbcCodecInfo::try_from(b.codec_extra()).expect("should parse");
                SbcCodecInfo::negotiate(&a, &b).map(|matched| MediaCodecConfig {
                    codec_type: MediaCodecType::AUDIO_SBC,
                    codec_extra: matched.to_bytes().to_vec(),
                })
            }
            _ => unreachable!(),
        }
    }

    /// Retrieves a set of EncoderSettings that is suitable to configure a StreamProcessor to encode
    /// to the target configuration for this MediaCodecConfig.
    /// Returns Err(OutOfRange) if this does not specify a single configuration.
    pub fn encoder_settings(&self) -> avdtp::Result<fidl_fuchsia_media::EncoderSettings> {
        let encoder_settings = match self.codec_type {
            MediaCodecType::AUDIO_SBC => {
                let codec_info = SbcCodecInfo::try_from(self.codec_extra())?;

                let sub_bands = match codec_info.sub_bands() {
                    SbcSubBands::FOUR => media::SbcSubBands::SubBands4,
                    SbcSubBands::EIGHT => media::SbcSubBands::SubBands8,
                    _ => return Err(avdtp::Error::OutOfRange),
                };

                let allocation = match codec_info.allocation_method() {
                    SbcAllocation::SNR => media::SbcAllocation::AllocSnr,
                    SbcAllocation::LOUDNESS => media::SbcAllocation::AllocLoudness,
                    _ => return Err(avdtp::Error::OutOfRange),
                };

                let block_count = match codec_info.block_count() {
                    SbcBlockCount::FOUR => media::SbcBlockCount::BlockCount4,
                    SbcBlockCount::EIGHT => media::SbcBlockCount::BlockCount8,
                    SbcBlockCount::TWELVE => media::SbcBlockCount::BlockCount12,
                    SbcBlockCount::SIXTEEN => media::SbcBlockCount::BlockCount16,
                    _ => return Err(avdtp::Error::OutOfRange),
                };

                let channel_mode = match codec_info.channel_mode() {
                    SbcChannelMode::MONO => media::SbcChannelMode::Mono,
                    SbcChannelMode::DUAL_CHANNEL => media::SbcChannelMode::Dual,
                    SbcChannelMode::STEREO => media::SbcChannelMode::Stereo,
                    SbcChannelMode::JOINT_STEREO => media::SbcChannelMode::JointStereo,
                    _ => return Err(avdtp::Error::OutOfRange),
                };

                media::EncoderSettings::Sbc(media::SbcEncoderSettings {
                    sub_bands,
                    allocation,
                    block_count,
                    channel_mode,
                    bit_pool: codec_info.max_bitpool() as u64,
                })
            }
            MediaCodecType::AUDIO_AAC => {
                let codec_info = AacCodecInfo::try_from(self.codec_extra())?;
                let bit_rate = if codec_info.variable_bit_rate() {
                    media::AacBitRate::Variable(media::AacVariableBitRate::V3)
                } else {
                    media::AacBitRate::Constant(media::AacConstantBitRate {
                        bit_rate: codec_info.bitrate(),
                    })
                };

                let channel_mode = match codec_info.channels() {
                    AacChannels::ONE => media::AacChannelMode::Mono,
                    AacChannels::TWO => media::AacChannelMode::Stereo,
                    x => return Err(format_err!("unsuported number of channels: {:?}", x).into()),
                };

                media::EncoderSettings::Aac(media::AacEncoderSettings {
                    transport: media::AacTransport::Latm(media::AacTransportLatm {
                        mux_config_present: true,
                    }),
                    channel_mode,
                    bit_rate,
                    aot: media::AacAudioObjectType::Mpeg2AacLc,
                })
            }
            _ => return Err(format_err!("Unsupported codec {:?}", self.codec_type).into()),
        };
        Ok(encoder_settings)
    }

    /// Construct a ServiceCapability that represents this codec config.
    pub fn capability(&self) -> ServiceCapability {
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => SbcCodecInfo::try_from(self.codec_extra()).unwrap().into(),
            MediaCodecType::AUDIO_AAC => AacCodecInfo::try_from(self.codec_extra()).unwrap().into(),
            _ => unreachable!(),
        }
    }

    /// The number of channels that is selected in the configuration.  Returns OutOfRange if
    /// the configuration supports a range of channel counts.
    pub fn channel_count(&self) -> avdtp::Result<usize> {
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => {
                SbcCodecInfo::try_from(self.codec_extra())?.channel_count()
            }
            MediaCodecType::AUDIO_AAC => {
                AacCodecInfo::try_from(self.codec_extra())?.channel_count()
            }
            _ => unreachable!(),
        }
    }

    /// The number of frames that should be included when building a packet to send to a peer.
    pub fn frames_per_packet(&self) -> usize {
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => ENCODED_FRAMES_PER_SBC_PACKET as usize,
            MediaCodecType::AUDIO_AAC => ENCODED_FRAMES_PER_AAC_PACKET as usize,
            _ => unreachable!(),
        }
    }

    pub fn pcm_frames_per_encoded_frame(&self) -> usize {
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => PCM_FRAMES_PER_SBC_FRAME as usize,
            MediaCodecType::AUDIO_AAC => PCM_FRAMES_PER_AAC_FRAME as usize,
            _ => unreachable!(),
        }
    }

    pub fn rtp_frame_header(&self) -> &[u8] {
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => &[ENCODED_FRAMES_PER_SBC_PACKET as u8],
            MediaCodecType::AUDIO_AAC => &[],
            _ => unreachable!(),
        }
    }

    /// Return the sampling freqeuncy selected by this configuration, or return OutOfRange if
    /// more than one is selected.
    pub fn sampling_frequency(&self) -> avdtp::Result<u32> {
        let freq = match self.codec_type {
            MediaCodecType::AUDIO_SBC => {
                SbcCodecInfo::try_from(self.codec_extra())?.sampling_frequency()?
            }
            MediaCodecType::AUDIO_AAC => {
                AacCodecInfo::try_from(self.codec_extra())?.sampling_frequency()?
            }
            _ => unreachable!(),
        };
        Ok(freq)
    }

    pub fn stream_encoding(&self) -> &'static str {
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => media::AUDIO_ENCODING_SBC,
            MediaCodecType::AUDIO_AAC => media::AUDIO_ENCODING_AAC,
            _ => unreachable!(),
        }
    }

    pub fn mime_type(&self) -> &'static str {
        match self.codec_type {
            MediaCodecType::AUDIO_SBC => "audio/sbc",
            MediaCodecType::AUDIO_AAC => "audio/aac",
            _ => unreachable!(),
        }
    }
}

impl From<&MediaCodecConfig> for ServiceCapability {
    fn from(config: &MediaCodecConfig) -> Self {
        config.capability()
    }
}

impl TryFrom<&ServiceCapability> for MediaCodecConfig {
    type Error = avdtp::Error;

    fn try_from(value: &ServiceCapability) -> Result<Self, Self::Error> {
        match value {
            ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type,
                codec_extra,
            } => {
                match codec_type {
                    &MediaCodecType::AUDIO_SBC => {
                        let _ = SbcCodecInfo::try_from(codec_extra.as_slice())?;
                    }
                    &MediaCodecType::AUDIO_AAC => {
                        let _ = AacCodecInfo::try_from(codec_extra.as_slice())?;
                    }
                    _ => return Err(avdtp::Error::OutOfRange),
                };
                Ok(MediaCodecConfig {
                    codec_type: codec_type.clone(),
                    codec_extra: codec_extra.clone(),
                })
            }
            _ => Err(avdtp::Error::OutOfRange),
        }
    }
}

/// Selects a codec and a set of capabilities for that codec, based on a preferential list of
/// partially-defined codec capabilities, using each codec's support to find best compatable
/// matching capability.
/// Currently supports SBC and AAC codec capabilities.
#[derive(Debug, Clone)]
pub struct CodecNegotiation {
    preferred_codecs: Vec<MediaCodecConfig>,
}

impl CodecNegotiation {
    /// Make a new codec negotation set using `codecs` as an ordered list "ideal" capabilities.
    /// Capabilities earlier in the list are preferred if compatable when selecting.
    /// Returns an error if any of the capabilities provided can't be negotiated, or aren't codecs.
    pub fn build(codecs: Vec<ServiceCapability>) -> avdtp::Result<Self> {
        let expected = codecs.len();
        let preferred_codecs: Vec<_> =
            codecs.iter().filter_map(|c| MediaCodecConfig::try_from(c).ok()).collect();
        if preferred_codecs.len() != expected {
            return Err(format_err!("Unsupported capability used in CodecNegotiation").into());
        }
        Ok(Self { preferred_codecs })
    }

    /// Given a set of endpoints, return the endpoint id, and a ServiceCapability representing the
    /// selected compatible codec parameters for that endpoint, based on our preferences.
    /// Returns None if none of the endpoints can be supported by the preferred codecs.
    pub fn select(
        &self,
        endpoints: &[avdtp::StreamEndpoint],
    ) -> Option<(ServiceCapability, StreamEndpointId)> {
        let codecs_with_ids: Vec<_> = endpoints
            .iter()
            .filter_map(|e| Self::get_codec_cap(e).map(|cap| (cap, e.local_id())))
            .collect();
        for preferred in &self.preferred_codecs {
            for (codec, id) in &codecs_with_ids {
                if let Ok(config) = MediaCodecConfig::try_from(*codec) {
                    if let Some(negotiated) = MediaCodecConfig::negotiate(&config, &preferred) {
                        trace!("Codec negotiation selected: {:?}", negotiated);
                        return Some((negotiated.capability(), (*id).clone()));
                    }
                }
            }
        }
        None
    }

    fn get_codec_cap<'a>(stream: &'a avdtp::StreamEndpoint) -> Option<&'a ServiceCapability> {
        stream
            .capabilities()
            .iter()
            .find(|cap| cap.category() == avdtp::ServiceCategory::MediaCodec)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use bt_avdtp::{MediaType, StreamEndpoint};
    use std::convert::TryInto;

    use crate::media_types::*;

    const TEST_SAMPLE_FREQ: u32 = 44100;

    fn test_codec_cap(codec_type: MediaCodecType) -> ServiceCapability {
        let codec_extra = match codec_type {
            MediaCodecType::AUDIO_SBC => vec![41, 245, 2, 53],
            MediaCodecType::AUDIO_AAC => vec![128, 1, 4, 4, 226, 0],
            _ => vec![],
        };
        ServiceCapability::MediaCodec { media_type: MediaType::Audio, codec_type, codec_extra }
    }

    fn change_extra(mut cap: &mut ServiceCapability, extra: Vec<u8>) {
        if let ServiceCapability::MediaCodec { codec_extra, .. } = &mut cap {
            *codec_extra = extra;
            return;
        }
        panic!("Can't change extra for a non-MediaCodec cap: {:?}", cap);
    }

    fn build_test_config(codec_type: MediaCodecType) -> MediaCodecConfig {
        MediaCodecConfig::try_from(&test_codec_cap(codec_type)).expect("builds okay")
    }

    #[test]
    fn test_basic() {
        let res =
            MediaCodecConfig::try_from(&test_codec_cap(MediaCodecType::AUDIO_SBC)).expect("builds");
        assert_eq!(Some(44100), res.sampling_frequency().ok());
        assert_eq!(&[5], res.rtp_frame_header());
        assert_eq!(media::AUDIO_ENCODING_SBC, res.stream_encoding());

        let res =
            MediaCodecConfig::try_from(&test_codec_cap(MediaCodecType::AUDIO_AAC)).expect("builds");
        assert_eq!(Some(44100), res.sampling_frequency().ok());
        assert_eq!(0, res.rtp_frame_header().len());
        assert_eq!(media::AUDIO_ENCODING_AAC, res.stream_encoding());
    }

    #[test]
    fn test_from_capability() {
        // Wrong length extra.
        let mut cap = test_codec_cap(MediaCodecType::AUDIO_SBC);
        assert!(MediaCodecConfig::try_from(&cap).is_ok());
        change_extra(&mut cap, vec![]);
        assert!(MediaCodecConfig::try_from(&cap).is_err());
        change_extra(&mut cap, vec![0; 6]);
        assert!(MediaCodecConfig::try_from(&cap).is_err());

        let mut cap = test_codec_cap(MediaCodecType::AUDIO_AAC);
        assert!(MediaCodecConfig::try_from(&cap).is_ok());
        change_extra(&mut cap, vec![]);
        assert!(MediaCodecConfig::try_from(&cap).is_err());
        change_extra(&mut cap, vec![0; 4]);
        assert!(MediaCodecConfig::try_from(&cap).is_err());

        // Unknown codec is error.
        let cap = avdtp::ServiceCapability::MediaCodec {
            media_type: MediaType::Audio,
            codec_type: MediaCodecType::AUDIO_NON_A2DP,
            codec_extra: vec![],
        };
        assert!(MediaCodecConfig::try_from(&cap).is_err());
    }

    #[test]
    fn test_sampling_frequency() {
        let freq = build_test_config(MediaCodecType::AUDIO_SBC)
            .sampling_frequency()
            .expect("SBC frequency should be known and singular");
        assert_eq!(TEST_SAMPLE_FREQ, freq);
        let freq = build_test_config(MediaCodecType::AUDIO_AAC)
            .sampling_frequency()
            .expect("SBC frequency should be known and singular");
        assert_eq!(TEST_SAMPLE_FREQ, freq);

        let multi_freq_info = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK, // SNK requires two frequencies, which is not singular.
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            2,
            250,
        )
        .expect("codecinfo");
        let multi_freq_config =
            MediaCodecConfig::build(MediaCodecType::AUDIO_SBC, &multi_freq_info.to_bytes())
                .expect("MediaCodecConfig should build");

        assert!(multi_freq_config.sampling_frequency().is_err());
    }

    #[test]
    fn test_supports() {
        // Codecs must match.
        let sbc = build_test_config(MediaCodecType::AUDIO_SBC);
        let aac = build_test_config(MediaCodecType::AUDIO_AAC);

        assert!(!sbc.supports(&aac));
        assert!(!aac.supports(&sbc));
        assert!(sbc.supports(&sbc));
    }

    /// Build an endpoint with the specified type, and local id.
    fn test_codec_endpoint(id: u8, codec_cap: ServiceCapability) -> StreamEndpoint {
        avdtp::StreamEndpoint::new(
            id,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![ServiceCapability::MediaTransport, codec_cap],
        )
        .expect("media endpoint")
    }

    #[test]
    fn test_codec_negotiation() {
        // It should choose nothing if there aren't any local priorities (there aren't any streams)
        // of if there is no endpoint to choose from.

        let empty_negotiation = CodecNegotiation::build(vec![]).expect("builds okay");

        let sbc_seid = 1u8;
        let aac_seid = 2u8;

        let remote_endpoints = vec![
            test_codec_endpoint(aac_seid, test_codec_cap(MediaCodecType::AUDIO_AAC)),
            test_codec_endpoint(sbc_seid, test_codec_cap(MediaCodecType::AUDIO_SBC)),
        ];

        assert!(empty_negotiation.select(&remote_endpoints).is_none());

        let priority_order = vec![
            test_codec_cap(MediaCodecType::AUDIO_AAC),
            test_codec_cap(MediaCodecType::AUDIO_SBC),
        ];
        let negotiation = CodecNegotiation::build(priority_order).expect("builds");

        assert!(negotiation.select(&Vec::new()).is_none());

        // Should choose the highest-priority capability that matches, regardless of order.

        let aac_config = MediaCodecConfig::try_from(&test_codec_cap(MediaCodecType::AUDIO_AAC))
            .expect("codec_config");
        let aac_negotiated =
            MediaCodecConfig::negotiate(&aac_config, &aac_config).expect("negotiated config");

        let sbc_config = MediaCodecConfig::try_from(&test_codec_cap(MediaCodecType::AUDIO_SBC))
            .expect("codec_config");
        let sbc_negotiated =
            MediaCodecConfig::negotiate(&sbc_config, &sbc_config).expect("negotiated config");

        assert_eq!(
            negotiation.select(&remote_endpoints),
            Some((aac_negotiated.capability(), aac_seid.try_into().unwrap()))
        );

        let mut reversed_endpoints: Vec<_> = remote_endpoints.iter().map(|e| e.as_new()).collect();
        reversed_endpoints.reverse();

        assert_eq!(
            negotiation.select(&reversed_endpoints),
            Some((aac_negotiated.capability(), aac_seid.try_into().unwrap()))
        );

        // Should skip an endpoint if it can't match up to one it supports,
        // even if it's higher priority.

        // An AAC endpoint incompatable with the test codec caps.
        let incompatible_aac_endpoint = test_codec_endpoint(
            aac_seid,
            AacCodecInfo::new(
                AacObjectType::MPEG4_AAC_SCALABLE,
                AacSamplingFrequency::FREQ96000HZ,
                AacChannels::ONE,
                true,
                0,
            )
            .expect("aac codec builds")
            .into(),
        );
        let incompatible_aac_endpoints =
            vec![incompatible_aac_endpoint, remote_endpoints[1].as_new()];

        assert_eq!(
            negotiation.select(&incompatible_aac_endpoints),
            Some((sbc_negotiated.capability(), sbc_seid.try_into().unwrap()))
        );
    }

    #[test]
    fn test_negotiate() {
        let sbc_mandatory_snk = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            23,
            SbcCodecInfo::BITPOOL_MAX,
        )
        .unwrap();
        let sbc_snk_config = MediaCodecConfig::try_from(&sbc_mandatory_snk.into()).unwrap();

        // When remote end has a different set of things, we choose the single config that is within
        // both.
        let sbc_codec_48 = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            SbcCodecInfo::BITPOOL_MIN,
            45,
        )
        .unwrap();
        let sbc_48_config = MediaCodecConfig::try_from(&sbc_codec_48.into()).unwrap();

        let negotiated = MediaCodecConfig::negotiate(&sbc_snk_config, &sbc_48_config)
            .expect("negotiation to succeed");

        assert_eq!(
            negotiated.capability(),
            SbcCodecInfo::new(
                SbcSamplingFrequency::FREQ48000HZ,
                SbcChannelMode::JOINT_STEREO,
                SbcBlockCount::SIXTEEN,
                SbcSubBands::EIGHT,
                SbcAllocation::LOUDNESS,
                23,
                45,
            )
            .unwrap()
            .into()
        );

        assert!(sbc_snk_config.supports(&negotiated));
        assert!(sbc_48_config.supports(&negotiated));

        // If the configs don't overlap, returns None.
        let sbc_codec_44 = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            SbcCodecInfo::BITPOOL_MIN,
            45,
        )
        .unwrap();
        let sbc_44_config = MediaCodecConfig::try_from(&sbc_codec_44.into()).unwrap();

        assert!(MediaCodecConfig::negotiate(&sbc_48_config, &sbc_44_config).is_none());
    }
}
