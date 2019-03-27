// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use base64;
use byteorder::{LittleEndian, WriteBytesExt};
use failure::Error;
use fidl::endpoints;
use fidl_fuchsia_media::*;
use fidl_fuchsia_virtualaudio::*;
use fuchsia_app as app;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::lock::Mutex;
use futures::{select, StreamExt, TryFutureExt, TryStreamExt};
use parking_lot::RwLock;
use serde_json::{to_value, Value};
use std::io::Write;
use std::iter::FromIterator;
use std::sync::Arc;
use zx::HandleBased;
use zx::Rights;

// Values found in:
//   zircon/system/public/zircon/device/audio.h
const AUDIO_SAMPLE_FORMAT_8BIT: u32 = (1 << 1);
const AUDIO_SAMPLE_FORMAT_16BIT: u32 = (1 << 2);
const AUDIO_SAMPLE_FORMAT_24BIT_IN32: u32 = (1 << 7);
const AUDIO_SAMPLE_FORMAT_32BIT_FLOAT: u32 = (1 << 9);

const ASF_RANGE_FLAG_FPS_CONTINUOUS: u16 = (1 << 0);

fn get_sample_size(format: u32) -> Result<usize, Error> {
    Ok(match format {
        // These are the currently implemented formats.
        AUDIO_SAMPLE_FORMAT_8BIT => 1,
        AUDIO_SAMPLE_FORMAT_16BIT => 2,
        AUDIO_SAMPLE_FORMAT_24BIT_IN32 => 4,
        AUDIO_SAMPLE_FORMAT_32BIT_FLOAT => 4,
        _ => bail!("Cannot handle sample_format: {:?}", format),
    })
}

fn get_zircon_sample_format(format: AudioSampleFormat) -> u32 {
    match format {
        AudioSampleFormat::Signed16 => AUDIO_SAMPLE_FORMAT_16BIT,
        AudioSampleFormat::Unsigned8 => AUDIO_SAMPLE_FORMAT_8BIT,
        AudioSampleFormat::Signed24In32 => AUDIO_SAMPLE_FORMAT_24BIT_IN32,
        AudioSampleFormat::Float => AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,
        // No default case, these are all the audio sample formats supported right now.
    }
}

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

#[derive(Debug, Default)]
struct InputWorker {
    va_input: Option<InputProxy>,
    inj_data: Vec<u8>,
    vmo: Option<zx::Vmo>,

    // How much of the vmo's data we're actually using, in bytes.
    work_space: u64,

    // How many frames ahead of the position we want to be writing to, when writing.
    target_frames: u64,

    // How close we let position get to where we've written before writing again.
    low_frames: u64,

    // How often, in frames, we want to be updated on the state.
    frames_per_notification: u64,

    // How many bytes a frame is.
    frame_size: u64,

    // Offset into inj_data, in bytes
    data_offset: usize,

    // Offset into vmo where we'll write next, in bytes.
    next_write: u64,

    // How many zeros we've written.
    zeros_written: usize,
}

impl InputWorker {
    // Events from the sl4f facade
    fn flush(&mut self) {
        // This will be set to false once the writer has zeroed out the vmo.
        self.inj_data.clear();
        self.data_offset = 0;
    }

    fn set_data(&mut self, data: Vec<u8>) -> Result<(), Error> {
        self.inj_data.clear();
        // This is a bad assumption, wav headers can be many different sizes.
        // 8 Bytes for riff header
        // 28 bytes for wave fmt block
        // 8 bytes for data header
        self.data_offset = 44;
        self.inj_data.write(&data)?;
        Ok(())
    }

    // Events from the Virtual Audio Device
    fn on_set_format(
        &mut self,
        frames_per_second: u32,
        sample_format: u32,
        num_channels: u32,
        _external_delay: i64,
    ) -> Result<(), Error> {
        let sample_size = get_sample_size(sample_format)?;
        self.frame_size = num_channels as u64 * sample_size as u64;

        let frames_per_millisecond = frames_per_second as u64 / 1000;

        self.target_frames = 200 * frames_per_millisecond;
        self.low_frames = 100 * frames_per_millisecond;
        self.frames_per_notification = 50 * frames_per_millisecond;
        Ok(())
    }

