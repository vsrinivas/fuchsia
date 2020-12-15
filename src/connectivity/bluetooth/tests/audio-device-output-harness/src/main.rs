// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context,
    fidl_fuchsia_media::{AudioChannelId, AudioDeviceEnumeratorMarker, AudioPcmMode, PcmFormat},
    fuchsia_async as fasync,
    fuchsia_async::futures::StreamExt,
    fuchsia_audio_device_output::driver::SoftPcmOutput,
    fuchsia_zircon::DurationNum,
    log::info,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init().expect("Unable to initialize logger");
    const LOCAL_MONOTONIC_CLOCK_DOMAIN: u32 = 0;
    let pcm_format = PcmFormat {
        pcm_mode: AudioPcmMode::Linear,
        bits_per_sample: 16,
        frames_per_second: 48000,
        channel_map: vec![AudioChannelId::Lf],
    };

    let id = [2; 16];
    let (client, mut frame_stream) = SoftPcmOutput::build(
        &id,
        "Fuchsia",
        "AudioOutHarness",
        LOCAL_MONOTONIC_CLOCK_DOMAIN,
        pcm_format,
        12.millis(),
    )?;

    let svc = fuchsia_component::client::connect_to_service::<AudioDeviceEnumeratorMarker>()
        .context("Failed to connect to AudioDeviceEnumerator")?;
    svc.add_device_by_channel2("AudioOutHarness", false, client)?;

    info!("Starting frame stream read loop");
    while let Some(Ok(_frame)) = frame_stream.next().await {}
    info!("Frame stream loop done");

    Ok(())
}
