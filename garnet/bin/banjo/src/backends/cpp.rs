// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst},
    crate::backends::Backend,
    failure::{format_err, Error},
    heck::SnakeCase,
    std::collections::HashSet,
    std::io,
    std::iter,
};

fn to_cpp_name(name: &str) -> &str {
    // strip FQN
    name.split(".").last().unwrap()
}

fn to_c_name(name: &str) -> String {
    // strip FQN
    let name = name.split(".").last().unwrap();
    let mut iter = name.chars().peekable();
    let mut accum = String::new();
    while let Some(c) = iter.next() {
        accum.push(c);
        if let Some(c2) = iter.peek() {
            if c.is_ascii_uppercase() && c.is_ascii_uppercase() {
                accum.push(c2.to_ascii_lowercase());
                iter.next();
            }
        }
    }
    accum.to_snake_case()
}

fn get_doc_comment(attrs: &ast::Attrs, tabs: usize) -> String {
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

fn handle_ty_to_cpp_str(_ast: &ast::BanjoAst, ty: &ast::HandleTy) -> Result<String, Error> {
    match ty {
        ast::HandleTy::Handle => Ok(String::from("zx::handle")),
        ast::HandleTy::Process => Ok(String::from("zx::process")),
        ast::HandleTy::Thread => Ok(String::from("zx::thread")),
        ast::HandleTy::Vmo => Ok(String::from("zx::vmo")),
        ast::HandleTy::Channel => Ok(String::from("zx::channel")),
        ast::HandleTy::Event => Ok(String::from("zx::event")),
        ast::HandleTy::Port => Ok(String::from("zx::port")),
        ast::HandleTy::Interrupt => Ok(String::from("zx::interrupt")),
        ast::HandleTy::Log => Ok(String::from("zx::log")),
        ast::HandleTy::Socket => Ok(String::from("zx::socket")),
        ast::HandleTy::Resource => Ok(String::from("zx::resource")),
        ast::HandleTy::EventPair => Ok(String::from("zx::eventpair")),
        ast::HandleTy::Job => Ok(String::from("zx::job")),
        ast::HandleTy::Vmar => Ok(String::from("zx::vmar")),
        ast::HandleTy::Fifo => Ok(String::from("zx::fifo")),
        ast::HandleTy::Guest => Ok(String::from("zx::guest")),
        ast::HandleTy::Timer => Ok(String::from("zx::timer")),
        ast::HandleTy::Bti => Ok(String::from("zx::bti")),
        ast::HandleTy::Profile => Ok(String::from("zx::profile")),
    }
}

fn ty_to_cpp_str(ast: &ast::BanjoAst, wrappers: bool, ty: &ast::Ty) -> Result<String, Error> {
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
        ast::Ty::Voidptr => Ok(String::from("void*")),
        ast::Ty::Str { .. } => Ok(String::from("const char*")),
        ast::Ty::Vector { ref ty, .. } => ty_to_cpp_str(ast, wrappers, ty),
        ast::Ty::Ident { id, .. } => {
            if id == "zx.status" {
                Ok("zx_status_t".to_string())
            } else {
                match ast.id_to_type(id) {
                    ast::Ty::Struct | ast::Ty::Union | ast::Ty::Interface | ast::Ty::Enum => {
                        return Ok(to_c_name(id.clone().as_str()) + "_t");
                    }
                    t => Err(format_err!("unknown ident type in ty_to_cpp_str {:?}", t)),
                }
            }
        }
        ast::Ty::Handle { ty, .. } => {
            if wrappers {
                handle_ty_to_cpp_str(ast, ty)
            } else {
                Ok(String::from("zx_handle_t"))
            }
        }
        t => Err(format_err!("unknown type in ty_to_cpp_str {:?}", t)),
    }
}

