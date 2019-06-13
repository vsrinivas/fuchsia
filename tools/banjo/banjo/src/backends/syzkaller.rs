// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst, Decl},
    crate::backends::util::to_c_name,
    crate::backends::Backend,
    failure::{format_err, Error},
    std::io,
};

pub struct SyzkallerBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> SyzkallerBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        SyzkallerBackend { w }
    }
}

fn ty_to_syzkaller_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    match ty {
        ast::Ty::Bool => Ok(String::from("bool")),
        ast::Ty::Int8 => Ok(String::from("int8")),
        ast::Ty::Int16 => Ok(String::from("int16")),
        ast::Ty::Int32 => Ok(String::from("int32")),
        ast::Ty::Int64 => Ok(String::from("int64")),
        ast::Ty::UInt8 => Ok(String::from("int8")),
        ast::Ty::UInt16 => Ok(String::from("int16")),
        ast::Ty::UInt32 => Ok(String::from("int32")),
        ast::Ty::UInt64 => Ok(String::from("int64")),
        ast::Ty::USize => Ok(String::from("intptr")),
        ast::Ty::Float32 => Ok(String::from("int32")),
        ast::Ty::Float64 => Ok(String::from("int64")),
        ast::Ty::Voidptr => Ok(String::from("int64")),
        ast::Ty::Identifier { id, .. } => {
            if id.is_base_type() {
                let resolved_type = ast.id_to_type(id);
                ty_to_syzkaller_str(ast, &resolved_type)
            } else {
                Err(format_err!("unsupported ident_type in ty_to_syzkaller_str {:?}", id))
            }
        }
        t => Err(format_err!("unknown type in ty_to_syzkaller_str {:?}", t)),
    }
}

fn get_in_params(m: &ast::Method, ast: &BanjoAst) -> Result<Vec<String>, Error> {
    m.in_params
        .iter()
        .map(|(name, ty)| {
            match ty {
                ast::Ty::Str { .. } => {
                    // TODO(SEC-327): handle string length dependency
                    // TODO(SEC-326): output pointer direction also
                    Ok(format!("{} {}", to_c_name(name), ty_to_syzkaller_str(ast, ty).unwrap()))
                }
                ast::Ty::Array { .. } => {
                    // TODO(SEC-327): handle array length dependency
                    // TODO(SEC-326): output pointer direction also
                    Ok(format!("{} {}", to_c_name(name), ty_to_syzkaller_str(ast, ty).unwrap()))
                }
                ast::Ty::Vector { .. } => Err(format_err!("unsupported: {}", ty)),
                _ => Ok(format!("{} {}", to_c_name(name), ty_to_syzkaller_str(ast, ty).unwrap())),
            }
        })
        .collect()
}

fn get_out_params(m: &ast::Method, ast: &BanjoAst) -> Result<Vec<String>, Error> {
    Ok(m.out_params
        .iter()
        .map(|(_name, ty)| {
            // TODO(SEC-326): handle pointers
            format!("{}", ty_to_syzkaller_str(ast, ty).unwrap())
        })
        .collect())
}

impl<'a, W: io::Write> SyzkallerBackend<'a, W> {
    fn codegen_protocol_def(
        &self,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let mut accum = String::new();
                let in_params = get_in_params(&m, ast)?.join(", ");
                let out_params = get_out_params(&m, ast)?.join(", ");
                accum.push_str(
                    format!(
                        "{fn_name}({in_params}) {out_params}",
                        fn_name = to_c_name(m.name.as_str()),
                        in_params = in_params,
                        out_params = out_params
                    )
                    .as_str(),
                );
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for SyzkallerBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/syzkaller/header.syzkaller.inc"),
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
                    Some(self.codegen_protocol_def(methods, &ast))
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        write!(&mut self.w, "{}", definitions)?;
        Ok(())
    }
}
