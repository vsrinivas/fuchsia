// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub struct Size {
    pub width: u32,
    pub height: u32,
}

pub fn get_window_size() -> Result<Size, anyhow::Error> {
    Err(anyhow::format_err!(
        "fuchsia-device is deprecated; please use fuchsia.hardware.pty.Device instead",
    ))
}