fn interface_to_ops_cpp_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    if let ast::Ty::Ident { id, .. } = ty {
        if ast.id_to_type(id) == ast::Ty::Interface {
            return Ok(to_c_name(id.clone().as_str()) + "_ops_t");
        }
    }
    Err(format_err!("unknown ident type in interface_to_ops_cpp_str {:?}", ty))
}

fn not_callback(ast: &ast::BanjoAst, id: &str) -> bool {
    if let Some(attributes) = ast.id_to_attributes(id) {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == " \"ddk-callback\"" {
            return false;
            }
        }
    }
    true
}

fn name_buffer(ty: &str) -> &'static str {
    if ty == "void" {
        "buffer"
    } else {
        "list"
    }
}
fn name_size(ty: &str) -> &'static str {
    if ty == "void" {
        "size"
    } else {
        "count"
    }
}

fn get_first_param(ast: &BanjoAst, method: &ast::Method) -> Result<(bool, String), Error> {
    // Return parameter if a primitive type.
    if method.out_params.get(0).map_or(false, |p| p.1.is_primitive()) {
        Ok((true, ty_to_cpp_str(ast, false, &method.out_params[0].1)?))
    } else {
        Ok((false, "void".to_string()))
    }
}
fn get_in_params(m: &ast::Method, wrappers: bool, transform: bool, ast: &BanjoAst) -> Result<Vec<String>, Error> {
    m.in_params
        .iter()
        .map(|(name, ty)| {
            match ty {
                ast::Ty::Ident { id, .. } => {
                    match ast.id_to_type(id) {
                        ast::Ty::Interface => {
                            let ty_name = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                            if ty_name == "zx_status_t" {
                                Ok(format!("{} {}", ty_name, to_c_name(name)))
                            } else if transform && not_callback(ast, id) {
                                let ty_name = interface_to_ops_cpp_str(ast, ty).unwrap();
                                Ok(format!("void* {name}_ctx, {ty_name}* {name}_ops",
                                           ty_name=ty_name, name=to_c_name(name)))
                            } else {
                                Ok(format!("const {}* {}", ty_name, to_c_name(name)))
                            }
                        }
                        ast::Ty::Struct | ast::Ty::Union => {
                            let ty_name = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                            // TODO: Using nullability to determine whether param is mutable is a hack.
                            let prefix = if ty.is_nullable() { "" } else { "const " };
                            Ok(format!("{}{}* {}", prefix, ty_name, to_c_name(name)))
                        }
                        ast::Ty::Enum => Ok(format!(
                            "{} {}",
                            ty_to_cpp_str(ast, wrappers, ty).unwrap(),
                            to_c_name(name)
                        )),
                        ty => {
                            let ty_name = ty_to_cpp_str(ast, wrappers, &ty).unwrap();
                            Ok(format!("{} {}", ty_name, to_c_name(name)))
                        }
                    }
                }
                ast::Ty::Vector { .. } => {
                    let ty = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                    Ok(format!(
                        "const {ty}* {name}_{buffer}, size_t {name}_{size}",
                        buffer = name_buffer(&ty),
                        size = name_size(&ty),
                        ty = ty,
                        name = to_c_name(name)
                    ))
                }
                _ => {
                    Ok(format!("{} {}", ty_to_cpp_str(ast, wrappers, ty).unwrap(), to_c_name(name)))
                }
            }
        })
        .collect()
}

fn get_out_params(
    m: &ast::Method,
    name: &str,
    wrappers: bool,
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
        let nullable = if ty.is_nullable() { "*" } else { "" };
        let ty_name = ty_to_cpp_str(ast, wrappers, ty).unwrap();
        match ty {
            ast::Ty::Interface => format!("const {}* {}", ty_name, to_c_name(name)),
            ast::Ty::Ident {..} => {
                let star = if ty_name == "zx_status_t" { "" } else { "*" };
                format!("{}{}{} out_{}", ty_name, star, nullable, to_c_name(name))
            },
            ast::Ty::Vector {..} => {
                if ty.is_nullable() {
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
            _ => format!("{}{}* out_{}", ty_name, nullable, to_c_name(name))
        }
    }).collect(), return_param))
}

