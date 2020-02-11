use bt_a2dp::media_types::*;
use bt_avdtp as avdtp;
use fidl_fuchsia_media::{AUDIO_ENCODING_AAC, AUDIO_ENCODING_SBC};
use fuchsia_syslog::fx_log_warn;
use std::convert::{TryFrom, TryInto};

#[derive(Clone, Debug)]
pub enum CodecExtra {
    Sbc([u8; SBC_CODEC_EXTRA_LEN]),
    Aac([u8; AAC_CODEC_EXTRA_LEN]),
    Unknown,
}

pub enum CodecExtraError {
    UnknownCodec,
    InvalidLength,
}

impl CodecExtra {
    /// Extract sampling freqency from SBC codec extra data field
    /// (A2DP Sec. 4.3.2)
    fn parse_sbc_sampling_frequency(codec_extra: &[u8]) -> Option<u32> {
        let codec_info = SbcCodecInfo::try_from(codec_extra).ok()?;
        let sample_freq = SbcSamplingFrequency::from_bits_truncate(codec_info.sampling_frequency());

        match sample_freq {
            SbcSamplingFrequency::FREQ48000HZ => Some(48000),
            SbcSamplingFrequency::FREQ44100HZ => Some(44100),
            _ => {
                fx_log_warn!("Invalid sample_freq set in configuration {:?}", sample_freq);
                None
            }
        }
    }

    /// Extract sampling frequency from AAC codec extra data field
    /// (A2DP Sec. 4.5.2)
    fn parse_aac_sampling_frequency(codec_extra: &[u8]) -> Option<u32> {
        let codec_info = AACMediaCodecInfo::try_from(codec_extra).ok()?;
        let sample_freq = AACSamplingFrequency::from_bits_truncate(codec_info.sampling_frequency());

        match sample_freq {
            AACSamplingFrequency::FREQ48000HZ => Some(48000),
            AACSamplingFrequency::FREQ44100HZ => Some(44100),
            _ => {
                fx_log_warn!("Invalid sample_freq set in configuration {:?}", sample_freq);
                None
            }
        }
    }

    /// Parse the sampling frequency for this codec extra or return None
    /// if none is configured.
    pub fn sample_freq(&self) -> Option<u32> {
        match self {
            CodecExtra::Sbc(codec_extra) => Self::parse_sbc_sampling_frequency(codec_extra),
            CodecExtra::Aac(codec_extra) => Self::parse_aac_sampling_frequency(codec_extra),
            _ => None,
        }
    }

    pub fn stream_type(&self) -> &str {
        match self {
            Self::Sbc(_) => AUDIO_ENCODING_SBC,
            Self::Aac(_) => AUDIO_ENCODING_AAC,
            Self::Unknown => "Unknown",
        }
    }
}

impl TryFrom<&avdtp::ServiceCapability> for CodecExtra {
    type Error = CodecExtraError;

    fn try_from(value: &avdtp::ServiceCapability) -> Result<Self, Self::Error> {
        match value {
            avdtp::ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                codec_extra,
            } => {
                if codec_extra.len() != SBC_CODEC_EXTRA_LEN {
                    return Err(CodecExtraError::InvalidLength);
                }
                Ok(CodecExtra::Sbc(codec_extra[0..SBC_CODEC_EXTRA_LEN].try_into().unwrap()))
            }
            avdtp::ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                codec_extra,
            } => {
                if codec_extra.len() != AAC_CODEC_EXTRA_LEN {
                    return Err(CodecExtraError::InvalidLength);
                }
                Ok(CodecExtra::Aac(codec_extra[0..AAC_CODEC_EXTRA_LEN].try_into().unwrap()))
            }
            _ => Err(CodecExtraError::UnknownCodec),
        }
    }
}
