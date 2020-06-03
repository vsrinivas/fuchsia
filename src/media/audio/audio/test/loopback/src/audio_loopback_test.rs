// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints;
use fidl_fuchsia_media::AudioSampleFormat;
use fuchsia_syslog::fx_log;
use fuchsia_zircon::{self as zx, Duration, HandleBased, Rights};
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
const MULTIPLE_RENDERERS_COUNT: u16 = 16;
const SAMPLE_RATE: u32 = 48000; // used for playback and capture
const CHANNEL_COUNT: u8 = 1;
const PLAYBACK_SECONDS: u32 = 1;
const PLAYBACK_DATA: [i16; 16] = [
    0x1000, 0xfff, -0x2345, -0x0123, 0x100, 0xff, -0x234, -0x04b7, 0x0310, 0x0def, -0x0101,
    -0x2020, 0x1357, 0x1324, 0x0135, 0x0132,
];
const NUM_SAMPLES_TO_CAPTURE: u64 = 1000;

struct BufferAssets {
    format: AudioStreamType,
    frames: usize,
    sample_size: usize,
    buffer_size: usize,
    vmo: zx::Vmo,
}

impl BufferAssets {
    fn create() -> Result<Self> {
        let format = AudioStreamType {
            sample_format: AudioSampleFormat::Signed16,
            channels: CHANNEL_COUNT as u32,
            frames_per_second: SAMPLE_RATE,
        };
        let sample_size = mem::size_of::<i16>();
        let frames = (format.frames_per_second * PLAYBACK_SECONDS) as usize;
        let buffer_size = frames * format.channels as usize * sample_size;
        let vmo = zx::Vmo::create(buffer_size as u64)?;

        Ok(Self { format, frames, sample_size, buffer_size, vmo })
    }
}

struct RendererAssets {
    renderer: AudioRendererProxy,
}

impl RendererAssets {
    fn setup(audio_core: &AudioCoreProxy, repeated_sample: i16) -> Result<Self> {
        let (renderer, renderer_request) = endpoints::create_proxy()?;
        audio_core.create_audio_renderer(renderer_request)?;

        let mut buffer = BufferAssets::create()?;

        // Write 16-bit repeated_sample to payload_buffer in 8-bit segments,
        // as necessitated by the Vmo type.
        let payload: Vec<u8> = repeat(repeated_sample)
            .take(buffer.frames)
            .map(i16::to_le_bytes)
            .flat_map(SmallVec::from)
            .collect();
        buffer.vmo.write(payload.as_slice(), 0)?;

        renderer.set_pcm_stream_type(&mut buffer.format)?;
        renderer.add_payload_buffer(0, buffer.vmo)?;
        renderer.set_pts_units(SAMPLE_RATE, 1)?;

        // All audio renderers, by default, are set to 0 dB unity gain (passthru).

        Ok(Self { renderer })
    }
}

struct CapturerAssets {
    capturer: AudioCapturerProxy,
    capture_buffer: zx::Vmo,
    buffer_size: usize,
    capture_sample_size: u64,
}

impl CapturerAssets {
    fn setup(audio_core: &AudioCoreProxy) -> Result<Self> {
        let (capturer, capturer_request) = endpoints::create_proxy()?;
        audio_core.create_audio_capturer(true, capturer_request)?;

        let mut buffer = BufferAssets::create()?;

        let capture_buffer = buffer.vmo.duplicate_handle(Rights::SAME_RIGHTS)?;
        let buffer_size = buffer.buffer_size;
        let capture_sample_size = buffer.sample_size as u64;

        capturer.set_pcm_stream_type(&mut buffer.format)?;
        capturer.add_payload_buffer(0, buffer.vmo)?;

        // All audio capturers, by default, are set to 0 dB unity gain (passthru).

        Ok(Self { capturer, capture_buffer, buffer_size, capture_sample_size })
    }

    // Read and combine two 8-bit results to create the 16-bit packet data.
    fn read_capture_buffer(&self, index: u64) -> Result<i16> {
        let mut recv = vec![0; 2];
        self.capture_buffer.read(&mut recv[..], index)?;
        Ok((recv[1] as i16) << 8 | recv[0] as i16)
    }

    // Find the true offset of the data within the capture buffer.
    fn find_packet_data_offset(&self, expected_val: i16, offset: u64) -> Result<Option<u64>> {
        let num_frames = (NUM_SAMPLES_TO_CAPTURE * self.capture_sample_size) as usize;
        let mut recv = vec![0; num_frames];
        self.capture_buffer.read(&mut recv[..], offset)?;

        for i in 0..num_frames - 1 {
            let captured_val = (recv[i + 1] as i16) << 8 | recv[i] as i16;
            if captured_val == expected_val {
                return Ok(Some(offset + i as u64));
            }
        }

        Ok(None)
    }
}

