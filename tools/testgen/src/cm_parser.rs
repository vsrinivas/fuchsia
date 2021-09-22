// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use fidl::encoding::decode_persistent;
use fidl_fuchsia_sys2::*;
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;

/// Parses a compiled .cm file.
pub fn read_cm(file: &str) -> Result<ComponentDecl> {
    let mut buffer = Vec::new();
    let path = PathBuf::from(file);
    File::open(&path)?.read_to_end(&mut buffer)?;
    Ok(decode_persistent(&buffer)?)
}
