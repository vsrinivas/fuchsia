// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::Backend, crate::fidl::FidlIr, anyhow::Error, std::io};

pub struct DummyCBackend<W: io::Write> {
    w: W,
}

impl<W: io::Write> DummyCBackend<W> {
    pub fn new(w: W) -> Self {
        DummyCBackend { w }
    }
}

impl<W: io::Write> Backend<W> for DummyCBackend<W> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/dummy_c/dummy_c.h"),
            library = ir.name.0,
        ))?;
        Ok(())
    }
}
