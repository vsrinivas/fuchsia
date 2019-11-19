// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use bt_avdtp as avdtp;
use failure::Error;
use fidl_fuchsia_media::{
    AudioFormat, AudioUncompressedFormat, DomainFormat, EncoderSettings, PcmFormat, SbcAllocation,
    SbcBlockCount, SbcChannelMode, SbcEncoderSettings, SbcSubBands,
};
use fuchsia_audio_encoder::{encoder::EncodedStream, Encoder};
use futures::{
    io::AsyncWrite,
    stream::BoxStream,
    task::{Context, Poll},
    Stream, StreamExt,
};
use std::{collections::VecDeque, iter::once, pin::Pin};

use fuchsia_syslog::fx_log_info;

pub struct RtpPacketBuilderSbc {
    /// The number of frames to be included in each packet
    frames_per_packet: u8,
    /// The next packet's sequence number
    next_sequence_number: u16,
    /// Timestamp of the end of the packets sent so far. This is currently in audio sample units.
    timestamp: u32,
    /// Frames that will be in the next RtpPacket to be sent.
    frames: VecDeque<Vec<u8>>,
    /// Time that those frames represent, in the same units as `timestamp`
    frame_time: u32,
}

bitfield! {
    struct RtpHeader(MSB0 [u8]);
    u8, version, set_version: 1, 0;
    bool, padding, set_padding: 2;
    bool, extension, set_extension: 3;
    u8, csrc_count, set_csrc_count: 7, 4;
    bool, marker, set_marker: 8;
    u8, payload_type, set_payload_type: 15, 9;
    u16, sequence_number, set_sequence_number: 31, 16;
    u32, timestamp, set_timestamp: 63, 32;
    u32, ssrc, set_ssrc: 95, 64;
}

impl RtpPacketBuilderSbc {
    /// Make a new builder that will vend a packet every `frames_per_packet` frames.
    pub fn new(frames_per_packet: u8) -> Self {
        Self {
            frames_per_packet,
            next_sequence_number: 1,
            timestamp: 0,
            frames: VecDeque::with_capacity(frames_per_packet.into()),
            frame_time: 0,
        }
    }

    /// Add a frame that is `time` audio samples long into the builder.
    /// Returns a serialized RTP packet if this frame fills the packet,
    /// or an Error.
    pub fn push_frame(&mut self, frame: Vec<u8>, time: u32) -> Result<Option<Vec<u8>>, Error> {
        self.frames.push_back(frame);
        self.frame_time = self.frame_time + time;
        if self.frames.len() >= self.frames_per_packet.into() {
            let mut header = RtpHeader([0; 12]);
            header.set_version(2);
            header.set_payload_type(96); // Dynamic payload type indicated by RFC 3551, recommended by the spec
            header.set_sequence_number(self.next_sequence_number);
            header.set_timestamp(self.timestamp);
            let header_iter = header.0.iter().cloned();
            let frame_bytes_iter = self.frames.drain(..).flatten();
            let packet =
                header_iter.chain(once(self.frames_per_packet)).chain(frame_bytes_iter).collect();
            self.next_sequence_number = self.next_sequence_number.wrapping_add(1);
            self.timestamp = self.timestamp + self.frame_time;
            self.frame_time = 0;
            return Ok(Some(packet));
        }
        Ok(None)
    }
}

pub struct EncodedStreamSbc {
    /// The input media stream
    source: BoxStream<'static, fuchsia_audio_device_output::Result<Vec<u8>>>,
    /// The underlying encoder object
    encoder: Encoder,
    /// The underlying encoder stream
    encoded_stream: EncodedStream,
    /// Bytes that have been sent to the encoder and not flushed.
    unflushed_bytecount: usize,
    /// Bytes that are buffered to send to the encoder
    encoder_input_buffers: VecDeque<Vec<u8>>,
    /// Cursor on the first buffer waiting indicating the next byte to be written to the encoder
    encoder_input_cursor: usize,
}

