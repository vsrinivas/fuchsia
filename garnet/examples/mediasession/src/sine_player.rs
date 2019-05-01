//! Sine player is a simple media player that plays a sine wave and publishes
//! its media session with the Media Session API.
//!
//! Sine player is an example of how a player can publish their media session so
//! other components on the system can observe its status and send it controls.
//!
//! Commentary is written starting in main and follows control flow.
//!
//! run `fx shell mediasession_cli_tool` and then start some of these players
//! with
//! `fx shell run fuchsia-pkg://fuchsia.com/sine_player#meta/sine_player.cmx` to
//! see how it works.

#![feature(async_await, await_macro)]
#![recursion_limit = "256"]

use byteorder::{ByteOrder, NativeEndian};
use failure::{Error, ResultExt};
use fidl::endpoints::{create_endpoints, ServerEnd};
use fidl_fuchsia_media::{
    AudioMarker, AudioRendererProxy, AudioSampleFormat, AudioStreamType, Metadata, Property,
    StreamPacket, TimelineFunction, METADATA_LABEL_ARTIST, NO_TIMESTAMP,
};
use fidl_fuchsia_media_sessions::*;
use fuchsia_async as fasync;
use fuchsia_component as component;
use fuchsia_zircon as zx;
use futures::{select, FutureExt, StreamExt, TryFutureExt, TryStreamExt};
use std::f32;

type Result<T> = std::result::Result<T, Error>;

// In this section we just prepare a buffer with sine wave data to pump into an
// audio renderer. This is just a simple audio output you can hear; if you are
// looking for examples for the audio services, try the "simple_sine" example
// apart from this.

const FRAME_RATE: f32 = 48000.0;

const PAYLOAD_COUNT: usize = 100;
const FRAMES_PER_PAYLOAD: usize = (FRAME_RATE as usize) / PAYLOAD_COUNT;
const PAYLOAD_SIZE: usize = FRAMES_PER_PAYLOAD * std::mem::size_of::<f32>();

const FREQUENCY: f32 = 439.0;
const AMPLITUDE: f32 = 0.125;

fn load_sine_wave(frequency: f32, amplitude: f32, renderer: &AudioRendererProxy) -> Result<()> {
    let buffer_size = PAYLOAD_SIZE * PAYLOAD_COUNT;
    let vmo = zx::Vmo::create_with_opts(
        zx::VmoOptions::from_bits_truncate(
            zx::sys::ZX_RIGHT_READ | zx::sys::ZX_RIGHT_WRITE | zx::sys::ZX_RIGHT_TRANSFER,
        ),
        buffer_size as u64,
    )?;

    for frame in 0..(FRAMES_PER_PAYLOAD * PAYLOAD_COUNT) {
        let value =
            amplitude * (frequency * (frame as f32) * 2.0 * f32::consts::PI / FRAME_RATE).sin();
        let mut value_bytes = [0; std::mem::size_of::<f32>()];
        NativeEndian::write_f32(&mut value_bytes, value);
        vmo.write(&value_bytes, (frame * std::mem::size_of::<f32>()) as u64)?;
    }

    renderer.add_payload_buffer(0, vmo)?;
    renderer.set_pcm_stream_type(&mut AudioStreamType {
        sample_format: AudioSampleFormat::Float,
        channels: 1,
        frames_per_second: FRAME_RATE as u32,
    })?;

    Ok(())
}

fn sine_wave_payload(i: usize) -> StreamPacket {
    let i = i % PAYLOAD_COUNT;
    StreamPacket {
        pts: NO_TIMESTAMP,
        payload_buffer_id: 0,
        payload_offset: (i * PAYLOAD_SIZE) as u64,
        payload_size: PAYLOAD_SIZE as u64,
        buffer_config: 0,
        stream_segment_id: 0,
        flags: 0,
    }
}

