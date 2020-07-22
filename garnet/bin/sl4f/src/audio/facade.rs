// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use base64;
use byteorder::{LittleEndian, WriteBytesExt};
use fidl_fuchsia_media::*;
use fidl_fuchsia_virtualaudio::*;
use fuchsia_async as fasync;
use fuchsia_component as app;
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::lock::Mutex;
use futures::{select, StreamExt, TryFutureExt, TryStreamExt};
use parking_lot::RwLock;
use serde_json::{to_value, Value};
use std::io::Write;
use std::iter::FromIterator;
use std::sync::Arc;

use fuchsia_syslog::macros::*;

// Values found in:
//   zircon/system/public/zircon/device/audio.h
const AUDIO_SAMPLE_FORMAT_8BIT: u32 = 1 << 1;
const AUDIO_SAMPLE_FORMAT_16BIT: u32 = 1 << 2;
const AUDIO_SAMPLE_FORMAT_24BIT_IN32: u32 = 1 << 7;
const AUDIO_SAMPLE_FORMAT_32BIT_FLOAT: u32 = 1 << 9;

const ASF_RANGE_FLAG_FPS_CONTINUOUS: u16 = 1 << 0;

// If this changes, so too must the astro audio_core_config.
const AUDIO_OUTPUT_ID: [u8; 16] = [0x01; 16];

