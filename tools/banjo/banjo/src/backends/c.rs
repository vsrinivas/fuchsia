// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst, Constant, Decl, Ident},
    crate::backends::Backend,
    failure::{format_err, Error},
    heck::SnakeCase,
    std::io,
    std::iter,
};

pub struct CBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> CBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        CBackend { w }
    }
}

pub fn to_c_name(name: &str) -> String {
    // strip FQN
    let name = name.split(".").last().unwrap();
    // Force uppercase characters the follow one another to lowercase.
    // e.g. GUIDType becomes Guidtype
    let mut iter = name.chars();
    let name = iter::once(iter.next().unwrap())
        .chain(iter.zip(name.chars()).map(|(c1, c2)| {
            if c2.is_ascii_uppercase() {
                c1.to_ascii_lowercase()
            } else {
                c1
            }
        }))
        .collect::<String>();
    name.trim().to_snake_case()
}

pub fn get_doc_comment(attrs: &ast::Attrs, tabs: usize) -> String {
    for attr in attrs.0.iter() {
        if attr.key == "Doc" {
            if let Some(ref val) = attr.val {
                let tabs: String = iter::repeat(' ').take(tabs * 4).collect();
                return val
                    .trim_end()
                    .split("\n")
                    .map(|line| format!("{}//{}\n", tabs, line))
                    .collect();
            }
        }
    }
    "".to_string()
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
        ast::Ty::Voidptr => Ok(String::from("void")),
        ast::Ty::Str { .. } => Ok(String::from("char*")),
        ast::Ty::Vector { ref ty, .. } => ty_to_c_str(ast, ty),
        ast::Ty::Array { ref ty, .. } => ty_to_c_str(ast, ty),
        ast::Ty::Identifier { id, .. } => {
            if id.is_base_type() {
                Ok(format!("zx_{}_t", id.name()))
            } else {
                match ast.id_to_type(id) {
                    ast::Ty::Struct | ast::Ty::Union | ast::Ty::Interface | ast::Ty::Enum => {
                        return Ok(format!("{}_t", to_c_name(id.name())));
                    }
                    t => return ty_to_c_str(ast, &t),
                }
            }
        }
        ast::Ty::Handle { .. } => Ok(String::from("zx_handle_t")),
        t => Err(format_err!("unknown type in ty_to_c_str {:?}", t)),
    }
}

pub fn array_bounds(ast: &ast::BanjoAst, ty: &ast::Ty) -> Option<String> {
    if let ast::Ty::Array { ref ty, size, .. } = ty {
        return if let Some(bounds) = array_bounds(ast, ty) {
            Some(format!("[{}]{}", size.0, bounds))
        } else {
            Some(format!("[{}]", size.0))
        };
    }
    None
}

fn interface_to_ops_c_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    if let ast::Ty::Identifier { id, .. } = ty {
        if ast.id_to_type(id) == ast::Ty::Interface {
            return Ok(to_c_name(id.name()) + "_ops_t");
        }
    }
    Err(format_err!("unknown ident type in interface_to_ops_c_str {:?}", ty))
}

pub fn not_callback(ast: &ast::BanjoAst, id: &Ident) -> bool {
    if let Some(attributes) = ast.id_to_attributes(id) {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == "ddk-callback" {
                return false;
            }
        }
    }
    true
}

fn size_to_c_str(ty: &ast::Ty, cons: &ast::Constant, ast: &ast::BanjoAst) -> String {
    let Constant(size) = cons;
    match ty {
        ast::Ty::Int8 => String::from(format!("INT8_C({})", size)),
        ast::Ty::Int16 => String::from(format!("INT16_C({})", size)),
        ast::Ty::Int32 => String::from(format!("INT32_C({})", size)),
        ast::Ty::Int64 => String::from(format!("INT64_C({})", size)),
        ast::Ty::UInt8 => String::from(format!("UINT8_C({})", size)),
        ast::Ty::UInt16 => String::from(format!("UINT16_C({})", size)),
        ast::Ty::UInt32 => String::from(format!("UINT32_C({})", size)),
        ast::Ty::UInt64 => String::from(format!("UINT64_C({})", size)),
        ast::Ty::USize | ast::Ty::Bool | ast::Ty::Str { .. } => size.clone(),
        ast::Ty::Identifier { id, reference: _ } => {
            let decl = ast.id_to_decl(id).unwrap();
            if let Decl::Enum { ty: enum_ty, variants, .. } = decl {
                for variant in variants {
                    if variant.name == *size {
                        return size_to_c_str(enum_ty, &variant.size, ast);
                    }
                }
            }
            panic!("don't handle this kind of identifier: {:?}", id);
        }
        s => panic!("don't handles this sized const: {}", s),
    }
}

