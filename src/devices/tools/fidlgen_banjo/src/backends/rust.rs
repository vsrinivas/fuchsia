// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::Backend,
    crate::fidl::*,
    anyhow::{anyhow, Error},
    std::io,
};

pub struct RustBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> RustBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        RustBackend { w }
    }
}

impl<'a, W: io::Write> Backend<'a, W> for RustBackend<'a, W> {
    fn codegen(&mut self, _ir: FidlIr) -> Result<(), Error> {
        self.w.write_fmt(format_args!("Yay!"))?;
        Err(anyhow!("Not implemented"))
    }
}
