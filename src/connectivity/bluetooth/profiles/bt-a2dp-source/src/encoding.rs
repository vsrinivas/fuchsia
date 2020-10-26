// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use bt_a2dp as a2dp;
use fidl_fuchsia_media::{AudioFormat, AudioUncompressedFormat, DomainFormat, PcmFormat};
use fuchsia_async as fasync;
use fuchsia_audio_codec::StreamProcessor;
use fuchsia_trace as trace;
use fuchsia_zircon::{self as zx, DurationNum};
use futures::{
    io::AsyncWrite,
    stream::BoxStream,
    task::{Context, Poll},
    FutureExt, Stream, StreamExt,
};
use log::{info, trace};
use std::{collections::VecDeque, pin::Pin};

pub struct EncodedStream {
    /// The input media stream
    source: BoxStream<'static, fuchsia_audio_device_output::Result<Vec<u8>>>,
    /// The encoder input.
    encoder: Box<dyn AsyncWrite + Unpin + Send>,
    /// The underlying encoder stream
    encoded_stream: BoxStream<'static, Result<Vec<u8>, Error>>,
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
        let mut encoder =
            Box::new(StreamProcessor::create_encoder(pcm_input_format, encoder_settings)?);
        let encoded_stream = encoder.take_output_stream()?.boxed();

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

