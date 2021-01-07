// Copyright 2020 The Fuchsia Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ast::BanjoAst, crate::backends::Backend, anyhow::Error, std::io};

#[derive(Debug)]
pub enum LegacySubtype {
    Ddk,
    Ddktl,
    Mock,
}

/// This backend exists purely for a soft transition.
pub struct LegacyBackend<'a, W: io::Write> {
    w: &'a mut W,
    subtype: LegacySubtype,
}

impl<'a, W: io::Write> LegacyBackend<'a, W> {
    pub fn new(w: &'a mut W, subtype: LegacySubtype) -> Self {
        LegacyBackend { w, subtype }
    }
}

impl<'a, W: io::Write> Backend<'a, W> for LegacyBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        match &self.subtype {
            LegacySubtype::Ddk => {
                self.w.write_fmt(format_args!(
                    include_str!("templates/legacy/ddk.h"),
                    namespace = &ast.primary_namespace,
                    include_path = ast.primary_namespace.replace('.', "/").as_str()
                ))?;
            }
            LegacySubtype::Ddktl => {
                self.w.write_fmt(format_args!(
                    include_str!("templates/legacy/ddktl.h"),
                    namespace = &ast.primary_namespace,
                    include_path = ast.primary_namespace.replace('.', "/").as_str()
                ))?;
            }
            LegacySubtype::Mock => {
                self.w.write_fmt(format_args!(
                    include_str!("templates/legacy/ddktl_mock.h"),
                    namespace = &ast.primary_namespace,
                    include_path = ast.primary_namespace.replace('.', "/").as_str()
                ))?;
            }
        }

        Ok(())
    }
}
