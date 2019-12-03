// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_media::*,
    fidl_fuchsia_mediacodec::*,
    fuchsia_stream_processors::*,
    fuchsia_syslog::fx_vlog,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{
        io::{self, AsyncWrite},
        ready,
        stream::{FusedStream, Stream},
        task::{Context, Poll, Waker},
        Future, StreamExt,
    },
    parking_lot::{Mutex, RwLock},
    std::{
        collections::{HashMap, HashSet, VecDeque},
        convert::TryFrom,
        mem,
        pin::Pin,
        sync::Arc,
    },
};

fn fidl_error_to_io_error(e: fidl::Error) -> io::Error {
    io::Error::new(io::ErrorKind::Other, format_err!("Fidl Error: {}", e))
}

#[derive(Debug)]
/// Listener is a three-valued Option that captures the waker that a listener needs to be woken
/// upon when it polls the future instead of at registration time.
enum Listener {
    /// No one is listening.
    None,
    /// Someone wants to listen but hasn't polled.
    New,
    /// Someone is listening, and can be woken with the waker.
    Some(Waker),
}

impl Listener {
    /// If a listener is registered, returns the current state and replaces it with
    /// New.  Otherwise returns None and does nothing.
    fn take(&mut self) -> Listener {
        match self {
            Listener::None => Listener::None,
            _ => mem::replace(self, Listener::New),
        }
    }
}

impl Default for Listener {
    fn default() -> Self {
        Listener::None
    }
}

/// A queue of encoded packets, to be sent to the `listener` when it polls next.
struct OutputQueue {
    /// The listener. Woken when a packet arrives after a previous poll() returned Pending.
    listener: Listener,
    /// A queue of encoded packets to be delivered to the receiver.
    queue: VecDeque<Packet>,
    /// True when the stream has received an end-of-stream message. The stream will return None
    /// after the `queue` is empty.
    ended: bool,
}

impl OutputQueue {
    /// Adds a packet to the queue and wakes the listener if necessary.
    fn enqueue(&mut self, packet: Packet) {
        self.queue.push_back(packet);
        self.notify_listener();
    }

    /// Signals the end of the stream has happened.
    /// Wakes the listener if necessary.
    fn mark_ended(&mut self) {
        self.ended = true;
        self.notify_listener();
    }

    fn notify_listener(&mut self) {
        if let Listener::Some(waker) = self.listener.take() {
            waker.wake_by_ref();
        }
    }
}

impl Default for OutputQueue {
    fn default() -> Self {
        OutputQueue { listener: Listener::default(), queue: VecDeque::new(), ended: false }
    }
}

impl Stream for OutputQueue {
    type Item = Packet;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        match self.queue.pop_front() {
            Some(packet) => Poll::Ready(Some(packet)),
            None if self.ended => Poll::Ready(None),
            None => {
                self.listener = Listener::Some(cx.waker().clone());
                Poll::Pending
            }
        }
    }
}

/// Index of an input buffer to be shared between the Encoder and the StreamProcesor.
#[derive(PartialEq, Eq, Hash, Clone)]
struct InputBufferIndex(u32);

/// The EncoderInner handles the events that come from the StreamProcessor, mostly related to setup
/// of the buffers and handling the output packets as they arrive.
struct EncoderInner {
    /// The proxy to the stream processor.
    processor: StreamProcessorProxy,
    /// The event stream from the StreamProcessor.  We handle these internally.
    events: StreamProcessorEventStream,
    /// Set of buffers that are used for input
    input_buffers: HashMap<InputBufferIndex, zx::Vmo>,
    /// The size in bytes of each input packet
    input_packet_size: u64,
    /// The set of input buffers that are available for writing by the client, without the one
    /// possibly being used by the input_cursor.
    encoder_owned: HashSet<InputBufferIndex>,
    /// A cursor on the next input buffer location to be written to when new input data arrives.
    input_cursor: Option<(InputBufferIndex, u64)>,
    /// A set of wakers waiting for the Encoder to be writable.
    input_wakers: Vec<Waker>,
    /// The encoded data - a set of output buffers that will be written by the server.
    output_buffers: Vec<zx::Vmo>,
    /// The size of each output packet
    output_packet_size: u64,
    /// An queue of the indexes of output buffers that have been filled by the processor and a
    /// waiter if someone is waiting on it.
    output_queue: Mutex<OutputQueue>,
}

