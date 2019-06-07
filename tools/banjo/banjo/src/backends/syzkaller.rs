// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ast::BanjoAst, crate::backends::Backend, failure::Error, std::io};

pub struct SyzkallerBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> SyzkallerBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        SyzkallerBackend { w }
    }
}

impl<'a, W: io::Write> Backend<'a, W> for SyzkallerBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/syzkaller/header.syzkaller.inc"),
            primary_namespace = ast.primary_namespace
        ))?;
        // TODO(SEC-321)
        Ok(())
    }
}
