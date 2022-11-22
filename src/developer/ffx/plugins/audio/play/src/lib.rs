// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::ffx_bail,
    ffx_audio_play_args::{PlayCommand, RenderCommand, SubCommand},
    ffx_core::ffx_plugin,
    fidl_fuchsia_audio_ffxdaemon::{
        AudioDaemonCreateAudioRendererRequest, AudioDaemonProxy, VmoWrapperWriteRequest,
        MAX_AUDIO_BUFFER_BYTES,
    },
    fidl_fuchsia_io as _,
    fidl_fuchsia_media::{
        AudioRendererEvent::OnMinLeadTimeChanged, AudioSampleFormat, AudioStreamType,
    },
    fidl_fuchsia_media_audio as _, fuchsia_async as _,
    futures::TryStreamExt,
    hound::{SampleFormat, WavReader},
    std::{cell::RefCell, cmp::min, collections::VecDeque, io, ops::Range, rc::Rc, sync::mpsc},
    tokio as _,
};

#[ffx_plugin(
    "audio",
    AudioDaemonProxy = "core/audio_ffx_daemon:expose:fuchsia.audio.ffxdaemon.AudioDaemon"
)]
pub async fn play_cmd(audio_proxy: AudioDaemonProxy, cmd: PlayCommand) -> Result<()> {
    match cmd.subcommand {
        SubCommand::Render(renderer_command) => {
            renderer_play(audio_proxy, renderer_command).await?
        }
    }
    Ok(())
}

fn get_sample_format_from_file(
    bits_per_sample: u16,
    sample_format: SampleFormat,
) -> AudioSampleFormat {
    match sample_format {
        SampleFormat::Int => {
            if bits_per_sample == 8 {
                AudioSampleFormat::Unsigned8
            } else if bits_per_sample <= 16 {
                AudioSampleFormat::Signed16
            } else {
                AudioSampleFormat::Signed24In32
            }
        }
        SampleFormat::Float => AudioSampleFormat::Float,
    }
}