impl EncoderInner {
    /// Handles an event from the StreamProcessor. A number of these events come on stream start to
    /// setup the input and output buffers, and from then on the output packets and end of stream
    /// marker, and the input packets are marked as usable after they are processed.
    fn handle_event(&mut self, evt: StreamProcessorEvent) -> Result<(), Error> {
        match evt {
            StreamProcessorEvent::OnInputConstraints { input_constraints } => {
                let input_constraints = ValidStreamBufferConstraints::try_from(input_constraints)?;
                let mut settings = input_constraints.default_settings;
                // Lifetime ordinal will always be set to 0 in the default settings.
                // We only have one stream, so we set this to 1 and accept the default settings.
                settings.buffer_lifetime_ordinal = 1;
                self.processor.set_input_buffer_settings(settings.into())?;

                let packet_count =
                    settings.packet_count_for_client + settings.packet_count_for_server;
                self.input_packet_size = settings.per_packet_buffer_bytes as u64;
                for idx in 0..packet_count {
                    // TODO(41553): look into letting sysmem allocate these buffers
                    let (stream_buffer, vmo) = make_buffer(self.input_packet_size, 1, idx)?;
                    self.input_buffers.insert(InputBufferIndex(idx), vmo);
                    self.encoder_owned.insert(InputBufferIndex(idx));
                    self.processor.add_input_buffer(stream_buffer)?;
                }
                self.setup_input_cursor();
            }
            StreamProcessorEvent::OnOutputConstraints { output_config } => {
                let output_config = ValidStreamOutputConstraints::try_from(output_config)?;
                if !output_config.buffer_constraints_action_required {
                    return Ok(());
                }

                let mut settings = output_config.buffer_constraints.default_settings;
                // We just accept the new output constraints and rebuild our buffers.
                // This is always set to zero in the defaults. We only have one buffer lifetime for
                // this encoder.
                settings.buffer_lifetime_ordinal = 1;

                self.processor.set_output_buffer_settings(settings.into())?;
                let packet_count =
                    settings.packet_count_for_client + settings.packet_count_for_server;
                self.output_packet_size = settings.per_packet_buffer_bytes as u64;
                for idx in 0..packet_count {
                    let (stream_buffer, vmo) = make_buffer(self.output_packet_size, 1, idx)?;
                    self.output_buffers.push(vmo);
                    self.processor.add_output_buffer(stream_buffer)?;
                }
            }
            StreamProcessorEvent::OnOutputPacket { output_packet, .. } => {
                let mut lock = self.output_queue.lock();
                lock.enqueue(output_packet);
            }
            StreamProcessorEvent::OnFreeInputPacket {
                free_input_packet:
                    PacketHeader { buffer_lifetime_ordinal: Some(_ord), packet_index: Some(idx) },
            } => {
                self.encoder_owned.insert(InputBufferIndex(idx));
                self.setup_input_cursor();
            }
            StreamProcessorEvent::OnOutputEndOfStream { .. } => {
                let mut lock = self.output_queue.lock();
                lock.mark_ended();
            }
            e => fx_vlog!(1, "Unhandled stream processor event: {:#?}", e),
        }
        Ok(())
    }

    /// Process all the events that are currently available from the StreamProcessor, and set the
    /// waker of `cx` to be woken when another event arrives.
    fn process_events(&mut self, cx: &mut Context<'_>) -> Result<usize, Error> {
        let mut processed = 0;
        loop {
            match self.events.poll_next_unpin(cx) {
                Poll::Pending => return Ok(processed),
                Poll::Ready(Some(Err(e))) => return Err(e.into()),
                Poll::Ready(Some(Ok(event))) => self.handle_event(event)?,
                Poll::Ready(None) => return Err(format_err!("Encoder disconnected")),
            }
            processed = processed + 1;
        }
    }

    /// Attempts to set up a new input cursor, out of the current set of client owned input buffers.
    /// If the cursor is already set, this does nothing.
    fn setup_input_cursor(&mut self) {
        if self.input_cursor.is_some() {
            // Nothing to be done
            return;
        }
        let next_idx = match self.encoder_owned.iter().cloned().next() {
            None => return,
            Some(idx) => idx,
        };
        self.encoder_owned.take(&next_idx).unwrap();
        self.input_cursor = Some((next_idx, 0));
        self.input_wakers.drain(..).for_each(|w| w.wake_by_ref());
    }