async fn loopback_test(num_renderers: u16) -> Result<()> {
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

            let mut capture_packet = StreamPacket {
                pts: 0,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: capturer_assets.buffer_size as u64,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            };
            let mut play_delay: sys::zx_duration_t = 0;
            let mut expected_val: i16 = 0;
            let mut renderer_list: Vec<AudioRendererProxy> = Vec::new();

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

                // Send packet with the specifications of the packet to be captured.
                renderer_assets.renderer.send_packet_no_reply(&mut capture_packet.clone())?;
                renderer_list.push(renderer_assets.renderer);
            }

            // Prepare to handle future events of a packet being produced before async capture
            // is begun; this prevents us from producing a packet before we are ready to capture it.
            let mut packet_produced_event =
                capturer_assets.capturer.take_event_stream().try_filter_map(move |e| {
                    future::ready(Ok(AudioCapturerEvent::into_on_packet_produced(e)))
                });

            // Set timestamp for renderers to play. Timestamp starts now and includes a play_delay
            // of the min_lead_time.
            let play_at: i64 = zx::Time::get(zx::ClockId::Monotonic).into_nanos() + play_delay;

            // Record the ref_time and media_time for one renderer -- arbitrarily, renderer 0.
            // Use the returned ref_time and media_time to synchronize all renderer playback.
            renderer_list[0].play(play_at, 0).await.map(|times| {
                let (ref_time_received, media_time_received) = times;
                assert_eq!(media_time_received, 0);
                assert_eq!(ref_time_received, play_at);

                // Start the other renderers at exactly the same [ref_time, media_time] correspondence.
                for renderer_num in 1..num_renderers {
                    renderer_list[renderer_num as usize]
                        .play_no_reply(ref_time_received, media_time_received)
                        .expect("To play renderer");
                }

                // Begin capture of NUM_SAMPLES_TO_CAPTURE samples of audio.
                capturer_assets
                    .capturer
                    .start_async_capture(NUM_SAMPLES_TO_CAPTURE as u32)
                    .expect("To start async capture");
            })?;

            // Await captured packets, and stop async capture after all valid frames are received.
            let mut received_first_packet = false;
            let mut received_all_expected_data = false;
            let mut true_offset = 0;
            let mut captured_frames = 0;
            while let Ok(Some(packet)) = packet_produced_event.try_next().await {
                if !received_first_packet {
                    if let Some(offset) = capturer_assets
                        .find_packet_data_offset(expected_val, packet.payload_offset)?
                    {
                        true_offset = offset;
                        let packet_data = capturer_assets.read_capture_buffer(true_offset)?;
                        fx_log!(
                            fuchsia_syslog::levels::INFO,
                            "Captured packet: pts {}, start {}, size {}, data {}",
                            packet.pts,
                            true_offset,
                            packet.payload_size,
                            packet_data
                        );
                        received_first_packet = true;
                        capture_packet = packet;
                        captured_frames = packet.payload_size;
                    }
                } else {
                    captured_frames += packet.payload_size;
                }

                // Check that we've received all expected frames, then break.
                if captured_frames == SAMPLE_RATE as u64 * capturer_assets.capture_sample_size {
                    capturer_assets.capturer.stop_async_capture_no_reply()?;
                    received_all_expected_data = true;
                    break;
                }
            }
            assert!(received_all_expected_data);

            // Check that we captured NUM_SAMPLES_TO_CAPTURE, as expected.
            assert_eq!(
                capture_packet.payload_size / capturer_assets.capture_sample_size,
                NUM_SAMPLES_TO_CAPTURE
            );

            // Ensure actual playback timestamp is no more than 100ms off of set |play_at| timestamp.
            //
            // TODO(fxb/50022) Revise this loose timestamp comparison to an assertion of (near)
            // equivalence between |capture_packet.pts| and |play_at|.
            let pts_offset = (true_offset - capture_packet.payload_offset) as i64 * 10000;
            assert!(
                (capture_packet.pts + pts_offset - play_at).abs()
                    < Duration::from_millis(100).into_nanos()
            );

            // Check that all samples contain the expected data.
            let total_samples = NUM_SAMPLES_TO_CAPTURE * capturer_assets.capture_sample_size;
            for i in (0..total_samples).step_by(2) {
                let index = true_offset + i;
                let actual_val: i16 = capturer_assets.read_capture_buffer(index)?;
                assert_eq!(expected_val, actual_val);
            }

            Ok(())
        }
    })
    .await
}

// TODO(49807): This test should automatically fail if underflows are detected. That functionality
// should be ported from HermeticAudioTest (C++) to here.

// Test Cases
//
// Create one output stream and one loopback capture to verify that the capturer receives
// what the renderer sent out.
#[fasync::run_singlethreaded]
#[test]
async fn single_stream_test() -> Result<()> {
    loopback_test(1).await
}

// Verify loopback capture of the output mix of multiple renderer streams.
#[fasync::run_singlethreaded]
#[test]
async fn multiple_stream_test() -> Result<()> {
    loopback_test(MULTIPLE_RENDERERS_COUNT).await
}
