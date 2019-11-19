// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*!

Provides an asynchronous wrapper around the StreamProcesor API to encode audio packets.

The main object to use is the Encoder, which is created with the input audio parameters and the
encoder settings to provide to the StreamProcessor for the encoding parameters. The `input_domain`
can be found as `fidl_fuchsia_media::DomainFormat` and it must be one of the Audio formats. The
`encoder_settings` is found as `fidl_fuchsia_media::EncoderSettings`.

After creating the encoder, audio input data can be provided either by using the Encoder as an
`AsyncWrite` provider.  The resulting encoded audio is provided via the output stream which can
be acquired via the `Encoder::take_encoded_stream` call.

# Example

```rust

let pcm_format = PcmFormat {
    pcm_mode: AudioPcmMode::Linear,
    bits_per_sample: 16,
    frames_per_second: 44100,
    channel_map: vec![AudioChannelId::Cf],
};

// Some sample SBC encoder settings.
let sbc_encoder_settings = EncoderSettings::Sbc(SbcEncoderSettings {
    sub_bands,
    block_count,
    allocation: SbcAllocation::AllocLoudness,
    channel_mode: SbcChannelMode::Mono,
    bit_pool: 59,
});


// Read a WAV file into memory.
let mut raw_audio = Vec::new();
File::open("sample.wav")?.read_to_end(&mut raw_audio)?;

let encoder = Encoder::create(pcm_format, sbc_encoder_settings);

// 44 bytes offset skips the RIFF header in the wav file.
// Delivering 16 audio frames at once that are 2 bytes each
for audio_frames in raw_audio[44..].chunks(2 * 16) {
    encoder.write(&audio_frames).await?;
}

encoder.close()?;

let mut encoded_stream = encoder.take_encoded_stream();

let mut output_file = File::create("sample.sbc")?;

while let Some(data) = encoded_stream.try_next().await? {
    output_file.write(&data)?;
}

output_file.close()?;

```

The encoded stream will begin producing data as soon as the StreamProcessor starts providing it -
the writing of the uncompressed data and the reading of the encoded stream can happen on separate
tasks.

*/

/// Interface to CodecFactory
pub mod encoder;

pub use encoder::Encoder;
