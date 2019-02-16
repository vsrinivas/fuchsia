// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use base64;
use byteorder::{LittleEndian, WriteBytesExt};
use failure::Error;
use fidl::endpoints;
use fidl_fuchsia_media::*;
use fuchsia_app as app;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::stream::FuturesUnordered;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use parking_lot::RwLock;
use serde_json::{to_value, Value};
use std::io::Write;
use std::iter::FromIterator;
use std::sync::Arc;
use zx::HandleBased;
use zx::Rights;

#[derive(Debug)]
struct AudioOutput {
    capturing: bool,
    writer_channel: mpsc::Sender<AudioMsg>,

    // Rename once virtual audio device is in use
    capturer_format: AudioStreamType,
    capturer_proxy: AudioCapturerProxy,
    capturer_vmo: zx::Vmo,
    capturer_data: Vec<u8>,
}

impl AudioOutput {
    pub fn new(
        sample_format: AudioSampleFormat,
        channels: u32,
        frames_per_second: u32,
    ) -> Result<AudioOutput, Error> {
        let audio = app::client::connect_to_service::<AudioMarker>()?;

        let bytes_per_sample = match sample_format {
            AudioSampleFormat::Signed16 => 2,
            AudioSampleFormat::Unsigned8 => 1,
            AudioSampleFormat::Signed24In32 => 4,
            AudioSampleFormat::Float => 4,
        };

        // Create Audio Capturer
        let (capture_proxy, capture_server) = endpoints::create_proxy()?;
        audio.create_audio_capturer(capture_server, true)?;

        let audio_capture_format = AudioStreamType { sample_format, channels, frames_per_second };

        capture_proxy.set_pcm_stream_type(&mut AudioStreamType {
            sample_format,
            channels,
            frames_per_second,
        })?;

        // Create a vmo large enough for 1s of audio
        let secs_audio = 1u64;

        let vmar_size =
            (secs_audio * frames_per_second as u64 * channels as u64 * bytes_per_sample as u64)
                as usize;

        let flags = Rights::READ | Rights::WRITE | Rights::MAP | Rights::TRANSFER;

        let capturer_vmo = zx::Vmo::create(vmar_size as u64)?;
        let capturer_audio_core_vmo = capturer_vmo.duplicate_handle(flags)?;
        let capturer_writer_vmo = capturer_vmo.duplicate_handle(flags)?;
        capture_proxy.add_payload_buffer(0, capturer_audio_core_vmo)?;

        let mut capturer_stream = capture_proxy.take_event_stream();

        // Create the workers
        let (mut tx, mut rx) = mpsc::channel(512);
        let writer_channel = tx.clone();

        // TODO(perley): when re-writing this for virtual audio device, make this spawn on start
        //               and go away on stop.
        fasync::spawn(
            async move {
                while let Some(evt) = await!(capturer_stream.try_next())? {
                    match evt {
                        AudioCapturerEvent::OnPacketProduced { packet } => {
                            let mut packet_data = vec![0; packet.payload_size as usize];
                            capturer_writer_vmo.read(&mut packet_data, packet.payload_offset)?;
                            tx.try_send(AudioMsg::Data { data: packet_data }).or_else(|e| {
                                if e.is_full() {
                                    println!("Capture Writer couldn't keep up {:?}", e);
                                    Ok(())
                                } else {
                                    eprintln!("Failed to save captured audio. {:?}", e);
                                    bail!("failed to send")
                                }
                            })?;
                        }
                        AudioCapturerEvent::OnEndOfStream {} => {}
                    }
                }
                Ok::<(), Error>(())
            }
                .unwrap_or_else(|e| {
                    eprintln!("Failed to listen for OnPacketCaptured events {:?}", e)
                }),
        );

        fasync::spawn(
            async move {
                while let Some(packet_data) = await!(rx.next()) {
                    if let AudioMsg::Start = packet_data {
                        // This should happen
                    } else {
                        bail!("Not Started")
                    }
                    let mut captured_data = vec![0u8; 0];
                    let mut payload = 0u32;

                    'data_active: while let Some(active_data) = await!(rx.next()) {
                        match active_data {
                            AudioMsg::Start => bail!("Already Started"),
                            AudioMsg::Data { data } => {
                                captured_data.write(&data)?;
                                payload += data.len() as u32;
                            }
                            AudioMsg::Stop { mut data } => {
                                let mut ret_data = vec![0u8; 0];
                                // 8 Bytes
                                ret_data.write("RIFF".as_bytes())?;
                                ret_data.write_u32::<LittleEndian>(payload + 8u32 + 28 + 8)?;

                                // 28 bytes
                                ret_data.write("WAVE".as_bytes())?; // wave_four_cc uint32
                                ret_data.write("fmt ".as_bytes())?; // fmt_four_cc uint32
                                ret_data.write_u32::<LittleEndian>(16u32)?; // fmt_chunk_len
                                ret_data.write_u16::<LittleEndian>(1u16)?; // format
                                ret_data.write_u16::<LittleEndian>(channels as u16)?;
                                ret_data.write_u32::<LittleEndian>(frames_per_second)?;
                                ret_data.write_u32::<LittleEndian>(
                                    bytes_per_sample * channels * frames_per_second,
                                )?; // avg_byte_rate
                                ret_data.write_u16::<LittleEndian>(
                                    (bytes_per_sample * channels) as u16,
                                )?;
                                ret_data
                                    .write_u16::<LittleEndian>((bytes_per_sample * 8) as u16)?;

                                // 8 bytes
                                ret_data.write("data".as_bytes())?;
                                ret_data.write_u32::<LittleEndian>(payload)?;
                                ret_data.append(&mut captured_data);
                                data.try_send(ret_data)?;
                                break 'data_active;
                            }
                        }
                    }
                }
                Ok::<(), Error>(())
            }
                .unwrap_or_else(|e| {
                    eprintln!("Failed to listen for OnPacketCaptured events {:?}", e)
                }),
        );
        Ok(AudioOutput {
            capturing: false,
            writer_channel,

            capturer_format: audio_capture_format,
            capturer_proxy: capture_proxy,
            capturer_vmo,
            capturer_data: vec![],
        })
    }
}

