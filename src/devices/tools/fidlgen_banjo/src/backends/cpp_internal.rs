// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, crate::fidl::*, anyhow::Error, std::io};

pub struct CppInternalBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> CppInternalBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        CppInternalBackend { w }
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CppInternalBackend<'a, W> {
    fn codegen(&mut self, _ir: FidlIr) -> Result<(), Error> {
        self.w.write_fmt(format_args!("I did something too!"))?;
        Ok(())
    }
}
