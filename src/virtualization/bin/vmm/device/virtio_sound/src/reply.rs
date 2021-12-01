// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire,
    crate::wire::LE32,
    std::io::Write,
    virtio_device::chain::{ReadableChain, WritableChain},
    virtio_device::mem::DriverMem,
    virtio_device::queue::DriverNotify,
    zerocopy::AsBytes,
};

/// Writes a successful response header, then returns a chain so the rest of
/// the response can be written.
///
/// Returns an error only if the virtqueue connection is broken.
pub fn reply_success<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: ReadableChain<'a, 'b, N, M>,
) -> Result<WritableChain<'a, 'b, N, M>, anyhow::Error> {
    reply_with_code(chain, wire::VIRTIO_SND_S_OK)
}

/// Writes an error response using the given code. Since error replies cannot
/// contain anything except a code, the caller is not given a WritableChain.
///
/// Returns an error only if the virtqueue connection is broken.
pub fn reply_with_err<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: ReadableChain<'a, 'b, N, M>,
    code: u32,
) -> Result<(), anyhow::Error> {
    reply_with_code(chain, code)?;
    Ok(())
}

/// Internal implementation.
fn reply_with_code<'c, 'd, N: DriverNotify, M: DriverMem>(
    chain: ReadableChain<'c, 'd, N, M>,
    code: u32,
) -> Result<WritableChain<'c, 'd, N, M>, anyhow::Error> {
    let resp: wire::GenericResponse = wire::GenericResponse { code: LE32::new(code) };
    let mut chain = WritableChain::from_incomplete_readable(chain)?;
    chain.write(resp.as_bytes())?;
    Ok(chain)
}