fn get_in_args(m: &ast::Method, wrappers: bool, ast: &BanjoAst) -> Result<Vec<String>, Error> {
    Ok(m.in_params
        .iter()
        .map(|(name, ty)| match ty {
            ast::Ty::Vector { .. } => {
                let ty = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                format!(
                    "{name}_{buffer}, {name}_{size}",
                    buffer = name_buffer(&ty),
                    size = name_size(&ty),
                    name = to_c_name(name)
                )
            }
            ast::Ty::Handle { .. } => {
                if wrappers {
                    format!("{}({})", ty_to_cpp_str(ast, wrappers, ty).unwrap(), to_c_name(name))
                } else {
                    format!("{}.release()", to_c_name(name))
                }
            }
            _ => format!("{}", to_c_name(name)),
        })
        .collect())
}

fn get_out_args(
    m: &ast::Method,
    client: bool,
    ast: &BanjoAst,
) -> Result<(Vec<String>, bool), Error> {
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
                    let ty_name = ty_to_cpp_str(ast, false, ty).unwrap();
                    if ty.is_nullable() {
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
                ast::Ty::Handle { .. } => {
                    if client {
                        format!("out_{}->reset_and_get_address()", to_c_name(name))
                    } else {
                        format!("&out_{}2", to_c_name(name))
                    }
                }
                _ => format!("out_{}", to_c_name(name)),
            })
            .collect(),
        skip,
    ))
}

/// Checks whether a decl is an interface, and if it is an interface, checks that it is a "ddk-interface".
fn filter_interface<'a>(
    decl: &'a ast::Decl,
) -> Option<(&'a String, &'a Vec<ast::Method>, &'a ast::Attrs)> {
    if let ast::Decl::Interface { ref name, ref methods, ref attributes } = *decl {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == " \"ddk-interface\"" {
                return Some((name, methods, attributes));
            }
        }
    }
    None
}

/// Checks whether a decl is an interface, and if it is an interface, checks that it is a "ddk-protocol".
fn filter_protocol<'a>(
    decl: &'a ast::Decl,
) -> Option<(&'a String, &'a Vec<ast::Method>, &'a ast::Attrs)> {
    if let ast::Decl::Interface { ref name, ref methods, ref attributes } = *decl {
        if let Some(layout) = attributes.get_attribute("Layout") {
            if layout == " \"ddk-protocol\"" {
                return Some((name, methods, attributes));
            }
        } else {
            return Some((name, methods, attributes));
        }
    }
    None
}

pub struct CppInternalBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> CppInternalBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        CppInternalBackend { w }
    }
}

impl<'a, W: io::Write> CppInternalBackend<'a, W> {
    fn codegen_decls(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(&m, name, true, ast)?;
                let in_params = get_in_params(&m, true, false, ast)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                Ok(format!(
                    include_str!("templates/cpp/internal_decl.h"),
                    return_param = return_param,
                    params = params,
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name),
                    method_name = to_cpp_name(m.name.as_str()),
                    method_name_snake = to_c_name(m.name.as_str())
                ))
            })
            .collect::<Result<Vec<String>, Error>>()
            .map(|fns| fns.join("\n"))
    }

    fn codegen_static_asserts(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(&m, name, true, ast)?;
                let in_params = get_in_params(&m, true, false, ast)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                Ok(format!(
                    include_str!("templates/cpp/internal_static_assert.h"),
                    return_param = return_param,
                    params = params,
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name),
                    method_name = to_cpp_name(m.name.as_str()),
                    method_name_snake = to_c_name(m.name.as_str())
                ))
            })
            .collect::<Result<Vec<String>, Error>>()
            .map(|fns| fns.join("\n"))
    }

    fn codegen_protocol(&self, ast: &BanjoAst) -> Result<String, Error> {
        ast.namespaces
            .iter()
            .filter(|n| *n.0 != "zx")
            .flat_map(|n| {
                n.1.iter().filter_map(filter_protocol).map(|(name, methods, _)| {
                    Ok(format!(
                        include_str!("templates/cpp/internal_protocol.h"),
                        protocol_name = to_cpp_name(name),
                        decls = self.codegen_decls(name, &methods, ast)?,
                        static_asserts = self.codegen_static_asserts(name, &methods, ast)?
                    ))
                })
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CppInternalBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        let namespace_include = &ast.primary_namespace.replace('.', "/").replace("ddk", "ddktl");
        self.w.write_fmt(format_args!(
            include_str!("templates/cpp/internal.h"),
            protocol_static_asserts = self.codegen_protocol(&ast)?,
            namespace = &ast.primary_namespace.replace('.', "-").as_str(),
            namespace_include = namespace_include,
            primary_namespace = to_c_name(&ast.primary_namespace).as_str()
        ))?;

        Ok(())
    }
}

pub struct CppBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> CppBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        CppBackend { w }
    }
}