#[derive(Debug)]
struct InjectState {
    active: bool,
    offset: u64,
}

#[derive(Debug)]
struct AudioInput {
    // Rename once the virtual audio driver has an API to support this
    renderer_proxy: AudioRendererProxy,
    renderer_vmo: zx::Vmo,
    renderer_data: Vec<u8>,
    renderer_packet_size: u64,

    state: Arc<RwLock<InjectState>>,
    have_data: bool,
}

impl AudioInput {
    fn new(
        sample_format: AudioSampleFormat,
        channels: u32,
        frames_per_second: u32,
    ) -> Result<AudioInput, Error> {
        let audio = app::client::connect_to_service::<AudioMarker>()?;

        let bytes_per_sample = match sample_format {
            AudioSampleFormat::Signed16 => 2,
            AudioSampleFormat::Unsigned8 => 1,
            AudioSampleFormat::Signed24In32 => 4,
            AudioSampleFormat::Float => 4,
        };

        let packet_size =
            channels as u64 * bytes_per_sample as u64 * frames_per_second as u64 / 10u64;

        // Create Audio Renderer to inject.
        let (renderer_proxy, render_server) = endpoints::create_proxy()?;
        audio.create_audio_renderer(render_server)?;

        renderer_proxy.set_pcm_stream_type(&mut AudioStreamType {
            sample_format,
            channels,
            frames_per_second,
        })?;

        // Create a vmo large enough for 1s of audio
        let secs_audio = 1u64;

        let vmar_size =
            (secs_audio * frames_per_second as u64 * channels as u64 * bytes_per_sample as u64)
                as usize;

        let flags = Rights::READ | Rights::MAP | Rights::TRANSFER;

        let renderer_vmo = zx::Vmo::create(vmar_size as u64)?;
        let renderer_audio_core_vmo = renderer_vmo.duplicate_handle(flags)?;
        renderer_proxy.add_payload_buffer(0, renderer_audio_core_vmo)?;

        Ok(AudioInput {
            renderer_proxy,
            renderer_vmo,
            renderer_data: vec![0u8; 0],
            renderer_packet_size: packet_size,

            state: Arc::new(RwLock::new(InjectState { offset: 0, active: false })),
            have_data: false,
        })
    }

