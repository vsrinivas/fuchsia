// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::wire, crate::wire::LE32, std::io::Write, zerocopy::AsBytes};

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
    pub fn success<'a, 'b>(
        chain: ReadableChain<'a, 'b>,
    ) -> Result<WritableChain<'a, 'b>, anyhow::Error> {
        reply_with_code(chain, wire::VIRTIO_SND_S_OK)
    }

    /// Writes a controlq error response using the given code. Since error replies cannot
    /// contain anything except a code, the caller is not given a WritableChain.
    ///
    /// Returns an error only if the virtqueue connection is broken.
    pub fn err<'a, 'b>(chain: ReadableChain<'a, 'b>, code: u32) -> Result<(), anyhow::Error> {
        reply_with_code(chain, code)?;
        Ok(())
    }

    /// Internal implementation.
    fn reply_with_code<'a, 'b>(
        chain: ReadableChain<'a, 'b>,
        code: u32,
    ) -> Result<WritableChain<'a, 'b>, anyhow::Error> {
        let resp: wire::GenericResponse = wire::GenericResponse { code: LE32::new(code) };
        let mut chain = WritableChain::from_incomplete_readable(chain)?;
        chain.write(resp.as_bytes())?;
        Ok(chain)
    }
}

pub mod reply_txq {
    use super::*;

    /// Writes a successful txq response header.
    /// Returns an error only if the virtqueue connection is broken.
    pub fn success<'a, 'b>(
        chain: ReadableChain<'a, 'b>,
        latency_bytes: u32,
    ) -> Result<(), anyhow::Error> {
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
    ) -> Result<(), anyhow::Error> {
        // Since error paths may not read the full request, use `from_incomplete_readable`.
        let chain = WritableChain::from_incomplete_readable(chain)?;
        reply_with_code(chain, status, latency_bytes)
    }

    /// Internal implementation.
    fn reply_with_code<'a, 'b>(
        mut chain: WritableChain<'a, 'b>,
        status: u32,
        latency_bytes: u32,
    ) -> Result<(), anyhow::Error> {
        let resp: wire::VirtioSndPcmStatus = wire::VirtioSndPcmStatus {
            status: LE32::new(status),
            latency_bytes: LE32::new(latency_bytes),
        };
        chain.write(resp.as_bytes())?;
        Ok(())
    }
}
