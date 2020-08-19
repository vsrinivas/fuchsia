// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use bt_a2dp as a2dp;
use fidl_fuchsia_media::{AudioFormat, AudioUncompressedFormat, DomainFormat, PcmFormat};
use fuchsia_async as fasync;
use fuchsia_audio_codec::{StreamProcessor, StreamProcessorOutputStream};
use fuchsia_trace as trace;
use fuchsia_zircon::{self as zx, DurationNum};
use futures::{
    io::AsyncWrite,
    stream::BoxStream,
    task::{Context, Poll},
    FutureExt, Stream, StreamExt,
};
use log::info;
use std::{collections::VecDeque, pin::Pin};

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
    /// Build a new EncodedStream which produces encoded frames from the given `source`.
    /// Returns an error if codec setup fails. Successfully building a EncodedStream does not
    /// guarantee that the system can encode - many errors can only be detected once encoding
    /// is attempted.  EncodedStream produces a Some(Err) result in these cases.  It is
    /// recommended to confirm that the system can encode using `EncodedStream::test()` first.
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

    /// Run a preliminary test for a encoding audio in `input_format` into the codec `config`.
    pub async fn test(
        input_format: PcmFormat,
        config: &a2dp::codec::MediaCodecConfig,
    ) -> Result<(), Error> {
        let silence_source = SilenceStream::build(input_format.clone());
        let mut encoder = EncodedStream::build(input_format, silence_source.boxed(), config)
            .context("Building encoder")?;
        match encoder.next().await {
            Some(Ok(encoded_frame)) => {
                if encoded_frame.is_empty() {
                    Err(format_err!("Encoded frame was empty"))
                } else {
                    Ok(())
                }
            }
            Some(Err(e)) => Err(e),
            None => Err(format_err!("SBC encoder ended stream")),
        }
    }
}

impl Stream for EncodedStream {
    type Item = Result<Vec<u8>, Error>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // Read audio out.
        while let Poll::Ready(item) = self.source.poll_next_unpin(cx) {
            match item {
                None => {
                    info!("Audio stream closed.");
                    return Poll::Ready(None);
                }
                Some(Err(e)) => return Poll::Ready(Some(Err(e.into()))),
                Some(Ok(bytes)) => {
                    trace::instant!("bt-a2dp-source", "Media:PacketReceived", trace::Scope::Thread);
                    self.encoder_input_buffers.push_back(bytes)
                }
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
                        if self.encoder.send_packet().is_ok() {
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

const PCM_SAMPLE_SIZE: usize = 2;

struct SilenceStream {
    pcm_format: PcmFormat,
    next_frame_timer: fasync::Timer,
    /// the last time we delivered frames.
    last_frame_time: Option<zx::Time>,
}

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
            self.next_frame_timer = fasync::Timer::new(last_time + 1.second());
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