pub fn name_buffer(ty: &str) -> &'static str {
    if ty == "void" {
        "buffer"
    } else {
        "list"
    }
}

pub fn name_size(ty: &str) -> &'static str {
    if ty == "void" {
        "size"
    } else {
        "count"
    }
}

fn get_first_param(ast: &BanjoAst, method: &ast::Method) -> Result<(bool, String), Error> {
    // Return parameter if a primitive type.
    if method.out_params.get(0).map_or(false, |p| p.1.is_primitive(&ast)) {
        Ok((true, ty_to_c_str(ast, &method.out_params[0].1)?))
    } else {
        Ok((false, "void".to_string()))
    }
}
fn get_in_params(m: &ast::Method, transform: bool, ast: &BanjoAst) -> Result<Vec<String>, Error> {
    m.in_params
        .iter()
        .map(|(name, ty)| {
            match ty {
                ast::Ty::Identifier { id, .. } => {
                    if id.is_base_type() {
                        let ty_name = ty_to_c_str(ast, ty).unwrap();
                        return Ok(format!("{} {}", ty_name, to_c_name(name)));
                    }
                    match ast.id_to_type(id) {
                        ast::Ty::Interface => {
                            let ty_name = ty_to_c_str(ast, ty).unwrap();
                            if transform && not_callback(ast, id) {
                                let ty_name = interface_to_ops_c_str(ast, ty).unwrap();
                                Ok(format!(
                                    "void* {name}_ctx, {ty_name}* {name}_ops",
                                    ty_name = ty_name,
                                    name = to_c_name(name)
                                ))
                            } else {
                                Ok(format!("const {}* {}", ty_name, to_c_name(name)))
                            }
                        }
                        ast::Ty::Struct | ast::Ty::Union => {
                            let ty_name = ty_to_c_str(ast, ty).unwrap();
                            // TODO: Using nullability to determine whether param is mutable is a hack.
                            let prefix = if ty.is_reference() { "" } else { "const " };
                            Ok(format!("{}{}* {}", prefix, ty_name, to_c_name(name)))
                        }
                        ast::Ty::Enum => {
                            Ok(format!("{} {}", ty_to_c_str(ast, ty).unwrap(), to_c_name(name)))
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
                        | ast::Ty::Voidptr => {
                            Ok(format!("{} {}", ty_to_c_str(ast, ty).unwrap(), to_c_name(name)))
                        }
                        e => Err(format_err!("unsupported: {}", e)),
                    }
                }
                ast::Ty::Str { .. } => {
                    Ok(format!("const {} {}", ty_to_c_str(ast, ty).unwrap(), to_c_name(name)))
                }
                ast::Ty::Array { .. } => {
                    let bounds = array_bounds(ast, ty).unwrap();
                    let ty = ty_to_c_str(ast, ty).unwrap();
                    Ok(format!(
                        "const {ty} {name}{bounds}",
                        bounds = bounds,
                        ty = ty,
                        name = to_c_name(name)
                    ))
                }
                ast::Ty::Vector { .. } => {
                    let ty = ty_to_c_str(ast, ty).unwrap();
                    Ok(format!(
                        "const {ty}* {name}_{buffer}, size_t {name}_{size}",
                        buffer = name_buffer(&ty),
                        size = name_size(&ty),
                        ty = ty,
                        name = to_c_name(name)
                    ))
                }
                _ => Ok(format!("{} {}", ty_to_c_str(ast, ty).unwrap(), to_c_name(name))),
            }
        })
        .collect()
}

fn get_out_params(
    m: &ast::Method,
    name: &str,
    ast: &BanjoAst,
) -> Result<(Vec<String>, String), Error> {
    if m.attributes.has_attribute("Async") {
        return Ok((
            vec![
                format!(
                    "{protocol_name}_{method_name}_callback callback",
                    protocol_name = to_c_name(name),
                    method_name = to_c_name(&m.name)
                ),
                "void* cookie".to_string(),
            ],
            "void".to_string(),
        ));
    }

    let (skip, return_param) = get_first_param(ast, m)?;
    let skip_amt = if skip { 1 } else { 0 };

    Ok((m.out_params.iter().skip(skip_amt).map(|(name, ty)| {
        let nullable = if ty.is_reference() { "*" } else { "" };
        let ty_name = ty_to_c_str(ast, ty).unwrap();
        match ty {
            ast::Ty::Interface => format!("const {}* {}", ty_name, to_c_name(name)),
            ast::Ty::Array { .. } => {
                let bounds = array_bounds(ast, ty).unwrap();
                let ty = ty_to_c_str(ast, ty).unwrap();
                format!(
                    "{ty} out_{name}{bounds}",
                    bounds = bounds,
                    ty = ty,
                    name = to_c_name(name)
                )
            }
            ast::Ty::Vector {..} => {
                if ty.is_reference() {
                    format!("{ty}** out_{name}_{buffer}, size_t* {name}_{size}",
                            buffer = name_buffer(&ty_name),
                            size = name_size(&ty_name),
                            ty = ty_name,
                            name = to_c_name(name))
                } else {
                    format!("{ty}* out_{name}_{buffer}, size_t {name}_{size}, size_t* out_{name}_actual",
                            buffer = name_buffer(&ty_name),
                            size = name_size(&ty_name),
                            ty = ty_name,
                            name = to_c_name(name))
                }
            },
            ast::Ty::Str {..} => {
                format!("{ty}{null} out_{c_name}, size_t {c_name}_capacity", ty = ty_name, null = nullable, c_name = to_c_name(name))
            }
            _ => format!("{}{}* out_{}", ty_name, nullable, to_c_name(name))
        }
    }).collect(), return_param))
}

fn get_in_args(m: &ast::Method, ast: &BanjoAst) -> Result<Vec<String>, Error> {
    Ok(m.in_params
        .iter()
        .map(|(name, ty)| match ty {
            ast::Ty::Vector { .. } => {
                let ty = ty_to_c_str(ast, ty).unwrap();
                format!(
                    "{name}_{buffer}, {name}_{size}",
                    buffer = name_buffer(&ty),
                    size = name_size(&ty),
                    name = to_c_name(name)
                )
            }
            _ => format!("{}", to_c_name(name)),
        })
        .collect())
}

fn get_out_args(m: &ast::Method, ast: &BanjoAst) -> Result<(Vec<String>, bool), Error> {
    if m.attributes.has_attribute("Async") {
        return Ok((vec!["callback".to_string(), "cookie".to_string()], false));
    }

    let (skip, _) = get_first_param(ast, m)?;
    let skip_amt = if skip { 1 } else { 0 };
    Ok((
        m.out_params
            .iter()
            .skip(skip_amt)
            .map(|(name, ty)| match ty {
                ast::Ty::Interface { .. } => format!("{}", to_c_name(name)),
                ast::Ty::Vector { .. } => {
                    let ty_name = ty_to_c_str(ast, ty).unwrap();
                    if ty.is_reference() {
                        format!(
                            "out_{name}_{buffer}, {name}_{size}",
                            buffer = name_buffer(&ty_name),
                            size = name_size(&ty_name),
                            name = to_c_name(name)
                        )
                    } else {
                        format!(
                            "out_{name}_{buffer}, {name}_{size}, out_{name}_actual",
                            buffer = name_buffer(&ty_name),
                            size = name_size(&ty_name),
                            name = to_c_name(name)
                        )
                    }
                }
                ast::Ty::Str { .. } => {
                    format!("out_{c_name}, {c_name}_capacity", c_name = to_c_name(name))
                }
                _ => format!("out_{}", to_c_name(name)),
            })
            .collect(),
        skip,
    ))
}

/// Checks whether a decl is a interface, and if it is a interface, checks that it is a "ddk-callback".
fn filter_callback<'a>(
    decl: &'a ast::Decl,
) -> Option<(&'a ast::Ident, &'a Vec<ast::Method>, &'a ast::Attrs)> {
    if let ast::Decl::Interface { ref name, ref methods, ref attributes } = *decl {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == "ddk-callback" {
                return Some((name, methods, attributes));
            }
        }
    }
    None
}

