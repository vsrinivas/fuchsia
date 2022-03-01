// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire::*, fuchsia_zircon as zx, num_traits::cast::cast, once_cell::sync::Lazy,
    std::collections::HashMap,
};

/// Stringify a request code VIRTIO_SND_R_*.
pub fn request_code_to_string(code: u32) -> &'static str {
    match code {
        VIRTIO_SND_R_JACK_INFO => "VIRTIO_SND_R_JACK_INFO",
        VIRTIO_SND_R_JACK_REMAP => "VIRTIO_SND_R_JACK_REMAP",
        VIRTIO_SND_R_PCM_INFO => "VIRTIO_SND_R_PCM_INFO",
        VIRTIO_SND_R_PCM_SET_PARAMS => "VIRTIO_SND_R_PCM_SET_PARAMS",
        VIRTIO_SND_R_PCM_PREPARE => "VIRTIO_SND_R_PCM_PREPARE",
        VIRTIO_SND_R_PCM_RELEASE => "VIRTIO_SND_R_PCM_RELEASE",
        VIRTIO_SND_R_PCM_START => "VIRTIO_SND_R_PCM_START",
        VIRTIO_SND_R_PCM_STOP => "VIRTIO_SND_R_PCM_STOP",
        VIRTIO_SND_R_CHMAP_INFO => "VIRTIO_SND_R_CHMAP_INFO",
        _ => "unknown_code",
    }
}

// Store this mapping as a data structure so we can iterate over the keys.
// See usage below.
static WIRE_FORMAT_TO_FIDL: Lazy<HashMap<u8, fidl_fuchsia_media::AudioSampleFormat>> =
    Lazy::new(|| {
        HashMap::from([
            (VIRTIO_SND_PCM_FMT_U8, fidl_fuchsia_media::AudioSampleFormat::Unsigned8),
            (VIRTIO_SND_PCM_FMT_S16, fidl_fuchsia_media::AudioSampleFormat::Signed16),
            (VIRTIO_SND_PCM_FMT_S24, fidl_fuchsia_media::AudioSampleFormat::Signed24In32),
            (VIRTIO_SND_PCM_FMT_FLOAT, fidl_fuchsia_media::AudioSampleFormat::Float),
        ])
    });

/// A bitmask of all supported formats: bit (1<<x) is set if we support format x.
pub static WIRE_FORMATS_SUPPORTED_BITMASK: Lazy<u64> =
    Lazy::new(|| WIRE_FORMAT_TO_FIDL.keys().fold(0u64, |acc, x| acc | (1u64 << x)));

/// Translates the given wire format to a FIDL format.
/// Returns None if the format is not supported.
pub fn wire_format_to_fidl(fmt: u8) -> Option<fidl_fuchsia_media::AudioSampleFormat> {
    WIRE_FORMAT_TO_FIDL.get(&fmt).cloned()
}

// Store this mapping as a data structure so we can iterate over the keys.
// See usage below.
static WIRE_RATE_TO_FIDL: Lazy<HashMap<u8, u32>> = Lazy::new(|| {
    HashMap::from([
        (VIRTIO_SND_PCM_RATE_8000, 8000),
        (VIRTIO_SND_PCM_RATE_11025, 11025),
        (VIRTIO_SND_PCM_RATE_16000, 16000),
        (VIRTIO_SND_PCM_RATE_22050, 22050),
        (VIRTIO_SND_PCM_RATE_32000, 32000),
        (VIRTIO_SND_PCM_RATE_44100, 44100),
        (VIRTIO_SND_PCM_RATE_48000, 48000),
        (VIRTIO_SND_PCM_RATE_64000, 64000),
        (VIRTIO_SND_PCM_RATE_88200, 88200),
        (VIRTIO_SND_PCM_RATE_96000, 96000),
        (VIRTIO_SND_PCM_RATE_176400, 176400),
        (VIRTIO_SND_PCM_RATE_192000, 192000),
    ])
});

