// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_audio::{PcmFormat, SampleFormat},
    fuchsia_zircon as zx,
    std::result,
    thiserror::Error,
};

/// Result type alias for brevity.
pub type Result<T> = result::Result<T, Error>;

/// The Error type of the fuchsia-media library
#[derive(Error, Debug, PartialEq)]
pub enum Error {
    /// The value that was received was out of range
    #[error("Value was out of range")]
    OutOfRange,

    /// The header was invalid when parsing a message.
    #[error("Invalid Header for a message")]
    InvalidHeader,

    /// Can't encode into a buffer
    #[error("Encoding error")]
    Encoding,

    /// Encountered an IO error reading
    #[error("Encountered an IO error reading from the channel: {}", _0)]
    PeerRead(zx::Status),

    /// Encountered an IO error writing
    #[error("Encountered an IO error writing to the channel: {}", _0)]
    PeerWrite(zx::Status),

    /// Other IO Error
    #[error("Encountered an IO error: {}", _0)]
    IOError(zx::Status),

    /// Action tried in an invalid state
    #[error("Tried to do an action in an invalid state")]
    InvalidState,

    /// Responder doesn't have a channel
    #[error("No channel found for reply")]
    NoChannel,

    /// When a message hasn't been implemented yet, the parser will return this.
    #[error("Message has not been implemented yet")]
    UnimplementedMessage,

    /// An argument is invalid.
    #[error("Invalid argument")]
    InvalidArgs,

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

#[derive(Debug, PartialEq, Clone)]
pub enum AudioSampleFormat {
    Eight { unsigned: bool },
    Sixteen { unsigned: bool, invert_endian: bool },
    TwentyFourPacked { unsigned: bool, invert_endian: bool },
    TwentyIn32 { unsigned: bool, invert_endian: bool },
    TwentyFourIn32 { unsigned: bool, invert_endian: bool },
    ThirtyTwo { unsigned: bool, invert_endian: bool },
    Float { invert_endian: bool },
}

/// Constructs a AudioSampleFormat from a fidl_fuchsia_hardware_audio::SampleFormat.
impl From<PcmFormat> for AudioSampleFormat {
    fn from(v: PcmFormat) -> Self {
        if let SampleFormat::PcmFloat = v.sample_format {
            if v.bytes_per_sample == 32 && v.valid_bits_per_sample == 32 {
                AudioSampleFormat::Float { invert_endian: false }
            } else {
                panic!("audio sample format not supported");
            }
        } else {
            let is_unsigned = v.sample_format == SampleFormat::PcmUnsigned;
            match v.bytes_per_sample {
                1u8 => {
                    assert_eq!(v.valid_bits_per_sample, 8u8);
                    AudioSampleFormat::Eight { unsigned: is_unsigned }
                }
                2u8 => {
                    assert_eq!(v.valid_bits_per_sample, 16u8);
                    AudioSampleFormat::Sixteen { unsigned: is_unsigned, invert_endian: false }
                }
                3u8 => {
                    assert_eq!(v.valid_bits_per_sample, 24u8);
                    AudioSampleFormat::TwentyFourPacked {
                        unsigned: is_unsigned,
                        invert_endian: false,
                    }
                }
                4u8 => match v.valid_bits_per_sample {
                    20u8 => AudioSampleFormat::TwentyIn32 {
                        unsigned: is_unsigned,
                        invert_endian: false,
                    },
                    24u8 => AudioSampleFormat::TwentyFourIn32 {
                        unsigned: is_unsigned,
                        invert_endian: false,
                    },
                    32u8 => {
                        AudioSampleFormat::ThirtyTwo { unsigned: is_unsigned, invert_endian: false }
                    }
                    _ => panic!(
                        "audio valie bits per sample {:?} not supported",
                        v.valid_bits_per_sample
                    ),
                },
                _ => panic!("audio bytes per samples {:?} not supported", v.bytes_per_sample),
            }
        }
    }
}

impl AudioSampleFormat {
    /// Compute the size of an audio frame based on the sample format.
    /// Returns Err(OutOfRange) in the case where it cannot be computed
    /// (bad channel count, bad sample format)
    pub fn compute_frame_size(&self, channels: usize) -> Result<usize> {
        let bytes_per_channel = match self {
            AudioSampleFormat::Eight { .. } => 1,
            AudioSampleFormat::Sixteen { .. } => 2,
            AudioSampleFormat::TwentyFourPacked { .. } => 3,
            AudioSampleFormat::TwentyIn32 { .. }
            | AudioSampleFormat::TwentyFourIn32 { .. }
            | AudioSampleFormat::ThirtyTwo { .. }
            | AudioSampleFormat::Float { .. } => 4,
        };
        Ok(channels * bytes_per_channel)
    }
}
