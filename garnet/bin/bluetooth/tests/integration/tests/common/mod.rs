// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    std::{
        fs::{File, OpenOptions},
        path::Path,
    },
};

pub fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new()
        .read(true)
        .write(true)
        .open(path)
        .map_err(|e| e.into())
}