    /// Build a test version of this, that replaces the encoder with a set of streams that are
    /// given in the constructor.
    #[cfg(test)]
    fn build_test(
        source: BoxStream<'static, fuchsia_audio_device_output::Result<Vec<u8>>>,
        encoder: Box<dyn AsyncWrite + Unpin + Send>,
        encoded_stream: BoxStream<'static, Result<Vec<u8>, Error>>,
        pcm_bytes_per_encoded_packet: usize,
    ) -> Self {
        Self {
            source,
            encoder,
            encoded_stream,
            unflushed_bytecount: 0,
            encoder_input_buffers: VecDeque::new(),
            encoder_input_cursor: 0,
            pcm_bytes_per_encoded_packet,
        }
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
            None => Err(format_err!("Encoder ended stream")),
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
                    trace::instant!( "bt-a2dp-source", "Media:PacketReceived", 
                        trace::Scope::Thread, "bytes" => bytes.len() as u64);
                    self.encoder_input_buffers.push_back(bytes)
                }
            }
        }

        // Push audio into the encoder.
        while let Some(vec) = self.encoder_input_buffers.pop_front() {
            let cursor = self.encoder_input_cursor;
            match Pin::new(&mut self.encoder).poll_write(cx, &vec[cursor..]) {
                Poll::Pending => {
                    self.encoder_input_buffers.push_front(vec);
                    break;
                }
                Poll::Ready(Err(e)) => return Poll::Ready(Some(Err(e.into()))),
                Poll::Ready(Ok(written)) => {
                    self.encoder_input_cursor = cursor + written;
                    self.unflushed_bytecount = self.unflushed_bytecount + written;
                    // flush() if we have sent enough bytes to generate a frame
                    if self.unflushed_bytecount > self.pcm_bytes_per_encoded_packet {
                        // Attempt to flush.
                        if let Poll::Ready(Ok(())) = Pin::new(&mut self.encoder).poll_flush(cx) {
                            self.unflushed_bytecount = 0;
                        }
                    }
                    if self.encoder_input_cursor != vec.len() {
                        trace!(
                            "{} left in {} byte buffer..",
                            vec.len() - self.encoder_input_cursor,
                            vec.len()
                        );
                        self.encoder_input_buffers.push_front(vec);
                    } else {
                        // Reset to the front of the next buffer.
                        self.encoder_input_cursor = 0;
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
        let now = zx::Time::get_monotonic();
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

#[cfg(test)]
mod tests {

    use super::*;
    use futures::io;
    use std::sync::{Arc, Mutex};

    /// A stream that just returns a looping string of numbers.
    #[derive(Clone)]
    struct CountingStream(Arc<Mutex<CountingStreamInner>>);

    struct CountingStreamInner {
        next: u16,
        ready_bytes: usize,
    }

    impl Default for CountingStream {
        fn default() -> Self {
            Self(Arc::new(Mutex::new(CountingStreamInner { next: 0, ready_bytes: 0 })))
        }
    }

    impl CountingStream {
        fn set_bytes_ready(&self, bytes: usize) {
            self.0.lock().unwrap().ready_bytes = bytes;
        }
    }

    impl futures::Stream for CountingStream {
        type Item = fuchsia_audio_device_output::Result<Vec<u8>>;

        fn poll_next(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            let s = Pin::into_inner(self);
            let mut locked = s.0.lock().unwrap();
            if locked.ready_bytes == 0 {
                return Poll::Pending;
            }
            let len = (locked.ready_bytes / std::mem::size_of::<u16>()) as u16;
            let mut vec = Vec::with_capacity(locked.ready_bytes);
            for i in 0..len {
                vec.extend_from_slice(&mut locked.next.wrapping_add(i).to_be_bytes());
            }
            locked.next = locked.next.wrapping_add(len);
            locked.ready_bytes = 0;
            Poll::Ready(Some(Ok(vec)))
        }
    }

    /// An "encoder" that just buffers the input and sends it to the output when it's asked for.

    #[derive(Clone)]
    struct PassthroughEncoder(Arc<Mutex<PassthroughEncoderInner>>);

    struct PassthroughEncoderInner {
        // The k
        buffered: VecDeque<Vec<u8>>,
        stalled: bool,
    }

    impl Default for PassthroughEncoder {
        fn default() -> Self {
            Self(Arc::new(Mutex::new(PassthroughEncoderInner {
                buffered: VecDeque::new(),
                stalled: false,
            })))
        }
    }

    impl PassthroughEncoder {
        fn stall_input(&self, stall: bool) {
            self.0.lock().unwrap().stalled = stall;
        }

        fn push_input(&self, input: Vec<u8>) {
            self.0.lock().unwrap().buffered.push_front(input);
        }

        fn get_output(&self) -> Option<Vec<u8>> {
            self.0.lock().unwrap().buffered.pop_back()
        }

        fn is_stalled(&self) -> bool {
            self.0.lock().unwrap().stalled
        }
    }

    impl AsyncWrite for PassthroughEncoder {
        fn poll_write(
            self: Pin<&mut Self>,
            _cx: &mut Context<'_>,
            buf: &[u8],
        ) -> Poll<io::Result<usize>> {
            if self.is_stalled() {
                Poll::Pending
            } else {
                self.push_input(buf.iter().cloned().collect());
                Poll::Ready(Ok(buf.len()))
            }
        }

        fn poll_flush(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
            Poll::Ready(Ok(()))
        }

        fn poll_close(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
            Poll::Ready(Ok(()))
        }
    }

    impl Stream for PassthroughEncoder {
        type Item = Result<Vec<u8>, Error>;

        fn poll_next(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            if let Some(vec) = self.get_output() {
                Poll::Ready(Some(Ok(vec)))
            } else {
                Poll::Pending
            }
        }
    }

    #[test]
    fn test_stalled_encoder_input() {
        let input_stream = CountingStream::default();
        let passthrough = PassthroughEncoder::default();
        let passthrough_input = passthrough.clone();
        let passthrough_output = passthrough.clone();
        let mut stream = EncodedStream::build_test(
            input_stream.clone().boxed(),
            Box::new(passthrough_input),
            passthrough_output.boxed(),
            /* pcm bytes per encoded packet */ 500,
        );

        let mut noop_cx = Context::from_waker(futures::task::noop_waker_ref());

        // Polling for the next thing should run a whole cycle without an issue.
        input_stream.set_bytes_ready(2);
        match stream.poll_next_unpin(&mut noop_cx) {
            Poll::Ready(Some(Ok(data))) => assert_eq!(vec![0, 0], data),
            x => panic!("Expected ready poll, got {:?}", x),
        };

        // Stall the input of the encoder.
        passthrough.stall_input(true);

        // Polling should queue up because the encoder is stalled.
        input_stream.set_bytes_ready(2);
        assert!(stream.poll_next_unpin(&mut noop_cx).is_pending());
        input_stream.set_bytes_ready(2);
        assert!(stream.poll_next_unpin(&mut noop_cx).is_pending());

        // Unstall the input of the encoder.
        passthrough.stall_input(false);

        // Next time we poll, we didn't skip any packets.
        input_stream.set_bytes_ready(2);
        match stream.poll_next_unpin(&mut noop_cx) {
            Poll::Ready(Some(Ok(data))) => assert_eq!(vec![0, 1], data),
            x => panic!("Expected ready poll, got {:?}", x),
        };
        match stream.poll_next_unpin(&mut noop_cx) {
            Poll::Ready(Some(Ok(data))) => assert_eq!(vec![0, 2], data),
            x => panic!("Expected ready poll, got {:?}", x),
        };
        match stream.poll_next_unpin(&mut noop_cx) {
            Poll::Ready(Some(Ok(data))) => assert_eq!(vec![0, 3], data),
            x => panic!("Expected ready poll, got {:?}", x),
        };
    }
}
