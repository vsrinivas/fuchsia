// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::audio_streams::*,
    crate::reply::*,
    crate::sequencer,
    crate::throttled_log,
    crate::wire,
    crate::wire_convert::*,
    anyhow::{anyhow, Error},
    futures::TryStreamExt,
    std::cell::Cell,
    std::io::{Read, Write},
    std::vec::Vec,
    tracing,
    zerocopy::{AsBytes, FromBytes},
};

/// Wraps a request parsed from a virtqueue.
///
/// Each virtqueue message is composed of a chain of readable buffers followed by
/// a chain of writable buffers. The "request" is contained in the readable chain
/// and consists of a struct followed (optionally) by audio data.
///
/// We may not know the request type statically. For example, each controlq message
/// is prefixed by a VirtSndHdr, where hdr.code tells us the type of the full struct.
/// To parse a controlq message header, we fill `header_buf` with enough bytes to
/// parse a VirtSndHdr, then when we know the full type, we read additional bytes
/// into `header_buf` so we can parse the full type.
///
/// Once our caller has fully parsed the header, they should use `take_chain` to
/// extract the rest of the message chain, which can be used to read additional
/// request data (as needed) and to write the response.
struct RequestWrapper<'a, 'b> {
    name: String,
    header_buf: Vec<u8>,
    chain: ReadableChain<'a, 'b>,
}

impl<'a, 'b> RequestWrapper<'a, 'b> {
    fn new(name: &str, chain: ReadableChain<'a, 'b>) -> Self {
        Self { name: String::from(name), header_buf: Vec::new(), chain }
    }

    /// Parse a request header with the given type. This can be called multiple
    /// times when the full header type is not initially known. For example, for
    /// controlq messages, we can call this once to parse a VirtSndHdr, then call
    /// it again to parse a more specific type.
    fn parse_header<T: FromBytes>(&mut self) -> Result<T, Error> {
        let header_size = std::mem::size_of::<T>();

        // Read additional header bytes if necessary.
        let old_size = self.header_buf.len();
        if old_size < header_size {
            self.header_buf.resize(header_size, 0u8);

            let want_size = header_size - old_size;
            let read_size = self.chain.read(&mut self.header_buf[old_size..header_size])?;
            if read_size != want_size {
                return Err(anyhow!(
                    "{}: header read failed: got {} bytes, expected {} bytes (of {} total)",
                    self.name,
                    read_size,
                    want_size,
                    header_size,
                ));
            }
        }

        let header = match T::read_from(&self.header_buf[..]) {
            Some(header) => header,
            None => {
                return Err(anyhow!(
                    "{}: failed to parse header of type {} from {} bytes",
                    self.name,
                    std::any::type_name::<T>(),
                    header_size
                ));
            }
        };

        Ok(header)
    }

    /// Like parse_header but also sends a controlq error message.
    /// We have three kinds of results:
    ///
    /// * `Ok(Some(req_wrapper, header))` means we successfully parsed the header.
    ///   Our "self" is carried through the returned `req_wrapper`.
    ///
    /// * `Ok(None)` means we failed to parse the header. We sent a BAD_MSG reply
    ///   and consumed `self`.
    ///
    /// * `Err()` means we failed to parse the type AND the device's virtqueue
    ///   connection is broken, so we were unable to return a BAD_MSG reply.
    ///   We consumed `self`.
    ///
    fn parse_header_or_reply_controlq_err<T: FromBytes>(
        mut self,
    ) -> Result<Option<(Self, T)>, Error> {
        match self.parse_header::<T>() {
            Ok(header) => Ok(Some((self, header))),
            Err(err) => {
                tracing::error!("{}", err);
                reply_controlq::err(self.chain, wire::VIRTIO_SND_S_BAD_MSG)?;
                Ok(None)
            }
        }
    }

    /// Consume this RequestWrapper. Used when the header is fully parsed and the
    /// caller needs access to additional request buffers or to the reply buffers.
    fn take_chain(self) -> ReadableChain<'a, 'b> {
        self.chain
    }
}

struct Jack {
    info: wire::VirtioSndJackInfo,
}

struct Chmap {
    info: wire::VirtioSndChmapInfo,
}

// Direction of PCM audio.
#[derive(Debug, Copy, Clone, PartialEq)]
enum PcmDir {
    Output,
    Input,
}