/// A bitmask of all supported rates: bit (1<<x) is set if we support format x.
pub static WIRE_RATES_SUPPORTED_BITMASK: Lazy<u64> =
    Lazy::new(|| WIRE_RATE_TO_FIDL.keys().fold(0u64, |acc, x| acc | (1u64 << x)));

/// Translates the given wire rate to an integer frames-per-second as used in FIDL.
/// Returns None if the rate is not supported.
pub fn wire_rate_to_fidl(rate: u8) -> Option<u32> {
    WIRE_RATE_TO_FIDL.get(&rate).cloned()
}

/// Translates the given wire params struct to a FIDL params struct.
pub fn wire_parameters_to_fidl_stream_type(
    req: &PcmSetParamsRequest,
) -> Option<fidl_fuchsia_media::AudioStreamType> {
    Some(fidl_fuchsia_media::AudioStreamType {
        sample_format: wire_format_to_fidl(req.format)?,
        frames_per_second: wire_rate_to_fidl(req.rate)?,
        channels: req.channels as u32,
    })
}

/// Returns the number of bytes-per-frame for a stream with the given format.
pub fn bytes_per_frame(stream_type: fidl_fuchsia_media::AudioStreamType) -> usize {
    match stream_type.sample_format {
        fidl_fuchsia_media::AudioSampleFormat::Unsigned8 => 1 * (stream_type.channels as usize),
        fidl_fuchsia_media::AudioSampleFormat::Signed16 => 2 * (stream_type.channels as usize),
        fidl_fuchsia_media::AudioSampleFormat::Signed24In32 => 4 * (stream_type.channels as usize),
        fidl_fuchsia_media::AudioSampleFormat::Float => 4 * (stream_type.channels as usize),
    }
}

/// Computes the number of bytes needed to represent the given duration, for the given audio format.
/// Returns None if the duration is negative.
pub fn bytes_for_duration(
    duration: zx::Duration,
    stream_type: fidl_fuchsia_media::AudioStreamType,
) -> Option<usize> {
    let nsec = cast::<i64, usize>(duration.into_nanos())?;
    let fps = cast::<u32, usize>(stream_type.frames_per_second)?;
    Some(nsec * fps * bytes_per_frame(stream_type) / 1_000_000_000)
}

#[cfg(test)]
mod tests {
    use {super::*, pretty_assertions::assert_eq};

