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

pub struct AbigenBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> AbigenBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        AbigenBackend { w }
    }
}

pub fn get_shortdesc(attrs: &ast::Attrs) -> String {
    for attr in attrs.0.iter() {
        if attr.key == "Doc" {
            if let Some(ref val) = attr.val {
                return format!("#^ {}\n", val.trim());
            }
        }
    }
    "".to_string()
}

pub fn get_all_rights(attrs: &ast::Attrs) -> String {
    let mut accum = String::new();
    for attr in attrs.0.iter() {
        if attr.key == "rights" {
            if let Some(ref val) = attr.val {
                accum.push_str(format!("#! {}\n", val).as_str());
            }
        }
    }
    accum
}

pub fn get_method_annotations(attrs: &ast::Attrs) -> (String, bool) {
    let mut accum: Vec<String> = Vec::new();
    let mut is_noreturn = false;
    for attr in attrs.0.iter() {
        if let "blocking" | "const" | "internal" | "noreturn" | "test_category1"
        | "test_category2" | "vdsocall" = attr.key.as_ref()
        {
            accum.push(attr.key.clone());
            if attr.key == "noreturn" {
                is_noreturn = true;
            }
        }
    }
    return (
        if accum.len() == 0 { "".to_string() } else { " ".to_owned() + &accum.join(" ") },
        is_noreturn,
    );
}

fn ty_to_c_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    match ty {
        ast::Ty::Bool => Ok(String::from("bool")),
        ast::Ty::Int8 => Ok(String::from("int8_t")),
        ast::Ty::Int16 => Ok(String::from("int16_t")),
        ast::Ty::Int32 => Ok(String::from("int32_t")),
        ast::Ty::Int64 => Ok(String::from("int64_t")),
        ast::Ty::UInt8 => Ok(String::from("uint8_t")),
        ast::Ty::UInt16 => Ok(String::from("uint16_t")),
        ast::Ty::UInt32 => Ok(String::from("uint32_t")),
        ast::Ty::UInt64 => Ok(String::from("uint64_t")),
        ast::Ty::USize => Ok(String::from("size_t")),
        ast::Ty::Float32 => Ok(String::from("float")),
        ast::Ty::Float64 => Ok(String::from("double")),
        ast::Ty::Voidptr => Ok(String::from("any")),
        ast::Ty::Vector { ref ty, .. } => ty_to_c_str(ast, ty),
        ast::Ty::Array { ref ty, .. } => ty_to_c_str(ast, ty),
        ast::Ty::Identifier { id, .. } => {
            if id.is_base_type() {
                Ok(format!("zx_{}_t", id.name()))
            } else {
                match ast.id_to_type(id) {
                    ast::Ty::Struct | ast::Ty::Union | ast::Ty::Enum | ast::Ty::Protocol => {
                        return Ok(format!("{}", to_c_name(id.name())));
                    }
                    t => return ty_to_c_str(ast, &t),
                }
            }
        }
        ast::Ty::Str { size, nullable } => {
            if *nullable {
                panic!("string cannot be nullable");
            }
            match size {
                Some(i) => Ok(format!("char[{}]", i)),
                None => panic!("string must have fixed length"),
            }
        }
        ast::Ty::Handle { .. } => Ok(String::from("zx_handle_t")),
        t => Err(format_err!("unknown type in ty_to_c_str {:?}", t)),
    }
}

pub fn array_bounds(ty: &ast::Ty, specified_bounds: &String) -> Option<String> {
    if let ast::Ty::Array { ty: _, size } = ty {
        return Some(format!(
            "[{}]",
            if !specified_bounds.is_empty() { specified_bounds } else { &size.0 }
        ));
    }
    None
}