// This state is derived from 5.14.6.6.1 PCM Command Lifecycle.
#[derive(Debug, Copy, Clone)]
enum PcmState {
    // Have not received a valid SetParameters command.
    // This state is not present in 5.14.6.6.1: it used for streams that have not been initialized.
    // Can transition to: Released
    NoParameters,
    // Received a valid SetParameters command, but do not have a FIDL connection yet.
    // This state combines SET PARAMETERS and RELEASE from 5.14.6.6.1.
    // Can transitions to: Released, Prepared
    Released(AudioStreamParams),
    // Received a valid SetParameters command and have a FIDL connection.
    // Can transition to: Released, Prepared, Started
    Prepared(AudioStreamParams),
    // Stream is running.
    // Can transition to: Stopped
    Started(AudioStreamParams),
    // Stream is stopped.
    // Can transition to: Started, Released
    Stopped(AudioStreamParams),
}

struct PcmStream<'s> {
    info: wire::VirtioSndPcmInfo,
    dir: PcmDir, // type-safe copy of info.direction
    state: Cell<PcmState>,
    stream: Box<dyn AudioStream<'s> + 's>,
}

impl<'s> PcmStream<'s> {
    /// Constructs a new stream.
    ///
    /// REQUIRES: info must have valid fields. For example, if info.direction is
    /// not a valid VIRTIO_SND_D_* value, this will panic.
    fn new(audio: &'s fidl_fuchsia_media::AudioProxy, info: wire::VirtioSndPcmInfo) -> Self {
        let (stream, dir) = match info.direction {
            wire::VIRTIO_SND_D_OUTPUT => (create_audio_output(audio), PcmDir::Output),
            wire::VIRTIO_SND_D_INPUT => (create_audio_input(audio), PcmDir::Input),
            _ => panic!("unexpected info.direction = {:?}", info.direction),
        };
        Self { info, dir, state: Cell::new(PcmState::NoParameters), stream }
    }