impl EncodedStreamSbc {
    pub fn build(
        input_format: PcmFormat,
        _sbc_settings: &avdtp::ServiceCapability,
        source: BoxStream<'static, fuchsia_audio_device_output::Result<Vec<u8>>>,
    ) -> Result<Self, Error> {
        // TODO: take these from the sbc_settings
        let sbc_encoder_settings = EncoderSettings::Sbc(SbcEncoderSettings {
            sub_bands: SbcSubBands::SubBands8,
            allocation: SbcAllocation::AllocLoudness,
            block_count: SbcBlockCount::BlockCount16,
            channel_mode: SbcChannelMode::JointStereo,
            bit_pool: 53,
        });

        let pcm_input_format = DomainFormat::Audio(AudioFormat::Uncompressed(
            AudioUncompressedFormat::Pcm(input_format),
        ));
        let mut encoder = Encoder::create(pcm_input_format, sbc_encoder_settings)?;
        let encoded_stream = encoder.take_encoded_stream()?;

        Ok(Self {
            source,
            encoder,
            encoded_stream,
            unflushed_bytecount: 0,
            encoder_input_buffers: VecDeque::new(),
            encoder_input_cursor: 0,
        })
    }
}

// TODO(40986, 41449): Update this based on the input format and the codec settings.
pub const PCM_FRAMES_PER_ENCODED: usize = 640;
const ENCODED_PER_PACKET: usize = 5;
const BYTES_PER_PCM_FRAME: usize = 4;

const PCM_BYTES_PER_ENCODED_PACKET: usize =
    PCM_FRAMES_PER_ENCODED * ENCODED_PER_PACKET * BYTES_PER_PCM_FRAME;

impl Stream for EncodedStreamSbc {
    type Item = Result<Vec<u8>, Error>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // Read audio out.
        while let Poll::Ready(item) = self.source.poll_next_unpin(cx) {
            match item {
                None => {
                    fx_log_info!("Audio stream closed.");
                    return Poll::Ready(None);
                }
                Some(Err(e)) => return Poll::Ready(Some(Err(e.into()))),
                Some(Ok(bytes)) => self.encoder_input_buffers.push_back(bytes),
            }
        }
        // Push audio into the encoder.
        while let Some(vec) = self.encoder_input_buffers.pop_front() {
            let cursor = self.encoder_input_cursor;
            match Pin::new(&mut self.encoder).poll_write(cx, &vec[cursor..]) {
                Poll::Pending => break,
                Poll::Ready(Err(e)) => return Poll::Ready(Some(Err(e.into()))),
                Poll::Ready(Ok(written)) => {
                    self.encoder_input_cursor = cursor + written;
                    self.unflushed_bytecount = self.unflushed_bytecount + written;
                    // flush() if we have sent enough bytes to generate a frame
                    if self.unflushed_bytecount > PCM_BYTES_PER_ENCODED_PACKET {
                        // Attempt to flush.
                        if self.encoder.flush().is_ok() {
                            self.unflushed_bytecount = 0;
                        }
                    }
                    if self.encoder_input_cursor != vec.len() {
                        self.encoder_input_buffers.push_front(vec);
                        continue;
                    } else {
                        // Reset to the front of the next buffer.
                        self.encoder_input_cursor = 0
                    }
                }
            }
        }
        // Finally, read data out of the encoder if it's ready.
        self.encoded_stream.poll_next_unpin(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode};
    use fuchsia_async::{self as fasync, TimeoutExt};
    use fuchsia_zircon::{self as zx, DurationNum};
    use futures::FutureExt;

