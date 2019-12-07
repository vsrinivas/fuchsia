// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod wav_reader;

use crate::Result;
use failure::ResultExt;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sounds::*;
use fuchsia_component as component;
use fuchsia_zircon::{self as zx, HandleBased};
use futures::{self, join, lock::Mutex, prelude::*, stream::FuturesUnordered};
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
        let saved_renderer: Mutex<Option<Renderer>> = Mutex::new(None);
        let mut futures = FuturesUnordered::new();

        loop {
            futures::select! {
                request = request_stream.select_next_some() => {
                    match request? {
                        PlayerRequest::AddSoundFromFile { id, file_channel, responder} => {
                            match Sound::from_file_channel(id, file_channel) {
                                Ok(sound) => {
                                    if self.sounds_by_id.insert(id, sound).is_some() {
                                        fuchsia_syslog::fx_log_err!("AddSound called with id already in use {}", id);
                                        return Err(failure::format_err!("Client error, disconnecting"));
                                    } else {
                                        responder.send(&mut Ok(())).unwrap_or(());
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
                                return Err(failure::format_err!("Client error, disconnecting"));
                            }
                        }
                        PlayerRequest::RemoveSound { id, control_handle } => {
                            if self.sounds_by_id.remove(&id).is_none() {
                                fuchsia_syslog::fx_log_warn!("RemoveSound called with unrecognized id {}", id);
                            }
                        }
                        PlayerRequest::PlaySound { id, usage, responder } => {
                            if let Some(sound) = self.sounds_by_id.get(&id) {
                                let renderer = saved_renderer.lock().await.take()
                                    .map_or_else(|| Renderer::new(), |r| Ok(r))?;
                                match renderer.prepare_packet(sound, usage) {
                                    Ok(packet) =>
                                        futures.push(renderer.play_packet(packet)
                                            .and_then(|renderer| {
                                                responder.send(&mut Ok(())).unwrap_or(());
                                                saved_renderer.lock()
                                                    .map(|mut opt| {
                                                        opt.replace(renderer); Ok(())
                                                    } )
                                            })
                                            .map_err(move |error| {
                                                fuchsia_syslog::fx_log_err!("Unable to play sound {}: {}", id, error)
                                            })),
                                    Err(error) =>
                                        fuchsia_syslog::fx_log_err!("Unable to play sound {}: {}", id, error)
                                }
                            } else {
                                responder.send(&mut Err(PlaySoundError::NoSuchSound)).unwrap_or(());
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
}

impl Sound {
    fn new(id: u32, buffer: fidl_fuchsia_mem::Buffer, stream_type: AudioStreamType) -> Self {
        Self { id, vmo: buffer.vmo, size: buffer.size, stream_type }
    }

    fn from_file_channel(
        id: u32,
        file_channel: zx::Channel,
    ) -> std::result::Result<Self, zx::Status> {
        let wav = wav_reader::WavReader::read(BufReader::new(fdio::create_fd(
            file_channel.into_handle(),
        )?))
        .map_err(|_| zx::Status::INVALID_ARGS)?;

        Ok(Self { id, vmo: wav.vmo, size: wav.size, stream_type: wav.stream_type })
    }
}

struct Renderer {
    proxy: AudioRendererProxy,
}

impl Renderer {
    fn new() -> Result<Self> {
        let (client_endpoint, server_endpoint) = create_endpoints::<AudioRendererMarker>()
            .context("Creating renderer channel endpoints.")?;

        component::client::connect_to_service::<AudioMarker>()
            .context("Connecting to fuchsia.media.Audio")?
            .create_audio_renderer(server_endpoint)
            .context("Creating audio renderer")?;

        Ok(Self { proxy: client_endpoint.into_proxy()? })
    }

    fn prepare_packet(&self, sound: &Sound, usage: AudioRenderUsage) -> Result<StreamPacket> {
        self.proxy.set_usage(usage)?;
        self.proxy.set_pcm_stream_type(&mut sound.stream_type.clone())?;

        // This buffer is removed in play_packet.
        self.proxy.add_payload_buffer(
            sound.id,
            sound.vmo.duplicate_handle(zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP)?,
        )?;

        Ok(StreamPacket {
            pts: 0,
            payload_buffer_id: sound.id,
            payload_offset: 0,
            payload_size: sound.size,
            flags: 0,
            buffer_config: 0,
            stream_segment_id: 0,
        })
    }

    async fn play_packet(self, mut packet: StreamPacket) -> Result<Self> {
        let send_packet = self.proxy.send_packet(&mut packet).map(|_| {
            self.proxy.pause_no_reply()?;
            self.proxy.remove_payload_buffer(packet.payload_buffer_id)?;
            Ok(())
        });

        let play = self.proxy.play(NO_TIMESTAMP, 0);

        let results = join!(send_packet, play);
        if let Err(e) = results.0 {
            return Err(e);
        }
        if let Err(e) = results.1 {
            return Err(failure::format_err!("AudioRenderer.Play failed: {}", e));
        }

        Ok(self)
    }
}
