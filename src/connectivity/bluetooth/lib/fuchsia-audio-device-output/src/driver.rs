// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_media, fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    fuchsia_zircon::{self as zx, DurationNum},
    futures::{
        select,
        stream::{FusedStream, Stream},
        task::{Context, Poll},
        Future, StreamExt,
    },
    parking_lot::Mutex,
    std::{convert::TryInto, pin::Pin, slice, sync::Arc},
};

use crate::types::{
    AudioSampleFormat, AudioStreamFormatRange, ChannelInner, Error, MaybeStream, RequestStream,
    Result,
};
use crate::{ring_buffer, stream};

/// A Stream that produces audio frames.
/// Usually acquired via SoftPcmOutput::take_frame_stream().
// TODO: return the time that the first frame is meant to be presented?
pub struct AudioFrameStream {
    /// The VMO that is receiving the frames.
    frame_vmo: Arc<Mutex<ring_buffer::FrameVmo>>,
    /// A timer to set the waiter to poll when there are no frames available.
    timer: fasync::Timer,
    /// The last time we received frames.
    /// None if it hasn't started yet.
    last_frame_time: Option<fasync::Time>,
    /// Minimum slice of time to deliver audio data in.
    /// This must be longer than the duration of one frame.
    min_packet_duration: zx::Duration,
}

impl AudioFrameStream {
    fn new(
        frame_vmo: Arc<Mutex<ring_buffer::FrameVmo>>,
        min_packet_duration: zx::Duration,
    ) -> AudioFrameStream {
        AudioFrameStream {
            frame_vmo,
            timer: fasync::Timer::new(fasync::Time::INFINITE_PAST),
            last_frame_time: Some(fasync::Time::now()),
            min_packet_duration,
        }
    }
}

impl Stream for AudioFrameStream {
    type Item = Result<Vec<u8>>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let now = fasync::Time::now();
        if self.last_frame_time.is_none() {
            self.last_frame_time = Some(now);
        }
        let from = self.last_frame_time.take().expect("need last frame time");
        let next_frame_time = {
            let mut lock = self.frame_vmo.lock();
            match lock.next_frame_after(from.into()) {
                Err(Error::InvalidState) => {
                    lock.set_start_waker(cx.waker().clone());
                    return Poll::Pending;
                }
                Err(e) => return Poll::Ready(Some(Err(e))),
                Ok(time) => time,
            }
        };
        if next_frame_time + self.min_packet_duration > now.into() {
            self.last_frame_time = Some(from);
            self.timer = fasync::Timer::new(from + self.min_packet_duration);
            let poll = fasync::Timer::poll(Pin::new(&mut self.timer), cx);
            assert!(poll == Poll::Pending);
            return Poll::Pending;
        }
        let res = self.frame_vmo.lock().get_frames(from.into(), now.into());
        match res {
            Ok((frames, missed)) => {
                if missed > 0 {
                    fx_log_info!("Missed {} frames due to slow polling", missed);
                }
                self.last_frame_time = Some(now);
                Poll::Ready(Some(Ok(frames)))
            }
            Err(e) => Poll::Ready(Some(Err(e))),
        }
    }
}

impl FusedStream for AudioFrameStream {
    fn is_terminated(&self) -> bool {
        false
    }
}

/// Open front, closed end frames from duration.
/// This means if duration is an exact duration of a number of frames, the last
/// frame will be considered to not be inside the duration, and will not be counted.
pub(crate) fn frames_from_duration(frames_per_second: usize, duration: zx::Duration) -> usize {
    assert!(duration >= 0.nanos(), "frames_from_duration is not defined for negative durations");
    if duration == 0.nanos() {
        return 0;
    }
    let mut frames = duration.into_seconds() * frames_per_second as i64;
    let mut frames_partial =
        ((duration.into_nanos() % 1_000_000_000) as f64 / 1e9) * frames_per_second as f64;
    if frames_partial.ceil() == frames_partial {
        // The end of this frame is exactly on the duration, but we don't include it.
        frames_partial -= 1.0;
    }
    frames += frames_partial as i64;
    frames as usize
}

