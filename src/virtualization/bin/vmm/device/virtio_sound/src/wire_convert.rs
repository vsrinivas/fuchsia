// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Keep defs which aren't used yet.
#![allow(dead_code)]

use {crate::wire::*, once_cell::sync::Lazy, std::collections::HashMap};

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