    /// Reads an output packet from the output buffers, and marks the packets as recycled so the
    /// output buffer can be reused. Allocates a new vector to hold the data.
    fn read_output_packet(&mut self, packet: Packet) -> Result<Vec<u8>, Error> {
        let packet = ValidPacket::try_from(packet)?;

        let output_size = packet.valid_length_bytes as usize;
        let offset = packet.start_offset as u64;
        let mut output = vec![0; output_size];
        let buf_idx = packet.buffer_index as usize;
        let vmo = self.output_buffers.get_mut(buf_idx).expect("output vmo should exist");
        vmo.read(&mut output, offset)?;
        self.processor.recycle_output_packet(packet.header.into())?;
        Ok(output)
    }
}

/// Struct representing a CodecFactory Encoder.
/// Input sent to the encoder via `Encoder::write` is queued for delivery, and delivered
/// whenever a buffer is full or `Encoder::flush` is called.  Encoded output can be retrieved using
/// an `EncodedStream` from `Encoder::take_encoded_stream`.
pub struct Encoder {
    inner: Arc<RwLock<EncoderInner>>,
}

fn make_buffer(
    size_bytes: u64,
    ordinal: u64,
    index: u32,
) -> Result<(StreamBuffer, zx::Vmo), Error> {
    let vmo = zx::Vmo::create(size_bytes)?;
    let vmo_copy = vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?;

    let data = StreamBufferData::Vmo(StreamBufferDataVmo {
        vmo_handle: Some(vmo),
        vmo_usable_start: Some(0),
        vmo_usable_size: Some(size_bytes),
    });
    Ok((
        StreamBuffer {
            buffer_lifetime_ordinal: Some(ordinal),
            buffer_index: Some(index),
            data: Some(data),
        },
        vmo_copy,
    ))
}

/// An EncodedStream is a Stream of encoded data from an Encoder.
/// Returned from `Encoder::take_encoded_stream`.
pub struct EncodedStream {
    inner: Arc<RwLock<EncoderInner>>,
}

impl Encoder {
    /// Create a new Encoder, with the given `input_domain` and `encoder_settings`.  See
    /// stream_processor.fidl for descriptions of these parameters.  This is only meant for audio
    /// encoding.
    pub fn create(
        input_domain: DomainFormat,
        encoder_settings: EncoderSettings,
    ) -> Result<Encoder, Error> {
        let format_details = FormatDetails {
            domain: Some(input_domain),
            encoder_settings: Some(encoder_settings),
            format_details_version_ordinal: Some(1),
            mime_type: Some("audio/pcm".to_string()),
            oob_bytes: None,
            pass_through_parameters: None,
            timebase: None,
        };

        let encoder_params =
            CreateEncoderParams { input_details: Some(format_details), require_hw: Some(false) };

        let encoder_svc = fuchsia_component::client::connect_to_service::<CodecFactoryMarker>()
            .context("Failed to connect to Codec Factory")?;

        let (stream_processor_client, stream_processor_serverend) =
            fidl::endpoints::create_endpoints()?;
        let processor = stream_processor_client.into_proxy()?;

        encoder_svc.create_encoder(encoder_params, stream_processor_serverend)?;

        let events = processor.take_event_stream();

        let encoder = Encoder {
            inner: Arc::new(RwLock::new(EncoderInner {
                processor,
                events,
                input_buffers: HashMap::new(),
                input_packet_size: 0,
                encoder_owned: HashSet::new(),
                input_cursor: None,
                input_wakers: Vec::new(),
                output_buffers: Vec::new(),
                output_packet_size: 0,
                output_queue: Default::default(),
            })),
        };

        Ok(encoder)
    }

    /// Take a stream object which will produce the output of the encoder.
    /// Only one EncodedStream object can exist at a time, and this will return an Error if it is
    /// already taken.
    pub fn take_encoded_stream(&mut self) -> Result<EncodedStream, Error> {
        {
            let read = self.inner.read();
            let mut lock = read.output_queue.lock();
            if let Listener::None = lock.listener {
                lock.listener = Listener::New;
            } else {
                return Err(format_err!("Encoded stream already taken"));
            }
        }
        Ok(EncodedStream { inner: self.inner.clone() })
    }

