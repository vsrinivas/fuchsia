// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::reply::*,
    crate::wire,
    crate::wire_convert::*,
    anyhow::Error,
    std::io::{Read, Write},
    std::vec::Vec,
    tracing,
    virtio_device::chain::ReadableChain,
    virtio_device::mem::DriverMem,
    virtio_device::queue::DriverNotify,
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
struct RequestWrapper<'a, 'b, 'c, N: DriverNotify, M: DriverMem> {
    name: &'c str,
    header_buf: Vec<u8>,
    chain: ReadableChain<'a, 'b, N, M>,
}

impl<'a, 'b, 'c, N: DriverNotify, M: DriverMem> RequestWrapper<'a, 'b, 'c, N, M> {
    fn new(name: &'c str, chain: ReadableChain<'a, 'b, N, M>) -> Self {
        Self { name, header_buf: Vec::new(), chain }
    }

    /// Returns a name for this request, used in log and error messages.
    /// For example: "VIRTIO_SND_R_JACK_INFO".
    fn name(&self) -> &str {
        self.name
    }

    /// Parse a request header with the given type. This can be called multiple
    /// times when the full header type is not initially known. For example, for
    /// controlq messages, we can call this once to parse a VirtSndHdr, then call
    /// it again to parse a more specific type.
    ///
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
    fn parse_header_or_reply_with_err<T: FromBytes>(mut self) -> Result<Option<(Self, T)>, Error> {
        let header_size = std::mem::size_of::<T>();

        // Read additional header bytes if necessary.
        let old_size = self.header_buf.len();
        if old_size < header_size {
            self.header_buf.resize(header_size, 0u8);

            let want_size = header_size - old_size;
            let read_size = self.chain.read(&mut self.header_buf[old_size..header_size])?;
            if read_size != want_size {
                tracing::error!(
                    "{}: header read failed: got {} bytes, expected {} bytes (of {} total)",
                    self.name,
                    read_size,
                    want_size,
                    header_size,
                );
                reply_with_err(self.take_chain(), wire::VIRTIO_SND_S_BAD_MSG)?;
                return Ok(None);
            }
        }

        let header = match T::read_from(&self.header_buf[..]) {
            Some(header) => header,
            None => {
                tracing::error!(
                    "{}: failed to parse header of type {} from {} bytes",
                    self.name,
                    std::any::type_name::<T>(),
                    header_size
                );
                reply_with_err(self.chain, wire::VIRTIO_SND_S_BAD_MSG)?;
                return Ok(None);
            }
        };

        Ok(Some((self, header)))
    }

    /// Consume this RequestWrapper. Used when the header is fully parsed and the
    /// caller needs access to additional request buffers or to the reply buffers.
    fn take_chain(self) -> ReadableChain<'a, 'b, N, M> {
        self.chain
    }
}

struct Jack {
    info: wire::VirtioSndJackInfo,
}

struct PcmStream {
    info: wire::VirtioSndPcmInfo,
}

struct Chmap {
    info: wire::VirtioSndChmapInfo,
}

// This trait enables a generic implementation of VIRTIO_SND_R_*_INFO requests.
// See 5.14.6.1 Item Information Request
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

impl Info for PcmStream {
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
pub struct VirtSoundService {
    jacks: Vec<Jack>,
    pcm_streams: Vec<PcmStream>,
    chmaps: Vec<Chmap>,
}

impl VirtSoundService {
    /// Construct a new device containing the given components.
    pub fn new(
        jack_infos: &Vec<wire::VirtioSndJackInfo>,
        pcm_infos: &Vec<wire::VirtioSndPcmInfo>,
        chmap_infos: &Vec<wire::VirtioSndChmapInfo>,
    ) -> Self {
        Self {
            jacks: jack_infos.iter().map(|info| Jack { info: *info }).collect(),
            pcm_streams: pcm_infos.iter().map(|info| PcmStream { info: *info }).collect(),
            chmaps: chmap_infos.iter().map(|info| Chmap { info: *info }).collect(),
        }
    }

    /// Dispatch a message from the controlq.
    /// On error:
    /// * If the error is recoverable, we send an error status back to the driver and return Ok().
    /// * If the error is not recoverable, we return Err().
    pub fn dispatch_controlq<'a, 'b, N: DriverNotify, M: DriverMem>(
        &mut self,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // Each control message is a single struct, where all control structs share
        // a common header. First read that common header to decode which kind of
        // message we have received.
        let req_wrapper = RequestWrapper::new("CONTROLQ", chain);
        let (mut req_wrapper, hdr) =
            match req_wrapper.parse_header_or_reply_with_err::<wire::VirtioSndHdr>()? {
                Some(x) => x,
                None => return Ok(()),
            };

        // More specific request name for better debugging.
        req_wrapper.name = request_code_to_string(hdr.code.get());

        // Dispatch.
        match hdr.code.get() {
            wire::VIRTIO_SND_R_JACK_INFO => VirtSoundService::handle_info(req_wrapper, &self.jacks),
            wire::VIRTIO_SND_R_PCM_INFO => {
                VirtSoundService::handle_info(req_wrapper, &self.pcm_streams)
            }
            wire::VIRTIO_SND_R_CHMAP_INFO => {
                VirtSoundService::handle_info(req_wrapper, &self.chmaps)
            }

            // TODO(fxbug.dev/87645): implement these commands
            // VIRTIO_SND_R_PCM_SET_PARAMS
            // VIRTIO_SND_R_PCM_PREPARE
            // VIRTIO_SND_R_PCM_RELEASE
            // VIRTIO_SND_R_PCM_START
            // VIRTIO_SND_R_PCM_STOP

            // VIRTIO_SND_R_JACK_REMAP is not supported. This command is currently not
            // used by Linux. It is intended to allow changing per-jack priorities and
            // how specific PCM channels map to hardware jacks. Neither of those features
            // are currently supported by audio_core.
            code => {
                tracing::error!("controlq dispatch error: unimplemented controlq code {}", code);
                reply_with_err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_NOT_SUPP)
            }
        }
    }

    /// Handle VIRTIO_SND_R_*_INFO.
    fn handle_info<'a, 'b, 'c, N: DriverNotify, M: DriverMem, T: Info>(
        req_wrapper: RequestWrapper<'a, 'b, 'c, N, M>,
        infos: &Vec<T>,
    ) -> Result<(), Error> {
        let (req_wrapper, req) =
            match req_wrapper.parse_header_or_reply_with_err::<wire::GenericInfoRequest>()? {
                Some(x) => x,
                None => return Ok(()),
            };

        // Validate the request.
        let start_id = req.start_id.get() as usize;
        let end_id = start_id + (req.count.get() as usize);
        if end_id > infos.len() {
            tracing::error!(
                "{} requested ids {}..{}, but device has only {} infos",
                req_wrapper.name(),
                req.start_id.get(),
                end_id,
                infos.len(),
            );
            return reply_with_err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }
        let min_size = std::mem::size_of::<wire::JackInfoRequest>();
        if (req.size.get() as usize) < min_size {
            tracing::error!(
                "{} response struct is {} bytes, but driver provided only {} bytes",
                req_wrapper.name(),
                min_size,
                req.size.get(),
            );
            return reply_with_err(req_wrapper.take_chain(), wire::VIRTIO_SND_S_BAD_MSG);
        }

        // Write all requested infos.
        let mut chain = reply_success(req_wrapper.take_chain())?;
        for id in start_id..end_id {
            chain.write(infos[id].get_info().as_bytes())?;
        }
        Ok(())
    }
}