    pub fn play(&mut self) -> Result<(), Error> {
        self.state.write().active = true;
        // 8 Bytes for riff header
        // 28 bytes for wave fmt block
        // 8 bytes for data header
        self.state.write().offset = 44u64;

        let render_data = self.renderer_data.clone();
        let render_proxy = self.renderer_proxy.clone();
        let state = self.state.clone();

        let vmo = self.renderer_vmo.duplicate_handle(Rights::WRITE)?;
        let render_packet_size = self.renderer_packet_size;

        fasync::spawn(
            async move {
                render_proxy.play_no_reply(NO_TIMESTAMP, 0)?;

                let next_packet = |vmo_addr: u64, len: u64| {
                    let render_proxy = render_proxy.clone();

                    async move {
                        let mut packet = StreamPacket {
                            payload_offset: vmo_addr,
                            payload_size: len,

                            payload_buffer_id: 0,
                            buffer_config: 0,
                            flags: 0,
                            pts: NO_TIMESTAMP,
                            stream_segment_id: 0,
                        };

                        await!(render_proxy.send_packet(&mut packet))?;
                        Ok::<u64, Error>(vmo_addr)
                    }
                };

                let mut concurrent = FuturesUnordered::new();

                for i in 0..10 {
                    if state.read().active {
                        let vmo_addr = i * render_packet_size;
                        let mut len = render_packet_size;
                        {
                            let mut holder = state.write();
                            let start = holder.offset;
                            let mut end = start + render_packet_size;

                            if end >= render_data.len() as u64 {
                                end = render_data.len() as u64;
                                len = end - start;
                                holder.active = false;
                            }

                            let packet_data = Vec::from_iter(
                                render_data[start as usize..end as usize].iter().cloned(),
                            );
                            holder.offset += render_packet_size;
                            vmo.write(&packet_data, vmo_addr)?;
                        }
                        concurrent.push(next_packet(vmo_addr, len));
                    }
                }

                while let Some(vmo_addr) = await!(concurrent.try_next())? {
                    if !state.read().active {
                        if concurrent.len() == 0 {
                            break;
                        }
                    } else {
                        let mut len = render_packet_size;
                        {
                            let mut holder = state.write();
                            let start = holder.offset;
                            let mut end = start + render_packet_size;

                            if end >= render_data.len() as u64 {
                                end = render_data.len() as u64;
                                len = end - start;
                                holder.active = false;
                            }

                            let packet_data = Vec::from_iter(
                                render_data[start as usize..end as usize].iter().cloned(),
                            );

                            holder.offset += render_packet_size;
                            vmo.write(&packet_data, vmo_addr)?;
                        }
                        concurrent.push(next_packet(vmo_addr, len));
                    }
                }
                render_proxy.end_of_stream()?;
                await!(render_proxy.pause())?;
                await!(render_proxy.discard_all_packets())?;
                Ok::<(), Error>(())
            }
                .unwrap_or_else(|e| {
                    eprintln!("Failed to listen for OnPacketCaptured events {:?}", e)
                }),
        );

        Ok(())
    }

    pub fn stop(&mut self) -> Result<(), Error> {
        self.state.write().active = false;
        Ok(())
    }
}

#[derive(Debug)]
struct VirtualAudio {
    // Output is from the AudioCore side, so it's what we'll be capturing
    output_sample_format: AudioSampleFormat,
    output_channels: u32,
    output_frames_per_second: u32,

    // Input is from the AudioCore side, so it's the audio we'll be injecting
    input_sample_format: AudioSampleFormat,
    input_channels: u32,
    input_frames_per_second: u32,
}

impl Default for VirtualAudio {
    fn default() -> Self {
        VirtualAudio {
            output_sample_format: AudioSampleFormat::Signed16,
            output_channels: 2u32,
            output_frames_per_second: 48000u32,

            input_sample_format: AudioSampleFormat::Signed16,
            input_channels: 2u32,
            input_frames_per_second: 16000u32,
        }
    }
}

