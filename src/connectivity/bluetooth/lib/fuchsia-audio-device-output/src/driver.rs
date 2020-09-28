// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl::endpoints::ClientEnd,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_hardware_audio::*,
    fidl_fuchsia_media, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, DurationNum},
    futures::{
        select,
        stream::{FusedStream, Stream},
        task::{Context, Poll},
        Future, StreamExt,
    },
    log::{info, warn},
    parking_lot::Mutex,
    std::{pin::Pin, sync::Arc},
};

use crate::frame_vmo;
use crate::types::{AudioSampleFormat, Error, MaybeStream, Result};

/// A Stream that produces audio frames.
/// Usually acquired via SoftPcmOutput::take_frame_stream().
// TODO: return the time that the first frame is meant to be presented?
pub struct AudioFrameStream {
    /// The VMO that is receiving the frames.
    frame_vmo: Arc<Mutex<frame_vmo::FrameVmo>>,
    /// A timer to set the waiter to poll when there are no frames available.
    timer: fasync::Timer,
    /// The last time we received frames.
    /// None if it hasn't started yet.
    last_frame_time: Option<fasync::Time>,
    /// Minimum slice of time to deliver audio data in.
    /// This must be longer than the duration of one frame.
    min_packet_duration: zx::Duration,
    /// Handle to remove the audio device processing future when this is dropped.
    control_handle: StreamConfigControlHandle,
}