fn renderer_proxy() -> Result<AudioRendererProxy> {
    let audio_proxy = component::client::connect_to_service::<AudioMarker>()?;
    let (renderer_client_end, renderer_server_end) = create_endpoints()?;
    audio_proxy.create_audio_renderer(renderer_server_end)?;
    Ok(renderer_client_end.into_proxy()?)
}

// TODO(turnage): Remove after FIDL-526
fn clone_playback_status(playback_status: &PlaybackStatus) -> PlaybackStatus {
    PlaybackStatus {
        duration: playback_status.duration,
        playback_state: playback_status.playback_state,
        playback_function: playback_status.playback_function.as_ref().map(|tf| TimelineFunction {
            subject_time: tf.subject_time,
            reference_time: tf.reference_time,
            subject_delta: tf.subject_delta,
            reference_delta: tf.reference_delta,
        }),
        repeat_mode: playback_status.repeat_mode,
        shuffle_on: playback_status.shuffle_on,
        has_next_item: playback_status.has_next_item,
        has_prev_item: playback_status.has_prev_item,
        error: playback_status.error.as_ref().map(|e| fidl_fuchsia_media_sessions::Error {
            code: e.code,
            description: e.description.clone(),
        }),
    }
}

// TODO(turnage): Remove after FIDL-526
fn clone_playback_capabilities(capabilities: &PlaybackCapabilities) -> PlaybackCapabilities {
    PlaybackCapabilities {
        flags: capabilities.flags,
        supported_skip_intervals: capabilities.supported_skip_intervals.clone(),
        supported_playback_rates: capabilities.supported_playback_rates.clone(),
        supported_repeat_modes: capabilities.supported_repeat_modes.clone(),
        custom_extensions: capabilities.custom_extensions.clone(),
    }
}

/// Media Session API example begins here.

struct SinePlayer {
    renderer_proxy: AudioRendererProxy,
    playback_status: PlaybackStatus,
    playback_capabilities: PlaybackCapabilities,
    metadata: Metadata,
}

impl SinePlayer {
    pub fn new() -> Result<Self> {
        let renderer_proxy = renderer_proxy().context("Connecting to audio service.")?;
        load_sine_wave(FREQUENCY, AMPLITUDE, &renderer_proxy)?;
        Ok(Self {
            renderer_proxy,
            playback_status: PlaybackStatus {
                duration: Some(0),
                playback_state: Some(PlaybackState::Stopped),
                playback_function: Some(SinePlayer::timeline_function(PlaybackState::Stopped)),
                repeat_mode: Some(RepeatMode::Single),
                shuffle_on: Some(false),
                has_next_item: Some(false),
                has_prev_item: Some(false),
                error: None,
            },
            playback_capabilities: PlaybackCapabilities {
                flags: Some(
                    PlaybackCapabilityFlags::Play
                        | PlaybackCapabilityFlags::Pause
                        | PlaybackCapabilityFlags::Stop,
                ),
                supported_skip_intervals: Some(vec![]),
                supported_playback_rates: Some(vec![1.0]),
                supported_repeat_modes: Some(vec![RepeatMode::Single]),
                custom_extensions: Some(vec![]),
            },
            metadata: Metadata {
                properties: vec![Property {
                    label: String::from(METADATA_LABEL_ARTIST),
                    value: String::from("Sine"),
                }],
            },
        })
    }

    fn timeline_function(state: PlaybackState) -> TimelineFunction {
        // Timeline functions are used to describe our playback rate and bounds.
        // See the documentation for `fuchsia.mediaplayer.TimelineFunction`.
        if state == PlaybackState::Playing {
            TimelineFunction {
                subject_time: 0,
                reference_time: zx::Time::get(zx::ClockId::Monotonic).nanos(),
                subject_delta: 1,
                reference_delta: 1,
            }
        } else {
            TimelineFunction {
                subject_time: 0,
                reference_time: 0,
                subject_delta: 0,
                reference_delta: 0,
            }
        }
    }