/// Perform Audio operations.
///
/// Note this object is shared among all threads created by server.
#[derive(Debug)]
pub struct AudioFacade {
    // TODO(perley): This will be needed after migrating to using virtual audio devices rather than
    //               renderer+capturer in the facade.
    vad_control: RwLock<VirtualAudio>,
    audio_output: RwLock<AudioOutput>,
    audio_input: RwLock<AudioInput>,
}

#[derive(Debug)]
enum AudioMsg {
    Start,
    Stop { data: mpsc::Sender<Vec<u8>> },

    Data { data: Vec<u8> },
}

impl AudioFacade {
    pub fn new() -> Result<AudioFacade, Error> {
        let vad_control = RwLock::new(VirtualAudio::default());
        let audio_output = RwLock::new(AudioOutput::new(
            vad_control.read().output_sample_format,
            vad_control.read().output_channels,
            vad_control.read().output_frames_per_second,
        )?);
        let audio_input = RwLock::new(AudioInput::new(
            vad_control.read().input_sample_format,
            vad_control.read().input_channels,
            vad_control.read().input_frames_per_second,
        )?);
        Ok(AudioFacade { vad_control, audio_output, audio_input })
    }

    pub fn start_output_save(&self) -> Result<Value, Error> {
        if !self.audio_output.read().capturing {
            self.audio_output.write().capturing = true;
            self.audio_output.write().writer_channel.try_send(AudioMsg::Start)?;
            self.audio_output.read().capturer_proxy.start_async_capture(1000)?;
            Ok(to_value(true)?)
        } else {
            bail!("Cannot StartOutputSave, already started.")
        }
    }

    pub async fn stop_output_save(&self) -> Result<Value, Error> {
        if self.audio_output.read().capturing {
            self.audio_output.write().capturing = false;
            self.audio_output.read().capturer_proxy.stop_async_capture_no_reply()?;

            let (tx, mut rx) = mpsc::channel(512);
            self.audio_output.write().writer_channel.try_send(AudioMsg::Stop { data: tx })?;
            let mut saved_audio = await!(rx.next())
                .ok_or_else(|| format_err!("StopOutputSave failed, could not retrieve data."))?;
            self.audio_output.write().capturer_data.clear();
            self.audio_output.write().capturer_data.append(&mut saved_audio);
            Ok(to_value(true)?)
        } else {
            bail!("Cannot StopOutputSave, not started.")
        }
    }

    pub fn get_output_audio(&self) -> Result<Value, Error> {
        if !self.audio_output.read().capturing {
            Ok(to_value(base64::encode(&self.audio_output.read().capturer_data))?)
        } else {
            bail!("GetOutputAudio failed, still saving.")
        }
    }

    pub fn put_input_audio(&self, args: Value) -> Result<Value, Error> {
        if self.audio_input.read().state.read().active {
            bail!("PutInputAudio failed, currently injecting audio.")
        } else {
            // Todo: It's really really bad to assume all of these calls are going to succeed or
            //       fail in a way that will be meaningful to the caller.

            // Extract and decode base64 encoded wav data.
            let mut wave_data_vec = base64::decode(args.as_str().unwrap())?;

            // TODO(perley): check wave format for correct bits per sample and float/int.
            let byte_cnt = wave_data_vec.len();

            self.audio_input.write().renderer_data.clear();
            self.audio_input.write().renderer_data.append(&mut wave_data_vec);
            self.audio_input.write().have_data = true;
            Ok(to_value(byte_cnt)?)
        }
    }

    pub fn start_input_injection(&self) -> Result<Value, Error> {
        if self.audio_input.read().state.read().active {
            bail!("StartInputInjection failed, already active.")
        } else if !self.audio_input.read().have_data {
            bail!("StartInputInjection failed, no Audio data to inject.")
        } else {
            self.audio_input.write().play()?;
            Ok(to_value(true)?)
        }
    }

    pub fn stop_input_injection(&self) -> Result<Value, Error> {
        if !self.audio_input.read().state.read().active {
            bail!("StopInputInjection failed, not active.")
        } else {
            self.audio_input.write().stop()?;
            Ok(to_value(true)?)
        }
    }
}