/// Checks whether a decl is an interface, and if it is an interface, checks that it is a "ddk-protocol".
fn filter_interface<'a>(
    decl: &'a ast::Decl,
) -> Option<(&'a ast::Ident, &'a Vec<ast::Method>, &'a ast::Attrs)> {
    if let ast::Decl::Interface { ref name, ref methods, ref attributes } = *decl {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == "ddk-interface" {
                return Some((name, methods, attributes));
            }
        }
    }
    None
}

/// Checks whether a decl is an interface, and if it is an interface, checks that it is a "ddk-protocol".
fn filter_protocol<'a>(
    decl: &'a ast::Decl,
) -> Option<(&'a ast::Ident, &'a Vec<ast::Method>, &'a ast::Attrs)> {
    if let ast::Decl::Interface { ref name, ref methods, ref attributes } = *decl {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == "ddk-callback" || layout == "ddk-interface" {
                None
            } else {
                Some((name, methods, attributes))
            }
        } else {
            Some((name, methods, attributes))
        }
    } else {
        None
    }
}

impl<'a, W: io::Write> CBackend<'a, W> {
    fn codegen_enum_decl(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespace
            .iter()
            .filter_map(|decl| {
                if let ast::Decl::Enum { ref name, ref ty, ref variants, .. } = *decl {
                    Some((name, ty, variants))
                } else {
                    None
                }
            })
            .map(|(name, ty, variants)| {
                let enum_defines = variants
                    .iter()
                    .map(|v| {
                        Ok(format!(
                            "#define {c_name}_{v_name} {c_size}",
                            c_name = to_c_name(name.name()).to_uppercase(),
                            v_name = v.name.to_uppercase().trim(),
                            c_size = size_to_c_str(ty, &v.size, ast)
                        ))
                    })
                    .collect::<Result<Vec<_>, Error>>()?
                    .join("\n");
                Ok(format!(
                    "typedef {ty} {c_name}_t;\n{enum_defines}",
                    c_name = to_c_name(name.name()),
                    ty = ty_to_c_str(ast, ty)?,
                    enum_defines = enum_defines
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_constant_defs(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespace
            .iter()
            .filter_map(|decl| {
                if let ast::Decl::Constant { ref name, ref ty, ref value, ref attributes } = *decl {
                    Some((name, ty, value, attributes))
                } else {
                    None
                }
            })
            .map(|(name, ty, value, attrs)| {
                let mut accum = String::new();
                accum.push_str(get_doc_comment(attrs, 0).as_str());
                accum.push_str(
                    format!(
                        "#define {name} {value}",
                        name = name.name().trim(),
                        value = size_to_c_str(ty, value, ast)
                    )
                    .as_str(),
                );
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_union_decl(
        &self,
        namespace: &Vec<ast::Decl>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(namespace
            .iter()
            .filter_map(|decl| {
                if let ast::Decl::Union { ref name, .. } = *decl {
                    Some(name)
                } else {
                    None
                }
            })
            .map(|name| {
                format!("typedef union {c_name} {c_name}_t;", c_name = to_c_name(name.name()))
            })
            .collect::<Vec<_>>()
            .join("\n"))
    }

    fn codegen_union_defs(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespace
            .iter()
            .filter_map(|decl| {
                if let ast::Decl::Union { ref name, ref fields, ref attributes, .. } = *decl {
                    Some((name, fields, attributes))
                } else {
                    None
                }
            })
            .map(|(name, fields, attrs)| {
                let members = fields
                    .iter()
                    .map(|f| {
                        let mut accum = String::new();
                        accum.push_str(get_doc_comment(&f.attributes, 1).as_str());
                        accum.push_str(
                            format!(
                                "    {ty} {c_name};",
                                c_name = to_c_name(f.ident.name()),
                                ty = ty_to_c_str(ast, &f.ty)?
                            )
                            .as_str(),
                        );
                        Ok(accum)
                    })
                    .collect::<Result<Vec<_>, Error>>()?
                    .join("\n");
                let mut accum = String::new();
                accum.push_str(get_doc_comment(attrs, 0).as_str());
                accum.push_str(
                    format!(
                        include_str!("templates/c/struct.h"),
                        c_name = to_c_name(name.name()),
                        decl = "union",
                        members = members
                    )
                    .as_str(),
                );
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_struct_decl(
        &self,
        namespace: &Vec<ast::Decl>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        let mut struct_decls = namespace
            .iter()
            .filter_map(|decl| {
                if let ast::Decl::Struct { ref name, .. } = *decl {
                    return Some(name);
                }
                None
            })
            .map(|name| {
                format!("typedef struct {c_name} {c_name}_t;", c_name = to_c_name(name.name()))
            })
            .collect::<Vec<_>>();
        // deterministic output
        struct_decls.sort();
        Ok(struct_decls.join("\n"))
    }

    fn codegen_struct_defs(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespace
            .iter()
            .filter_map(|decl| {
                if let ast::Decl::Struct { ref name, ref fields, ref attributes } = *decl {
                    Some((name, fields, attributes))
                } else {
                    None
                }
            })
            .map(|(name, fields, attrs)| {
                let members = fields
                    .iter()
                    .map(|f| {
                        let mut accum = String::new();
                        accum.push_str(get_doc_comment(&f.attributes, 1).as_str());
                        match f.ty {
                            ast::Ty::Vector { .. } => {
                                let ty_name = ty_to_c_str(ast, &f.ty)?;
                                accum.push_str(
                                    format!(
                                        "    {ty}* {c_name}_{buffer};\n    size_t {c_name}_{size};",
                                        buffer = name_buffer(&ty_name),
                                        size = name_size(&ty_name),
                                        c_name = to_c_name(f.ident.name()),
                                        ty = ty_name,
                                    )
                                    .as_str(),
                                );
                            }
                            ast::Ty::Array { ref size, .. } => {
                                accum.push_str(
                                    format!(
                                        "    {ty} {c_name}[{size}];",
                                        c_name = to_c_name(f.ident.name()),
                                        size = size,
                                        ty = ty_to_c_str(ast, &f.ty)?
                                    )
                                    .as_str(),
                                );
                            }
                            _ => {
                                accum.push_str(
                                    format!(
                                        "    {ty} {c_name};",
                                        c_name = to_c_name(f.ident.name()),
                                        ty = ty_to_c_str(ast, &f.ty)?
                                    )
                                    .as_str(),
                                );
                            }
                        }
                        Ok(accum)
                    })
                    .collect::<Result<Vec<_>, Error>>()?
                    .join("\n");
                let mut accum = String::new();
                accum.push_str(get_doc_comment(attrs, 0).as_str());
                accum.push_str(
                    format!(
                        include_str!("templates/c/struct.h"),
                        c_name = to_c_name(name.name()),
                        decl = "struct",
                        members = members
                    )
                    .as_str(),
                );
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_protocol_def(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        protocol: bool,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let fns = methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(&m, name, ast)?;
                let in_params = get_in_params(&m, false, ast)?;

                let params = iter::once("void* ctx".to_string())
                    .chain(in_params)
                    .chain(out_params)
                    .collect::<Vec<_>>()
                    .join(", ");
                Ok(format!(
                    "    {return_param} (*{fn_name})({params});",
                    return_param = return_param,
                    params = params,
                    fn_name = to_c_name(m.name.as_str())
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        let protocol = if protocol { "protocol_" } else { "" };
        Ok(format!(
            include_str!("templates/c/protocol_ops.h"),
            c_name = to_c_name(name),
            protocol = protocol,
            fns = fns
        ))
    }

    fn codegen_helper_def(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        protocol: bool,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let mut accum = String::new();
                accum.push_str(get_doc_comment(&m.attributes, 0).as_str());

                let (out_params, return_param) = get_out_params(&m, name, ast)?;
                let in_params = get_in_params(&m, true, ast)?;

                let protocol = if protocol { "_protocol" } else { "" };
                let first_param = format!("const {}{}_t* proto", to_c_name(name), protocol);

                let params = iter::once(first_param)
                    .chain(in_params)
                    .chain(out_params)
                    .collect::<Vec<_>>()
                    .join(", ");

                accum.push_str(
                    format!(
                        "static inline {return_param} {protocol_name}_{fn_name}({params}) {{\n",
                        return_param = return_param,
                        params = params,
                        protocol_name = to_c_name(name),
                        fn_name = to_c_name(m.name.as_str())
                    )
                    .as_str(),
                );

                let (out_args, skip) = get_out_args(&m, ast)?;
                let in_args = get_in_args(&m, ast)?;

                let proto_args = m
                    .in_params
                    .iter()
                    .filter_map(|(name, ty)| {
                        if let ast::Ty::Identifier { id, .. } = ty {
                            if ast.id_to_type(id) == ast::Ty::Interface && not_callback(ast, id) {
                                return Some((to_c_name(name), ty_to_c_str(ast, ty).unwrap()));
                            }
                        }
                        None
                    })
                    .collect::<Vec<_>>();
                for (name, ty) in proto_args.iter() {
                    accum.push_str(
                        format!(
                            include_str!("templates/c/proto_transform.h"),
                            ty = ty,
                            name = name
                        )
                        .as_str(),
                    );
                }

                let args = iter::once("proto->ctx".to_string())
                    .chain(in_args)
                    .chain(out_args)
                    .collect::<Vec<_>>()
                    .join(", ");

                let return_statement = if skip { "return " } else { "" };

                accum.push_str(
                    format!(
                        "    {return_statement}proto->ops->{fn_name}({args});\n",
                        return_statement = return_statement,
                        args = args,
                        fn_name = to_c_name(m.name.as_str())
                    )
                    .as_str(),
                );
                accum.push_str("}\n");
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_protocol_defs(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespace
            .into_iter()
            .filter_map(filter_protocol)
            .map(|(name, methods, _)| {
                Ok(format!(
                    include_str!("templates/c/protocol.h"),
                    protocol_name = to_c_name(name.name()),
                    protocol_def = self.codegen_protocol_def(name.name(), methods, true, ast)?,
                    helper_def = self.codegen_helper_def(name.name(), methods, true, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_interface_defs(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespace
            .into_iter()
            .filter_map(filter_interface)
            .map(|(name, methods, _)| {
                Ok(format!(
                    include_str!("templates/c/interface.h"),
                    protocol_name = to_c_name(name.name()),
                    protocol_def = self.codegen_protocol_def(name.name(), methods, false, ast)?,
                    helper_def = self.codegen_helper_def(name.name(), methods, false, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_callback_defs(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespace
            .into_iter()
            .filter_map(filter_callback)
            .map(|(name, methods, _)| {
                let m = methods.get(0).ok_or(format_err!("callback has no methods"))?;
                let (out_params, return_param) = get_out_params(&m, name.name(), ast)?;
                let in_params = get_in_params(&m, false, ast)?;

                let params = iter::once("void* ctx".to_string())
                    .chain(in_params)
                    .chain(out_params)
                    .collect::<Vec<_>>()
                    .join(", ");
                let method = format!(
                    "{return_param} (*{fn_name})({params})",
                    return_param = return_param,
                    params = params,
                    fn_name = to_c_name(m.name.as_str())
                );
                Ok(format!(
                    include_str!("templates/c/callback.h"),
                    callback_name = to_c_name(name.name()),
                    callback = method,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_protocol_decl(
        &self,
        namespace: &Vec<ast::Decl>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(namespace
            .into_iter()
            .filter_map(filter_protocol)
            .map(|(name, _, _)| {
                format!(
                    "typedef struct {c_name}_protocol {c_name}_protocol_t;",
                    c_name = to_c_name(name.name())
                )
            })
            .collect::<Vec<_>>()
            .join("\n"))
    }

    fn codegen_interface_decl(
        &self,
        namespace: &Vec<ast::Decl>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(namespace
            .into_iter()
            .filter_map(filter_interface)
            .map(|(name, _, _)| {
                format!("typedef struct {c_name} {c_name}_t;", c_name = to_c_name(name.name()))
            })
            .collect::<Vec<_>>()
            .join("\n"))
    }

    fn codegen_callback_decl(
        &self,
        namespace: &Vec<ast::Decl>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(namespace
            .into_iter()
            .filter_map(filter_callback)
            .map(|(name, _, _)| {
                format!("typedef struct {c_name} {c_name}_t;", c_name = to_c_name(name.name()))
            })
            .collect::<Vec<_>>()
            .join("\n"))
    }

    fn codegen_async_decl(
        &self,
        namespace: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(namespace
            .into_iter()
            .filter_map(|decl| {
                if let ast::Decl::Interface { ref name, ref methods, .. } = *decl {
                    return Some((name, methods));
                }
                None
            })
            .flat_map(|(name, methods)| iter::repeat(name).zip(methods.iter()))
            .filter(|(_, method)| method.attributes.has_attribute("Async"))
            .map(|(name, method)| {
                let method = ast::Method {
                    attributes: method.attributes.clone(),
                    name: method.name.clone(),
                    in_params: method.out_params.clone(),
                    out_params: Vec::new(),
                };
                let in_params = get_in_params(&method, true, ast)?;
                let params = iter::once("void* ctx".to_string())
                    .chain(in_params)
                    .collect::<Vec<_>>()
                    .join(", ");
                Ok(format!(
                    "typedef void (*{protocol_name}_{method_name}_callback)({params});",
                    protocol_name = to_c_name(name.name()),
                    method_name = to_c_name(method.name.as_str()),
                    params = params
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n"))
    }

    fn codegen_includes(&self, ast: &BanjoAst) -> Result<String, Error> {
        Ok(ast
            .namespaces
            .iter()
            .filter(|n| *n.0 != ast.primary_namespace)
            .filter(|n| *n.0 != "zx")
            .map(|n| format!("#include <{}.h>", n.0.replace('.', "/")))
            .collect::<Vec<_>>()
            .join("\n"))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/c/header.h"),
            includes = self.codegen_includes(&ast)?,
            primary_namespace = ast.primary_namespace
        ))?;

        let namespace = &ast.namespaces[&ast.primary_namespace];
        self.w.write_fmt(format_args!(
            include_str!("templates/c/body.h"),
            enum_decls = self.codegen_enum_decl(namespace, &ast)?,
            union_decls = self.codegen_union_decl(namespace, &ast)?,
            struct_decls = self.codegen_struct_decl(namespace, &ast)?,
            callback_decls = self.codegen_callback_decl(namespace, &ast)?,
            interface_decls = self.codegen_interface_decl(namespace, &ast)?,
            protocol_decls = self.codegen_protocol_decl(namespace, &ast)?,
            async_decls = self.codegen_async_decl(namespace, &ast)?,
            constant_definitions = self.codegen_constant_defs(namespace, &ast)?,
            union_definitions = self.codegen_union_defs(namespace, &ast)?,
            struct_definitions = self.codegen_struct_defs(namespace, &ast)?,
            callback_definitions = self.codegen_callback_defs(namespace, &ast)?,
            interface_definitions = self.codegen_interface_defs(namespace, &ast)?,
            protocol_definitions = self.codegen_protocol_defs(namespace, &ast)?
        ))?;
        Ok(())
    }
}