    #[test]
    fn test_packet_builder_sbc() {
        let mut builder = RtpPacketBuilderSbc::new(5);

        assert!(builder.push_frame(vec![0xf0], 1).unwrap().is_none());
        assert!(builder.push_frame(vec![0x9f], 2).unwrap().is_none());
        assert!(builder.push_frame(vec![0x92], 4).unwrap().is_none());
        assert!(builder.push_frame(vec![0x96], 8).unwrap().is_none());

        let result = builder.push_frame(vec![0x33], 16);
        assert!(result.is_ok());

        let expected = &[
            0x80, 0x60, 0x00, 0x01, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x05, // Frames in this packet
            0xf0, 0x9f, 0x92, 0x96, 0x33, // 💖!
        ];

        let result = result.unwrap().expect("a packet after 5 more frames");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        assert!(builder.push_frame(vec![0xf0], 32).unwrap().is_none());
        assert!(builder.push_frame(vec![0x9f], 64).unwrap().is_none());
        assert!(builder.push_frame(vec![0x92], 128).unwrap().is_none());
        assert!(builder.push_frame(vec![0x96], 256).unwrap().is_none());

        let result = builder.push_frame(vec![0x33], 512);
        assert!(result.is_ok());

        let expected = &[
            0x80, 0x60, 0x00, 0x02, // Sequence num
            0x00, 0x00, 0x00, 0x1F, // timestamp = 2^0 + 2^1 + 2^2 + 2^3 + 2^4)
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x05, // Frames in this packet
            0xf0, 0x9f, 0x92, 0x96, 0x33, // 💖!
        ];

        let result = result.unwrap().expect("a packet after 5 more frames");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);
    }

    struct SilenceStream {
        pcm_format: PcmFormat,
        next_frame_timer: fasync::Timer,
        /// the last time we delivered frames.
        last_frame_time: Option<zx::Time>,
    }

    const PCM_SAMPLE_SIZE: usize = 2;

    impl futures::Stream for SilenceStream {
        type Item = fuchsia_audio_device_output::Result<Vec<u8>>;

        fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            let now = zx::Time::get(zx::ClockId::Monotonic);
            if self.last_frame_time.is_none() {
                self.last_frame_time = Some(now - 1.second());
            }
            let last_time = self.last_frame_time.as_ref().unwrap().clone();
            let repeats = (now - last_time).into_seconds();
            if repeats == 0 {
                self.next_frame_timer = fasync::Timer::new((last_time + 1.second()).into());
                let poll = self.next_frame_timer.poll_unpin(cx);
                assert_eq!(Poll::Pending, poll);
                return Poll::Pending;
            }
            // Generate one second of silence.
            let pcm_frame_size = self.pcm_format.channel_map.len() * PCM_SAMPLE_SIZE;
            let buffer = vec![0; self.pcm_format.frames_per_second as usize * pcm_frame_size];
            self.last_frame_time = Some(last_time + 1.second());
            Poll::Ready(Some(Ok(buffer)))
        }
    }

    impl SilenceStream {
        fn build(pcm_format: PcmFormat) -> Self {
            Self {
                pcm_format,
                next_frame_timer: fasync::Timer::new(fasync::Time::INFINITE_PAST),
                last_frame_time: None,
            }
        }
    }

    const TIMEOUT: zx::Duration = zx::Duration::from_millis(5000);

    #[test]
    fn test_encodes_correctly() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let pcm_format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 48000,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        };
        let sbc_settings = avdtp::ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: vec![0x11, 0x15, 2, 53],
        };
        // Mayve just replace this with future::repeat(0)
        let silence_source = SilenceStream::build(pcm_format.clone());
        let mut encoder =
            EncodedStreamSbc::build(pcm_format, &sbc_settings, silence_source.boxed())
                .expect("building Stream works");
        let mut next_frame_fut = encoder
            .next()
            .on_timeout(fasync::Time::after(TIMEOUT), || panic!("Encoder took too long"));

        match exec.run_singlethreaded(&mut next_frame_fut) {
            Some(Ok(enc_frame)) => {
                // maybe match the actual SBC output here, but since the encoder itself is tested,
                // just confirming output can be created here is probably sufficient
                assert!(!enc_frame.is_empty());
            }
            Some(Err(e)) => panic!("Uexpected error encoding: {}", e),
            None => panic!("Encoder finished without a frame"),
        }
    }
}