/// A software fuchsia audio output, which implements Audio Driver Streaming Interface
/// as defined in //docs/zircon/driver_interfaces/audio.md
pub struct SoftPcmOutput {
    /// The Stream channel handles format negotiation, plug detection, and gain
    stream: Arc<ChannelInner<stream::Request>>,

    /// The RingBuffer command channel handles audio buffers and buffer notifications
    rb_chan: Option<Arc<ChannelInner<ring_buffer::Request>>>,

    /// The Unique ID that this stream will present to the system
    unique_id: [u8; 16],
    /// The manufacturer of the hardware for this stream
    manufacturer: String,
    /// A product description for the hardware for the stream
    product: String,

    /// The supported format of this output.
    /// Currently only support one format per output is supported.
    supported_format: AudioStreamFormatRange,

    /// The minimum amount of time between audio frames output from the frame stream.
    /// Used to calculate minimum audio buffer sizes.
    min_packet_duration: zx::Duration,

    /// The currently set format, in frames per second, audio sample format, and channels.
    current_format: Option<(u32, AudioSampleFormat, u16)>,

    /// The request stream for the ringbuffer.
    rb_requests: MaybeStream<RequestStream<ring_buffer::Request>>,

    /// A pointer to the ring buffer for this stream
    frame_vmo: Arc<Mutex<ring_buffer::FrameVmo>>,
}

impl SoftPcmOutput {
    /// Create a new software audio device, returning a client channel which can be supplied
    /// to the AudioCore and will act correectly as an audio output driver channel which can
    /// render audio in the `pcm_format` format, and an AudioFrameStream which produces the
    /// audio frames delivered to the audio output.
    /// Spawns a task to handle messages from the Audio Core and setup of internal VMO buffers
    /// required for audio output.  See AudioFrameStream for more information on timing
    /// requirements for audio output.
    /// `min_packet_duration`: the minimum duration of an audio packet returned by the strream.
    /// Setting this higher will mean more memory may be used to buffer audio.
    pub fn build(
        unique_id: &[u8; 16],
        manufacturer: &str,
        product: &str,
        pcm_format: fidl_fuchsia_media::PcmFormat,
        min_packet_duration: zx::Duration,
    ) -> Result<(zx::Channel, AudioFrameStream)> {
        let (client_channel, stream_channel) =
            zx::Channel::create().or_else(|e| Err(Error::IOError(e)))?;

        let supported_format = pcm_format.try_into()?;

        let stream = SoftPcmOutput {
            stream: Arc::new(ChannelInner::new(stream_channel)?),
            rb_chan: None,
            unique_id: unique_id.clone(),
            manufacturer: manufacturer.to_string(),
            product: product.to_string(),
            supported_format,
            min_packet_duration,
            current_format: None,
            rb_requests: Default::default(),
            frame_vmo: Arc::new(Mutex::new(ring_buffer::FrameVmo::new()?)),
        };

        let rb = stream.frame_vmo.clone();
        fuchsia_async::spawn(stream.process_events());
        Ok((client_channel, AudioFrameStream::new(rb, min_packet_duration)))
    }

    fn take_stream_requests(&self) -> RequestStream<stream::Request> {
        ChannelInner::take_request_stream(self.stream.clone())
    }

    fn take_ringbuffer_requests(&self) -> RequestStream<ring_buffer::Request> {
        let s = self.rb_chan.as_ref().expect("should exist when you take requests").clone();
        ChannelInner::take_request_stream(s)
    }

