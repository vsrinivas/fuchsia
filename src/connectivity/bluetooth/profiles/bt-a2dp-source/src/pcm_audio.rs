// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{ByteOrder, NativeEndian};
use fidl_fuchsia_media::PcmFormat;

const PCM_SAMPLE_SIZE: usize = 2;

#[derive(Clone, Debug)]
pub struct PcmAudio {
    pub pcm_format: PcmFormat,
    pub frequency: f32,
    pub amplitude: f32,
    pub buffer: Vec<u8>,
}

impl PcmAudio {
    pub fn create_saw_wave(
        frequency: f32,
        amplitude: f32,
        pcm_format: PcmFormat,
        frame_count: usize,
    ) -> Self {
        let pcm_frame_size = PCM_SAMPLE_SIZE * pcm_format.channel_map.len();
        let samples_per_frame = pcm_format.channel_map.len();
        let sample_count = frame_count * samples_per_frame;

        let mut buffer = vec![0; frame_count * pcm_frame_size];

        let amplitude = amplitude.min(1.0).max(0.0);

        for i in 0..sample_count {
            let frame = (i / samples_per_frame) as f32;
            let value =
                ((frame * frequency / (pcm_format.frames_per_second as f32)) % 1.0) * amplitude;
            let sample = (value * i16::max_value() as f32) as i16;

            let mut sample_bytes = [0; std::mem::size_of::<i16>()];
            NativeEndian::write_i16(&mut sample_bytes, sample);

            let offset = i * PCM_SAMPLE_SIZE;
            buffer[offset] = sample_bytes[0];
            buffer[offset + 1] = sample_bytes[1];
        }

        Self { pcm_format, frequency, amplitude, buffer }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode};
    use hex;
    use mundane::hash::{Digest, Hasher, Sha256};

    #[test]
    fn saw_wave_matches_hash() {
        /// This was obtained by writing the buffer out to file and inspecting the wave on each channel.
        const GOLDEN_DIGEST: &str =
            "2bf4f233a179f0cb572b72570a28c07a334e406baa7fb4fc65f641b82d0ae64a";

        let pcm_audio = PcmAudio::create_saw_wave(
            20.0,
            0.2,
            PcmFormat {
                pcm_mode: AudioPcmMode::Linear,
                bits_per_sample: 16,
                frames_per_second: 44100,
                channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
            },
            /*frame_count=*/ 50000,
        );

        let actual_digest = hex::encode(Sha256::hash(&pcm_audio.buffer).bytes());
        assert_eq!(&actual_digest, GOLDEN_DIGEST);
    }
}
