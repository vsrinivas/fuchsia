// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::anyhow;
use std::ffi::OsStr;
use std::path::PathBuf;
use std::{env, io};

/// Return the fuchsia root directory.
pub fn get_fuchsia_root() -> Result<PathBuf, env::VarError> {
    Ok(PathBuf::from(env::var("FUCHSIA_DIR")?))
}

/// Returns the path to the template files.
pub fn get_templates_dir_path() -> io::Result<PathBuf> {
    let exe_path = env::current_exe()?;
    let exe_dir_path = exe_path.parent().ok_or_else(|| {
        io::Error::new(io::ErrorKind::InvalidData, anyhow!("exe directory is root"))
    })?;
    Ok(exe_dir_path.join("create_templates"))
}

/// Converts an OS-specific filename to a String, returning an io::Error
/// if a failure occurs. The io::Error contains the invalid filename,
/// which gives the user a better indication of what went wrong.
pub fn filename_to_string(filename: impl AsRef<OsStr>) -> io::Result<String> {
    let filename = filename.as_ref();
    Ok(filename
        .to_str()
        .ok_or_else(|| {
            io::Error::new(io::ErrorKind::InvalidData, anyhow!("invalid filename {:?}", filename))
        })?
        .to_string())
}