    /// Returns a future that will wait on any events from the underlying StreamProcessor.
    /// This is not necessary in normal operation, as StreamProcessor events are also processed
    /// when writing to the Encoder (using AsyncWrite) and reading from the EncoderStream.
    pub fn process_events(&self) -> EncoderProcess {
        EncoderProcess { inner: self.inner.clone() }
    }

    /// Deliver input to the encoder.  Returns the number of bytes delivered to the encoder.
    fn send_to_encoder(&mut self, bytes: &[u8]) -> Result<usize, io::Error> {
        let mut bytes_idx = 0;
        while bytes.len() > bytes_idx {
            {
                let mut write = self.inner.write();
                let (idx, size) = match write.input_cursor.take() {
                    None => return Ok(bytes_idx),
                    Some(x) => x,
                };
                let space_left = write.input_packet_size - size;
                let left_to_write = bytes.len() - bytes_idx;
                let buffer_vmo = write.input_buffers.get_mut(&idx).expect("need buffer vmo");
                if space_left as usize >= left_to_write {
                    let write_buf = &bytes[bytes_idx..];
                    let write_len = write_buf.len();
                    buffer_vmo.write(write_buf, size)?;
                    bytes_idx += write_len;
                    write.input_cursor = Some((idx, size + write_len as u64));
                    assert!(bytes.len() == bytes_idx);
                    return Ok(bytes_idx);
                }
                let end_idx = bytes_idx + space_left as usize;
                let write_buf = &bytes[bytes_idx..end_idx];
                let write_len = write_buf.len();
                buffer_vmo.write(write_buf, size)?;
                bytes_idx += write_len;
                // this buffer is done, ship it!
                assert_eq!(size + write_len as u64, write.input_packet_size);
                write.input_cursor = Some((idx, write.input_packet_size));
            }
            self.flush()?;
        }
        Ok(bytes_idx)
    }

    /// Flush the input buffer to the processor, relinquishing the ownership of the buffer
    /// currently in the input cursor, and picking a new input buffer.  If there is no input
    /// buffer left, the input cursor is left as None.
    pub fn flush(&mut self) -> Result<(), io::Error> {
        let mut write = self.inner.write();
        if write.input_cursor.is_none() {
            // Nothing to flush, nothing can have been written to an empty input cursor.
            return Ok(());
        }
        let (idx, size) = write.input_cursor.take().expect("input cursor is none");
        if size == 0 {
            // Can't send empty packet to processor.
            write.input_cursor = Some((idx, size));
            return Ok(());
        }
        let packet = Packet {
            header: Some(PacketHeader {
                buffer_lifetime_ordinal: Some(1),
                packet_index: Some(idx.0),
            }),
            buffer_index: Some(idx.0),
            stream_lifetime_ordinal: Some(1),
            start_offset: Some(0),
            valid_length_bytes: Some(size as u32),
            timestamp_ish: None,
            start_access_unit: Some(true),
            known_end_access_unit: Some(true),
        };
        write.processor.queue_input_packet(packet).map_err(fidl_error_to_io_error)?;
        // pick another buffer for the input cursor
        write.setup_input_cursor();
        Ok(())
    }

    /// Test whether it is possible to write to the Encoder. If there are no input buffers
    /// available, returns Poll::Pending and arranges for the current task to receive a
    /// notification when an input buffer becomes available or the encoder is closed.
    fn poll_writable(&mut self, cx: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        let mut write = self.inner.write();
        if let Err(e) = write.process_events(cx) {
            return Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, e)));
        }
        match &write.input_cursor {
            Some(_) => Poll::Ready(Ok(())),
            None => {
                write.input_wakers.push(cx.waker().clone());
                Poll::Pending
            }
        }
    }

    pub fn close(&mut self) -> Result<(), io::Error> {
        self.flush()?;

        let mut write = self.inner.write();

        write.processor.queue_input_end_of_stream(1).map_err(fidl_error_to_io_error)?;
        // TODO: indicate this another way so that we can send an error if someone tries to write
        // it after it's closed.
        write.input_cursor = None;
        Ok(())
    }
}

/// Returned by `Encoder::process_events()`. Rarely used in normal operation, as delivering data to
/// the Encoder and reading from the EncodedStream will process events.
pub struct EncoderProcess {
    inner: Arc<RwLock<EncoderInner>>,
}

