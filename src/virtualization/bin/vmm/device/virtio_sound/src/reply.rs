// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire,
    crate::wire::LE32,
    anyhow::{anyhow, Error},
    std::io::Write,
    zerocopy::AsBytes,
};

/// Type alias to reduce generic parameter noise.
pub type ReadableChain<'a, 'b> = virtio_device::chain::ReadableChain<
    'a,
    'b,
    machina_virtio_device::NotifyEvent,
    machina_virtio_device::GuestMem,
>;

/// Type alias to reduce generic parameter noise.
pub type WritableChain<'a, 'b> = virtio_device::chain::WritableChain<
    'a,
    'b,
    machina_virtio_device::NotifyEvent,
    machina_virtio_device::GuestMem,
>;

pub mod reply_controlq {
    use super::*;

    /// Writes a successful controlq response header, then returns a chain so the
    /// rest of the response can be written.
    ///
    /// Returns an error only if the virtqueue connection is broken.
    pub fn success<'a, 'b>(chain: ReadableChain<'a, 'b>) -> Result<WritableChain<'a, 'b>, Error> {
        reply_with_code(chain, wire::VIRTIO_SND_S_OK)
    }

    /// Writes a controlq error response using the given code. Since error replies cannot
    /// contain anything except a code, the caller is not given a WritableChain.
    ///
    /// Returns an error only if the virtqueue connection is broken.
    pub fn err<'a, 'b>(chain: ReadableChain<'a, 'b>, code: u32) -> Result<(), Error> {
        reply_with_code(chain, code)?;
        Ok(())
    }

    /// Internal implementation.
    fn reply_with_code<'a, 'b>(
        chain: ReadableChain<'a, 'b>,
        code: u32,
    ) -> Result<WritableChain<'a, 'b>, Error> {
        let resp: wire::GenericResponse = wire::GenericResponse { code: LE32::new(code) };
        let mut chain = WritableChain::from_incomplete_readable(chain)?;
        chain.write_all(resp.as_bytes())?;
        Ok(chain)
    }
}

pub mod reply_txq {
    use super::*;

    /// Writes a successful txq response header.
    /// Returns an error only if the virtqueue connection is broken.
    pub fn success<'a, 'b>(chain: ReadableChain<'a, 'b>, latency_bytes: u32) -> Result<(), Error> {
        // Use `from_readable`, not `from_incomplete_readable`, because we should always
        // read the entire buffer on successful requests.
        let chain = WritableChain::from_readable(chain)?;
        reply_with_code(chain, wire::VIRTIO_SND_S_OK, latency_bytes)
    }

    /// Writes an txq error response using the given status code.
    /// Returns an error only if the virtqueue connection is broken.
    pub fn err<'a, 'b>(
        chain: ReadableChain<'a, 'b>,
        status: u32,
        latency_bytes: u32,
    ) -> Result<(), Error> {
        // Since error paths may not read the full request, use `from_incomplete_readable`.
        let chain = WritableChain::from_incomplete_readable(chain)?;
        reply_with_code(chain, status, latency_bytes)
    }

    /// Internal implementation.
    fn reply_with_code<'a, 'b>(
        mut chain: WritableChain<'a, 'b>,
        status: u32,
        latency_bytes: u32,
    ) -> Result<(), Error> {
        let resp: wire::VirtioSndPcmStatus = wire::VirtioSndPcmStatus {
            status: LE32::new(status),
            latency_bytes: LE32::new(latency_bytes),
        };
        chain.write_all(resp.as_bytes())?;
        Ok(())
    }
}

pub mod reply_rxq {
    use super::*;

    /// Writes a successful rxq response header.
    /// Returns an error only if the virtqueue connection is broken.
    /// REQUIRES: The full audio data buffer must have been written.
    pub fn success<'a, 'b>(chain: WritableChain<'a, 'b>, latency_bytes: u32) -> Result<(), Error> {
        reply_with_code(chain, wire::VIRTIO_SND_S_OK, latency_bytes)
    }

    /// Writes an rxq error response using the given status code.
    /// Returns an error only if the virtqueue connection is broken.
    pub fn err_from_readable<'a, 'b>(
        chain: ReadableChain<'a, 'b>,
        status: u32,
        latency_bytes: u32,
    ) -> Result<(), Error> {
        // Since error paths may not read the full request, use `from_incomplete_readable`.
        let chain = WritableChain::from_incomplete_readable(chain)?;
        err_from_writable(chain, status, latency_bytes)
    }

    /// Writes an rxq error response using the given status code.
    /// Returns an error only if the virtqueue connection is broken.
    pub fn err_from_writable<'a, 'b>(
        mut chain: WritableChain<'a, 'b>,
        status: u32,
        latency_bytes: u32,
    ) -> Result<(), Error> {
        // Write zeroes to fill out the buffer before writing the status.
        let size = buffer_size(&chain)?;
        let mut vec: std::vec::Vec<u8> = std::vec::Vec::new();
        vec.resize(size, 0u8);
        chain.write_all(&vec)?;
        reply_with_code(chain, status, latency_bytes)
    }

    /// Reports the remaining size of the audio data buffer.
    ///
    /// From 5.14.6.8: An RX message has the readable header, then the writable buffer, then
    /// the writable status. If this is called when no data has yet been written to `chain`,
    /// then this will return the full size of the audio data buffer.
    pub fn buffer_size<'a, 'b>(chain: &WritableChain<'a, 'b>) -> Result<usize, Error> {
        // Therefore, the size of the writable buffer is the size of the total writable area
        // minus the size of the status field.
        let total_size = chain.remaining()?.bytes;
        let status_size = std::mem::size_of::<wire::VirtioSndPcmStatus>();
        if total_size < status_size {
            Err(anyhow!(
                "writable chain is too small: has {} writable bytes, need {} to write status",
                total_size,
                status_size
            ))
        } else {
            Ok(total_size - status_size)
        }
    }

    /// Internal implementation.
    /// REQUIRES: The full audio data buffer must have been written.
    fn reply_with_code<'a, 'b>(
        mut chain: WritableChain<'a, 'b>,
        status: u32,
        latency_bytes: u32,
    ) -> Result<(), Error> {
        let remaining = chain.remaining()?.bytes;
        let status_size = std::mem::size_of::<wire::VirtioSndPcmStatus>();
        if remaining != status_size {
            panic!(
                "cannot write status code {}: have {} bytes left, expected {}",
                status, remaining, status_size
            );
        }
        let resp: wire::VirtioSndPcmStatus = wire::VirtioSndPcmStatus {
            status: LE32::new(status),
            latency_bytes: LE32::new(latency_bytes),
        };
        chain.write_all(resp.as_bytes())?;
        Ok(())
    }
}
