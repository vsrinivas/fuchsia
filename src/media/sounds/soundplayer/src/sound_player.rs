// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod wav_reader;

use crate::Result;
use anyhow::Context as _;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sounds::*;
use fuchsia_component as component;
use fuchsia_zircon::{self as zx, HandleBased};
use futures::{channel::oneshot, future::FutureExt, prelude::*, select, stream::FuturesUnordered};
use std::{collections::HashMap, io::BufReader};

/// Implements `fuchsia.media.sounds.Player`.
pub struct SoundPlayer {
    sounds_by_id: HashMap<u32, Sound>,
}

impl SoundPlayer {
    pub fn new() -> Self {
        Self { sounds_by_id: HashMap::new() }
    }

    pub async fn serve(mut self, mut request_stream: PlayerRequestStream) -> Result<()> {
        let mut futures = FuturesUnordered::new();

        loop {
            select! {
                request = request_stream.select_next_some() => {
                    match request? {
                        PlayerRequest::AddSoundFromFile { id, file, responder } => {
                            match Sound::from_file_channel(id, file) {
                                Ok(sound) => {
                                    let duration = sound.duration();
                                    if self.sounds_by_id.insert(id, sound).is_some() {
                                        fuchsia_syslog::fx_log_err!("AddSound called with id already in use {}", id);
                                        return Err(anyhow::format_err!("Client error, disconnecting"));
                                    } else {
                                        responder.send(&mut Ok(duration.into_nanos())).unwrap_or(());
                                    }
                                },
                                Err(status) => {
                                    fuchsia_syslog::fx_log_info!("Failed to add sound: {}", status);
                                    responder.send(&mut Err(status.into_raw())).unwrap_or(());
                                },
                            }
                        }
                        PlayerRequest::AddSoundBuffer { id, buffer, stream_type, control_handle} => {
                            if self.sounds_by_id.insert(id, Sound::new(id, buffer, stream_type)).is_some() {
                                fuchsia_syslog::fx_log_err!("AddSound called with id already in use {}", id);
                                return Err(anyhow::format_err!("Client error, disconnecting"));
                            }
                        }
                        PlayerRequest::RemoveSound { id, control_handle } => {
                            if self.sounds_by_id.remove(&id).is_none() {
                                fuchsia_syslog::fx_log_warn!("RemoveSound called with unrecognized id {}", id);
                            }
                        }
                        PlayerRequest::PlaySound { id, usage, responder } => {
                            if let Some(mut sound) = self.sounds_by_id.get_mut(&id) {
                                if let Ok(renderer) = Renderer::new(usage) {
                                    match renderer.prepare_packets(&sound) {
                                        Ok(packets) => {
                                            let (sender, receiver) = oneshot::channel::<()>();
                                            sound.stop_sender.replace(sender);
                                            futures.push((renderer.play_packets(packets, receiver)
                                                .map(move |mut result| {
                                                    responder.send(&mut result).unwrap_or(());
                                                })).boxed())
                                        }
                                        Err(error) => {
                                            fuchsia_syslog::fx_log_err!("Unable to play sound {}: {}", id, error);
                                            responder.send(&mut Err(PlaySoundError::RendererFailed)).unwrap_or(());
                                        }
                                    }
                                } else {
                                    responder.send(&mut Err(PlaySoundError::RendererFailed)).unwrap_or(());
                                }
                            } else {
                                responder.send(&mut Err(PlaySoundError::NoSuchSound)).unwrap_or(());
                            }
                        }
                        PlayerRequest::StopPlayingSound { id, control_handle } => {
                            if let Some(mut sound) = self.sounds_by_id.get_mut(&id) {
                                if sound.stop_sender.is_some() {
                                    sound.stop_sender.take().unwrap().send(()).unwrap_or(());
                                }
                            }
                        }
                    };
                },
                _ = futures.next() => {}
                complete => break,
            }
        }

        Ok(())
    }
}

struct Sound {
    id: u32,
    vmo: zx::Vmo,
    size: u64,
    stream_type: AudioStreamType,
    stop_sender: Option<oneshot::Sender<()>>,
}

impl Sound {
    fn new(id: u32, buffer: fidl_fuchsia_mem::Buffer, stream_type: AudioStreamType) -> Self {
        Self { id, vmo: buffer.vmo, size: buffer.size, stream_type, stop_sender: None }
    }

    fn from_file_channel(
        id: u32,
        file_channel: fidl::endpoints::ClientEnd<fidl_fuchsia_io::FileMarker>,
    ) -> std::result::Result<Self, zx::Status> {
        let wav = wav_reader::WavReader::read(BufReader::new(fdio::create_fd::<std::fs::File>(
            file_channel.into_handle(),
        )?))
        .map_err(|_| zx::Status::INVALID_ARGS)?;

        Ok(Self {
            id,
            vmo: wav.vmo,
            size: wav.size,
            stream_type: wav.stream_type,
            stop_sender: None,
        })
    }

