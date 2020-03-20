// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use bitfield::bitfield;
use bt_a2dp as a2dp;
use fidl_fuchsia_media::{AudioFormat, AudioUncompressedFormat, DomainFormat, PcmFormat};
use fuchsia_audio_codec::{StreamProcessor, StreamProcessorOutputStream};
use futures::{
    io::AsyncWrite,
    stream::BoxStream,
    task::{Context, Poll},
    Stream, StreamExt,
};
use std::{collections::VecDeque, pin::Pin};

use fuchsia_syslog::fx_log_info;

pub struct RtpPacketBuilder {
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
    /// Extra header to include in each packet before `frames` are added
    frame_header: Vec<u8>,
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

impl RtpPacketBuilder {
    /// Make a new builder that will vend a packet every `frames_per_packet` frames. `frame_header`
    /// are header bytes added to each packet before frames are added.
    pub fn new(frames_per_packet: u8, frame_header: Vec<u8>) -> Self {
        Self {
            frames_per_packet,
            next_sequence_number: 1,
            timestamp: 0,
            frames: VecDeque::with_capacity(frames_per_packet.into()),
            frame_time: 0,
            frame_header,
        }
    }

    /// Add a frame that represents `time` pcm audio samples into the builder.
    /// Returns a serialized RTP packet if this frame fills a packet.
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
            let frame_header_iter = self.frame_header.iter().cloned();
            let frame_bytes_iter = self.frames.drain(..).flatten();
            let packet = header_iter.chain(frame_header_iter).chain(frame_bytes_iter).collect();
            self.next_sequence_number = self.next_sequence_number.wrapping_add(1);
            self.timestamp = self.timestamp + self.frame_time;
            self.frame_time = 0;
            return Ok(Some(packet));
        }
        Ok(None)
    }
}

pub struct EncodedStream {
    /// The input media stream
    source: BoxStream<'static, fuchsia_audio_device_output::Result<Vec<u8>>>,
    /// The underlying encoder object
    encoder: StreamProcessor,
    /// The underlying encoder stream
    encoded_stream: StreamProcessorOutputStream,
    /// Bytes that have been sent to the encoder and not flushed.
    unflushed_bytecount: usize,
    /// Bytes that are buffered to send to the encoder
    encoder_input_buffers: VecDeque<Vec<u8>>,
    /// Cursor on the first buffer waiting indicating the next byte to be written to the encoder
    encoder_input_cursor: usize,
    /// Number of bytes to encode of the input before flushing to get an output packet
    pcm_bytes_per_encoded_packet: usize,
}

impl EncodedStream {
    pub fn build(
        input_format: PcmFormat,
        source: BoxStream<'static, fuchsia_audio_device_output::Result<Vec<u8>>>,
        config: &a2dp::codec::MediaCodecConfig,
    ) -> Result<Self, Error> {
        let encoder_settings = config.encoder_settings()?;

        let bytes_per_pcm_frame =
            (input_format.bits_per_sample / 8) as usize * input_format.channel_map.len();
        let pcm_bytes_per_encoded_packet = config.pcm_frames_per_encoded_frame()
            * bytes_per_pcm_frame
            * config.frames_per_packet();

        let pcm_input_format = DomainFormat::Audio(AudioFormat::Uncompressed(
            AudioUncompressedFormat::Pcm(input_format),
        ));
        let mut encoder = StreamProcessor::create_encoder(pcm_input_format, encoder_settings)?;
        let encoded_stream = encoder.take_output_stream()?;

        Ok(Self {
            source,
            encoder,
            encoded_stream,
            unflushed_bytecount: 0,
            encoder_input_buffers: VecDeque::new(),
            encoder_input_cursor: 0,
            pcm_bytes_per_encoded_packet,
        })
    }
}

impl Stream for EncodedStream {
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
                    if self.unflushed_bytecount > self.pcm_bytes_per_encoded_packet {
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

    use bt_avdtp as avdtp;
    use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode};
    use fuchsia_async::{self as fasync, TimeoutExt};
    use fuchsia_zircon::{self as zx, DurationNum};
    use futures::FutureExt;
    use std::convert::TryFrom;

    #[test]
    fn test_packet_builder_sbc() {
        let mut builder = RtpPacketBuilder::new(5, vec![5]);

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

    fn pcm_format() -> PcmFormat {
        PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 48000,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        }
    }

    #[test]
    fn test_sbc_encodes_correctly() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let sbc_capability = &avdtp::ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: vec![0x11, 0x15, 2, 53],
        };

        let sbc_config =
            a2dp::codec::MediaCodecConfig::try_from(sbc_capability).expect("config builds");
        // Maybe just replace this with future::repeat(0)
        let silence_source = SilenceStream::build(pcm_format());
        let mut encoder = EncodedStream::build(pcm_format(), silence_source.boxed(), &sbc_config)
            .expect("building Stream works");
        let mut next_frame_fut = encoder
            .next()
            .on_timeout(fasync::Time::after(TIMEOUT), || panic!("Encoder took too long"));

        match exec.run_singlethreaded(&mut next_frame_fut) {
            Some(Ok(enc_frame)) => {
                // TODO(45775) validate encoder settings match what we specified in sbc_settings
                assert!(!enc_frame.is_empty());
            }
            Some(Err(e)) => panic!("Uexpected error encoding: {}", e),
            None => panic!("Encoder finished without a frame"),
        }
    }

    #[test]
    fn test_aac_encodes_correctly() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let aac_capability = &avdtp::ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_AAC,
            codec_extra: vec![128, 1, 4, 4, 226, 0],
        };
        let aac_config =
            a2dp::codec::MediaCodecConfig::try_from(aac_capability).expect("config builds");
        // Maybe just replace this with future::repeat(0)
        let silence_source = SilenceStream::build(pcm_format());
        let mut encoder = EncodedStream::build(pcm_format(), silence_source.boxed(), &aac_config)
            .expect("building Stream works");
        let mut next_frame_fut = encoder
            .next()
            .on_timeout(fasync::Time::after(TIMEOUT), || panic!("Encoder took too long"));

        match exec.run_singlethreaded(&mut next_frame_fut) {
            Some(Ok(enc_frame)) => {
                // TODO(45775) validate encoder settings match what we specified in aac_settings
                assert!(!enc_frame.is_empty());
            }
            Some(Err(e)) => panic!("Uexpected error encoding: {}", e),
            None => panic!("Encoder finished without a frame"),
        }
    }
}