impl Future for EncoderProcess {
    type Output = Result<usize, Error>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut write = self.inner.write();
        match write.process_events(cx) {
            Ok(0) => Poll::Pending,
            n => Poll::Ready(n),
        }
    }
}

impl AsyncWrite for Encoder {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        ready!(self.poll_writable(cx))?;
        match self.send_to_encoder(buf) {
            Ok(written) => Poll::Ready(Ok(written)),
            Err(e) => Poll::Ready(Err(e.into())),
        }
    }

    fn poll_flush(mut self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(self.flush())
    }

    fn poll_close(mut self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(self.flush())
    }
}

impl Stream for EncodedStream {
    type Item = Result<Vec<u8>, Error>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut write = self.inner.write();
        // Process the event stream if we can, or set a waker on it.
        if let Err(e) = write.process_events(cx) {
            return Poll::Ready(Some(Err(e.into())));
        }

        let packet = {
            let mut queue = write.output_queue.lock();
            match ready!(queue.poll_next_unpin(cx)) {
                None => return Poll::Ready(None),
                Some(packet) => packet,
            }
        };
        Poll::Ready(Some(write.read_output_packet(packet)))
    }
}

impl FusedStream for EncodedStream {
    fn is_terminated(&self) -> bool {
        self.inner.read().output_queue.lock().ended
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use byteorder::{ByteOrder, NativeEndian};
    use fuchsia_async::{self as fasync, TimeoutExt};
    use fuchsia_zircon::Duration;
    use futures::io::AsyncWriteExt;
    use futures_test::task::new_count_waker;
    use hex;
    use mundane::hash::{Digest, Hasher, Sha256};
    use std::io::Write;
    use stream_processor_test::ExpectedDigest;

    const PCM_SAMPLE_SIZE: usize = 2;

    #[derive(Clone, Debug)]
    pub struct PcmAudio {
        pcm_format: PcmFormat,
        buffer: Vec<u8>,
    }

    impl PcmAudio {
        pub fn create_saw_wave(pcm_format: PcmFormat, frame_count: usize) -> Self {
            const FREQUENCY: f32 = 20.0;
            const AMPLITUDE: f32 = 0.2;

            let pcm_frame_size = PCM_SAMPLE_SIZE * pcm_format.channel_map.len();
            let samples_per_frame = pcm_format.channel_map.len();
            let sample_count = frame_count * samples_per_frame;

            let mut buffer = vec![0; frame_count * pcm_frame_size];

            for i in 0..sample_count {
                let frame = (i / samples_per_frame) as f32;
                let value =
                    ((frame * FREQUENCY / (pcm_format.frames_per_second as f32)) % 1.0) * AMPLITUDE;
                let sample = (value * i16::max_value() as f32) as i16;

                let mut sample_bytes = [0; std::mem::size_of::<i16>()];
                NativeEndian::write_i16(&mut sample_bytes, sample);

                let offset = i * PCM_SAMPLE_SIZE;
                buffer[offset] = sample_bytes[0];
                buffer[offset + 1] = sample_bytes[1];
            }

            Self { pcm_format, buffer }
        }

        pub fn frame_size(&self) -> usize {
            self.pcm_format.channel_map.len() * PCM_SAMPLE_SIZE
        }
    }

    // Note: stolen from audio_encoder_test, update to stream_processor_test lib when this gets
    // moved.
    pub struct BytesValidator {
        pub output_file: Option<&'static str>,
        pub expected_digest: ExpectedDigest,
    }

    impl BytesValidator {
        fn write_and_hash(&self, mut file: impl Write, bytes: &[u8]) -> Result<(), Error> {
            let mut hasher = Sha256::default();

            file.write_all(&bytes)?;
            hasher.update(&bytes);

            let digest = hasher.finish().bytes();
            if self.expected_digest.bytes != digest {
                return Err(format_err!(
                    "Expected {}; got {}",
                    self.expected_digest,
                    hex::encode(digest)
                ))
                .into();
            }

            Ok(())
        }

        fn output_file(&self) -> Result<impl Write, Error> {
            Ok(if let Some(file) = self.output_file {
                Box::new(std::fs::File::create(file)?) as Box<dyn Write>
            } else {
                Box::new(std::io::sink()) as Box<dyn Write>
            })
        }

        fn validate(&self, bytes: &[u8]) -> Result<(), Error> {
            self.write_and_hash(self.output_file()?, &bytes)
        }
    }

    const TIMEOUT: Duration = Duration::from_millis(250);

    #[test]
    fn encode_sbc() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let pcm_format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 44100,
            channel_map: vec![AudioChannelId::Cf],
        };