impl<'a, W: io::Write> CppBackend<'a, W> {
    fn codegen_protocol_constructor_def(
        &self,
        name: &str,
        _attributes: &ast::Attrs,
        methods: &Vec<ast::Method>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        let assignments = methods
            .into_iter()
            .map(|m| {
                format!(
                    "        {c_name}_protocol_ops_.{c_name} = {protocol_name}{cpp_name};",
                    protocol_name = to_cpp_name(&name),
                    c_name = to_c_name(&m.name),
                    cpp_name = to_cpp_name(&m.name)
                )
            })
            .collect::<Vec<_>>()
            .join("\n");

        Ok(format!(
            include_str!("templates/cpp/base_protocol.h"),
            assignments = assignments,
            protocol_name_uppercase = to_c_name(name).to_uppercase(),
        )
        .trim_end()
        .to_string())
    }

    fn codegen_interface_constructor_def(
        &self,
        name: &str,
        _attributes: &ast::Attrs,
        methods: &Vec<ast::Method>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        let assignments = methods
            .into_iter()
            .map(|m| {
                format!(
                    "        {c_name}_ops_.{c_name} = {protocol_name}{cpp_name};",
                    protocol_name = to_cpp_name(&name),
                    c_name = to_c_name(&m.name),
                    cpp_name = to_cpp_name(&m.name)
                )
            })
            .collect::<Vec<_>>()
            .join("\n");

        Ok(assignments)
    }

    fn codegen_protocol_defs(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods.iter().map(|m| {
            let mut accum = String::new();
            accum.push_str(get_doc_comment(&m.attributes, 1).as_str());

            let (out_params, return_param) = get_out_params(&m, name, false, ast)?;
            let in_params = get_in_params(&m, false, false, ast)?;

            let params = iter::once("void* ctx".to_string()).chain(in_params)
                                                            .chain(out_params)
                                                            .collect::<Vec<_>>()
                                                            .join(", ");

            accum.push_str(format!("    static {return_param} {protocol_name}{function_name}({params}) {{\n",
                                   return_param = return_param,
                                   protocol_name = to_cpp_name(name),
                                   params = params,
                                   function_name = to_cpp_name(m.name.as_str())).as_str());

            let (out_args, skip) = get_out_args(&m, false, ast)?;
            let in_args = get_in_args(&m, true, ast)?;

            let handle_args = if m.attributes.has_attribute("Async") {
                vec![]
            } else {
                let skip_amt = if skip { 1 } else { 0 };
                m.out_params.iter().skip(skip_amt).filter_map(|(name, ty)| {
                    match ty {
                        ast::Ty::Handle {..} => Some((to_c_name(name), ty_to_cpp_str(ast, true, ty).unwrap())),
                        _ => None
                    }
                }).collect::<Vec<_>>()
            };
            for (name, ty) in handle_args.iter() {
                accum.push_str(format!("        {ty} out_{name}2;\n", ty = ty, name = name).as_str());
            }

            let args = in_args.into_iter()
                              .chain(out_args)
                              .collect::<Vec<_>>()
                              .join(", ");
            let initial = if skip { "auto ret = " } else { "" };
            accum.push_str(format!("        {initial}static_cast<D*>(ctx)->{protocol_name}{function_name}({args});\n",
                                   initial = initial,
                                   protocol_name = to_cpp_name(name),
                                   args = args,
                                   function_name = to_cpp_name(m.name.as_str())).as_str());
            for (name, _) in handle_args {
                accum.push_str(format!("        *out_{name} = out_{name}2.release();\n", name = name).as_str());
            }
            if skip {
                accum.push_str("        return ret;\n");
            }
            accum.push_str("    }");
            Ok(accum)
        }).collect::<Result<Vec<String>, Error>>()
          .map(|fns| fns.join("\n"))
    }