    /// Handle VIRTIO_SND_R_PCM_SET_PARAMS
    async fn handle_set_params<'a, 'b>(
        &self,
        req_wrapper: RequestWrapper<'a, 'b>,
    ) -> Result<(), Error> {
        // 5.14.6.6.1 allows calling SetParameters from these states.
        match self.state.get() {
            PcmState::NoParameters | PcmState::Released(_) | PcmState::Prepared(_) => (),
            _ => return self.reply_controlq_err_bad_state(req_wrapper),
        };

        // Validate the request.
        let (req_wrapper, req) =
            match req_wrapper.parse_header_or_reply_controlq_err::<wire::PcmSetParamsRequest>()? {
                Some(x) => x,
                None => return Ok(()),
            };

        let buffer_bytes = req.buffer_bytes.get() as usize;
        let period_bytes = req.period_bytes.get() as usize;
        let features = req.features.get();

        // 5.14.6.6.3.2 Driver Requirements: Stream Parameters
        // - buffer_bytes must be divisible by period_bytes
        if period_bytes == 0 || buffer_bytes % period_bytes != 0 {
            tracing::warn!(
                "{}: buffer_bytes must be divisible by period_bytes: {:?}",
                req_wrapper.name,
                req,
            );
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // - The driver must select supported features only
        if features != 0 {
            tracing::warn!("{}: requested unsupported features: {:?}", req_wrapper.name, req);
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // - The driver must select a valid format, rate, and channel.
        let stream_type = match wire_parameters_to_fidl_stream_type(&req) {
            Some(stream_type) => stream_type,
            None => {
                tracing::warn!(
                    "{}: could not convert to FIDL AudioStreamType: {:?}",
                    req_wrapper.name,
                    req,
                );
                return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
            }
        };

        if stream_type.channels < (self.info.channels_min as u32)
            || stream_type.channels > (self.info.channels_max as u32)
        {
            tracing::warn!(
                "{}: channel count not in supported range [{}, {}]: {:?}",
                req_wrapper.name,
                self.info.channels_min,
                self.info.channels_max,
                req,
            );
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // - The driver must initialize the padding byte to 0.
        if req.padding != 0 {
            tracing::warn!("{}: padding must be zero: {:?}", req_wrapper.name, req);
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // AudioCore requires that each packet contains an integral number of frames.
        // The virtio-sound spec doesn't require this explicitly but it seems unlikely
        // that any driver would use a fractional number of frames per period.
        let frame_bytes = bytes_per_frame(stream_type) as usize;
        if frame_bytes == 0 || period_bytes % frame_bytes != 0 {
            tracing::warn!(
                "{}: period_bytes must be divisible by the frame size ({} bytes): {:?}",
                req_wrapper.name,
                frame_bytes,
                req,
            );
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // 5.14.6.6.1 allows SetParameters after Prepare. When this happens, we should
        // tear down the connection since we're transitioning out of Prepared.
        if let PcmState::Prepared(_) = self.state.get() {
            if let Err(err) = self.stream.disconnect().await {
                tracing::warn!("{}: failed to disconnect: {}", req_wrapper.name, err);
                return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_IO_ERR);
            }
        }

        let params = AudioStreamParams { stream_type, buffer_bytes, period_bytes };
        tracing::info!("{}: set parameters to {:?}", req_wrapper.name, params);
        self.state.set(PcmState::Released(params));
        reply_controlq::success(req_wrapper.take_chain())?;
        Ok(())
    }

    /// Handle VIRTIO_SND_R_PCM_PREPARE
    async fn handle_prepare<'a, 'b>(
        &self,
        req_wrapper: RequestWrapper<'a, 'b>,
    ) -> Result<(), Error> {
        // 5.14.6.6.1 allows calling Prepare from these states.
        let params = match self.state.get() {
            PcmState::Prepared(_) => return Ok(()), // no-op: already prepared
            PcmState::Released(params) => params,
            _ => return self.reply_controlq_err_bad_state(req_wrapper),
        };

        // We must be coming from Released, meaning the stream must be disconnected,
        // hence no need to disconnect before connecting.
        if let Err(err) = self.stream.connect(params).await {
            tracing::warn!("{}: failed to connect: {}", req_wrapper.name, err);
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_IO_ERR);
        }

        self.state.set(PcmState::Prepared(params));
        reply_controlq::success(req_wrapper.take_chain())?;
        Ok(())
    }

    /// Handle VIRTIO_SND_R_PCM_RELEASE
    async fn handle_release<'a, 'b>(
        &self,
        req_wrapper: RequestWrapper<'a, 'b>,
    ) -> Result<(), Error> {
        // 5.14.6.6.1 allows calling Release from these states.
        let params = match self.state.get() {
            PcmState::Prepared(params) | PcmState::Stopped(params) => params,
            _ => return self.reply_controlq_err_bad_state(req_wrapper),
        };

        // In the Prepared and Stopped states, we must already be connected.
        if let Err(err) = self.stream.disconnect().await {
            tracing::warn!("{}: failed to disconnect: {}", req_wrapper.name, err);
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_IO_ERR);
        }

        self.state.set(PcmState::Released(params));
        reply_controlq::success(req_wrapper.take_chain())?;
        Ok(())
    }

    /// Handle VIRTIO_SND_R_PCM_START
    async fn handle_start<'a, 'b>(&self, req_wrapper: RequestWrapper<'a, 'b>) -> Result<(), Error> {
        // 5.14.6.6.1 allows calling Start from these states.
        let params = match self.state.get() {
            PcmState::Prepared(params) | PcmState::Stopped(params) => params,
            _ => return self.reply_controlq_err_bad_state(req_wrapper),
        };

        if let Err(err) = self.stream.start().await {
            tracing::warn!("{}: failed to start: {}", req_wrapper.name, err);
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_IO_ERR);
        }

        self.state.set(PcmState::Started(params));
        reply_controlq::success(req_wrapper.take_chain())?;
        Ok(())
    }

    /// Handle VIRTIO_SND_R_PCM_STOP
    async fn handle_stop<'a, 'b>(&self, req_wrapper: RequestWrapper<'a, 'b>) -> Result<(), Error> {
        // 5.14.6.6.1 allows calling Stop from these states.
        let params = match self.state.get() {
            PcmState::Started(params) => params,
            _ => return self.reply_controlq_err_bad_state(req_wrapper),
        };

        if let Err(err) = self.stream.stop().await {
            tracing::warn!("{}: failed to stop: {}", req_wrapper.name, err);
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_IO_ERR);
        }

        self.state.set(PcmState::Stopped(params));
        reply_controlq::success(req_wrapper.take_chain())?;
        Ok(())
    }