    fn on_buffer_created(
        &mut self,
        ring_buffer: zx::Vmo,
        num_ring_buffer_frames: u32,
        _notifications_per_ring: u32,
    ) -> Result<(), Error> {
        let va_input = self.va_input.as_mut().ok_or(format_err!("va_input not initialized"))?;

        let target_frames_per_notification =
            num_ring_buffer_frames as u64 / self.frames_per_notification;

        va_input.set_notification_frequency(target_frames_per_notification as u32)?;

        self.work_space = num_ring_buffer_frames as u64 * self.frame_size;

        // It starts zeroed, so just pretend we wrote the zeroes at the start.
        self.next_write = 2 * self.frames_per_notification * self.frame_size;

        self.vmo = Some(ring_buffer);
        Ok(())
    }

    fn on_position_notify(&mut self, ring_position: u32, _clock_time: i64) -> Result<(), Error> {
        let vmo = if let Some(vmo) = &self.vmo { vmo } else { return Ok(()) };

        let current_fill = if self.next_write < ring_position as u64 {
            self.work_space - ring_position as u64 + self.next_write
        } else {
            self.next_write - ring_position as u64
        };

        // If we've still have more than low_frames notifications worth of data, just continue.
        if current_fill > self.low_frames * self.frame_size {
            return Ok(());
        }

        // Calculate where to stop writing to hit the target buffer fill.
        let write_end =
            (ring_position as u64 + self.target_frames * self.frame_size) % self.work_space;

        // Calculate how many bytes we're writing, and figure out if we've wrapped around, and
        // if so, calculate the split.
        let (split_point, write_size) = if write_end < self.next_write {
            (
                (self.work_space - self.next_write) as usize,
                (self.work_space - self.next_write + write_end) as usize,
            )
        } else {
            (0 as usize, (write_end - self.next_write) as usize)
        };

        // Calculate the range of the data we need to use.
        let start = self.data_offset as usize;
        let end;

        let data_size;
        let src_data_size = self.inj_data.len();
        if write_size as usize > (src_data_size - self.data_offset) {
            data_size = src_data_size - self.data_offset;
            end = src_data_size as usize;
        } else {
            data_size = write_size;
            end = self.data_offset + write_size as usize;
        }

        // Build the data to send.
        let mut wav_data = Vec::from_iter(self.inj_data[start..end].iter().cloned());
        // Fill out with zeroes.
        wav_data.resize_with(write_size as usize, || 0);

        // If we're writing any wav data, clear our bookkeeping of having zeroed out the buffer.
        if !(data_size == 0) {
            self.zeros_written = 0;
        }

        // Only write to the VMO when we're going to be changing the contents.
        if self.zeros_written < self.work_space as usize {
            if split_point == 0 {
                vmo.write(&wav_data, self.next_write)?;
            } else {
                vmo.write(&wav_data[0..split_point], self.next_write)?;
                vmo.write(&wav_data[split_point..], 0)?;
            }
        }
        self.data_offset += data_size as usize;
        self.zeros_written += write_size - data_size;
        self.next_write = write_end;

        Ok(())
    }

    async fn run(
        &mut self,
        mut rx: mpsc::Receiver<InjectMsg>,
        va_input: InputProxy,
        active: Arc<Mutex<bool>>,
    ) -> Result<(), Error> {
        let mut input_events = va_input.take_event_stream();
        self.va_input = Some(va_input);

        'InjectEvents: loop {
            select! {
                rx_msg = rx.next() => {
                    match rx_msg {
                        None => {
                            bail!("Got None InjectMsg Event, exiting worker");
                        },
                        Some(InjectMsg::Flush) => {
                            let active = await!(active.lock());
                            self.flush();
                        }
                        Some(InjectMsg::Data { data }) => {
                            let mut active = await!(active.lock());
                            self.set_data(data)?;
                            *(active) = true;
                        }
                    }
                },
                input_msg = input_events.try_next() => {
                    match input_msg? {
                        None => {
                            bail!("Got None InputEvent Message, exiting worker");
                        },
                        Some(InputEvent::OnSetFormat { frames_per_second, sample_format, num_channels, external_delay}) => {
                            self.on_set_format(frames_per_second, sample_format, num_channels, external_delay)?;
                        },
                        Some(InputEvent::OnBufferCreated { ring_buffer, num_ring_buffer_frames, notifications_per_ring }) => {
                            self.on_buffer_created(ring_buffer, num_ring_buffer_frames, notifications_per_ring)?;
                        },
                        Some(InputEvent::OnPositionNotify { ring_position, clock_time }) => {
                            let mut active = await!(active.lock());
                            if !*(active) {
                                continue 'InjectEvents
                            }
                            self.on_position_notify(ring_position, clock_time)?;
                            if self.zeros_written >= self.work_space as usize {
                                *(active) = false;
                            }
                        },
                        Some(evt) => {
                            println!("Got unknown InputEvent {:?}", evt);
                        }
                    }
                },
            };
        }
    }
}

