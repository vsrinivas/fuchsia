// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{
        self, Attrs, BanjoAst, Constant, Decl, EnumVariant, Ident, Method, StructField, UnionField,
    },
    crate::backends::util::to_c_name,
    crate::backends::Backend,
    anyhow::{format_err, Error},
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
                    ast::Ty::Struct | ast::Ty::Union | ast::Ty::Enum => {
                        return Ok(format!("{}_t", to_c_name(id.name())));
                    }
                    ast::Ty::Protocol => {
                        if not_callback(ast, id) {
                            return Ok(format!("{}_protocol_t", to_c_name(id.name())));
                        } else {
                            return Ok(format!("{}_t", to_c_name(id.name())));
                        }
                    }
                    t => return ty_to_c_str(ast, &t),
                }
            }
        }
        ast::Ty::Handle { .. } => Ok(String::from("zx_handle_t")),
        t => Err(format_err!("unknown type in ty_to_c_str {:?}", t)),
    }
}

fn ident_to_c_str(ast: &ast::BanjoAst, ident: &Ident) -> Result<String, Error> {
    ty_to_c_str(ast, &ast::Ty::Identifier { id: ident.clone(), reference: false })
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

fn protocol_to_ops_c_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    if let ast::Ty::Identifier { id, .. } = ty {
        if ast.id_to_type(id) == ast::Ty::Protocol {
            return Ok(to_c_name(id.name()) + "_protocol_ops_t");
        }
    }
    Err(format_err!("unknown ident type in protocol_to_ops_c_str {:?}", ty))
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
            let decl = ast.id_to_decl(id).expect(format!("id: {:?}", id).as_str());
            if let Decl::Enum { ty: enum_ty, variants, .. } = decl {
                for variant in variants {
                    if variant.name == *size {
                        return size_to_c_str(enum_ty, &variant.value, ast);
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

fn struct_attrs_to_c_str(attributes: &Attrs) -> String {
    attributes
        .0
        .iter()
        .filter_map(|a| match a.key.as_ref() {
            "Packed" => Some("__attribute__ ((packed))"),
            _ => None,
        })
        .collect::<Vec<_>>()
        .join(" ")
}

fn field_to_c_str(
    attrs: &Attrs,
    ty: &ast::Ty,
    ident: &Ident,
    indent: &str,
    preserve_names: bool,
    ast: &ast::BanjoAst,
) -> Result<String, Error> {
    let mut accum = String::new();
    accum.push_str(get_doc_comment(attrs, 1).as_str());
    let prefix = if ty.is_reference() { "" } else { "const " };
    let c_name = if preserve_names { String::from(ident.name()) } else { to_c_name(ident.name()) };
    match ty {
        ast::Ty::Vector { ty: ref inner_ty, .. } => {
            let ty_name = ty_to_c_str(ast, &ty)?;
            // TODO(surajmalhotra): Support multi-dimensional vectors.
            let ptr = if inner_ty.is_reference() { "*" } else { "" };
            accum.push_str(
                format!(
                    "{indent}{prefix}{ty}{ptr}* {c_name}_{buffer};\n\
                     {indent}size_t {c_name}_{size};",
                    indent = indent,
                    buffer = name_buffer(&ty_name),
                    size = name_size(&ty_name),
                    c_name = c_name,
                    prefix = prefix,
                    ty = ty_name,
                    ptr = ptr,
                )
                .as_str(),
            );
        }
        ast::Ty::Array { .. } => {
            let bounds = array_bounds(ast, &ty).unwrap();
            accum.push_str(
                format!(
                    "{indent}{ty} {c_name}{bounds};",
                    indent = indent,
                    c_name = c_name,
                    bounds = bounds,
                    ty = ty_to_c_str(ast, &ty)?
                )
                .as_str(),
            );
        }
        ast::Ty::Str { ref size, .. } => {
            if let Some(size) = size {
                accum.push_str(
                    format!(
                        "{indent}char {c_name}[{size}];",
                        indent = indent,
                        c_name = c_name,
                        size = size,
                    )
                    .as_str(),
                );
            } else {
                accum.push_str(
                    format!(
                        "{indent}{prefix}{ty} {c_name};",
                        indent = indent,
                        c_name = c_name,
                        prefix = prefix,
                        ty = ty_to_c_str(ast, &ty)?
                    )
                    .as_str(),
                );
            }
        }
        _ => {
            accum.push_str(
                format!(
                    "{indent}{ty} {c_name};",
                    indent = indent,
                    c_name = c_name,
                    ty = ty_to_c_str(ast, &ty)?
                )
                .as_str(),
            );
        }
    }
    Ok(accum)
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
                        ast::Ty::Protocol => {
                            let ty_name = ty_to_c_str(ast, ty).unwrap();
                            if transform && not_callback(ast, id) {
                                let ty_name = protocol_to_ops_c_str(ast, ty).unwrap();
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
                ast::Ty::Vector { ty: inner_ty, .. } => {
                    let ty = ty_to_c_str(ast, ty).unwrap();
                    // TODO(surajmalhotra): Support multi-dimensional vectors.
                    let ptr = if inner_ty.is_reference() { "*" } else { "" };
                    Ok(format!(
                        "const {ty}{ptr}* {name}_{buffer}, size_t {name}_{size}",
                        buffer = name_buffer(&ty),
                        size = name_size(&ty),
                        ty = ty,
                        ptr = ptr,
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
            ast::Ty::Protocol => format!("const {}* {}", ty_name, to_c_name(name)),
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
            ast::Ty::Vector { ty: inner_ty, .. } => {
                // TODO(surajmalhotra): Support multi-dimensional vectors.
                let ptr = if inner_ty.is_reference() { "*" } else { "" };
                if ty.is_reference() {
                    format!("{ty}{ptr}** out_{name}_{buffer}, size_t* {name}_{size}",
                            buffer = name_buffer(&ty_name),
                            size = name_size(&ty_name),
                            ty = ty_name,
                            ptr = ptr,
                            name = to_c_name(name))
                } else {
                    format!("{ty}{ptr}* out_{name}_{buffer}, size_t {name}_{size}, size_t* out_{name}_actual",
                            buffer = name_buffer(&ty_name),
                            size = name_size(&ty_name),
                            ty = ty_name,
                            ptr = ptr,
                            name = to_c_name(name))
                }
            },
            ast::Ty::Str {..} => {
                format!("{ty} out_{c_name}, size_t {c_name}_capacity",
                        ty = ty_name, c_name = to_c_name(name))
            }
            ast::Ty::Handle {..} => format!("{}* out_{}", ty_name, to_c_name(name)),
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
                ast::Ty::Protocol { .. } => format!("{}", to_c_name(name)),
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

enum ProtocolType {
    Callback,
    Interface,
    Protocol,
}

impl From<&Attrs> for ProtocolType {
    fn from(attributes: &Attrs) -> Self {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == "ddk-callback" {
                ProtocolType::Callback
            } else if layout == "ddk-interface" {
                ProtocolType::Interface
            } else if layout == "ddk-protocol" {
                ProtocolType::Protocol
            } else {
                panic!("Unknown layout attribute: {}", layout);
            }
        } else {
            ProtocolType::Protocol
        }
    }
}

impl<'a, W: io::Write> CBackend<'a, W> {
    fn codegen_enum_decl(
        &self,
        _attributes: &Attrs,
        name: &Ident,
        ty: &ast::Ty,
        variants: &Vec<EnumVariant>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let enum_defines = variants
            .iter()
            .map(|v| {
                Ok(format!(
                    "#define {c_name}_{v_name} {c_size}",
                    c_name = to_c_name(name.name()).to_uppercase(),
                    v_name = v.name.to_uppercase().trim(),
                    c_size = size_to_c_str(ty, &v.value, ast)
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
    }

    fn codegen_constant_decl(
        &self,
        attributes: &Attrs,
        name: &Ident,
        ty: &ast::Ty,
        value: &Constant,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let mut accum = String::new();
        accum.push_str(get_doc_comment(attributes, 0).as_str());
        accum.push_str(
            format!(
                "#define {name} {value}",
                name = name.name().trim(),
                value = size_to_c_str(ty, value, ast)
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_union_decl(
        &self,
        _attributes: &Attrs,
        name: &Ident,
        _fields: &Vec<UnionField>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(format!("typedef union {c_name} {c_name}_t;", c_name = to_c_name(name.name())))
    }

    fn codegen_union_def(
        &self,
        attributes: &Attrs,
        name: &Ident,
        fields: &Vec<UnionField>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let attrs = struct_attrs_to_c_str(attributes);
        let members = fields
            .iter()
            .map(|f| match f.ty {
                ast::Ty::Vector { .. } => Err(format_err!("unsupported for UnionField: {:?}", f)),
                _ => field_to_c_str(&f.attributes, &f.ty, &f.ident, "    ", false, &ast),
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        let mut accum = String::new();
        accum.push_str(get_doc_comment(attributes, 0).as_str());
        accum.push_str(
            format!(
                include_str!("templates/c/struct.h"),
                c_name = to_c_name(name.name()),
                decl = "union",
                attrs = if attrs.is_empty() { "".to_string() } else { format!(" {}", attrs) },
                members = members
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_struct_decl(
        &self,
        _attributes: &Attrs,
        name: &Ident,
        fields: &Vec<StructField>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        // TODO(surajmalhotra): Remove this hack once we no longer include C types.
        if fields.len() == 0 {
            return Ok("".to_string());
        }
        Ok(format!("typedef struct {c_name} {c_name}_t;", c_name = to_c_name(name.name())))
    }

    fn codegen_struct_def(
        &self,
        attributes: &Attrs,
        name: &Ident,
        fields: &Vec<StructField>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        // TODO(surajmalhotra): Remove this hack once we no longer include C types.
        if fields.len() == 0 {
            return Ok("".to_string());
        }
        let attrs = struct_attrs_to_c_str(attributes);
        let preserve_names = attributes.has_attribute("PreserveCNames");
        let members = fields
            .iter()
            .map(|f| field_to_c_str(&f.attributes, &f.ty, &f.ident, "    ", preserve_names, &ast))
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        let mut accum = String::new();
        accum.push_str(get_doc_comment(attributes, 0).as_str());
        accum.push_str(
            format!(
                include_str!("templates/c/struct.h"),
                c_name = to_c_name(name.name()),
                decl = "struct",
                attrs = if attrs.is_empty() { "".to_string() } else { format!(" {}", attrs) },
                members = members
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_protocol_def2(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
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
        Ok(format!(include_str!("templates/c/protocol_ops.h"), c_name = to_c_name(name), fns = fns))
    }

    fn codegen_helper_def(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let mut accum = String::new();
                accum.push_str(get_doc_comment(&m.attributes, 0).as_str());

                let (out_params, return_param) = get_out_params(&m, name, ast)?;
                let in_params = get_in_params(&m, true, ast)?;

                let first_param = format!("const {}_protocol_t* proto", to_c_name(name));

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
                            if ast.id_to_type(id) == ast::Ty::Protocol && not_callback(ast, id) {
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

    fn codegen_protocol_def(
        &self,
        attributes: &Attrs,
        name: &Ident,
        methods: &Vec<Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(match ProtocolType::from(attributes) {
            ProtocolType::Interface | ProtocolType::Protocol => format!(
                include_str!("templates/c/protocol.h"),
                protocol_name = to_c_name(name.name()),
                protocol_def = self.codegen_protocol_def2(name.name(), methods, ast)?,
                helper_def = self.codegen_helper_def(name.name(), methods, ast)?
            ),
            ProtocolType::Callback => {
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
                format!(
                    include_str!("templates/c/callback.h"),
                    callback_name = to_c_name(name.name()),
                    callback = method,
                )
            }
        })
    }

    fn codegen_async_decls(
        &self,
        name: &Ident,
        methods: &Vec<Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(methods
            .iter()
            .filter(|method| method.attributes.has_attribute("Async"))
            .map(|method| {
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
                    "typedef void (*{protocol_name}_{method_name}_callback)({params});\n",
                    protocol_name = to_c_name(name.name()),
                    method_name = to_c_name(method.name.as_str()),
                    params = params
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join(""))
    }

    fn codegen_protocol_decl(
        &self,
        attributes: &Attrs,
        name: &Ident,
        methods: &Vec<Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        Ok(match ProtocolType::from(attributes) {
            ProtocolType::Interface | ProtocolType::Protocol => format!(
                "{async_decls}typedef struct {c_name}_protocol {c_name}_protocol_t;",
                async_decls = self.codegen_async_decls(name, methods, ast)?,
                c_name = to_c_name(name.name())
            ),
            ProtocolType::Callback => {
                format!("typedef struct {c_name} {c_name}_t;", c_name = to_c_name(name.name()))
            }
        })
    }

    fn codegen_alias_decl(
        &self,
        to: &Ident,
        from: &Ident,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        if to.is_base_type() {
            // Not generating anything for base types that for now are sourced from the sysroot.
            Ok("".to_string())
        } else {
            Ok(format!(
                "typedef {from} {to}_t;",
                to = to_c_name(to.name()),
                from = ident_to_c_str(ast, from)?
            ))
        }
    }

    fn codegen_includes(&self, ast: &BanjoAst) -> Result<String, Error> {
        Ok(ast
            .namespaces
            .iter()
            .filter(|n| *n.0 != ast.primary_namespace)
            .filter(|n| *n.0 != "zx")
            .map(|n| {
                if n.0.contains("fuchsia.hardware") || n.0.contains("ddk.hw") {
                    n.0.replace('.', "/") + "/c/banjo"
                } else {
                    n.0.replace('.', "/")
                }
            })
            .map(|n| format!("#include <{}.h>", n))
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

        let decl_order = ast.validate_declaration_deps()?;

        let declarations = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Struct { attributes, name, fields } => {
                    Some(self.codegen_struct_decl(attributes, name, fields, &ast))
                }
                Decl::Union { attributes, name, fields } => {
                    Some(self.codegen_union_decl(attributes, name, fields, &ast))
                }
                Decl::Enum { attributes, name, ty, variants } => {
                    Some(self.codegen_enum_decl(attributes, name, ty, variants, &ast))
                }
                Decl::Constant { attributes, name, ty, value } => {
                    Some(self.codegen_constant_decl(attributes, name, ty, value, &ast))
                }
                Decl::Protocol { attributes, name, methods } => {
                    Some(self.codegen_protocol_decl(attributes, name, methods, &ast))
                }
                Decl::Alias(to, from) => Some(self.codegen_alias_decl(to, from, &ast)),
                Decl::Resource { .. } => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        let definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Struct { attributes, name, fields } => {
                    Some(self.codegen_struct_def(attributes, name, fields, &ast))
                }
                Decl::Union { attributes, name, fields } => {
                    Some(self.codegen_union_def(attributes, name, fields, &ast))
                }
                Decl::Enum { .. } => None,
                Decl::Constant { .. } => None,
                Decl::Protocol { attributes, name, methods } => {
                    Some(self.codegen_protocol_def(attributes, name, methods, &ast))
                }
                Decl::Alias(_to, _from) => None,
                Decl::Resource { .. } => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        self.w.write_fmt(format_args!(
            include_str!("templates/c/body.h"),
            declarations = declarations,
            definitions = definitions,
        ))?;
        Ok(())
    }
}