        let sub_bands = SbcSubBands::SubBands4;
        let block_count = SbcBlockCount::BlockCount8;

        let input_frames = 3000;

        let pcm_audio = PcmAudio::create_saw_wave(pcm_format.clone(), input_frames);

        let sbc_encoder_settings = EncoderSettings::Sbc(SbcEncoderSettings {
            sub_bands,
            block_count,
            allocation: SbcAllocation::AllocLoudness,
            channel_mode: SbcChannelMode::Mono,
            bit_pool: 59, // Recommended from the SBC spec for these parameters.
        });

        let input_domain = DomainFormat::Audio(AudioFormat::Uncompressed(
            AudioUncompressedFormat::Pcm(pcm_format),
        ));

        let mut encoder =
            Encoder::create(input_domain, sbc_encoder_settings).expect("to create Encoder");

        let mut encoded_stream = encoder.take_encoded_stream().expect("Stream shouldn't be taken");

        // Shouldn't be able to take the stream twice
        assert!(encoder.take_encoded_stream().is_err());

        // Polling the encoded stream before the encoder has started up should wake it when
        // output starts happening, set up the poll here.
        let encoded_fut = encoded_stream.next();

        let (waker, encoder_fut_wake_count) = new_count_waker();
        let mut counting_ctx = Context::from_waker(&waker);

        fasync::pin_mut!(encoded_fut);
        assert!(encoded_fut.poll(&mut counting_ctx).is_pending());

        let mut frames_sent = 0;

        let frames_per_packet: usize = 8; // Randomly chosen by fair d10 roll.
        let packet_size = pcm_audio.frame_size() * frames_per_packet;

        for frames in pcm_audio.buffer.as_slice().chunks(packet_size) {
            let written_fut = encoder.write(&frames);

            let mut written_fut = written_fut
                .on_timeout(fasync::Time::after(TIMEOUT), || panic!("Encoder write timed out"));

            let written_bytes =
                exec.run_singlethreaded(&mut written_fut).expect("to write to encoder");

            assert_eq!(frames.len(), written_bytes);
            frames_sent += frames.len() / pcm_audio.frame_size();
        }

        encoder.close().expect("stream should always be closable");

        assert_eq!(input_frames, frames_sent);

        // After the encoder runs, the encoded future should have been woken once the output
        // started.
        let mut processed_events = 0;
        // 2 encoder events that happen before Output is guaranteed: OnOutputConstraints, OnOutputPacket
        while processed_events < 2 {
            let mut process_fut = encoder
                .process_events()
                .on_timeout(fasync::Time::after(TIMEOUT), || panic!("Encoder wait too long"));
            let process_result = exec.run_singlethreaded(&mut process_fut);
            assert!(process_result.is_ok());
            processed_events += process_result.unwrap();
        }

        assert_eq!(encoder_fut_wake_count.get(), 1);

        // Get data from the output now.
        let mut encoded = Vec::new();

        loop {
            let encoded_fut = encoded_stream.next();

            let mut encoded_fut = encoded_fut.on_timeout(fasync::Time::after(TIMEOUT), || {
                panic!("Encoder processing timed out")
            });

            match exec.run_singlethreaded(&mut encoded_fut) {
                Some(Ok(enc_data)) => {
                    assert!(!enc_data.is_empty());
                    encoded.extend_from_slice(&enc_data);
                }
                Some(Err(e)) => {
                    panic!("Unexpected error when polling encoded data: {}", e);
                }
                None => {
                    break;
                }
            }
        }

        // Match the encoded data to the known hash.
        let expected_digest = ExpectedDigest::new(
            "Sbc: 44.1kHz/Loudness/Mono/bitpool 56/blocks 8/subbands 4",
            "5c65a88bda3f132538966d87df34aa8675f85c9892b7f9f5571f76f3c7813562",
        );
        let hash_validator = BytesValidator { output_file: None, expected_digest };

        assert_eq!(6110, encoded.len(), "Encoded size should be equal");

        let validated = hash_validator.validate(encoded.as_slice());
        assert!(validated.is_ok(), "Failed hash: {:?}", validated);
    }
}