    fn change_playback_state(
        &mut self,
        handle: &SessionControlHandle,
        new_state: PlaybackState,
    ) -> Result<()> {
        let old_state = self.playback_status.playback_state;
        self.playback_status.playback_state = Some(new_state);
        self.playback_status.playback_function = Some(Self::timeline_function(new_state));
        if self.playback_status.playback_state != old_state {
            handle.send_on_playback_status_changed(clone_playback_status(&self.playback_status))?;
        }
        Ok(())
    }

    async fn serve(mut self, server_end: ServerEnd<SessionMarker>) -> Result<()> {
        let (mut requests, handle) = server_end.into_stream_and_control_handle()?;
        self.renderer_proxy.play_no_reply(NO_TIMESTAMP, NO_TIMESTAMP)?;

        // Well behaved `fuchsia.mediasession.Session` implementations always
        // send a complete picture of their status on startup. Here we send
        // our playback capabilities as a player, the metadata for our media,
        // and our playback status.
        handle.send_on_playback_capabilities_changed(clone_playback_capabilities(
            &self.playback_capabilities,
        ))?;
        handle.send_on_metadata_changed(&mut self.metadata)?;
        self.change_playback_state(&handle, PlaybackState::Playing)?;

        let mut payload_i = 0;
        let refresh_per_second: usize = 80;
        let refresh_wait = zx::Duration::from_nanos(1e9 as i64 / (refresh_per_second as i64));
        let payloads_per_burst = (FRAME_RATE as usize) / refresh_per_second;
        let mut ticker = fasync::Interval::new(refresh_wait);

        // Here we select over a timer and any incoming requests to our session.
        // `refresh_per_second` times per second we update our playback based on
        // the state we have.
        loop {
            select! {
                request = requests.try_next() => {
                    if let Some(request) = request? {
                        // Media Session Service has sent us a request from one of the clients
                        // it is multiplexing through our one FIDL connection. We can discard
                        // the ones we don't advertise support for in `PlaybackCapabilities`.
                        match request {
                            SessionRequest::Play { .. } => {
                                self.change_playback_state(&handle, PlaybackState::Playing)?;
                            }
                            SessionRequest::Pause { .. } => {
                                self.change_playback_state(&handle, PlaybackState::Paused)?;
                            }
                            SessionRequest::Stop { .. } => {
                                self.change_playback_state(&handle, PlaybackState::Stopped)?;
                            }
                            _ => ()
                        }
                    }
                },
                timer = ticker.select_next_some() => {
                    if self.playback_status.playback_state == Some(PlaybackState::Playing) {
                        (payload_i..(payload_i + payloads_per_burst)).into_iter().map(
                            |i| Ok(self.renderer_proxy.send_packet_no_reply(&mut sine_wave_payload(i))?)
                        ).collect::<Result<()>>()?;
                        payload_i += payloads_per_burst;
                    } else {
                        // If we are not in the playing state we cease all output immediately.
                        self.renderer_proxy.discard_all_packets_no_reply()?;
                    }
                }
            }
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    // First thing we'll create is two endpoints for a
    // `fuchsia.mediasession.Session` interface. We'll hook the server end up
    // to our player and hand the client end off to the Media Session Service,
    // which will be our sole client.
    let (controller_client_end, controller_server_end) =
        create_endpoints::<SessionMarker>().context("Creating session channels.")?;

    // Here we hand the client side off to Media Session Service via
    // `fuchsia.mediasession.Publisher`. The unique id we get in return can be
    // used to request a client end from Media Session Service, and can be handed
    // off to interested parties.
    let publisher_proxy =
        component::client::connect_to_service::<PublisherMarker>().context("Connecting to publisher.")?;
    let session_id = await!(publisher_proxy.publish(controller_client_end))
        .context("Publishing our session client end.")?;
    println!("Registered with Media Session API. Our id is {:?}.", session_id);

    // Start the player! (Commentary continues in serve().)
    let player = SinePlayer::new().context("Building sine player.")?;
    await!(player.serve(controller_server_end).map_err(|e| eprintln!("{:?}", e)).map(|_| ()));

    Ok(())
}