    fn reply_controlq_err_bad_state<'a, 'b>(
        &self,
        req_wrapper: RequestWrapper<'a, 'b>,
    ) -> Result<(), Error> {
        tracing::warn!(
            "{}: invoked from wrong state {:?}; ignoring",
            req_wrapper.name,
            self.state.get(),
        );
        reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG)
    }

    /// Handle output data messages sent to TXQ
    async fn handle_tx<'a, 'b>(
        &self,
        req_wrapper: RequestWrapper<'a, 'b>,
        lock: sequencer::Lock,
    ) -> Result<(), Error> {
        // We can't transfer data unless we have prepared a connection.
        match self.state.get() {
            PcmState::Prepared(_) | PcmState::Started(_) | PcmState::Stopped(_) => (),
            _ => {
                tracing::warn!(
                    "TXQ message received from wrong state {:?}; ignoring",
                    self.state.get()
                );
                reply_txq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG, 0)?;
                return Ok(());
            }
        };

        // Dispatch to our output stream.
        self.stream.on_receive_data(req_wrapper.take_chain(), lock).await?;
        Ok(())
    }

    /// Handle input data requests sent to RXQ
    async fn handle_rx<'a, 'b>(
        &self,
        req_wrapper: RequestWrapper<'a, 'b>,
        lock: sequencer::Lock,
    ) -> Result<(), Error> {
        // We can't transfer data unless we have prepared a connection.
        match self.state.get() {
            PcmState::Prepared(_) | PcmState::Started(_) | PcmState::Stopped(_) => (),
            _ => {
                tracing::warn!(
                    "RXQ message received from wrong state {:?}; ignoring",
                    self.state.get()
                );
                reply_rxq::err_from_readable(
                    req_wrapper.take_chain(),
                    wire::VIRTIO_SND_S_BAD_MSG,
                    0,
                )?;
                return Ok(());
            }
        };

        // Dispatch to our input stream.
        self.stream.on_receive_data(req_wrapper.take_chain(), lock).await?;
        Ok(())
    }
}

/// This trait enables a generic implementation of VIRTIO_SND_R_*_INFO requests.
/// See 5.14.6.1 Item Information Request
trait Info {
    type WireT: AsBytes + std::fmt::Debug;
    fn get_info(&self) -> &Self::WireT;
}

impl Info for Jack {
    type WireT = wire::VirtioSndJackInfo;
    fn get_info(&self) -> &Self::WireT {
        &self.info
    }
}

impl<'s> Info for PcmStream<'s> {
    type WireT = wire::VirtioSndPcmInfo;
    fn get_info(&self) -> &Self::WireT {
        &self.info
    }
}

impl Info for Chmap {
    type WireT = wire::VirtioSndChmapInfo;
    fn get_info(&self) -> &Self::WireT {
        &self.info
    }
}

/// VirtSoundService dispatches messages parsed from virtio-sound virtqs.
pub struct VirtSoundService<'s> {
    jacks: Vec<Jack>,
    pcm_streams: Vec<PcmStream<'s>>,
    chmaps: Vec<Chmap>,
}

impl<'s> VirtSoundService<'s> {
    /// Construct a new device containing the given components.
    ///
    /// REQUIRES: each info must have valid fields. For example, if pcm_infos[k].formats
    /// contains invalid formats, we may fail to accept a legitimate PCM_PREPARE request
    /// from the driver.
    pub fn new(
        jack_infos: &Vec<wire::VirtioSndJackInfo>,
        pcm_infos: &Vec<wire::VirtioSndPcmInfo>,
        chmap_infos: &Vec<wire::VirtioSndChmapInfo>,
        audio: &'s fidl_fuchsia_media::AudioProxy,
    ) -> Self {
        Self {
            jacks: jack_infos.iter().map(|info| Jack { info: *info }).collect(),
            pcm_streams: pcm_infos.iter().map(|info| PcmStream::new(audio, *info)).collect(),
            chmaps: chmap_infos.iter().map(|info| Chmap { info: *info }).collect(),
        }
    }