pub async fn renderer_play(audio_proxy: AudioDaemonProxy, cmd: RenderCommand) -> Result<()> {
    // Channels for sending WAV Header and WAV data. Need WAV Header first to determine buffer size,
    // then read WAV data later on demand as we send packets.

    let (tx_header, rx_header) = mpsc::channel();
    let (tx_data, rx_data) = mpsc::channel();

    // Create separate thread for reading from stdin. WavReader reads header first, then
    // on-demand. Use one wave reader since data (stdinlock) cannot be synced across threads.
    let t = std::thread::spawn(move || {
        let mut executor = fuchsia_async::LocalExecutor::new().unwrap();
        executor.run_singlethreaded(async move {
            let mut reader = WavReader::new(io::stdin().lock()).unwrap();
            let spec = reader.spec();

            let buffer_size_bytes =
                spec.sample_rate as u64 * audio_utils::bytes_per_frame(spec) as u64;
            let bytes_per_packet = min(buffer_size_bytes / 2, MAX_AUDIO_BUFFER_BYTES as u64);
            let frames_per_packet = bytes_per_packet / audio_utils::bytes_per_frame(spec) as u64;

            let samples_per_chunk: usize = (frames_per_packet * spec.channels as u64) as usize;

            tx_header.send(reader.spec()).unwrap();

            println!(
                "Samples per chunk from stdin: {}. \n Expecting to send {} packets.",
                samples_per_chunk,
                audio_utils::packets_per_file(&reader)
            );

            // TODO(camlloyd): Support floating type as well.
            let mut samples = reader.samples::<i32>().map(|r| r.unwrap());

            let mut index: i64 = 0;
            let mut buffer = Vec::with_capacity(samples_per_chunk as usize);

            // TODO(camlloyd): Use iter_next_chunk #98326 when available. Nightly only right now.
            // TODO(camlloyd): Use buffering to send on tx_data. Currently sends all at once.
            while let Some(sample) = samples.next() {
                index = index + 1;
                if spec.bits_per_sample > 16 {
                    buffer.extend_from_slice(&sample.to_ne_bytes());
                } else if spec.bits_per_sample > 8 {
                    let sample16 = (sample & 0xFFFF) as i16;
                    buffer.extend_from_slice(&sample16.to_ne_bytes());
                } else {
                    let sample_signed = (sample & 0xFF) as i16;
                    let sample8_unsigned = (sample_signed + 128) as u8;
                    buffer.push(sample8_unsigned);
                }

                if index.checked_rem(samples_per_chunk as i64) == Some(0) {
                    tx_data.send(buffer).unwrap();
                    buffer = Vec::with_capacity(MAX_AUDIO_BUFFER_BYTES as usize);
                }
            }
            // Need to send remaining samples that didn't fill whole chunk.
            if buffer.len() > 0 {
                tx_data.send(buffer).unwrap();
            }
        });
    });

    // Get WavSpec in main thread.
    let mut iter = rx_header.iter();
    let spec = iter.next().ok_or(anyhow!("Failed to read WavSpec from header"))?;

    // size of buffer is large enough for packet release delay + ffx logic compute time
    // don't know lead time until after created renderer - but we can make a ~probably sufficient buffer
    // buffer large enough for 1 second. of audio 48000 * 4bytes * 2 = 500kb frame_rate * byte_per_frame * channel  / second (1 second)
    // lead times should be less than 1 second, so this buffer size should be enough

    let buffer_size_bytes = spec.sample_rate as u64 * audio_utils::bytes_per_frame(spec) as u64;
    let bytes_per_packet = min(buffer_size_bytes / 2, MAX_AUDIO_BUFFER_BYTES as u64);
    let frames_per_packet = bytes_per_packet / audio_utils::bytes_per_frame(spec) as u64;

    println!(
        "Size of vmo: {}, bytes_per_packet {}, frames_per_packet {}",
        buffer_size_bytes, bytes_per_packet, frames_per_packet
    );

    // CreateAudioDaemon returns a client end to an AudioRenderer channel and a VMOProxy channel.
    // Payload buffer has already been added.
    let request = AudioDaemonCreateAudioRendererRequest {
        buffer_size: Some(buffer_size_bytes),
        ..AudioDaemonCreateAudioRendererRequest::EMPTY
    };

    let (renderer_client_end, vmo_client_end, gain_control_client_end) = match audio_proxy
        .create_audio_renderer(request)
        .await
        .context("Error sending request to create audio renderer")?
    {
        Ok(value) => (
            value
                .renderer
                .context("Failed to retrieve client end of AudioRenderer from response")?,
            value
                .vmo_channel
                .context("Failed to retrieve client end of VmoWrapper from response")?,
            value
                .gain_control
                .context("Failed to retrieve gain control client end from response")?,
        ),
        Err(err) => ffx_bail!("Create audio renderer returned error {}", err),
    };

    let renderer_proxy = Rc::new(renderer_client_end.into_proxy()?);

    // Settings for AudioRenderer, from command parameters or WavSpec of stdin wav file.
    renderer_proxy.set_usage(cmd.usage)?;
    renderer_proxy.set_pcm_stream_type(&mut AudioStreamType {
        sample_format: get_sample_format_from_file(spec.bits_per_sample, spec.sample_format),
        channels: spec.channels as u32,
        frames_per_second: spec.sample_rate,
    })?;

    let gain_control_proxy = gain_control_client_end.into_proxy()?;

    gain_control_proxy.set_gain(
        cmd.gain
            .clamp(fidl_fuchsia_media_audio::MUTED_GAIN_DB, fidl_fuchsia_media_audio::MAX_GAIN_DB),
    )?;
    gain_control_proxy.set_mute(cmd.mute)?;

    renderer_proxy.enable_min_lead_time_events(true)?;

    // Wait for AudioRenderer to initialize (lead_time > 0)
    let mut stream = renderer_proxy.take_event_stream();
    while let Some(event) = stream.try_next().await? {
        println!("{:?}", event);
        match event {
            OnMinLeadTimeChanged { min_lead_time_nsec } => {
                if min_lead_time_nsec > 0 {
                    break;
                }
            }
        }
    }

    let available_packets = Rc::new(RefCell::new(VecDeque::new()));
    let num_packets = audio_utils::packets_per_second(spec);

    for k in 0..num_packets {
        let offset = k as u64 * bytes_per_packet as u64;
        available_packets
            .borrow_mut()
            .push_back(Range { start: offset, end: offset as u64 + bytes_per_packet as u64 });
    }

    let vmo_proxy = Rc::new(vmo_client_end.into_proxy()?);

    let semaphore = Rc::new(tokio::sync::Semaphore::new(num_packets as usize));
    let mut num_packets_so_far = 0;

    let futs = rx_data.into_iter().map(|received| {
        num_packets_so_far += 1;
        let vmo = Rc::clone(&vmo_proxy);
        let renderer = Rc::clone(&renderer_proxy);
        let available_packets = Rc::clone(&available_packets);
        let semaphore = Rc::clone(&semaphore);

        async move {
            let _permit = semaphore.acquire().await?;
            let payload_size = received.len();
            let offset = available_packets.borrow_mut().pop_front().unwrap();

            let request = VmoWrapperWriteRequest {
                data: Some(received),
                offset: Some(offset.start as u64),
                ..VmoWrapperWriteRequest::EMPTY
            };
            let _written = vmo.write(request).await?;

            let fut = renderer.send_packet(&mut fidl_fuchsia_media::StreamPacket {
                pts: fidl_fuchsia_media::NO_TIMESTAMP,
                payload_buffer_id: 0,
                payload_offset: offset.start as u64,
                payload_size: payload_size as u64,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            });

            if num_packets_so_far == 1 {
                renderer
                    .play(fidl_fuchsia_media::NO_TIMESTAMP, fidl_fuchsia_media::NO_TIMESTAMP)
                    .await?;
            }
            fut.await?;
            available_packets.borrow_mut().push_back(offset);

            Ok::<(), anyhow::Error>(())
        }
    });

    let res: Vec<()> = futures::future::try_join_all(futs).await?;
    println!("Got {} responses for packets sent.", res.len());
    t.join().unwrap(); // Make sure stdin thread gets to execute fully.

    Ok(())
}
