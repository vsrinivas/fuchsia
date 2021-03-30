// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::BanjoAst, crate::backends::Backend, anyhow::Error, lazy_static::lazy_static,
    regex::Regex, std::io,
};

lazy_static! {
    static ref SINGLE_QUOTE_WITH_PRECEEDING_NON_SLASH_CHAR: Regex =
        Regex::new(r"([^\\])'").unwrap();
}

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
        // Temporarily use regex to support old Rust (pre-2020-03-27) and new.
        // Old debug format adds backslash in front of single quotes.
        //
        // Once the new toolchain is rolled, this can be replaced with:
        // self.w.write(format!("{:#?}\n", ast).replace("'", "\\'"))?;
        self.w.write(
            SINGLE_QUOTE_WITH_PRECEEDING_NON_SLASH_CHAR
                .replace_all(&format!("{:#?}\n", ast), "$1\\'")
                .to_string()
                .as_bytes(),
        )?;
        Ok(())
    }
}
