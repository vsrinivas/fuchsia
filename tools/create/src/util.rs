// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::anyhow;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::{env, fs, io};

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
    Ok(exe_dir_path.join("create_templates/templates"))
}

/// Returns whether the directory `dir` contains an entry named `filename`.
pub fn dir_contains(dir: &Path, filename: &str) -> io::Result<bool> {
    if dir.is_dir() {
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let dir_filename = filename_to_string(entry.file_name())?;
            if dir_filename == filename {
                return Ok(true);
            }
        }
    }
    return Ok(false);
}

/// Converts an OS-specific filename to a String, returning an io::Error
/// if a failure occurs. The io::Error contains the invalid filename,
/// which gives the user a better indication of what went wrong.
pub fn filename_to_string(filename: OsString) -> io::Result<String> {
    filename.into_string().map_err(|s| {
        io::Error::new(io::ErrorKind::InvalidData, anyhow!("invalid filename {:?}", s))
    })
}

/// Strips `suffix` from the end of `s` if `s` ends with `suffix`.
pub fn strip_suffix<'a>(s: &'a str, suffix: &str) -> Option<&'a str> {
    if s.ends_with(suffix) {
        Some(&s[0..s.len() - suffix.len()])
    } else {
        None
    }
}