fn get_in_params(
    m: &ast::Method,
    ast: &BanjoAst,
    arg_types: &ValuedAttributes,
    array_sizes: &ValuedAttributes,
) -> Result<Vec<String>, Error> {
    m.in_params
        .iter()
        .map(|(name, ty)| {
            let extra = arg_types.get_arg_with_prefix(name, " ");
            let arrsize = array_sizes.get_arg(name);
            match ty {
                ast::Ty::Identifier { id, .. } => {
                    if id.is_base_type() {
                        let ty_name = ty_to_c_str(ast, ty).unwrap();
                        return Ok(format!("{}: {}{}", to_c_name(name), ty_name, extra));
                    }
                    match ast.id_to_type(id) {
                        ast::Ty::Struct | ast::Ty::Union => {
                            let ty_name = ty_to_c_str(ast, ty).unwrap();
                            if ty_name == "uintptr_t" || ty_name == "zx_futex_t" {
                                Ok(format!("{}: {}", to_c_name(name), ty_name))
                            } else {
                                panic!("unexpected struct/union arg")
                            }
                        }
                        ast::Ty::Bool
                        | ast::Ty::Int8
                        | ast::Ty::Int16
                        | ast::Ty::Int32
                        | ast::Ty::Int64
                        | ast::Ty::UInt8
                        | ast::Ty::UInt16
                        | ast::Ty::UInt32
                        | ast::Ty::UInt64
                        | ast::Ty::USize
                        | ast::Ty::Voidptr => Ok(format!(
                            "{}: {}{}",
                            to_c_name(name),
                            ty_to_c_str(ast, ty).unwrap(),
                            extra
                        )),
                        e => Err(format_err!("unsupported: {}", e)),
                    }
                }
                ast::Ty::Str { .. } => {
                    Ok(format!("{}: {}{}", to_c_name(name), ty_to_c_str(ast, ty).unwrap(), extra))
                }
                ast::Ty::Array { .. } => {
                    let bounds = array_bounds(ty, &arrsize).unwrap();
                    let ty = ty_to_c_str(ast, ty).unwrap();
                    Ok(format!(
                        "{name}: {ty}{bounds}{extra}",
                        bounds = bounds,
                        ty = ty,
                        name = to_c_name(name),
                        extra = extra
                    ))
                }
                ast::Ty::Vector { .. } => Err(format_err!("unsupported: {}", ty)),
                _ => Ok(format!("{}: {}{}", to_c_name(name), ty_to_c_str(ast, ty).unwrap(), extra)),
            }
        })
        .collect()
}

fn get_out_params(
    m: &ast::Method,
    ast: &BanjoAst,
    arg_types: &ValuedAttributes,
) -> Result<(Vec<String>), Error> {
    Ok(m.out_params
        .iter()
        .enumerate()
        .map(|(index, (name, ty))| {
            let ty_name = ty_to_c_str(ast, ty).unwrap();
            if index == 0 {
                return ty_name;
            }
            let extra = arg_types.get_arg_with_prefix(name, " ");
            match ty {
                ast::Ty::Handle { .. } => format!("{}: {}{}", to_c_name(name), ty_name, extra),
                _ => format!("{}: {}{}", to_c_name(name), ty_name, extra),
            }
        })
        .collect())
}

impl<'a, W: io::Write> AbigenBackend<'a, W> {
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
                let array_sizes = ValuedAttributes::new(&m.attributes, "arraysize");

                let in_params = get_in_params(&m, ast, &arg_types, &array_sizes)?.join(", ");
                let out_params = get_out_params(&m, ast, &arg_types)?.join(", ");

                let (annotations, is_noreturn) = get_method_annotations(&m.attributes);

                accum.push_str(get_shortdesc(&m.attributes).as_str());
                accum.push_str(get_all_rights(&m.attributes).as_str());
                let mut returns = String::new();
                if !is_noreturn {
                    returns = format!("\n    returns ({})", out_params);
                }
                accum.push_str(
                    format!(
                        "syscall {fn_name}{annots}\n    ({ins}){returns};\n",
                        annots = annotations,
                        ins = in_params,
                        returns = returns,
                        fn_name = to_c_name(m.name.as_str())
                    )
                    .as_str(),
                );

                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for AbigenBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/abigen/header.abigen.in"),
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