    /// Dispatch a message from the controlq.
    ///
    /// On error:
    /// * If the error is recoverable, we send an error status back to the driver and return Ok().
    /// * If the error is not recoverable, we return Err().
    pub async fn dispatch_controlq<'a, 'b>(
        &self,
        chain: ReadableChain<'a, 'b>,
    ) -> Result<(), Error> {
        // Each control message is a single struct, where all control structs share
        // a common header. First read that common header to decode which kind of
        // message we have received.
        let req_wrapper = RequestWrapper::new("CONTROLQ", chain);
        let (mut req_wrapper, hdr) =
            match req_wrapper.parse_header_or_reply_controlq_err::<wire::VirtioSndHdr>()? {
                Some(x) => x,
                None => return Ok(()),
            };

        // More specific request name for better debugging.
        req_wrapper.name = String::from(request_code_to_string(hdr.code.get()));

        // Dispatch.
        match hdr.code.get() {
            wire::VIRTIO_SND_R_JACK_INFO => VirtSoundService::handle_info(req_wrapper, &self.jacks),
            wire::VIRTIO_SND_R_PCM_INFO => {
                VirtSoundService::handle_info(req_wrapper, &self.pcm_streams)
            }
            wire::VIRTIO_SND_R_CHMAP_INFO => {
                VirtSoundService::handle_info(req_wrapper, &self.chmaps)
            }

            wire::VIRTIO_SND_R_PCM_SET_PARAMS
            | wire::VIRTIO_SND_R_PCM_PREPARE
            | wire::VIRTIO_SND_R_PCM_RELEASE
            | wire::VIRTIO_SND_R_PCM_START
            | wire::VIRTIO_SND_R_PCM_STOP => self.handle_pcm_op(req_wrapper).await,

            // VIRTIO_SND_R_JACK_REMAP is not supported. This command is currently not
            // used by Linux. It is intended to allow changing per-jack priorities and
            // how specific PCM channels map to hardware jacks. Neither of those features
            // are currently supported by audio_core.
            code => {
                tracing::error!("controlq dispatch error: unimplemented controlq code {}", code);
                reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_NOT_SUPP)
            }
        }
    }

    /// Handle VIRTIO_SND_R_*_INFO.
    fn handle_info<'a, 'b, T: Info>(
        req_wrapper: RequestWrapper<'a, 'b>,
        infos: &Vec<T>,
    ) -> Result<(), Error> {
        // CONTROLQ messages should be infrequent enough that we can log all of them.
        throttled_log::info!("CONTROLQ request: {}", req_wrapper.name);

        let (req_wrapper, req) =
            match req_wrapper.parse_header_or_reply_controlq_err::<wire::GenericInfoRequest>()? {
                Some(x) => x,
                None => return Ok(()),
            };

        // Validate the request.
        let start_id = req.start_id.get() as usize;
        let end_id = start_id + (req.count.get() as usize);
        if end_id > infos.len() {
            tracing::error!(
                "{}: requested ids {}..{}, but device has only {} infos",
                req_wrapper.name,
                req.start_id.get(),
                end_id,
                infos.len(),
            );
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }
        let min_size = std::mem::size_of::<T::WireT>();
        if (req.size.get() as usize) < min_size {
            tracing::error!(
                "{}: response struct is {} bytes, but driver provided only {} bytes",
                req_wrapper.name,
                min_size,
                req.size.get(),
            );
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // Write all requested infos.
        let mut chain = reply_controlq::success(req_wrapper.take_chain())?;
        for id in start_id..end_id {
            chain.write(infos[id].get_info().as_bytes())?;
        }
        Ok(())
    }

    /// Handle VIRTIO_SND_R_PCM_*, except VIRTIO_SND_R_PCM_INFO.
    async fn handle_pcm_op<'a, 'b>(
        &self,
        req_wrapper: RequestWrapper<'a, 'b>,
    ) -> Result<(), Error> {
        let (mut req_wrapper, hdr) =
            match req_wrapper.parse_header_or_reply_controlq_err::<wire::VirtioSndPcmHdr>()? {
                Some(x) => x,
                None => return Ok(()),
            };

        // Validate the request.
        let id = hdr.stream_id.get() as usize;
        if id >= self.pcm_streams.len() {
            tracing::error!("{}: unknown stream_id {}", req_wrapper.name, id);
            return reply_controlq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // CONTROLQ messages should be infrequent enough that we can log all of them.
        req_wrapper.name = format!("stream[{}].{}", id, req_wrapper.name);
        throttled_log::info!("CONTROLQ request: {}", req_wrapper.name);

        let stream = &self.pcm_streams[id];
        match hdr.hdr.code.get() {
            wire::VIRTIO_SND_R_PCM_SET_PARAMS => stream.handle_set_params(req_wrapper).await,
            wire::VIRTIO_SND_R_PCM_PREPARE => stream.handle_prepare(req_wrapper).await,
            wire::VIRTIO_SND_R_PCM_RELEASE => stream.handle_release(req_wrapper).await,
            wire::VIRTIO_SND_R_PCM_START => stream.handle_start(req_wrapper).await,
            wire::VIRTIO_SND_R_PCM_STOP => stream.handle_stop(req_wrapper).await,
            code => panic!("unexpected code {}", code),
        }
    }

    /// Dispatch a message from the txq.
    /// On error:
    /// * If the error is recoverable, we send an error status back to the driver and return Ok().
    /// * If the error is not recoverable, we return Err().
    ///
    /// The sequencer lock is used to ensure that packets are forwarded in the expected order.
    /// It will be held until the packet is forwarded, then may be released to allow waiting
    /// concurrently for multiple packets.
    pub async fn dispatch_txq<'a, 'b>(
        &self,
        chain: ReadableChain<'a, 'b>,
        lock: sequencer::Lock,
    ) -> Result<(), Error> {
        // Each tx message is composed of a header struct followed by PCM audio data.
        // First read the header so we know which stream to dispatch to.
        let mut req_wrapper = RequestWrapper::new("TXQ", chain);
        let hdr = match req_wrapper.parse_header::<wire::VirtioSndPcmXfer>() {
            Ok(x) => x,
            Err(err) => {
                tracing::error!("{}", err);
                return reply_txq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG, 0);
            }
        };

        // TX messages must target a valid output stream.
        let id = hdr.stream_id.get() as usize;
        if id >= self.pcm_streams.len() {
            tracing::error!("txq dispatch error: unknown stream_id {}", id);
            return reply_txq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG, 0);
        }

        let stream = &self.pcm_streams[id];
        if stream.dir != PcmDir::Output {
            tracing::error!("txq dispatch error: stream_id {} is an input stream", id);
            return reply_txq::err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG, 0);
        }

        // Dispatch to the stream.
        stream.handle_tx(req_wrapper, lock).await
    }

    /// Dispatch a message from the rxq.
    /// On error:
    /// * If the error is recoverable, we send an error status back to the driver and return Ok().
    /// * If the error is not recoverable, we return Err().
    ///
    /// The sequencer lock is used to ensure that packets are forwarded in the expected order.
    /// It will be held until the packet is forwarded, then may be released to allow waiting
    /// concurrently for multiple packets.
    pub async fn dispatch_rxq<'a, 'b>(
        &self,
        chain: ReadableChain<'a, 'b>,
        lock: sequencer::Lock,
    ) -> Result<(), Error> {
        // Each rx message is composed of a header struct followed by a writable buffer and status.
        // First read the header so we know which stream to dispatch to.
        let mut req_wrapper = RequestWrapper::new("RXQ", chain);
        let hdr = match req_wrapper.parse_header::<wire::VirtioSndPcmXfer>() {
            Ok(x) => x,
            Err(err) => {
                tracing::error!("{}", err);
                return reply_rxq::err_from_readable(
                    req_wrapper.take_chain(),
                    wire::VIRTIO_SND_S_BAD_MSG,
                    0,
                );
            }
        };

        // RX messages must target a valid input stream.
        let id = hdr.stream_id.get() as usize;
        if id >= self.pcm_streams.len() {
            tracing::error!("rxq dispatch error: unknown stream_id {}", id);
            return reply_rxq::err_from_readable(
                req_wrapper.take_chain(),
                wire::VIRTIO_SND_S_BAD_MSG,
                0,
            );
        }

        let stream = &self.pcm_streams[id];
        if stream.dir != PcmDir::Input {
            tracing::error!("rxq dispatch error: stream_id {} is an output stream", id);
            return reply_rxq::err_from_readable(
                req_wrapper.take_chain(),
                wire::VIRTIO_SND_S_BAD_MSG,
                0,
            );
        }

        // Dispatch to the stream.
        stream.handle_rx(req_wrapper, lock).await
    }

    /// Performs background work. The returned future may never complete. To stop performing
    /// background work, drop the returned future.
    pub async fn do_background_work(&self) -> Result<(), Error> {
        futures::stream::iter(self.pcm_streams.iter().map(|pcm_stream| Ok(pcm_stream)))
            .try_for_each_concurrent(
                None, /* unlimited concurrency */
                |pcm_stream| async move { pcm_stream.stream.do_background_work().await },
            )
            .await
    }
}