fn get_sample_size(format: u32) -> Result<usize, Error> {
    Ok(match format {
        // These are the currently implemented formats.
        AUDIO_SAMPLE_FORMAT_8BIT => 1,
        AUDIO_SAMPLE_FORMAT_16BIT => 2,
        AUDIO_SAMPLE_FORMAT_24BIT_IN32 => 4,
        AUDIO_SAMPLE_FORMAT_32BIT_FLOAT => 4,
        _ => return Err(format_err!("Cannot handle sample_format: {:?}", format)),
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
enum ExtractMsg {
    Start,
    Stop { out_sender: mpsc::Sender<Vec<u8>> },
}

#[derive(Debug, Default)]
struct OutputWorker {
    va_output: Option<OutputProxy>,
    extracted_data: Vec<u8>,
    vmo: Option<zx::Vmo>,

    // Whether we should store samples when we receive notification from the VAD
    capturing: bool,

    // How much of the vmo's data we're actually using, in bytes.
    work_space: u64,

    // How often, in frames, we want to be updated on the state of the extraction ring buffer.
    frames_per_notification: u64,

    // How many bytes a frame is.
    frame_size: u64,

    // Offset into vmo where we'll start to read next, in bytes.
    next_read: u64,

    // Offset into vmo where we'll finish reading next, in bytes.
    next_read_end: u64,
}

impl OutputWorker {
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

        self.frames_per_notification = 50 * frames_per_millisecond;

        Ok(())
    }

    fn on_buffer_created(
        &mut self,
        ring_buffer: zx::Vmo,
        num_ring_buffer_frames: u32,
        _notifications_per_ring: u32,
    ) -> Result<(), Error> {
        let va_output = self.va_output.as_mut().ok_or(format_err!("va_output not initialized"))?;

        // Ignore AudioCore's notification cadence (_notifications_per_ring); set up our own.
        let target_notifications_per_ring =
            num_ring_buffer_frames as u64 / self.frames_per_notification;

        va_output.set_notification_frequency(target_notifications_per_ring as u32)?;

        self.work_space = num_ring_buffer_frames as u64 * self.frame_size;

        // Start reading from the beginning.
        self.next_read = 0;
        self.next_read_end = 0;

        self.vmo = Some(ring_buffer);
        Ok(())
    }

    fn on_position_notify(
        &mut self,
        _monotonic_time: i64,
        ring_position: u32,
        capturing: bool,
    ) -> Result<(), Error> {
        if capturing && self.next_read != self.next_read_end {
            let vmo = if let Some(vmo) = &self.vmo { vmo } else { return Ok(()) };

            if (self.next_read_end as u64) < self.next_read {
                // Wrap-around case, read through the end.
                let mut data = vec![0u8; (self.work_space - self.next_read) as usize];
                let overwrite1 = vec![1u8; (self.work_space - self.next_read) as usize];
                vmo.read(&mut data, self.next_read)?;
                vmo.write(&overwrite1, self.next_read)?;
                self.extracted_data.append(&mut data);

                // Read remaining data.
                let mut data = vec![0u8; self.next_read_end as usize];
                let overwrite2 = vec![1u8; self.next_read_end as usize];
                vmo.read(&mut data, 0)?;
                vmo.write(&overwrite2, 0)?;

                self.extracted_data.append(&mut data);
            } else {
                // Normal case, just read all the bytes.
                let mut data = vec![0u8; (self.next_read_end - self.next_read) as usize];
                let overwrite = vec![1u8; (self.next_read_end - self.next_read) as usize];
                vmo.read(&mut data, self.next_read)?;
                vmo.write(&overwrite, self.next_read)?;

                self.extracted_data.append(&mut data);
            }
        }
        // We always stay 1 notification behind, since audio_core writes audio data into
        // our shared buffer based on these same notifications. This avoids audio glitches.
        self.next_read = self.next_read_end;
        self.next_read_end = ring_position as u64;
        Ok(())
    }

    async fn run(
        &mut self,
        mut rx: mpsc::Receiver<ExtractMsg>,
        va_output: OutputProxy,
    ) -> Result<(), Error> {
        let mut output_events = va_output.take_event_stream();
        self.va_output = Some(va_output);

        // Monotonic timestamp returned by the most-recent OnStart/OnPositionNotify/OnStop response.
        let mut last_timestamp = zx::Time::from_nanos(0);
        // Observed monotonic time that OnStart/OnPositionNotify/OnStop messages actually arrived.
        let mut last_event_time = zx::Time::from_nanos(0);

        loop {
            select! {
                rx_msg = rx.next() => {
                    match rx_msg {
                        None => {
                            return Err(format_err!("Got None ExtractMsg Event, exiting worker"));
                        },
                        Some(ExtractMsg::Stop { mut out_sender }) => {
                            self.capturing = false;
                            let mut ret_data = vec![0u8; 0];

                            ret_data.append(&mut self.extracted_data);

                            out_sender.try_send(ret_data)?;
                        }
                        Some(ExtractMsg::Start) => {
                            self.extracted_data.clear();
                            self.capturing = true;
                        }
                    }
                },
                output_msg = output_events.try_next() => {
                    match output_msg? {
                        None => {
                            return Err(format_err!("Got None OutputEvent Message, exiting worker"));
                        },
                        Some(OutputEvent::OnSetFormat { frames_per_second, sample_format,
                                                        num_channels, external_delay}) => {
                            self.on_set_format(frames_per_second, sample_format, num_channels,
                                               external_delay)?;
                        },
                        Some(OutputEvent::OnBufferCreated { ring_buffer, num_ring_buffer_frames,
                                                            notifications_per_ring }) => {
                            self.on_buffer_created(ring_buffer, num_ring_buffer_frames,
                                                   notifications_per_ring)?;
                        },
                        Some(OutputEvent::OnStart { start_time }) => {
                            if last_timestamp > zx::Time::from_nanos(0) {
                                fx_log_info!("Extraction OnPositionNotify received before OnStart");
                            }
                            last_timestamp = zx::Time::from_nanos(start_time);
                            last_event_time = zx::Time::get(zx::ClockId::Monotonic);
                        },
                        Some(OutputEvent::OnStop { stop_time, ring_position }) => {
                            if last_timestamp == zx::Time::from_nanos(0) {
                                fx_log_info!(
                                    "Extraction OnPositionNotify timestamp cleared before OnStop");
                            }
                            last_timestamp = zx::Time::from_nanos(0);
                            last_event_time = zx::Time::from_nanos(0);
                        },
                        Some(OutputEvent::OnPositionNotify { monotonic_time, ring_position }) => {
                            if last_timestamp == zx::Time::from_nanos(0) {
                                fx_log_info!(
                                    "Extraction OnStart not received before OnPositionNotify");
                            }
                            let monotonic_zx_time = zx::Time::from_nanos(monotonic_time);

                            // Log if our timestamps had a gap of more than 100ms. This is highly
                            // abnormal and indicates possible glitching while receiving playback
                            // audio from the system and/or extracting it for analysis.
                            let timestamp_interval = monotonic_zx_time - last_timestamp;

                            if  timestamp_interval > zx::Duration::from_millis(100) {
                                fx_log_info!(
                "Extraction position timestamp jumped by more than 100ms ({:?}). Expect glitches.",
                                    timestamp_interval.into_millis());
                            }
                            if  monotonic_zx_time < last_timestamp {
                                fx_log_info!(
                        "Extraction position timestamp moved backwards ({:?}). Expect glitches.",
                                    timestamp_interval.into_millis());
                            }
                            last_timestamp = monotonic_zx_time;

                            // Log if there was a gap in position notification arrivals of more
                            // than 150ms. This is highly abnormal and indicates possible glitching
                            // while receiving playback audio from the system and/or extracting it
                            // for analysis.
                            let now = zx::Time::get(zx::ClockId::Monotonic);
                            let observed_interval = now - last_event_time;

                            if  observed_interval > zx::Duration::from_millis(150) {
                                fx_log_info!(
                            "Extraction position not updated for 150ms ({:?}). Expect glitches.",
                                    observed_interval.into_millis());
                            }
                            last_event_time = now;

                            self.on_position_notify(monotonic_time, ring_position, self.capturing)?;
                        },
                        Some(evt) => {
                            fx_log_info!("Got unknown OutputEvent {:?}", evt);
                        }
                    }
                },
            };
        }
    }
}

#[derive(Debug)]
struct VirtualOutput {
    extracted_data: Vec<u8>,
    capturing: Arc<Mutex<bool>>,
    have_data: bool,

    sample_format: AudioSampleFormat,
    channels: u8,
    frames_per_second: u32,

    output: Option<OutputProxy>,
    output_sender: Option<mpsc::Sender<ExtractMsg>>,
}

impl VirtualOutput {
    pub fn new(
        sample_format: AudioSampleFormat,
        channels: u8,
        frames_per_second: u32,
    ) -> Result<VirtualOutput, Error> {
        Ok(VirtualOutput {
            extracted_data: vec![],
            capturing: Arc::new(Mutex::new(false)),
            have_data: false,

            sample_format,
            channels,
            frames_per_second,

            output: None,
            output_sender: None,
        })
    }

    pub fn start_output(&mut self) -> Result<(), Error> {
        let va_output = app::client::connect_to_service::<OutputMarker>()?;
        va_output.clear_format_ranges()?;
        va_output.set_fifo_depth(0)?;
        va_output.set_external_delay(0)?;
        va_output.set_unique_id(&mut AUDIO_OUTPUT_ID.clone())?;

        let sample_format = get_zircon_sample_format(self.sample_format);
        va_output.add_format_range(
            sample_format as u32,
            self.frames_per_second,
            self.frames_per_second,
            self.channels,
            self.channels,
            ASF_RANGE_FLAG_FPS_CONTINUOUS,
        )?;

        // set buffer size to be 500ms-1000ms
        let frames_1ms = self.frames_per_second / 1000;
        let frames_low = 500 * frames_1ms;
        let frames_high = 1000 * frames_1ms;
        let frames_modulo = 1 * frames_1ms;
        va_output.set_ring_buffer_restrictions(frames_low, frames_high, frames_modulo)?;

        let (tx, rx) = mpsc::channel(512);
        va_output.add()?;
        fasync::Task::spawn(
            async move {
                let mut worker = OutputWorker::default();
                worker.run(rx, va_output).await?;
                Ok::<(), Error>(())
            }
            .unwrap_or_else(|e| eprintln!("Output extraction thread failed: {:?}", e)),
        )
        .detach();

        self.output_sender = Some(tx);
        Ok(())
    }

    pub fn write_header(&mut self, len: u32) -> Result<(), Error> {
        let bytes_per_sample = get_sample_size(get_zircon_sample_format(self.sample_format))?;

        // 8 Bytes
        self.extracted_data.write("RIFF".as_bytes())?;
        self.extracted_data.write_u32::<LittleEndian>(len as u32 + 8 + 28 + 8)?;

        // 28 bytes
        self.extracted_data.write("WAVE".as_bytes())?; // wave_four_cc uint32
        self.extracted_data.write("fmt ".as_bytes())?; // fmt_four_cc uint32
        self.extracted_data.write_u32::<LittleEndian>(16u32)?; // fmt_chunk_len
        self.extracted_data.write_u16::<LittleEndian>(1u16)?; // format
        self.extracted_data.write_u16::<LittleEndian>(self.channels as u16)?;
        self.extracted_data.write_u32::<LittleEndian>(self.frames_per_second)?;
        self.extracted_data.write_u32::<LittleEndian>(
            bytes_per_sample as u32 * self.channels as u32 * self.frames_per_second,
        )?; // avg_byte_rate
        self.extracted_data
            .write_u16::<LittleEndian>(bytes_per_sample as u16 * self.channels as u16)?;
        self.extracted_data.write_u16::<LittleEndian>((bytes_per_sample * 8) as u16)?;

        // 8 bytes
        self.extracted_data.write("data".as_bytes())?;
        self.extracted_data.write_u32::<LittleEndian>(len as u32)?;

        Ok(())
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

    // How often, in frames, we want to be updated on the state of the injection ring buffer.
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
        self.zeros_written = 0;
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

        self.target_frames = 150 * frames_per_millisecond;
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

        // Ignore AudioCore's notification cadence (_notifications_per_ring); set up our own.
        let target_notifications_per_ring =
            num_ring_buffer_frames as u64 / self.frames_per_notification;

        va_input.set_notification_frequency(target_notifications_per_ring as u32)?;

        self.work_space = num_ring_buffer_frames as u64 * self.frame_size;

        // It starts zeroed, so just pretend we wrote the zeroes at the start.
        self.next_write = 2 * self.frames_per_notification * self.frame_size;

        self.vmo = Some(ring_buffer);
        Ok(())
    }

    fn on_position_notify(
        &mut self,
        _monotonic_time: i64,
        ring_position: u32,
        active: bool,
    ) -> Result<(), Error> {
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

        if active {
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
        }
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

        // Monotonic timestamp returned by the most-recent OnStart/OnPositionNotify/OnStop response.
        let mut last_timestamp = zx::Time::from_nanos(0);
        // Observed monotonic time that OnStart/OnPositionNotify/OnStop messages actually arrived.
        let mut last_event_time = zx::Time::from_nanos(0);

        loop {
            select! {
                rx_msg = rx.next() => {
                    match rx_msg {
                        None => {
                            return Err(format_err!("Got None InjectMsg Event, exiting worker"));
                        },
                        Some(InjectMsg::Flush) => {
                            let active = active.lock().await;
                            self.flush();
                        }
                        Some(InjectMsg::Data { data }) => {
                            let mut active = active.lock().await;
                            self.set_data(data)?;
                            *(active) = true;
                        }
                    }
                },
                input_msg = input_events.try_next() => {
                    match input_msg? {
                        None => {
                            return Err(format_err!("Got None InputEvent Message, exiting worker"));
                        },
                        Some(InputEvent::OnSetFormat { frames_per_second, sample_format,
                                                       num_channels, external_delay}) => {
                            self.on_set_format(frames_per_second, sample_format, num_channels,
                                               external_delay)?;
                        },
                        Some(InputEvent::OnBufferCreated { ring_buffer, num_ring_buffer_frames,
                                                           notifications_per_ring }) => {
                            self.on_buffer_created(ring_buffer, num_ring_buffer_frames,
                                                   notifications_per_ring)?;
                        },
                        Some(InputEvent::OnStart { start_time }) => {
                            if last_timestamp > zx::Time::from_nanos(0) {
                                fx_log_info!("Injection OnPositionNotify received before OnStart");
                            }
                            last_timestamp = zx::Time::from_nanos(start_time);
                            last_event_time = zx::Time::get(zx::ClockId::Monotonic);
                        },
                        Some(InputEvent::OnStop { stop_time, ring_position }) => {
                            if last_timestamp == zx::Time::from_nanos(0) {
                                fx_log_info!(
                                    "Injection OnPositionNotify timestamp cleared before OnStop");
                            }
                            last_timestamp = zx::Time::from_nanos(0);
                            last_event_time = zx::Time::from_nanos(0);
                        },
                        Some(InputEvent::OnPositionNotify { monotonic_time, ring_position }) => {
                            if last_timestamp == zx::Time::from_nanos(0) {
                                fx_log_info!(
                                    "Injection OnStart not received before OnPositionNotify");
                            }
                            let monotonic_zx_time = zx::Time::from_nanos(monotonic_time);

                            // Log if our timestamps had a gap of more than 100ms. This is highly
                            // abnormal and indicates possible glitching while receiving audio to
                            // be injected and/or providing it to the system.
                            let timestamp_interval = monotonic_zx_time - last_timestamp;

                            if  timestamp_interval > zx::Duration::from_millis(100) {
                                fx_log_info!(
                "Injection position timestamp jumped by more than 100ms ({:?}). Expect glitches.",
                                    timestamp_interval.into_millis());
                            }
                            if  monotonic_zx_time < last_timestamp {
                                fx_log_info!(
                            "Injection position timestamp moved backwards ({:?}). Expect glitches.",
                                    timestamp_interval.into_millis());
                            }
                            last_timestamp = monotonic_zx_time;

                            // Log if there was a gap in position notification arrivals of more
                            // than 150ms. This is highly abnormal and indicates possible glitching
                            // while receiving audio to be injected and/or providing it to the
                            // system.
                            let now = zx::Time::get(zx::ClockId::Monotonic);
                            let observed_interval = now - last_event_time;

                            if  observed_interval > zx::Duration::from_millis(150) {
                                fx_log_info!(
                                "Injection position not updated for 150ms ({:?}). Expect glitches.",
                                    observed_interval.into_millis());
                            }
                            last_event_time = now;

                            let mut active = active.lock().await;
                            self.on_position_notify(monotonic_time, ring_position, *active)?;
                            if self.zeros_written >= self.work_space as usize {
                                *(active) = false;
                            }
                        },
                        Some(evt) => {
                            fx_log_info!("Got unknown InputEvent {:?}", evt);
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

            input_sender: None,
        }
    }

    pub fn start_input(&mut self) -> Result<(), Error> {
        let va_input = app::client::connect_to_service::<InputMarker>()?;
        va_input.clear_format_ranges()?;
        va_input.set_fifo_depth(0)?;
        va_input.set_external_delay(0)?;

        let sample_format = get_zircon_sample_format(self.sample_format);
        va_input.add_format_range(
            sample_format as u32,
            self.frames_per_second,
            self.frames_per_second,
            self.channels,
            self.channels,
            ASF_RANGE_FLAG_FPS_CONTINUOUS,
        )?;

        // set buffer size to be 500ms-1000ms
        let frames_1ms = self.frames_per_second / 1000;
        let frames_low = 500 * frames_1ms;
        let frames_high = 1000 * frames_1ms;
        let frames_modulo = 1 * frames_1ms;
        va_input.set_ring_buffer_restrictions(frames_low, frames_high, frames_modulo)?;

        va_input.add()?;
        let (tx, rx) = mpsc::channel(512);
        let active = self.active.clone();
        fasync::Task::spawn(
            async move {
                let mut worker = InputWorker::default();
                worker.run(rx, va_input, active).await?;
                Ok::<(), Error>(())
            }
            .unwrap_or_else(|e| eprintln!("Input injection thread failed: {:?}", e)),
        )
        .detach();

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
        // The input worker will set the active flag to false after it has zeroed out the vmo.
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
    // Output is from the AudioCore side, so it's what we'll be capturing and extracting
    output_sample_format: AudioSampleFormat,
    output_channels: u8,
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
    audio_output: RwLock<VirtualOutput>,
    audio_input: RwLock<VirtualInput>,
    initialized: Mutex<bool>,
}

impl AudioFacade {
    async fn ensure_initialized(&self) -> Result<(), Error> {
        let mut initialized = self.initialized.lock().await;
        if !*(initialized) {
            let controller = self.vad_control.read().controller.clone();
            controller.disable().await?;
            controller.enable().await?;
            self.audio_input.write().start_input()?;
            self.audio_output.write().start_output()?;
            *(initialized) = true;
        }
        Ok(())
    }

    pub fn new() -> Result<AudioFacade, Error> {
        let vad_control = RwLock::new(VirtualAudio::new()?);
        let audio_output = RwLock::new(VirtualOutput::new(
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
        self.ensure_initialized().await?;
        let capturing = self.audio_output.read().capturing.clone();
        let mut capturing = capturing.lock().await;
        if !*capturing {
            let mut write = self.audio_output.write();
            let sender = write
                .output_sender
                .as_mut()
                .ok_or(format_err!("Failed unwrapping output sender"))?;
            sender.try_send(ExtractMsg::Start)?;
            *(capturing) = true;

            Ok(to_value(true)?)
        } else {
            return Err(format_err!("Cannot StartOutputSave, already started."));
        }
    }

    pub async fn stop_output_save(&self) -> Result<Value, Error> {
        self.ensure_initialized().await?;
        let capturing = self.audio_output.read().capturing.clone();
        let mut capturing = capturing.lock().await;
        if *capturing {
            let mut write = self.audio_output.write();
            write.extracted_data.clear();

            let sender = write
                .output_sender
                .as_mut()
                .ok_or(format_err!("Failed unwrapping output sender"))?;

            let (tx, mut rx) = mpsc::channel(512);
            sender.try_send(ExtractMsg::Stop { out_sender: tx })?;

            let mut saved_audio = rx
                .next()
                .await
                .ok_or_else(|| format_err!("StopOutputSave failed, could not retrieve data."))?;

            write.write_header(saved_audio.len() as u32)?;

            write.extracted_data.append(&mut saved_audio);
            *(capturing) = false;
            Ok(to_value(true)?)
        } else {
            return Err(format_err!("Cannot StopOutputSave, not started."));
        }
    }

    pub async fn get_output_audio(&self) -> Result<Value, Error> {
        self.ensure_initialized().await?;
        let capturing = self.audio_output.read().capturing.clone();
        let capturing = capturing.lock().await;
        if !*capturing {
            Ok(to_value(base64::encode(&self.audio_output.read().extracted_data))?)
        } else {
            return Err(format_err!("GetOutputAudio failed, still saving."));
        }
    }

    pub async fn put_input_audio(&self, args: Value) -> Result<Value, Error> {
        self.ensure_initialized().await?;
        let data = args.get("data").ok_or(format_err!("PutInputAudio failed, no data"))?;
        let data = data.as_str().ok_or(format_err!("PutInputAudio failed, data not string"))?;

        let mut wave_data_vec = base64::decode(data)?;
        let sample_index = args["index"].as_u64().ok_or(format_err!("index not a number"))?;
        let sample_index = sample_index as usize;

        // TODO(perley): check wave format for correct bits per sample and float/int.
        let byte_cnt = wave_data_vec.len();
        {
            let active = self.audio_input.read().active.clone();
            let active = active.lock().await;
            if *active {
                return Err(format_err!("PutInputAudio failed, currently injecting audio."));
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
        self.ensure_initialized().await?;
        {
            let sample_index = args["index"].as_u64().ok_or(format_err!("index not a number"))?;
            let sample_index = sample_index as usize;

            let active = self.audio_input.read().active.clone();
            let active = active.lock().await;
            if *active {
                return Err(format_err!("StartInputInjection failed, already active."));
            } else if !self.audio_input.read().have_data {
                return Err(format_err!("StartInputInjection failed, no Audio data to inject."));
            }
            self.audio_input.write().play(sample_index)?;
        }
        Ok(to_value(true)?)
    }

    pub async fn stop_input_injection(&self) -> Result<Value, Error> {
        self.ensure_initialized().await?;
        {
            let active = self.audio_input.read().active.clone();
            let active = active.lock().await;
            if !*active {
                return Err(format_err!("StopInputInjection failed, not active."));
            }
            self.audio_input.write().stop()?;
        }

        Ok(to_value(true)?)
    }
}
