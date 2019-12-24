// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_mem::Buffer, fuchsia_zircon as zx};

/// Converts a module_path into a string.
/// Example: ["abc", "1:2"] -> "abc:1\:2"
pub fn encoded_module_path(module_path: Vec<String>) -> String {
    module_path.iter().map(|part| part.replace(":", "\\:")).collect::<Vec<String>>().join(":")
}

/// Converts a transport VMO into a string.
pub fn vmo_buffer_to_string(buffer: Box<Buffer>) -> Result<String, Error> {
    let buffer_size = buffer.size;
    let buffer_vmo = buffer.vmo;
    let mut bytes = vec![0; buffer_size as usize];
    buffer_vmo.read(&mut bytes, 0)?;
    Ok(String::from_utf8_lossy(&bytes).to_string())
}

/// Converts an impl Into<String> to a VMO buffer.
pub fn string_to_vmo_buffer(content: impl Into<String>) -> Result<Buffer, Error> {
    let content_string = content.into();
    let data_to_write = content_string.as_bytes();
    let vmo = zx::Vmo::create(data_to_write.len() as u64)?;
    vmo.write(&data_to_write, 0)?;
    Ok(Buffer { vmo, size: data_to_write.len() as u64 })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn string_conversion() -> Result<(), Error> {
        let converted_buffer = string_to_vmo_buffer("value")?;
        let converted_string = vmo_buffer_to_string(Box::new(converted_buffer))?;
        assert_eq!(&converted_string, "value");
        Ok(())
    }
}