impl AudioFrameStream {
    fn new(
        frame_vmo: Arc<Mutex<frame_vmo::FrameVmo>>,
        min_packet_duration: zx::Duration,
        control_handle: StreamConfigControlHandle,
    ) -> AudioFrameStream {
        AudioFrameStream {
            frame_vmo,
            timer: fasync::Timer::new(fasync::Time::INFINITE_PAST),
            last_frame_time: Some(fasync::Time::now()),
            min_packet_duration,
            control_handle,
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
                    info!("Missed {} frames due to slow polling", missed);
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

impl Drop for AudioFrameStream {
    fn drop(&mut self) {
        self.control_handle.shutdown();
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
/// as defined in //docs/concepts/drivers/driver_interfaces/audio_streaming.md
pub struct SoftPcmOutput {
    /// The Stream channel handles format negotiation, plug detection, and gain
    stream_config_stream: StreamConfigRequestStream,

    /// The Unique ID that this stream will present to the system
    unique_id: [u8; 16],
    /// The manufacturer of the hardware for this stream
    manufacturer: String,
    /// A product description for the hardware for the stream
    product: String,
    /// The clock domain that this stream will present to the system
    clock_domain: u32,

    /// The supported format of this output.
    /// Currently only support one format per output is supported.
    supported_formats: PcmSupportedFormats,

    /// The minimum amount of time between audio frames output from the frame stream.
    /// Used to calculate minimum audio buffer sizes.
    min_packet_duration: zx::Duration,

    /// The currently set format, in frames per second, audio sample format, and channels.
    current_format: Option<(u32, AudioSampleFormat, u16)>,

    /// The request stream for the ringbuffer.
    ring_buffer_stream: MaybeStream<RingBufferRequestStream>,

    /// A pointer to the ring buffer for this stream
    frame_vmo: Arc<Mutex<frame_vmo::FrameVmo>>,

    /// Replied to plugged state watch.
    plug_state_replied: bool,

    /// Replied to gain state watch.
    gain_state_replied: bool,
}

impl SoftPcmOutput {
    /// Create a new software audio device, returning a client channel which can be supplied
    /// to the AudioCore and will act correctly as an audio output driver channel which can
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
        clock_domain: u32,
        pcm_format: fidl_fuchsia_media::PcmFormat,
        min_packet_duration: zx::Duration,
    ) -> Result<(ClientEnd<StreamConfigMarker>, AudioFrameStream)> {
        if pcm_format.bits_per_sample % 8 != 0 {
            // Non-byte-aligned format not allowed.
            return Err(Error::InvalidArgs);
        }
        let (client, request_stream) =
            fidl::endpoints::create_request_stream::<StreamConfigMarker>()
                .expect("Error creating stream config endpoint");

        let number_of_channels = pcm_format.channel_map.len() as u8;
        let supported_formats = PcmSupportedFormats {
            number_of_channels: vec![number_of_channels],
            sample_formats: vec![SampleFormat::PcmSigned],
            bytes_per_sample: vec![(pcm_format.bits_per_sample / 8) as u8],
            valid_bits_per_sample: vec![pcm_format.bits_per_sample as u8],
            frame_rates: vec![pcm_format.frames_per_second],
        };

        let stream = SoftPcmOutput {
            stream_config_stream: request_stream,
            unique_id: unique_id.clone(),
            manufacturer: manufacturer.to_string(),
            product: product.to_string(),
            clock_domain,
            supported_formats,
            min_packet_duration,
            current_format: None,
            ring_buffer_stream: Default::default(),
            frame_vmo: Arc::new(Mutex::new(frame_vmo::FrameVmo::new()?)),
            plug_state_replied: false,
            gain_state_replied: false,
        };

        let rb = stream.frame_vmo.clone();
        let control = stream.stream_config_stream.control_handle().clone();
        fasync::Task::spawn(stream.process_requests()).detach();
        Ok((client, AudioFrameStream::new(rb, min_packet_duration, control)))
    }

    async fn process_requests(mut self) {
        loop {
            select! {
                stream_config_request = self.stream_config_stream.next() => {
                    match stream_config_request {
                        Some(Ok(r)) => {
                            if let Err(e) = self.handle_stream_request(r) {
                                warn!("stream config request error: {:?}", e)
                            }
                        },
                        Some(Err(e)) => {
                            warn!("stream config error: {:?}, stopping", e);
                            return
                        },
                        None => {
                            warn!("no stream config error, stopping");
                            return
                        },
                    }
                }
                ring_buffer_request = self.ring_buffer_stream.next() => {
                    match ring_buffer_request {
                        Some(Ok(r)) => {
                            if let Err(e) = self.handle_ring_buffer_request(r) {
                                warn!("ring buffer request error: {:?}", e)
                            }
                        },
                        Some(Err(e)) => {
                            warn!("ring buffer error: {:?}, stopping", e);
                            return
                        },
                        None => {
                            warn!("no ring buffer error, stopping");
                            return
                        },
                    }
                }
            }
        }
    }

    fn handle_stream_request(
        &mut self,
        request: StreamConfigRequest,
    ) -> std::result::Result<(), anyhow::Error> {
        match request {
            StreamConfigRequest::GetProperties { responder } => {
                #[rustfmt::skip]
                let prop = StreamProperties {
                    unique_id:                Some(self.unique_id),
                    is_input:                 Some(false),
                    can_mute:                 Some(false),
                    can_agc:                  Some(false),
                    min_gain_db:              Some(0f32),
                    max_gain_db:              Some(0f32),
                    gain_step_db:             Some(0f32),
                    plug_detect_capabilities: Some(PlugDetectCapabilities::Hardwired),
                    clock_domain:             Some(self.clock_domain),
                    manufacturer:             Some(self.manufacturer.to_string()),
                    product:                  Some(self.product.to_string()),
                };
                responder.send(prop)?;
            }
            StreamConfigRequest::GetSupportedFormats { responder } => {
                let pcm_formats = self.supported_formats.clone();
                let formats_vector =
                    vec![SupportedFormats { pcm_supported_formats: Some(pcm_formats) }];
                responder.send(&mut formats_vector.into_iter())?;
            }
            StreamConfigRequest::CreateRingBuffer { format, ring_buffer, control_handle: _ } => {
                let pcm = (format.pcm_format.ok_or(format_err!("No pcm_format included")))?;
                self.ring_buffer_stream.set(ring_buffer.into_stream()?);
                self.current_format =
                    Some((pcm.frame_rate, pcm.into(), pcm.number_of_channels.into()))
            }
            StreamConfigRequest::WatchGainState { responder } => {
                if self.gain_state_replied == true {
                    // We will never change gain state.
                    responder.drop_without_shutdown();
                    return Ok(());
                }
                let gain_state = GainState {
                    muted: Some(false),
                    agc_enabled: Some(false),
                    gain_db: Some(0.0f32),
                };
                responder.send(gain_state)?;
                self.gain_state_replied = true
            }
            StreamConfigRequest::WatchPlugState { responder } => {
                if self.plug_state_replied == true {
                    // We will never change plug state.
                    responder.drop_without_shutdown();
                    return Ok(());
                }
                let time = fasync::Time::now();
                let plug_state = PlugState {
                    plugged: Some(true),
                    plug_state_time: Some(time.into_nanos() as i64),
                };
                responder.send(plug_state)?;
                self.plug_state_replied = true;
            }
            StreamConfigRequest::SetGain { target_state, control_handle: _ } => {
                if let Some(true) = target_state.muted {
                    warn!("Mute is not supported");
                }
                if let Some(true) = target_state.agc_enabled {
                    warn!("AGC is not supported");
                }
                if let Some(gain) = target_state.gain_db {
                    if gain != 0.0 {
                        warn!("Non-zero gain setting not supported");
                    }
                }
            }
        }
        Ok(())
    }

    fn handle_ring_buffer_request(
        &mut self,
        request: RingBufferRequest,
    ) -> std::result::Result<(), anyhow::Error> {
        match request {
            RingBufferRequest::GetProperties { responder } => {
                let prop = RingBufferProperties {
                    // TODO(fxbug.dev/51726): Set external_delay and fifo_depth from outside the crate.
                    external_delay: Some(0),
                    fifo_depth: Some(0),
                    needs_cache_flush_or_invalidate: Some(false),
                };
                responder.send(prop)?;
            }
            RingBufferRequest::GetVmo {
                min_frames,
                clock_recovery_notifications_per_ring,
                responder,
            } => {
                let (fps, format, channels) = match &self.current_format {
                    None => {
                        let mut error = Err(GetVmoError::InternalError);
                        if let Err(e) = responder.send(&mut error) {
                            warn!("Error on get vmo error send: {:?}", e);
                        }
                        return Ok(());
                    }
                    Some(x) => x.clone(),
                };
                // Require a minimum amount of frames for three fetches of packets.
                let min_frames_from_duration =
                    3 * frames_from_duration(fps as usize, self.min_packet_duration) as u32;
                let ring_buffer_frames = min_frames.max(min_frames_from_duration);
                match self.frame_vmo.lock().set_format(
                    fps,
                    format,
                    channels,
                    ring_buffer_frames as usize,
                    clock_recovery_notifications_per_ring,
                ) {
                    Err(_) => {
                        let mut error = Err(GetVmoError::InternalError);
                        responder.send(&mut error)?;
                    }
                    Ok(vmo_handle) => {
                        let mut result = Ok((min_frames, vmo_handle.into()));
                        responder.send(&mut result)?;
                    }
                }
            }
            RingBufferRequest::Start { responder } => {
                let time = fasync::Time::now();
                if let Err(e) = self.frame_vmo.lock().start(time.into()) {
                    warn!("Error on frame vmo start: {:?}", e);
                }
                responder.send(time.into_nanos() as i64)?;
            }
            RingBufferRequest::Stop { responder } => {
                if self.frame_vmo.lock().stop() == false {
                    warn!("Stopping a not started ring buffer");
                }
                responder.send()?;
            }
            RingBufferRequest::WatchClockRecoveryPositionInfo { responder } => {
                self.frame_vmo.lock().set_position_responder(responder);
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat};

    use futures::future;

    const TEST_UNIQUE_ID: &[u8; 16] = &[5; 16];
    const TEST_CLOCK_DOMAIN: u32 = 0x00010203;

    fn setup() -> (StreamConfigProxy, AudioFrameStream) {
        let format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 44100,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        };
        let (client, frame_stream) = SoftPcmOutput::build(
            TEST_UNIQUE_ID,
            "Google",
            "UnitTest",
            TEST_CLOCK_DOMAIN,
            format,
            zx::Duration::from_millis(100),
        )
        .expect("should always build");
        (client.into_proxy().expect("channel should be available"), frame_stream)
    }

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

    #[test]
    fn soft_pcm_audio_should_end_when_stream_dropped() {
        let format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 48000,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        };

        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (client, frame_stream) = SoftPcmOutput::build(
            TEST_UNIQUE_ID,
            &"Google".to_string(),
            &"UnitTest".to_string(),
            TEST_CLOCK_DOMAIN,
            format,
            zx::Duration::from_millis(100),
        )
        .expect("should always build");

        drop(frame_stream);

        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future::pending::<()>()));

        // The audio client should be dropped (normally this causes audio to remove the device)
        assert_eq!(Err(zx::Status::PEER_CLOSED), client.channel().write(&[0], &mut Vec::new()));
    }

    #[test]
    #[rustfmt::skip]
    fn soft_pcm_audio_out() {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (stream_config, mut frame_stream) = setup();

        let result = exec.run_until_stalled(&mut stream_config.get_properties());
        assert!(result.is_ready());
        let props1 = match result {
            Poll::Ready(Ok(v)) => v,
            _ => panic!("stream config get properties error"),
        };

        assert_eq!(props1.unique_id.unwrap(),                *TEST_UNIQUE_ID);
        assert_eq!(props1.is_input.unwrap(),                 false);
        assert_eq!(props1.can_mute.unwrap(),                 false);
        assert_eq!(props1.can_agc.unwrap(),                  false);
        assert_eq!(props1.min_gain_db.unwrap(),              0f32);
        assert_eq!(props1.max_gain_db.unwrap(),              0f32);
        assert_eq!(props1.gain_step_db.unwrap(),             0f32);
        assert_eq!(props1.plug_detect_capabilities.unwrap(), PlugDetectCapabilities::Hardwired);
        assert_eq!(props1.manufacturer.unwrap(),             "Google");
        assert_eq!(props1.product.unwrap(),                  "UnitTest");
        assert_eq!(props1.clock_domain.unwrap(),             TEST_CLOCK_DOMAIN);

        let result = exec.run_until_stalled(&mut stream_config.get_supported_formats());
        assert!(result.is_ready());

        let formats = match result {
            Poll::Ready(Ok(v)) => v,
            _ => panic!("get supported formats error"),
        };

        let first = formats.first().to_owned().expect("supported formats to be present");
        let pcm = first.pcm_supported_formats.to_owned().expect("pcm format to be present");
        assert_eq!(pcm.number_of_channels[0],    2u8);
        assert_eq!(pcm.sample_formats[0],        SampleFormat::PcmSigned);
        assert_eq!(pcm.bytes_per_sample[0],      2u8);
        assert_eq!(pcm.valid_bits_per_sample[0], 16u8);
        assert_eq!(pcm.frame_rates[0],           44100);

        let (ring_buffer, server) = fidl::endpoints::create_proxy::<RingBufferMarker>()
            .expect("creating ring buffer endpoint error");

        let format = Format {
            pcm_format: Some(fidl_fuchsia_hardware_audio::PcmFormat {
                number_of_channels:      2u8,
                channels_to_use_bitmask: 3u64,
                sample_format:           SampleFormat::PcmSigned,
                bytes_per_sample:        2u8,
                valid_bits_per_sample:   16u8,
                frame_rate:              44100,
            }),
        };

        stream_config.create_ring_buffer(format, server).expect("ring buffer error");

        let props2 = match exec.run_until_stalled(&mut ring_buffer.get_properties()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("expected Ready Ok from get_properties, got {:?}", x),
        };
        assert_eq!(props2.external_delay, Some(0i64));
        assert_eq!(props2.fifo_depth, Some(0u32));
        assert_eq!(props2.needs_cache_flush_or_invalidate, Some(false));

        let result = exec.run_until_stalled(&mut ring_buffer.get_vmo(88200, 0)); // 2 seconds.
        assert!(result.is_ready());
        let reply = match result {
            Poll::Ready(Ok(Ok(v))) => v,
            _ => panic!("ring buffer get vmo error"),
        };
        let audio_vmo = reply.1;

        // Frames * bytes per sample * channels per sample.
        let bytes_per_second = 44100 * 2 * 2;
        assert!(
            bytes_per_second <= audio_vmo.get_size().expect("should always exist after getbuffer")
        );

        // Put "audio" in buffer.
        let mut sent_audio = Vec::new();
        let mut x: u8 = 0x01;
        sent_audio.resize_with(bytes_per_second as usize, || {
            x = x.wrapping_add(2);
            x
        });

        assert_eq!(Ok(()), audio_vmo.write(&sent_audio, 0));

        exec.set_fake_time(fasync::Time::from_nanos(42));
        exec.wake_expired_timers();
        let start_time = exec.run_until_stalled(&mut ring_buffer.start());
        if let Poll::Ready(s) = start_time {
            assert_eq!(s.expect("start time error"), 42);
        } else {
            panic!("start error");
        }

        let mut frame_fut = frame_stream.next();
        let result = exec.run_until_stalled(&mut frame_fut);
        assert!(!result.is_ready());

        // Run the ring buffer for a bit over 1 second.
        exec.set_fake_time(fasync::Time::after(zx::Duration::from_millis(1001)));
        exec.wake_expired_timers();

        let result = exec.run_until_stalled(&mut frame_fut);
        assert!(result.is_ready());
        let mut audio_recv = match result {
            Poll::Ready(Some(Ok(v))) => v,
            x => panic!("expected Ready Ok from frame stream, got {:?}", x),
        };

        // We receive a bit more than 1 second of byte, resize to match and compare.
        audio_recv.resize(bytes_per_second as usize, 0);
        assert_eq!(&sent_audio, &audio_recv);

        let result = exec.run_until_stalled(&mut ring_buffer.stop());
        assert!(result.is_ready());

        // Watch gain only replies once.
        let result = exec.run_until_stalled(&mut stream_config.watch_gain_state());
        assert!(result.is_ready());
        let result = exec.run_until_stalled(&mut stream_config.watch_gain_state());
        assert!(!result.is_ready());

        // Watch plug state only replies once.
        let result = exec.run_until_stalled(&mut stream_config.watch_plug_state());
        assert!(result.is_ready());
        let result = exec.run_until_stalled(&mut stream_config.watch_plug_state());
        assert!(!result.is_ready());
    }

    #[test]
    fn send_positions() {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (stream_config, mut frame_stream) = setup();
        let _stream_config_properties = exec.run_until_stalled(&mut stream_config.get_properties());
        let _formats = exec.run_until_stalled(&mut stream_config.get_supported_formats());
        let (ring_buffer, server) = fidl::endpoints::create_proxy::<RingBufferMarker>()
            .expect("creating ring buffer endpoint error");

        #[rustfmt::skip]
        let format = Format {
            pcm_format: Some(fidl_fuchsia_hardware_audio::PcmFormat {
                number_of_channels:      2u8,
                channels_to_use_bitmask: 3u64,
                sample_format:           SampleFormat::PcmSigned,
                bytes_per_sample:        2u8,
                valid_bits_per_sample:   16u8,
                frame_rate:              44100,
            }),
        };

        let result = stream_config.create_ring_buffer(format, server);
        assert!(result.is_ok());

        let _ring_buffer_properties = exec.run_until_stalled(&mut ring_buffer.get_properties());

        let clock_recovery_notifications_per_ring = 10u32;
        let _ = exec.run_until_stalled(
            &mut ring_buffer.get_vmo(88200, clock_recovery_notifications_per_ring),
        ); // 2 seconds.

        exec.set_fake_time(fasync::Time::from_nanos(42));
        exec.wake_expired_timers();
        let start_time = exec.run_until_stalled(&mut ring_buffer.start());
        if let Poll::Ready(s) = start_time {
            assert_eq!(s.expect("start time error"), 42);
        } else {
            panic!("start error");
        }

        // Watch number 1.
        let mut position_info = ring_buffer.watch_clock_recovery_position_info();
        let result = exec.run_until_stalled(&mut position_info);
        assert!(!result.is_ready());

        let mut frame_fut = frame_stream.next();

        // Now advance in between notifications, with a 2 seconds total in the ring buffer
        // and 10 notifications per ring we can get watch notifications every 200 msecs.
        exec.set_fake_time(fasync::Time::after(zx::Duration::from_millis(201)));
        exec.wake_expired_timers();
        let result = exec.run_until_stalled(&mut frame_fut);
        assert!(result.is_ready());
        let result = exec.run_until_stalled(&mut position_info);
        assert!(result.is_ready());

        // Watch number 2.
        let mut position_info = ring_buffer.watch_clock_recovery_position_info();
        let result = exec.run_until_stalled(&mut position_info);
        assert!(!result.is_ready());
        exec.set_fake_time(fasync::Time::after(zx::Duration::from_millis(201)));
        exec.wake_expired_timers();
        let result = exec.run_until_stalled(&mut frame_fut);
        assert!(result.is_ready());
        let result = exec.run_until_stalled(&mut position_info);
        assert!(result.is_ready());

        // Watch number 3.
        let mut position_info = ring_buffer.watch_clock_recovery_position_info();
        let result = exec.run_until_stalled(&mut position_info);
        assert!(!result.is_ready());
        exec.set_fake_time(fasync::Time::after(zx::Duration::from_millis(201)));
        exec.wake_expired_timers();
        let result = exec.run_until_stalled(&mut frame_fut);
        assert!(result.is_ready());
        let result = exec.run_until_stalled(&mut position_info);
        assert!(result.is_ready());

        let result = exec.run_until_stalled(&mut ring_buffer.stop());
        assert!(result.is_ready());
    }
}
