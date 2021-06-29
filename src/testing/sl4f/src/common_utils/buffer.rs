// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_mem as fmem;
use fuchsia_zircon as zx;

pub fn try_from_bytes(value: &[u8]) -> Result<fmem::Buffer, zx::Status> {
    let size = value.len() as u64;
    let vmo = zx::Vmo::create(size)?;
    vmo.write(value, 0)?;
    Ok(fmem::Buffer { vmo, size })
}

pub fn try_into_bytes(buffer: fmem::Buffer) -> Result<Vec<u8>, zx::Status> {
    let mut data: Vec<u8> = vec![0; buffer.size as usize];
    buffer.vmo.read(data.as_mut_slice(), 0)?;
    Ok(data)
}