#[derive(Debug)]
struct VirtualInput {
    injection_data: Vec<Vec<u8>>,
    active: Arc<Mutex<bool>>,
    have_data: bool,

    sample_format: AudioSampleFormat,
    channels: u8,
    frames_per_second: u32,

    input: Option<InputProxy>,
    input_sender: Option<mpsc::Sender<InjectMsg>>,
}

impl VirtualInput {
    fn new(sample_format: AudioSampleFormat, channels: u8, frames_per_second: u32) -> Self {
        VirtualInput {
            injection_data: vec![],
            active: Arc::new(Mutex::new(false)),
            have_data: false,

            sample_format,
            channels,
            frames_per_second,

            input: None,
            input_sender: None,
        }
    }

    pub fn start_input(&mut self) -> Result<(), Error> {
        let va_input = app::client::connect_to_service::<InputMarker>()?;
        va_input.clear_format_ranges()?;
        let sample_format = get_zircon_sample_format(self.sample_format);

        va_input.add_format_range(
            sample_format as u32,
            self.frames_per_second,
            self.frames_per_second,
            self.channels,
            self.channels,
            ASF_RANGE_FLAG_FPS_CONTINUOUS,
        )?;

        // set buffer size to be 250ms-1000ms
        let frames_1ms = self.frames_per_second / 1000;
        let frames_low = 250 * frames_1ms;
        let frames_high = 1000 * frames_1ms;
        let frames_modulo = 1 * frames_1ms;
        va_input.set_ring_buffer_restrictions(frames_low, frames_high, frames_modulo)?;

        va_input.add()?;
        let (tx, rx) = mpsc::channel(512);
        let active = self.active.clone();
        fasync::spawn(
            async move {
                let mut worker = InputWorker::default();
                await!(worker.run(rx, va_input, active))?;
                Ok::<(), Error>(())
            }
                .unwrap_or_else(|e| eprintln!("Input injection thread failed: {:?}", e)),
        );

        self.input_sender = Some(tx);
        Ok(())
    }

    pub fn play(&mut self, index: usize) -> Result<(), Error> {
        let sender =
            self.input_sender.as_mut().ok_or(format_err!("input_sender not initialized"))?;
        sender.try_send(InjectMsg::Flush)?;
        sender.try_send(InjectMsg::Data { data: self.injection_data[index].clone() })?;
        Ok(())
    }

    pub fn stop(&mut self) -> Result<(), Error> {
        // The input worker will handle setting the active flag to false after it has zeroed out the vmo.
        let sender =
            self.input_sender.as_mut().ok_or(format_err!("input_sender not initialized"))?;
        sender.try_send(InjectMsg::Flush)?;
        Ok(())
    }
}

#[derive(Debug)]
enum InjectMsg {
    Flush,
    Data { data: Vec<u8> },
}

#[derive(Debug)]
struct VirtualAudio {
    // Output is from the AudioCore side, so it's what we'll be capturing
    output_sample_format: AudioSampleFormat,
    output_channels: u32,
    output_frames_per_second: u32,
    output: Option<OutputProxy>,

    // Input is from the AudioCore side, so it's the audio we'll be injecting
    input_sample_format: AudioSampleFormat,
    input_channels: u8,
    input_frames_per_second: u32,

    controller: ControlProxy,
}