    fn codegen_client_defs(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let mut accum = String::new();
                accum.push_str(get_doc_comment(&m.attributes, 1).as_str());

                let (out_params, return_param) = get_out_params(&m, name, true, ast)?;
                let in_params = get_in_params(&m, true, true, ast)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                accum.push_str(
                    format!(
                        "    {return_param} {function_name}({params}) {{\n",
                        return_param = return_param,
                        params = params,
                        function_name = to_cpp_name(m.name.as_str())
                    )
                    .as_str(),
                );

                let (out_args, skip) = get_out_args(&m, true, ast)?;
                let in_args = get_in_args(&m, false, ast)?;

                let proto_args = m.in_params.iter().filter_map(|(name, ty)| {
                    if let ast::Ty::Ident { id, .. } =  ty {
                        if ast.id_to_type(id) == ast::Ty::Interface && not_callback(ast, id) {
                            return Some((to_c_name(name), ty_to_cpp_str(ast, true, ty).unwrap()));
                        }
                    }
                    None
                }).collect::<Vec<_>>();
                for (name, ty) in proto_args.iter() {
                    accum.push_str(format!(include_str!("templates/cpp/proto_transform.h"),
                                    ty = ty, name = name).as_str());
                }

                let args = iter::once("ctx_".to_string())
                    .chain(in_args)
                    .chain(out_args)
                    .collect::<Vec<_>>()
                    .join(", ");

                let return_statement = if skip { "return " } else { "" };
                accum.push_str(
                    format!(
                        "        {return_statement}ops_->{function_name_snake}({args});\n",
                        return_statement = return_statement,
                        args = args,
                        function_name_snake = to_c_name(m.name.as_str())
                    )
                    .as_str(),
                );
                accum.push_str("    }\n");
                Ok(accum)
            })
            .collect::<Result<Vec<String>, Error>>()
            .map(|fns| fns.join("\n"))
    }

    fn codegen_interfaces(
        &self,
        namespaces: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespaces
            .iter()
            .filter_map(filter_interface)
            .map(|(name, methods, attributes)| {
                Ok(format!(
                    include_str!("templates/cpp/interface.h"),
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name).as_str(),
                    protocol_docs = get_doc_comment(attributes, 0),
                    constructor_definition =
                        self.codegen_interface_constructor_def(name, attributes, methods, ast)?,
                    protocol_definitions = self.codegen_protocol_defs(name, methods, ast)?,
                    client_definitions = self.codegen_client_defs(name, methods, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_protocols(
        &self,
        namespaces: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespaces
            .iter()
            .filter_map(filter_protocol)
            .map(|(name, methods, attributes)| {
                Ok(format!(
                    include_str!("templates/cpp/protocol.h"),
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name).as_str(),
                    protocol_docs = get_doc_comment(attributes, 0),
                    constructor_definition =
                        self.codegen_protocol_constructor_def(name, attributes, methods, ast)?,
                    protocol_definitions = self.codegen_protocol_defs(name, methods, ast)?,
                    client_definitions = self.codegen_client_defs(name, methods, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_includes(&self, ast: &BanjoAst) -> Result<String, Error> {
        let mut includes = vec![
            "ddktl/device-internal".to_string(),
            "zircon/assert".to_string(),
            "zircon/compiler".to_string(),
            "zircon/types".to_string(),
        ]
        .into_iter()
        .chain(ast.namespaces.iter().filter(|n| n.0 != "zx").map(|n| n.0.replace('.', "/")))
        .map(|n| format!("#include <{}.h>", n))
        .chain(
            // Include handle headers for zx_handle_t wrapper types used in interfaces.
            ast.namespaces
                .iter()
                .filter(|n| n.0 != "zx")
                .flat_map(|n| {
                    n.1.iter()
                        .filter_map(|decl| {
                            // Find all interfaces and extract their methods.
                            if let ast::Decl::Interface { ref methods, .. } = *decl {
                                Some(methods)
                            } else {
                                None
                            }
                        })
                        .flat_map(|methods| {
                            // Find all handle in/out params in each method.
                            methods.iter().flat_map(|method| {
                                method
                                    .in_params
                                    .iter()
                                    .filter_map(|(_, ty)| match ty {
                                        ast::Ty::Handle { .. } => Some(ty),
                                        _ => None,
                                    })
                                    .chain(method.out_params.iter().filter_map(
                                        |(_, ty)| match ty {
                                            ast::Ty::Handle { .. } => Some(ty),
                                            _ => None,
                                        },
                                    ))
                                    .map(|ty| ty_to_cpp_str(ast, true, ty).unwrap())
                            })
                        })
                    // Place into set to remove duplicates.
                })
                .collect::<HashSet<String>>()
                .into_iter()
                .map(|ty| format!("#include <lib/zx/{}.h>", &ty[4..])),
        )
        .collect::<Vec<_>>();
        includes.sort();
        Ok(includes.join("\n"))
    }

    fn codegen_examples(
        &self,
        namespaces: &Vec<ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        namespaces
            .iter()
            .filter_map(filter_protocol)
            .map(|(name, methods, _)| {
                let example_decls = methods
                    .iter()
                    .map(|m| {
                        let (out_params, return_param) = get_out_params(&m, name, true, ast)?;
                        let in_params = get_in_params(&m, true, false, ast)?;

                        let params =
                            in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                        Ok(format!(
                            "//     {return_param} {protocol_name}{function_name}({params});",
                            return_param = return_param,
                            protocol_name = to_cpp_name(name),
                            params = params,
                            function_name = to_cpp_name(m.name.as_str())
                        ))
                    })
                    .collect::<Result<Vec<_>, Error>>()?
                    .join("\n//\n");
                Ok(format!(
                    include_str!("templates/cpp/example.h"),
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name),
                    protocol_name_lisp = to_c_name(name).replace('_', "-"),
                    protocol_name_uppercase = to_c_name(name).to_uppercase(),
                    example_decls = example_decls
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CppBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        let namespace = &ast.namespaces[&ast.primary_namespace];
        self.w.write_fmt(format_args!(
            include_str!("templates/cpp/header.h"),
            includes = self.codegen_includes(&ast)?,
            examples = self.codegen_examples(namespace, &ast)?,
            namespace = &ast.primary_namespace.replace('.', "-").as_str(),
            primary_namespace = to_c_name(&ast.primary_namespace).as_str()
        ))?;

        self.w.write_fmt(format_args!("{}", self.codegen_interfaces(namespace, &ast)?))?;
        self.w.write_fmt(format_args!("{}", self.codegen_protocols(namespace, &ast)?))?;
        self.w.write_fmt(format_args!(include_str!("templates/cpp/footer.h")))?;

        Ok(())
    }
}
