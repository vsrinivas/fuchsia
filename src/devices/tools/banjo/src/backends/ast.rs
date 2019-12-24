// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ast::BanjoAst, crate::backends::Backend, anyhow::Error, std::io};

pub struct AstBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> AstBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        AstBackend { w }
    }
}

impl<'a, W: io::Write> Backend<'a, W> for AstBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!("{:#?}\n", ast))?;
        Ok(())
    }
}