    fn frame_size(&self) -> u32 {
        let bytes_per_sample = match self.stream_type.sample_format {
            AudioSampleFormat::Unsigned8 => 1,
            AudioSampleFormat::Signed16 => 2,
            AudioSampleFormat::Signed24In32 => 4,
            AudioSampleFormat::Float => 4,
        };

        bytes_per_sample * self.stream_type.channels
    }

    fn frame_count(&self) -> u64 {
        self.size / self.frame_size() as u64
    }

    fn duration(&self) -> zx::Duration {
        zx::Duration::from_nanos(
            ((self.frame_count() * 1000000000) / self.stream_type.frames_per_second as u64) as i64,
        )
    }
}

struct Renderer {
    proxy: AudioRendererProxy,
}

impl Renderer {
    fn new(usage: AudioRenderUsage) -> Result<Self> {
        let (client_endpoint, server_endpoint) = create_endpoints::<AudioRendererMarker>()
            .context("Creating renderer channel endpoints.")?;

        component::client::connect_to_service::<AudioMarker>()
            .context("Connecting to fuchsia.media.Audio")?
            .create_audio_renderer(server_endpoint)
            .context("Creating audio renderer")?;

        let new_self = Self { proxy: client_endpoint.into_proxy()? };
        new_self.proxy.set_usage(usage)?;
        Ok(new_self)
    }

    fn prepare_packets(&self, sound: &Sound) -> Result<Vec<StreamPacket>> {
        self.proxy.set_pcm_stream_type(&mut sound.stream_type.clone())?;

        self.proxy.add_payload_buffer(
            sound.id,
            sound.vmo.duplicate_handle(zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP)?,
        )?;

        let mut result = Vec::new();
        let mut frames_remaining = sound.frame_count();
        let mut offset = 0;

        while frames_remaining != 0 {
            let frames_to_send =
                std::cmp::min(frames_remaining, MAX_FRAMES_PER_RENDERER_PACKET as u64);

            result.push(StreamPacket {
                pts: NO_TIMESTAMP,
                payload_buffer_id: sound.id,
                payload_offset: offset,
                payload_size: frames_to_send * sound.frame_size() as u64,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            });

            frames_remaining -= frames_to_send;
            offset += frames_to_send * sound.frame_size() as u64;
        }

        Ok(result)
    }

    async fn play_packets(
        self,
        mut packets: Vec<StreamPacket>,
        stop_receiver: oneshot::Receiver<()>,
    ) -> PlayerPlaySoundResult {
        // We're discarding |self| when this method returns, so there's no need
        // to clean up (e.g. remove the payload buffer, pause playback). If
        // we wanted to cache renderers, we'd need to add that.

        let packets_len = packets.len();
        if packets_len > 1 {
            for index in 0..packets_len - 1 {
                self.proxy.send_packet_no_reply(&mut packets[index]).map_err(|e| {
                    fuchsia_syslog::fx_log_err!("AudioRenderer.SendPacketNoReply failed: {}", e);
                    PlaySoundError::RendererFailed
                })?;
            }
        }

        let mut send_packet = self.proxy.send_packet(&mut packets[packets_len - 1]).map_err(|e| {
            fuchsia_syslog::fx_log_err!("AudioRenderer.SendPacket failed: {}", e);
            e
        });

        let mut play = self.proxy.play(NO_TIMESTAMP, 0).map_err(|e| {
            fuchsia_syslog::fx_log_err!("AudioRenderer.Play failed: {}", e);
            e
        });

        let mut stop_receiver = stop_receiver.fuse();

        let mut completed_one = false;

        // Wait for send_packet and play to complete, or for stop_receiver to receive successfully.
        loop {
            select! {
                result = send_packet => {
                    if result.is_err() {
                        return Err(PlaySoundError::RendererFailed);
                    }
                    if (completed_one) {
                        return Ok(());
                    }
                    completed_one = true;
                }
                result = play => {
                    if result.is_err() {
                        return Err(PlaySoundError::RendererFailed);
                    }
                    if (completed_one) {
                        return Ok(());
                    }
                    completed_one = true;
                }
                result = stop_receiver => {
                    // stop_recevier will return an error if the sender is discarded. This will
                    // happen if the same sound is played again while this one is still playing.
                    if result.is_ok() {
                        return Err(PlaySoundError::Stopped);
                    }
                }
            }
        }
    }
}