    #[test]
    fn test_wire_formats_supported_bitmask() {
        // All formats we support.
        assert!(*WIRE_FORMATS_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_FMT_U8) != 0);
        assert!(*WIRE_FORMATS_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_FMT_S16) != 0);
        assert!(*WIRE_FORMATS_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_FMT_S24) != 0);
        assert!(*WIRE_FORMATS_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_FMT_FLOAT) != 0);

        // Sample of formats we don't support.
        assert!(*WIRE_FORMATS_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_FMT_MU_LAW) == 0);
        assert!(*WIRE_FORMATS_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_FMT_S20) == 0);
    }

    #[test]
    fn test_wire_format_to_fidl() {
        // All formats we support.
        assert_eq!(
            wire_format_to_fidl(VIRTIO_SND_PCM_FMT_U8),
            Some(fidl_fuchsia_media::AudioSampleFormat::Unsigned8)
        );
        assert_eq!(
            wire_format_to_fidl(VIRTIO_SND_PCM_FMT_S16),
            Some(fidl_fuchsia_media::AudioSampleFormat::Signed16)
        );
        assert_eq!(
            wire_format_to_fidl(VIRTIO_SND_PCM_FMT_S24),
            Some(fidl_fuchsia_media::AudioSampleFormat::Signed24In32)
        );
        assert_eq!(
            wire_format_to_fidl(VIRTIO_SND_PCM_FMT_FLOAT),
            Some(fidl_fuchsia_media::AudioSampleFormat::Float)
        );

        // Sample of formats we don't support.
        assert_eq!(wire_format_to_fidl(VIRTIO_SND_PCM_FMT_MU_LAW), None);
        assert_eq!(wire_format_to_fidl(VIRTIO_SND_PCM_FMT_S20), None);
    }

    #[test]
    fn test_wire_rates_supported_bitmask() {
        // Sample of rates we support.
        assert!(*WIRE_RATES_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_RATE_8000) != 0);
        assert!(*WIRE_RATES_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_RATE_48000) != 0);
        assert!(*WIRE_RATES_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_RATE_96000) != 0);

        // Sample of rates we don't support.
        assert!(*WIRE_RATES_SUPPORTED_BITMASK & (1u64 << VIRTIO_SND_PCM_RATE_384000) == 0);
    }

    #[test]
    fn test_wire_rate_to_fidl() {
        // Sample of rates we support.
        assert_eq!(wire_rate_to_fidl(VIRTIO_SND_PCM_RATE_8000), Some(8000));
        assert_eq!(wire_rate_to_fidl(VIRTIO_SND_PCM_RATE_48000), Some(48000));
        assert_eq!(wire_rate_to_fidl(VIRTIO_SND_PCM_RATE_96000), Some(96000));

        // Sample of rates we don't support.
        assert_eq!(wire_rate_to_fidl(VIRTIO_SND_PCM_RATE_384000), None);
    }

    #[test]
    fn test_bytes_per_frame() {
        use fidl_fuchsia_media::AudioSampleFormat as ASF;
        use fidl_fuchsia_media::AudioStreamType;

        assert_eq!(
            2,
            bytes_per_frame(AudioStreamType {
                sample_format: ASF::Unsigned8,
                channels: 2,
                frames_per_second: 48000,
            })
        );
        assert_eq!(
            4,
            bytes_per_frame(AudioStreamType {
                sample_format: ASF::Signed16,
                channels: 2,
                frames_per_second: 48000,
            })
        );
        assert_eq!(
            8,
            bytes_per_frame(AudioStreamType {
                sample_format: ASF::Signed24In32,
                channels: 2,
                frames_per_second: 48000,
            })
        );
        assert_eq!(
            8,
            bytes_per_frame(AudioStreamType {
                sample_format: ASF::Float,
                channels: 2,
                frames_per_second: 48000,
            })
        );
    }

    #[test]
    fn test_bytes_for_duration() {
        use fidl_fuchsia_media::AudioSampleFormat as ASF;
        use fidl_fuchsia_media::AudioStreamType;

        assert_eq!(
            None,
            bytes_for_duration(
                zx::Duration::from_millis(-10),
                AudioStreamType {
                    sample_format: ASF::Unsigned8,
                    channels: 2,
                    frames_per_second: 48000,
                }
            )
        );

        assert_eq!(
            Some(480 * 2),
            bytes_for_duration(
                zx::Duration::from_millis(10),
                AudioStreamType {
                    sample_format: ASF::Unsigned8,
                    channels: 2,
                    frames_per_second: 48000,
                }
            )
        );
        assert_eq!(
            Some(480 * 4),
            bytes_for_duration(
                zx::Duration::from_millis(10),
                AudioStreamType {
                    sample_format: ASF::Signed16,
                    channels: 2,
                    frames_per_second: 48000,
                }
            )
        );
        assert_eq!(
            Some(480 * 8),
            bytes_for_duration(
                zx::Duration::from_millis(10),
                AudioStreamType {
                    sample_format: ASF::Signed24In32,
                    channels: 2,
                    frames_per_second: 48000,
                }
            )
        );
        assert_eq!(
            Some(480 * 8),
            bytes_for_duration(
                zx::Duration::from_millis(10),
                AudioStreamType {
                    sample_format: ASF::Float,
                    channels: 2,
                    frames_per_second: 48000,
                }
            )
        );
    }
}