impl VirtualAudio {
    fn new() -> Result<VirtualAudio, Error> {
        let va_control = app::client::connect_to_service::<ControlMarker>()?;
        Ok(VirtualAudio {
            output_sample_format: AudioSampleFormat::Signed16,
            output_channels: 2,
            output_frames_per_second: 48000,
            output: None,

            input_sample_format: AudioSampleFormat::Signed16,
            input_channels: 2,
            input_frames_per_second: 16000,

            controller: va_control,
        })
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
    audio_input: RwLock<VirtualInput>,
    initialized: Mutex<bool>,
}

#[derive(Debug)]
enum AudioMsg {
    Start,
    Stop { data: mpsc::Sender<Vec<u8>> },

    Data { data: Vec<u8> },
}

impl AudioFacade {
    async fn ensure_initialized(&self) -> Result<(), Error> {
        let mut initialized = await!(self.initialized.lock());
        if !*(initialized) {
            let controller = self.vad_control.read().controller.clone();
            await!(controller.disable())?;
            await!(controller.enable())?;
            self.audio_input.write().start_input()?;
            *(initialized) = true;
        }
        Ok(())
    }

    pub fn new() -> Result<AudioFacade, Error> {
        let vad_control = RwLock::new(VirtualAudio::new()?);
        let audio_output = RwLock::new(AudioOutput::new(
            vad_control.read().output_sample_format,
            vad_control.read().output_channels,
            vad_control.read().output_frames_per_second,
        )?);
        let audio_input = RwLock::new(VirtualInput::new(
            vad_control.read().input_sample_format,
            vad_control.read().input_channels,
            vad_control.read().input_frames_per_second,
        ));
        let initialized = Mutex::new(false);

        Ok(AudioFacade { vad_control, audio_output, audio_input, initialized })
    }

    pub async fn start_output_save(&self) -> Result<Value, Error> {
        await!(self.ensure_initialized())?;
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
        await!(self.ensure_initialized())?;
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

    pub async fn get_output_audio(&self) -> Result<Value, Error> {
        await!(self.ensure_initialized())?;
        if !self.audio_output.read().capturing {
            Ok(to_value(base64::encode(&self.audio_output.read().capturer_data))?)
        } else {
            bail!("GetOutputAudio failed, still saving.")
        }
    }

    pub async fn put_input_audio(&self, args: Value) -> Result<Value, Error> {
        await!(self.ensure_initialized())?;
        let data = args.get("data").ok_or(format_err!("PutInputAudio failed, no data"))?;
        let data = data.as_str().ok_or(format_err!("PutInputAudio failed, data not string"))?;

        let mut wave_data_vec = base64::decode(data)?;
        let sample_index = args["index"].as_u64().ok_or(format_err!("index not a number"))?;
        let sample_index = sample_index as usize;

        // TODO(perley): check wave format for correct bits per sample and float/int.
        let byte_cnt = wave_data_vec.len();
        {
            let active = self.audio_input.read().active.clone();
            let active = await!(active.lock());
            if *active {
                bail!("PutInputAudio failed, currently injecting audio.")
            }

            {
                let mut write = self.audio_input.write();
                // Make sure we have somewhere to store the wav data.
                if write.injection_data.len() <= sample_index {
                    write.injection_data.resize(sample_index + 1, vec![]);
                }

                write.injection_data[sample_index].clear();
                write.injection_data[sample_index].append(&mut wave_data_vec);
                write.have_data = true;
            }
        }
        Ok(to_value(byte_cnt)?)
    }

    pub async fn start_input_injection(&self, args: Value) -> Result<Value, Error> {
        await!(self.ensure_initialized())?;
        {
            let sample_index = args["index"].as_u64().ok_or(format_err!("index not a number"))?;
            let sample_index = sample_index as usize;

            let active = self.audio_input.read().active.clone();
            let active = await!(active.lock());
            if *active {
                bail!("StartInputInjection failed, already active.")
            } else if !self.audio_input.read().have_data {
                bail!("StartInputInjection failed, no Audio data to inject.")
            }
            self.audio_input.write().play(sample_index)?;
        }
        Ok(to_value(true)?)
    }

    pub async fn stop_input_injection(&self) -> Result<Value, Error> {
        await!(self.ensure_initialized())?;
        {
            let active = self.audio_input.read().active.clone();
            let active = await!(active.lock());
            if !*active {
                bail!("StopInputInjection failed, not active.")
            }
            self.audio_input.write().stop()?;
        }

        Ok(to_value(true)?)
    }
}