    fn handle_control_request(&mut self, request: stream::Request) -> Result<()> {
        match request {
            stream::Request::GetFormats { responder } => {
                responder.reply(slice::from_ref(&self.supported_format))
            }
            stream::Request::SetFormat {
                responder,
                frames_per_second,
                sample_format,
                channels,
            } => match self.set_format(frames_per_second, sample_format, channels) {
                Ok(channel) => {
                    self.rb_requests.set(self.take_ringbuffer_requests());
                    // TODO: make a way to pass the external expected delay and add it here
                    responder.reply(
                        zx::Status::OK,
                        self.min_packet_duration.into_nanos() as u64,
                        Some(channel),
                    )
                }
                Err(e) => responder.reply(zx::Status::NOT_SUPPORTED, 0, None).and(Err(e)),
            },
            // TODO(####): Implement a client to this inteface for gain control
            stream::Request::GetGain { responder } => {
                responder.reply(None, None, 0.0, [0.0, 0.0], 0.0)
            }
            stream::Request::SetGain { mute, agc, gain, responder } => {
                if mute.is_some() || agc.is_some() {
                    // We don't support setting mute or agc
                    return responder.reply(zx::Status::INVALID_ARGS, false, false, 0.0);
                } else if let Some(db) = gain {
                    if db == 0.0 {
                        return responder.reply(zx::Status::OK, false, false, 0.0);
                    }
                }
                responder.reply(zx::Status::INVALID_ARGS, false, false, 0.0)
            }
            stream::Request::PlugDetect { responder, .. } => {
                responder.reply(stream::PlugState::Hardwired, zx::Time::get(zx::ClockId::Monotonic))
            }
            stream::Request::GetUniqueId { responder } => responder.reply(&self.unique_id),
            stream::Request::GetString { id, responder } => {
                let s = match id {
                    stream::StringId::Manufacturer => &self.manufacturer,
                    stream::StringId::Product => &self.product,
                };
                responder.reply(s)
            }
        }
    }

    fn handle_ring_buffer_request(&self, request: ring_buffer::Request) -> Result<()> {
        match request {
            ring_buffer::Request::GetFifoDepth { responder } => {
                // We will never read beyond the nominal playback position.
                responder.reply(zx::Status::OK, 0)
            }
            ring_buffer::Request::GetBuffer {
                min_ring_buffer_frames,
                position_responder,
                responder,
            } => {
                let (fps, format, channels) = match &self.current_format {
                    None => {
                        responder.reply(zx::Status::BAD_STATE, 0, None)?;
                        return Err(Error::InvalidState);
                    }
                    Some(x) => x.clone(),
                };
                // Require a minimum amount of frames for three fetches of packets.
                let min_frames_from_duration =
                    3 * frames_from_duration(fps as usize, self.min_packet_duration) as u32;
                let ring_buffer_frames = min_ring_buffer_frames.max(min_frames_from_duration);
                match self.frame_vmo.lock().set_format(
                    fps,
                    format,
                    channels,
                    ring_buffer_frames as usize,
                    position_responder,
                ) {
                    Err(e) => {
                        let audio_err = match e {
                            Error::IOError(zx_err) => zx_err,
                            _ => zx::Status::BAD_STATE,
                        };
                        responder.reply(audio_err, 0, None).and(Err(e))
                    }
                    Ok(vmo_handle) => responder.reply(
                        zx::Status::OK,
                        min_ring_buffer_frames,
                        Some(vmo_handle.into()),
                    ),
                }
            }
            ring_buffer::Request::Start { responder } => {
                let time = fasync::Time::now();
                match self.frame_vmo.lock().start(time.into()) {
                    Err(_) => responder.reply(zx::Status::BAD_STATE, 0),
                    Ok(_) => responder.reply(zx::Status::OK, time.into_nanos() as u64),
                }
            }
            ring_buffer::Request::Stop { responder } => {
                if self.frame_vmo.lock().stop() {
                    responder.reply(zx::Status::OK)
                } else {
                    responder.reply(zx::Status::BAD_STATE)
                }
            }
        }
    }

    async fn process_events(mut self) {
        let mut requests: RequestStream<stream::Request> = self.take_stream_requests();

        loop {
            let mut rb_request_fut = self.rb_requests.next();
            select! {
                request = requests.next() => {
                    let res = match request {
                        None => Err(Error::PeerRead(zx::Status::UNAVAILABLE)),
                        Some(Err(e)) => Err(e),
                        Some(Ok(r)) => self.handle_control_request(r),
                    };
                    if let Err(e) = res {
                        fx_log_warn!("Audio Control Error: {:?}, stopping", e);
                        return;
                    }
                }
                rb_request = rb_request_fut => {
                    let res = match rb_request {
                        None => Err(Error::PeerRead(zx::Status::UNAVAILABLE)),
                        Some(Err(e)) => Err(e),
                        Some(Ok(r)) => self.handle_ring_buffer_request(r),
                    };
                    if let Err(e) = res {
                        fx_log_warn!("Ring Buffer Error: {:?}, stopping", e);
                        return;
                    }
                }
                complete => break,
            }
        }

        fx_log_info!("All Streams ended, stopping processing...");
    }

