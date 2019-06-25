// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst, Decl},
    crate::backends::util::{to_c_name, ValuedAttributes},
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

fn get_in_params(
    m: &ast::Method,
    ast: &BanjoAst,
    arg_types: &ValuedAttributes,
) -> Result<Vec<String>, Error> {
    m.in_params
        .iter()
        .map(|(name, ty)| {
            let direction = arg_types.get_arg(name);
            match ty {
                ast::Ty::Str { size, nullable } => {
                    // TODO(SEC-327): handle string length dependency
                    if *nullable {
                        panic!("string cannot be nullable");
                    }
                    match size {
                        Some(_sz) => {
                            if !direction.is_empty() {
                                let resolved_type = format!(
                                    "ptr[{}, string]",
                                    to_c_name(&direction)
                                );
                                Ok(format!("{} {}", to_c_name(name), resolved_type))
                            } else {
                                panic!("missing 'arg_type' attribute: string direction must be specified")
                            }
                        }
                        None => panic!("string must have fixed length"),
                    }
                }
                ast::Ty::Array { ty, size: _ } => {
                    // TODO(SEC-327): handle array length dependency
                    if !direction.is_empty() {
                        let resolved_type = format!(
                            "ptr[{}, array[{}]]",
                            to_c_name(&direction),
                            ty_to_syzkaller_str(ast, ty).unwrap()
                        );
                        Ok(format!("{} {}", to_c_name(name), resolved_type))
                    } else {
                        panic!("missing 'arg_type' attribute: array direction must be specified")
                    }
                }
                _ => Ok(format!("{} {}", to_c_name(name), ty_to_syzkaller_str(ast, ty).unwrap())),
            }
        })
        .collect()
}

fn is_resource(_ty: &ast::Ty) -> bool {
    // TODO(SEC-333): Add support for syzkaller resources
    false
}

fn get_first_param(ast: &BanjoAst, method: &ast::Method) -> Result<(bool, String), Error> {
    if method.out_params.get(0).map_or(false, |p| p.1.is_primitive(&ast) && is_resource(&p.1)) {
        // Return parameter if a primitive type and a resource.
        Ok((true, ty_to_syzkaller_str(ast, &method.out_params[0].1)?))
    } else if method.out_params.get(0).map_or(false, |p| p.1.is_primitive(&ast)) {
        // primitive but not a resource
        Ok((true, String::default()))
    } else {
        Ok((false, String::default()))
    }
}

fn get_out_params(m: &ast::Method, ast: &BanjoAst) -> Result<(Vec<String>, String), Error> {
    let (skip, return_param) = get_first_param(ast, m)?;
    let skip_amt = if skip { 1 } else { 0 };

    Ok((
        m.out_params
            .iter()
            .skip(skip_amt)
            .map(|(name, ty)| {
                let resolved_type = format!(
                    "ptr[{}, {}]",
                    String::from("out"),
                    ty_to_syzkaller_str(ast, ty).unwrap()
                );
                format!("{} {}", to_c_name(name), resolved_type)
            })
            .collect(),
        return_param,
    ))
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
                let arg_types = ValuedAttributes::new(&m.attributes, "argtype");
                let mut in_params = get_in_params(&m, ast, &arg_types)?;
                let (out_params, return_param) = get_out_params(&m, ast)?;
                in_params.extend(out_params);
                let params = in_params.join(", ");
                accum.push_str(
                    format!(
                        "{fn_name}({params})",
                        fn_name = to_c_name(m.name.as_str()),
                        params = params,
                    )
                    .as_str(),
                );
                if !return_param.is_empty() {
                    accum
                        .push_str(format!(" {return_param}", return_param = return_param).as_str());
                }
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
