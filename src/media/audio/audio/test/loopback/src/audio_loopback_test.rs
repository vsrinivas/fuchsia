// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints;
use fidl_fuchsia_media::AudioSampleFormat;
use fuchsia_syslog::{fx_log, init_with_tags};
use fuchsia_zircon::{self as zx, HandleBased, Rights};
use fuchsia_zircon_sys as sys;
use hermetic_audio_environment::prelude::*;
use hermetic_audio_environment::virtual_audio::{with_connected_device, DeviceTestAssets};
use smallvec::SmallVec;
use std::cmp::max;
use std::iter::repeat;
use std::mem;

// Values found in:
//    zircon/system/public/zircon/device/audio.h
const AUDIO_SAMPLE_FORMAT_16BIT: u32 = 1 << 2;
const ASF_RANGE_FLAG_FPS_CONTINUOUS: u16 = 1 << 0;

// Test constants
const SAMPLE_RATE: u32 = 48000; // used for playback and capture
const CHANNEL_COUNT: u8 = 1;
const PLAYBACK_SECONDS: u32 = 1;
const PLAYBACK_DATA: [i16; 16] = [
    0x1000, 0xfff, -0x2345, -0x0123, 0x100, 0xff, -0x234, -0x04b7, 0x0310, 0x0def, -0x0101,
    -0x2020, 0x1357, 0x1324, 0x0135, 0x0132,
];
const NUM_SAMPLES_TO_CAPTURE: u32 = 1000;

pub struct RendererAssets {
    renderer: AudioRendererProxy,
    playback_size: usize,
}

impl RendererAssets {
    pub fn setup(audio_core: &AudioCoreProxy, repeated_sample: i16) -> Result<Self> {
        let (renderer, renderer_request) = endpoints::create_proxy()?;
        audio_core.create_audio_renderer(renderer_request)?;

        let mut format = AudioStreamType {
            sample_format: AudioSampleFormat::Signed16,
            channels: CHANNEL_COUNT as u32,
            frames_per_second: SAMPLE_RATE,
        };

        let playback_sample_size = mem::size_of::<i16>();
        let playback_size = format.frames_per_second as usize
            * format.channels as usize
            * playback_sample_size
            * PLAYBACK_SECONDS as usize;

        // Write 16-bit repeated_sample to payload_buffer in 8-bit segments,
        // as necessitated by the Vmo type.
        let payload_vmo = zx::Vmo::create(playback_size as u64)?;
        let payload: Vec<u8> = repeat(repeated_sample)
            .take((SAMPLE_RATE * PLAYBACK_SECONDS) as usize)
            .map(i16::to_le_bytes)
            .flat_map(SmallVec::from)
            .collect();
        payload_vmo.write(payload.as_slice(), 0)?;

        renderer.set_pcm_stream_type(&mut format)?;
        renderer.add_payload_buffer(0, payload_vmo)?;

        // All audio renderers, by default, are set to 0 dB unity gain (passthru).

        Ok(Self { renderer, playback_size })
    }
}

pub struct CapturerAssets {
    capturer: AudioCapturerProxy,
    capture_buffer: zx::Vmo,
    capture_sample_size: usize,
}

impl CapturerAssets {
    pub fn setup(audio_core: &AudioCoreProxy) -> Result<Self> {
        let (capturer, capturer_request) = endpoints::create_proxy()?;
        audio_core.create_audio_capturer(true, capturer_request)?;

        let mut format = AudioStreamType {
            sample_format: AudioSampleFormat::Signed16,
            channels: CHANNEL_COUNT as u32,
            frames_per_second: SAMPLE_RATE,
        };

        let capture_sample_size = mem::size_of::<i16>();
        let capture_frames = (format.frames_per_second * PLAYBACK_SECONDS) as usize;
        let capture_size = capture_frames * format.channels as usize * capture_sample_size * 2;

        let capture_vmo = zx::Vmo::create(capture_size as u64)?;
        let capture_buffer = capture_vmo.duplicate_handle(Rights::SAME_RIGHTS)?;

        capturer.set_pcm_stream_type(&mut format)?;
        capturer.add_payload_buffer(0, capture_vmo)?;

        // All audio capturers, by default, are set to 0 dB unity gain (passthru).

        Ok(Self { capturer, capture_buffer, capture_sample_size })
    }
}