    fn set_format(
        &mut self,
        frames_per_second: u32,
        sample_format: AudioSampleFormat,
        channels: u16,
    ) -> Result<zx::Channel> {
        // TODO: validate against the self.supported_sample_format
        self.current_format = Some((frames_per_second, sample_format, channels));
        let (client_rb_channel, rb_channel) =
            zx::Channel::create().or_else(|e| Err(Error::IOError(e)))?;
        self.rb_chan = Some(Arc::new(ChannelInner::new(rb_channel)?));
        Ok(client_rb_channel)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat};
    use fuchsia_zircon::HandleBased;
    use futures::future;

    #[test]
    fn test_frames_from_duration() {
        const FPS: usize = 48000;
        // At 48kHz, each frame is 20833 and 1/3 nanoseconds. We add one nanosecond
        // because frames need to be completely within the duration.
        const ONE_FRAME_NANOS: i64 = 20833 + 1;
        const THREE_FRAME_NANOS: i64 = 20833 * 3 + 1;

        assert_eq!(0, frames_from_duration(FPS, 0.nanos()));

        assert_eq!(0, frames_from_duration(FPS, (ONE_FRAME_NANOS - 1).nanos()));
        assert_eq!(1, frames_from_duration(FPS, ONE_FRAME_NANOS.nanos()));

        // Three frames is an exact number of nanoseconds, so it should only count if we provide
        // a duration that is LONGER.
        assert_eq!(2, frames_from_duration(FPS, (THREE_FRAME_NANOS - 1).nanos()));
        assert_eq!(2, frames_from_duration(FPS, THREE_FRAME_NANOS.nanos()));
        assert_eq!(3, frames_from_duration(FPS, (THREE_FRAME_NANOS + 1).nanos()));

        assert_eq!(FPS - 1, frames_from_duration(FPS, 1.second()));
        assert_eq!(72000 - 1, frames_from_duration(FPS, 1500.millis()));

        assert_eq!(10660, frames_from_duration(FPS, 222084000.nanos()));
    }

    const TEST_UNIQUE_ID: &[u8; 16] = &[5; 16];

