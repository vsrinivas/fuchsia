// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_mem::Buffer};

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
