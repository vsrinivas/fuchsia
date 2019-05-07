// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst, Decl},
    crate::backends::{util, Backend},
    failure::Error,
    std::io,
};

#[derive(Debug)]
pub enum KernelSubtype {
    Numbers,
    Trace,
}

pub struct KernelBackend<'a, W: io::Write> {
    w: &'a mut W,
    subtype: KernelSubtype,
}

impl<'a, W: io::Write> KernelBackend<'a, W> {
    pub fn new(w: &'a mut W, subtype: KernelSubtype) -> Self {
        KernelBackend { w, subtype }
    }
}

fn count_of_natively_returned_out_params(ast: &BanjoAst, method: &ast::Method) -> usize {
    // Return parameter if a primitive type.
    if method.out_params.get(0).map_or(false, |p| p.1.is_primitive(&ast)) {
        1
    } else {
        0
    }
}

impl<'a, W: io::Write> KernelBackend<'a, W> {
    fn codegen_trace(&self, methods: &Vec<ast::Method>, ast: &BanjoAst) -> Result<String, Error> {
        methods
            .iter()
            .filter(|m| !m.attributes.0.iter().any(|x| x.key == "vdsocall"))
            .enumerate()
            .map(|(id, m)| {
                let mut accum = String::new();

                let nargs = m.in_params.len() + m.out_params.len()
                    - count_of_natively_returned_out_params(&ast, &m);
                accum.push_str(
                    format!(
                        "{{{id}, {nargs}, \"{fn_name}\"}},",
                        id = id,
                        nargs = nargs,
                        fn_name = util::to_c_name(m.name.as_str())
                    )
                    .as_str(),
                );
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_numbers(
        &self,
        methods: &Vec<ast::Method>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .filter(|m| !m.attributes.0.iter().any(|x| x.key == "vdsocall"))
            .enumerate()
            .map(|(id, m)| {
                Ok(format!(
                    "#define ZX_SYS_{fn_name} {id}",
                    fn_name = util::to_c_name(m.name.as_str()),
                    id = id
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n") + format!("\n#define ZX_SYS_COUNT {}", x.len()).as_str())
    }
}

impl<'a, W: io::Write> Backend<'a, W> for KernelBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/kernel/header.kernel.inc"),
            primary_namespace = ast.primary_namespace
        ))?;

        let decl_order = ast.validate_declaration_deps()?;

        let definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Protocol { attributes: _, name, methods } => {
                    if name.name() != "Api" {
                        panic!("Expecting single protocol 'Api'");
                    }
                    match &self.subtype {
                        KernelSubtype::Trace => Some(self.codegen_trace(methods, &ast)),
                        KernelSubtype::Numbers => Some(self.codegen_numbers(methods, &ast)),
                    }
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        write!(&mut self.w, "{}\n", definitions)?;
        Ok(())
    }
}