    // Receive a message from the client channel.  Wait and expect a response that starts with the
    // expected bytes.  Returns handles that were sent in the response.
    fn recv_and_expect_response(
        exec: &mut fasync::Executor,
        client_chan: &zx::Channel,
        request: &[u8],
        expected: &[u8],
    ) -> Vec<zx::Handle> {
        assert_eq!(Ok(()), client_chan.write(request, &mut Vec::new()));

        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future::pending::<()>()));

        let mut buf = zx::MessageBuf::new();
        assert_eq!(Ok(()), client_chan.read(&mut buf));

        let sent = buf.bytes();
        assert!(sent.len() >= expected.len());
        assert_eq!(expected, &sent[0..expected.len()]);

        let mut handles = Vec::new();
        while let Some(h) = buf.take_handle(handles.len()) {
            handles.push(h)
        }
        handles
    }

    #[test]
    fn soft_pcm_audio_out() {
        let format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 48000,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        };

        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (client, mut frame_stream) = SoftPcmOutput::build(
            TEST_UNIQUE_ID,
            &"Google".to_string(),
            &"UnitTest".to_string(),
            format,
            zx::Duration::from_millis(100),
        )
        .expect("should always build");

        // Canned requests and checked responses
        // GET_UNIQUE_ID
        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x05, 0x10, 0x00, 0x00, // get_unique_id
        ];
        let expected: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x05, 0x10, 0x00, 0x00, // get_unique_id
            0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
            0x05, 0x05, // Unique ID is all 5's
        ];

        recv_and_expect_response(&mut exec, &client, request, expected);

        // GET_STRING x2
        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x06, 0x10, 0x00, 0x00, // get_string
            0x00, 0x00, 0x00, 0x80, // string_id: manufacturer
        ];

        let expected: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x06, 0x10, 0x00, 0x00, // get_string
            0x00, 0x00, 0x00, 0x00, // status: OK
            0x00, 0x00, 0x00, 0x80, // string_id: manufacturer
            0x06, 0x00, 0x00, 0x00, // string_len: 6
            0x47, 0x6F, 0x6F, 0x67, 0x6C, 0x65, // string: Google
        ];

        recv_and_expect_response(&mut exec, &client, request, expected);

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x06, 0x10, 0x00, 0x00, // get_string
            0x01, 0x00, 0x00, 0x80, // string_id: product
        ];

        let expected: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x06, 0x10, 0x00, 0x00, // get_string
            0x00, 0x00, 0x00, 0x00, // status: OK
            0x01, 0x00, 0x00, 0x80, // string_id: product
            0x08, 0x00, 0x00, 0x00, // string_len: 8
            0x55, 0x6E, 0x69, 0x74, 0x54, 0x65, 0x73, 0x74, // string: UnitTest
        ];

        recv_and_expect_response(&mut exec, &client, request, expected);

        // GET_FORMATS
        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x00, 0x10, 0x00, 0x00, // get_formats
        ];
        let expected: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction_id
            0x00, 0x10, 0x00, 0x00, // get_formats
            0x00, 0x00, 0x00, 0x00, // pad
            0x01, 0x00, // format_range_count
            0x00, 0x00, // first_format_range_index
            0x04, 0x00, 0x00, 0x00, // 0: sample_formats (16-bit PCM)
            0x80, 0xBB, 0x00, 0x00, // 0: min_frames_per_second (48000)
            0x80, 0xBB, 0x00, 0x00, // 0: max_frames_per_second (48000)
            0x02, 0x02, // 0: min_channels, max_channels (2)
            0x01, 0x00, // 0: flags (FPS_CONTINUOUS)
        ]; // rest of bytes are not mattering
        recv_and_expect_response(&mut exec, &client, request, expected);

        // GET_GAIN
        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x02, 0x10, 0x00, 0x00, // get_gain
        ];
        let expected: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction_id
            0x02, 0x10, 0x00, 0x00, // get_gain
            0x00, // cur_mute: false
            0x00, // cur_agc: false
            0x00, 0x00, // 2x padding bytes that I don't care about
            0x00, 0x00, 0x00, 0x00, // 0.0 db current gain
            0x00, // can_mute: false
            0x00, // can_agc: false
            0x00, 0x00, // 2x padding bytes that I don't care about
            0x00, 0x00, 0x00, 0x00, // 0.0 min_gain
            0x00, 0x00, 0x00, 0x00, // 0.0 max_gain
            0x00, 0x00, 0x00, 0x00, // 0.0 gain_step
        ];
        recv_and_expect_response(&mut exec, &client, request, expected);

        // SET_GAIN
        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x03, 0x10, 0x00, 0x00, // set_gain
            0x04, 0x00, 0x00, 0x00, // gain_flags: only gain valif
            0x00, 0x00, 0x00, 0x00, // cur_gain: 0.0
        ];
        let expected: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction_id
            0x03, 0x10, 0x00, 0x00, // set_gain
            0x00, 0x00, 0x00, 0x00, // status: OK
            0x00, // mute: false
            0x00, // agc: false
            0x00, 0x00, // 2x padding bytes (set to 0)
            0x00, 0x00, 0x00, 0x00, // 0.0 db current gain
        ];
        recv_and_expect_response(&mut exec, &client, request, expected);

        // SET_FORMAT
        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x01, 0x10, 0x00, 0x00, // set_format
            0x44, 0xAC, 0x00, 0x00, // frames_per_second (44100)
            0x04, 0x00, 0x00, 0x00, // 16 bit PCM
            0x02, 0x00, // channels: 2
        ];
        let expected: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x01, 0x10, 0x00, 0x00, // set_format
            0x00, 0x00, 0x00, 0x00, // zx::status::ok
            0x00, 0x00, 0x00, 0x00, // padding
            0x00, 0xE1, 0xF5, 0x05, // minimum packet duration (100ms) from the construction
            // is used as external delay (0x05F5E100)
            0x00, 0x00, 0x00, 0x00,
        ];

        let mut handles = recv_and_expect_response(&mut exec, &client, request, expected);
        assert_eq!(1, handles.len());

        let rb = zx::Channel::from_handle(handles.pop().unwrap());

        // RB GET FIFO DEPTH
        let request: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x00, 0x30, 0x00, 0x00, // get_fifo_depth
        ];

        let expected: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x00, 0x30, 0x00, 0x00, // get_fifo_depth
            0x00, 0x00, 0x00, 0x00, // ZX_OK
            0x00, 0x00, 0x00, 0x00, // 0 bytes
        ];

        recv_and_expect_response(&mut exec, &rb, request, expected);

        // RB GET BUFFER
        // Request a minimum of one second of audio frame buffer
        let request: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x01, 0x30, 0x00, 0x00, // get_buffer
            0x88, 0x58, 0x01, 0x00, // min_ring_buffer_frames (two seconds worth) (88200)
            0x00, 0x00, 0x00, 0x00, // notifications_per_ring (none)
        ];
        let expected: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x01, 0x30, 0x00, 0x00, // get_buffer
            0x00, 0x00, 0x00, 0x00, // ZX_OK
            0x88, 0x58, 0x01, 0x00, // 88200 frames
        ];

        let mut handles = recv_and_expect_response(&mut exec, &rb, request, expected);
        assert_eq!(1, handles.len());

        let audio_vmo = zx::Vmo::from_handle(handles.pop().expect("should receive a handle"));

        // Frames * bytes per sample * channels per sample
        let bytes_per_second = 44100 * 2 * 2;
        assert!(
            bytes_per_second <= audio_vmo.get_size().expect("should always exist after getbuffer")
        );

        // Put "audio" in buffer
        let mut sent_audio = Vec::new();
        let mut x: u8 = 0x01;
        sent_audio.resize_with(bytes_per_second as usize, || {
            x = x.wrapping_add(2);
            x
        });

        assert_eq!(Ok(()), audio_vmo.write(&sent_audio, 0));

        // RB START
        exec.set_fake_time(fasync::Time::from_nanos(27));

        let request: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x02, 0x30, 0x00, 0x00, // start
        ];
        let expected: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x02, 0x30, 0x00, 0x00, // start
            0x00, 0x00, 0x00, 0x00, // ZX_OK
            0x00, 0x00, 0x00, 0x00, // padding
            27, 0x00, 0x00, 0x00, // Started at 27
            0x00, 0x00, 0x00, 0x00,
        ];

        recv_and_expect_response(&mut exec, &rb, request, expected);

        // Advance time enough for data to exist, but not far enough to hit min_packet_duration
        exec.set_fake_time(fasync::Time::after(zx::Duration::from_millis(50)));

        // No data should be ready yet
        let mut frame_fut = frame_stream.next();
        let result = exec.run_until_stalled(&mut frame_fut);
        assert!(!result.is_ready());

        // Now advance to after min_packet_duration, to 1 second + 1 nanos
        exec.set_fake_time(fasync::Time::after(
            zx::Duration::from_seconds(1) + zx::Duration::from_nanos(1)
                - zx::Duration::from_millis(50),
        ));

        let result = exec.run_until_stalled(&mut frame_fut);
        assert!(result.is_ready());

        // Read audio out, it should match
        if let Poll::Ready(Some(Ok(audio_recv))) = result {
            assert_eq!(bytes_per_second as usize, audio_recv.len());
            assert_eq!(&sent_audio, &audio_recv);
        } else {
            panic!("Expected Ready(Some(data)) got {:?}", result);
        }

        // RB STOP
        let request: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x03, 0x30, 0x00, 0x00, // stop
        ];
        let expected: &[u8] = &[
            0xFA, 0xCE, 0xBA, 0xD0, // transaction id
            0x03, 0x30, 0x00, 0x00, // stop
            0x00, 0x00, 0x00, 0x00, // ZX_OK
        ];

        recv_and_expect_response(&mut exec, &rb, request, expected);
    }
}