pub async fn loopback_test(num_renderers: u16) -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<OutputProxy>| {
        async move {
            // 'Plug in' output device for use in loopback capture.
            assets.device.change_plug_state(
                zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                /*plugged=*/ true,
            )?;

            // To minimize any potential change in data, the virtual audio output device's format
            // should exactly match the format being sent and received.
            // Each virtual audio device has one format range by default, so it must be removed
            // before adding the format range needed.
            assets.device.clear_format_ranges()?;
            assets.device.add_format_range(
                AUDIO_SAMPLE_FORMAT_16BIT,
                SAMPLE_RATE,
                SAMPLE_RATE,
                CHANNEL_COUNT,
                CHANNEL_COUNT,
                ASF_RANGE_FLAG_FPS_CONTINUOUS,
            )?;

            // Connect to audio_core and initialize capturer.
            let audio_core = assets.env.connect_to_service::<AudioCoreMarker>()?;
            let capturer_assets = CapturerAssets::setup(&audio_core)?;

            let mut packet: Vec<StreamPacket> = Vec::new();
            let mut capture_packet = StreamPacket {
                pts: NO_TIMESTAMP,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: 0,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            };

            let mut play_delay: sys::zx_duration_t = 0;
            let mut expected_val: i16 = 0;
            let mut renderer_list: Vec<RendererAssets> = Vec::new();

            // Set up playback streams, including determining necessary lead time and
            // sending packets.
            for renderer_num in 0..num_renderers {
                let index = renderer_num as usize;
                let renderer_assets = RendererAssets::setup(&audio_core, PLAYBACK_DATA[index])?;
                expected_val += PLAYBACK_DATA[index];

                // Get the expected duration from packet submittal to the beginning of
                // capturing what was sent on the loopback interface.
                // Use the largest 'min_lead_time' across all renderers.
                renderer_assets
                    .renderer
                    .get_min_lead_time()
                    .await
                    .map(|min_lead_time| play_delay = max(play_delay, min_lead_time))?;

                packet.push(StreamPacket {
                    pts: NO_TIMESTAMP,
                    payload_buffer_id: 0,
                    payload_offset: 0,
                    payload_size: renderer_assets.playback_size as u64,
                    flags: 0,
                    buffer_config: 0,
                    stream_segment_id: 0,
                });

                renderer_assets.renderer.send_packet_no_reply(&mut packet[index])?;
                renderer_list.push(renderer_assets);
            }

            // Begin capture of NUM_SAMPLES_TO_CAPTURE samples of audio.
            capturer_assets.capturer.start_async_capture(NUM_SAMPLES_TO_CAPTURE)?;

            // Set timestamp for renderers to play. Timestamp starts now and includes a play_delay
            // of the min_lead_time.
            let play_at: i64 = zx::Time::get(zx::ClockId::Monotonic).into_nanos() + play_delay;

            // Record the ref_time and media_time for one renderer -- arbitrarily, renderer 0.
            // Use the returned ref_time and media_time to synchronize all renderer playback.
            let mut times_received: (i64, i64) = (-1, -1);
            renderer_list[0].renderer.play(play_at, 0).await.map(|times| times_received = times)?;

            let (_ref_time_received, _media_time_received) = times_received;
            assert_eq!(_media_time_received, 0);
            assert_eq!(_ref_time_received, play_at);

            // Start the other renderers at exactly the same [ref_time, media_time] correspondence.
            for renderer_num in 1..num_renderers {
                renderer_list[renderer_num as usize]
                    .renderer
                    .play_no_reply(_ref_time_received, _media_time_received)?;
            }

            // Await captured packets, and stop async capture after second valid packet received.
            // The first captured packet is from continuous play of the first renderer, and the
            // second captured packet provides the synchronized playback of the remaining renderers.
            let mut received_packet = false;
            while let Ok(Some(packet)) = capturer_assets
                .capturer
                .take_event_stream()
                .try_filter_map(move |e| {
                    future::ready(Ok(AudioCapturerEvent::into_on_packet_produced(e)))
                })
                .try_next()
                .await
            {
                let offset = packet.payload_offset;
                let mut packet_data = vec![0; 2];
                capturer_assets.capture_buffer.read(&mut packet_data[..], offset)?;

                let packet_data: i16 = (packet_data[1] as i16) << 8 | packet_data[0] as i16;
                if packet_data == expected_val && packet.payload_size != 0 {
                    fx_log!(
                        fuchsia_syslog::levels::INFO,
                        "Capturing packet: pts {}, start {}, size {}, data {}",
                        packet.pts,
                        packet.payload_offset,
                        packet.payload_size,
                        packet_data
                    );
                    received_packet = true;
                    capture_packet = packet;
                    capturer_assets.capturer.stop_async_capture_no_reply()?;
                    break;
                }
            }
            assert!(received_packet);

            // Check that we captured NUM_SAMPLES_TO_CAPTURE, as expected.
            assert_eq!(
                capture_packet.payload_size as u32 / capturer_assets.capture_sample_size as u32,
                NUM_SAMPLES_TO_CAPTURE
            );

            // Check that all samples contain the expected data.
            // Read and combine two 8-bit results to compare against 16-bit expected value,
            // as necessitated by the Vmo type.
            for i in (0..NUM_SAMPLES_TO_CAPTURE).step_by(2) {
                let index = capture_packet.payload_offset + i as u64;
                let mut recv = vec![0; 2];
                capturer_assets.capture_buffer.read(&mut recv[..], index)?;

                let actual_val: i16 = (recv[1] as i16) << 8 | recv[0] as i16;
                assert_eq!(actual_val, expected_val);
            }

            Ok(())
        }
    })
    .await
}

// Test Cases
//
// Create one output stream and one loopback capture to verify that the capturer receives
// what the renderer sent out.
#[fasync::run_singlethreaded]
#[test]
async fn single_stream_test() -> Result<()> {
    init_with_tags(&["audio_loopback_test"])?;
    loopback_test(1).await
}

// Verify loopback capture of the output mix of 2 renderer streams.
#[fasync::run_singlethreaded]
#[test]
async fn multiple_stream_test() -> Result<()> {
    loopback_test(2).await
}
