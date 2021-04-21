// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst, Ident},
    crate::backends::c::{array_bounds, get_doc_comment, name_buffer, name_size, not_callback},
    crate::backends::util::{is_banjo_namespace, to_c_name},
    crate::backends::Backend,
    anyhow::{format_err, Error},
    std::collections::HashSet,
    std::io,
    std::iter,
};

#[derive(Debug)]
pub enum CppSubtype {
    Base,
    Internal,
    Mock,
}

pub struct CppBackend<'a, W: io::Write> {
    w: &'a mut W,
    subtype: CppSubtype,
}

impl<'a, W: io::Write> CppBackend<'a, W> {
    pub fn new(w: &'a mut W, subtype: CppSubtype) -> Self {
        CppBackend { w, subtype }
    }
}

fn to_cpp_name(name: &str) -> &str {
    // strip FQN
    name.split(".").last().unwrap()
}

fn handle_ty_to_cpp_str(_ast: &ast::BanjoAst, ty: &ast::HandleTy) -> Result<String, Error> {
    match ty {
        ast::HandleTy::None => Ok(String::from("zx::handle")),
        ast::HandleTy::Process => Ok(String::from("zx::process")),
        ast::HandleTy::Thread => Ok(String::from("zx::thread")),
        ast::HandleTy::Vmo => Ok(String::from("zx::vmo")),
        ast::HandleTy::Channel => Ok(String::from("zx::channel")),
        ast::HandleTy::Event => Ok(String::from("zx::event")),
        ast::HandleTy::Port => Ok(String::from("zx::port")),
        ast::HandleTy::Interrupt => Ok(String::from("zx::interrupt")),
        ast::HandleTy::PciDevice => Ok(String::from("zx::handle")),
        ast::HandleTy::Log => Ok(String::from("zx::log")),
        ast::HandleTy::Socket => Ok(String::from("zx::socket")),
        ast::HandleTy::Resource => Ok(String::from("zx::resource")),
        ast::HandleTy::EventPair => Ok(String::from("zx::eventpair")),
        ast::HandleTy::Job => Ok(String::from("zx::job")),
        ast::HandleTy::Vmar => Ok(String::from("zx::vmar")),
        ast::HandleTy::Fifo => Ok(String::from("zx::fifo")),
        ast::HandleTy::Guest => Ok(String::from("zx::guest")),
        ast::HandleTy::VCpu => Ok(String::from("zx::vcpu")),
        ast::HandleTy::Timer => Ok(String::from("zx::timer")),
        ast::HandleTy::IoMmu => Ok(String::from("zx::iommu")),
        ast::HandleTy::Bti => Ok(String::from("zx::bti")),
        ast::HandleTy::Profile => Ok(String::from("zx::profile")),
        ast::HandleTy::Pmt => Ok(String::from("zx::pmt")),
        ast::HandleTy::SuspendToken => Ok(String::from("zx::handle")),
        ast::HandleTy::Pager => Ok(String::from("zx::pager")),
        ast::HandleTy::Exception => Ok(String::from("zx::handle")),
        ast::HandleTy::Clock => Ok(String::from("zx::clock")),
        ast::HandleTy::Stream => Ok(String::from("zx::handle")),
        // This mapping supports the MSI --> MSI_ALLOCATION transition.
        ast::HandleTy::MsiAllocation => Ok(String::from("zx::msi")),
        ast::HandleTy::MsiInterrupt => Ok(String::from("zx::handle")),
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
        ast::Ty::Float32 => Ok(String::from("float")),
        ast::Ty::Float64 => Ok(String::from("double")),
        ast::Ty::Str { .. } => Ok(String::from("char*")),
        ast::Ty::Array { ref ty, .. } => ty_to_cpp_str(ast, wrappers, ty),
        ast::Ty::Vector { ref ty, .. } => ty_to_cpp_str(ast, wrappers, ty),
        ast::Ty::Identifier { id, .. } => {
            if id.is_base_type() {
                Ok(format!("zx_{}_t", id.name()))
            } else {
                match ast.id_to_type(id) {
                    ast::Ty::Struct | ast::Ty::Union | ast::Ty::Enum => {
                        return Ok(to_c_name(&id.name()) + "_t");
                    }
                    ast::Ty::Protocol => {
                        if not_callback(ast, id) {
                            return Ok(format!("{}_protocol_t", to_c_name(id.name())));
                        } else {
                            return Ok(format!("{}_t", to_c_name(id.name())));
                        }
                    }
                    t => return ty_to_cpp_str(ast, wrappers, &t),
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

fn protocol_to_ops_cpp_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    if let ast::Ty::Identifier { id, .. } = ty {
        if ast.id_to_type(id) == ast::Ty::Protocol {
            return Ok(to_c_name(&id.name()) + "_protocol_ops_t");
        }
    }
    Err(format_err!("unknown ident type in protocol_to_ops_cpp_str {:?}", ty))
}

fn get_first_param(ast: &BanjoAst, method: &ast::Method) -> Result<(bool, String), Error> {
    // Return parameter if a primitive type.
    if method.out_params.get(0).map_or(false, |p| p.1.is_primitive(&ast)) {
        Ok((true, ty_to_cpp_str(ast, false, &method.out_params[0].1)?))
    } else {
        Ok((false, "void".to_string()))
    }
}

fn get_in_params(
    m: &ast::Method,
    wrappers: bool,
    transform: bool,
    ast: &BanjoAst,
) -> Result<Vec<String>, Error> {
    m.in_params
        .iter()
        .map(|(name, ty, attrs)| {
            match ty {
                ast::Ty::Identifier { id, .. } => {
                    if id.is_base_type() {
                        let ty_name = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                        return Ok(format!("{} {}", ty_name, to_c_name(name)));
                    }
                    match ast.id_to_type(id) {
                        ast::Ty::Protocol => {
                            let ty_name = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                            if transform && not_callback(ast, id) {
                                let ty_name = protocol_to_ops_cpp_str(ast, ty).unwrap();
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
                            let ty_name = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                            let prefix = if attrs.has_attribute("InOut") { "" } else { "const " };
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
                ast::Ty::Str { .. } => Ok(format!(
                    "const {} {}",
                    ty_to_cpp_str(ast, false, ty).unwrap(),
                    to_c_name(name)
                )),
                ast::Ty::Array { .. } => {
                    let bounds = array_bounds(ast, ty).unwrap();
                    let ty = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                    Ok(format!(
                        "const {ty} {name}{bounds}",
                        bounds = bounds,
                        ty = ty,
                        name = to_c_name(name)
                    ))
                }
                ast::Ty::Vector { .. } => {
                    // TODO(surajmalhotra): Support zx wrappers for vectors.
                    let ty = ty_to_cpp_str(ast, false, ty).unwrap();
                    // TODO(surajmalhotra): Support multi-dimensional vectors.
                    let ptr = if attrs.has_attribute("InnerPointer") { "*" } else { "" };
                    Ok(format!(
                        "const {ty}{ptr}* {name}_{buffer}, size_t {name}_{size}",
                        buffer = name_buffer(&ty, &attrs),
                        size = name_size(&ty, &attrs),
                        ptr = ptr,
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

    Ok((m.out_params.iter().skip(skip_amt).map(|(name, ty, attrs)| {
        let nullable = if ty.is_reference() { "*" } else { "" };
        let ty_name = ty_to_cpp_str(ast, wrappers, ty).unwrap();
        match ty {
            ast::Ty::Protocol => format!("const {}* {}", ty_name, to_c_name(name)),
            ast::Ty::Str {..} => {
                format!("{} out_{name}, size_t {name}_capacity", ty_name,
                        name=to_c_name(name))
            }
            ast::Ty::Array { .. } => {
                let bounds = array_bounds(ast, ty).unwrap();
                let ty = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                format!(
                    "{ty} out_{name}{bounds}",
                    bounds = bounds,
                    ty = ty,
                    name = to_c_name(name)
                )
            }
            ast::Ty::Vector { .. } => {
                // TODO(surajmalhotra): Support zx wrappers for vectors.
                let ty_name = ty_to_cpp_str(ast, false, ty).unwrap();
                // TODO(surajmalhotra): Support multi-dimensional vectors.
                if attrs.has_attribute("CalleeAllocated") {
                    format!("{ty}** out_{name}_{buffer}, size_t* {name}_{size}",
                            buffer = name_buffer(&ty_name, &attrs),
                            size = name_size(&ty_name, &attrs),
                            ty = ty_name,
                            name = to_c_name(name))
                } else {
                    format!("{ty}* out_{name}_{buffer}, size_t {name}_{size}, size_t* out_{name}_actual",
                            buffer = name_buffer(&ty_name, &attrs),
                            size = name_size(&ty_name, &attrs),
                            ty = ty_name,
                            name = to_c_name(name))
                }
            },
            ast::Ty::Handle {..} => format!("{}* out_{}", ty_name, to_c_name(name)),
            _ => format!("{}{}* out_{}", ty_name, nullable, to_c_name(name))
        }
    }).collect(), return_param))
}

fn get_in_args(m: &ast::Method, wrappers: bool, ast: &BanjoAst) -> Result<Vec<String>, Error> {
    Ok(m.in_params
        .iter()
        .map(|(name, ty, attrs)| match ty {
            ast::Ty::Vector { .. } => {
                let ty = ty_to_cpp_str(ast, wrappers, ty).unwrap();
                format!(
                    "{name}_{buffer}, {name}_{size}",
                    buffer = name_buffer(&ty, &attrs),
                    size = name_size(&ty, &attrs),
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
            .map(|(name, ty, attrs)| match ty {
                ast::Ty::Protocol { .. } => format!("{}", to_c_name(name)),
                ast::Ty::Str { .. } => {
                    format!("out_{name}, {name}_capacity", name = to_c_name(name))
                }
                ast::Ty::Vector { .. } => {
                    let ty_name = ty_to_cpp_str(ast, false, ty).unwrap();
                    if attrs.has_attribute("CalleeAllocated") {
                        format!(
                            "out_{name}_{buffer}, {name}_{size}",
                            buffer = name_buffer(&ty_name, &attrs),
                            size = name_size(&ty_name, &attrs),
                            name = to_c_name(name)
                        )
                    } else {
                        format!(
                            "out_{name}_{buffer}, {name}_{size}, out_{name}_actual",
                            buffer = name_buffer(&ty_name, &attrs),
                            size = name_size(&ty_name, &attrs),
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

fn get_mock_out_param_types(m: &ast::Method, ast: &BanjoAst) -> Result<String, Error> {
    if m.out_params.is_empty() {
        Ok("void".to_string())
    } else {
        Ok(format!(
            "std::tuple<{}>",
            m.out_params
                .iter()
                .map(|(_name, ty, _)| match ty {
                    ast::Ty::Str { .. } => "std::string".to_string(),
                    ast::Ty::Vector { ref ty, .. } => {
                        format!("std::vector<{}>", ty_to_cpp_str(ast, false, ty).unwrap())
                    }
                    _ => ty_to_cpp_str(ast, true, ty).unwrap(),
                })
                .collect::<Vec<_>>()
                .join(", "),
        ))
    }
}

fn get_mock_param_types(m: &ast::Method, ast: &BanjoAst) -> Result<String, Error> {
    Ok(iter::once(get_mock_out_param_types(m, ast)?)
        .chain(m.in_params.iter().map(|(_name, ty, _)| match ty {
            ast::Ty::Str { .. } => "std::string".to_string(),
            ast::Ty::Vector { ref ty, .. } => {
                format!("std::vector<{}>", ty_to_cpp_str(ast, false, ty).unwrap())
            }
            _ => ty_to_cpp_str(ast, true, ty).unwrap(),
        }))
        .collect::<Vec<_>>()
        .join(", "))
}

fn get_mock_params(m: &ast::Method, ast: &BanjoAst) -> Result<String, Error> {
    // If async, put all output parameters last and add callback and cookie. Otherwise put first
    // output param first, then all input params, then the rest of the output params.
    let (skip, return_param) = get_first_param(ast, m)?;
    let has_return_value = skip && !m.attributes.has_attribute("Async");

    let mut params = Vec::new();
    if has_return_value {
        params.push(format!("{} out_{}", return_param, m.out_params[0].0));
    }

    Ok(params
        .into_iter()
        .chain(m.in_params.iter().map(|(name, ty, _)| match ty {
            ast::Ty::Handle { .. } => {
                format!("const {}& {}", ty_to_cpp_str(ast, true, ty).unwrap(), to_c_name(name))
            }
            ast::Ty::Str { .. } => format!("std::string {}", to_c_name(name)),
            ast::Ty::Vector { ref ty, .. } => format!(
                "std::vector<{ty}> {name}",
                ty = ty_to_cpp_str(ast, false, ty).unwrap(),
                name = to_c_name(name),
            ),
            _ => format!("{} {}", ty_to_cpp_str(ast, true, ty).unwrap(), to_c_name(name)),
        }))
        .chain(m.out_params.iter().skip(if has_return_value { 1 } else { 0 }).map(
            |(name, ty, _)| match ty {
                ast::Ty::Str { .. } => format!("std::string {}", to_c_name(name)),
                ast::Ty::Vector { ref ty, .. } => format!(
                    "std::vector<{ty}> out_{name}",
                    ty = ty_to_cpp_str(ast, false, ty).unwrap(),
                    name = to_c_name(name),
                ),
                _ => format!("{} out_{}", ty_to_cpp_str(ast, true, ty).unwrap(), to_c_name(name)),
            },
        ))
        .collect::<Vec<_>>()
        .join(", "))
}

fn get_mock_expect_args(m: &ast::Method) -> Result<String, Error> {
    let mut args = Vec::new();
    if !m.out_params.is_empty() {
        args.push(format!(
            "{{{}}}",
            m.out_params
                .iter()
                .map(|(name, ty, _)| match ty {
                    ast::Ty::Handle { .. } => format!("std::move(out_{})", to_c_name(name)),
                    ast::Ty::Str { .. } => format!("std::move(out_{})", to_c_name(name)),
                    ast::Ty::Vector { .. } => format!("std::move(out_{})", to_c_name(name)),
                    _ => format!("out_{}", to_c_name(name)),
                })
                .collect::<Vec<_>>()
                .join(", "),
        ));
    }

    Ok(args
        .into_iter()
        .chain(m.in_params.iter().map(|(name, ty, _)| match ty {
            ast::Ty::Handle { .. } => format!("{}.get()", to_c_name(name)),
            ast::Ty::Str { .. } => format!("std::move({})", to_c_name(name)),
            ast::Ty::Vector { .. } => format!("std::move({})", to_c_name(name)),
            _ => to_c_name(name).to_string(),
        }))
        .collect::<Vec<_>>()
        .join(", "))
}

/// Checks whether a decl is a protocol, and if it is a protocol, checks that it is a "ddk-interface".
fn filter_interface<'a>(
    decl: &&'a ast::Decl,
) -> Option<(&'a Ident, &'a Vec<ast::Method>, &'a ast::Attrs)> {
    if let ast::Decl::Protocol { ref name, ref methods, ref attributes, .. } = *decl {
        if let Some(layout) = attributes.get_attribute("BanjoLayout") {
            if layout == "ddk-interface" {
                return Some((name, methods, attributes));
            }
        }
    }
    None
}

/// Checks whether a decl is a protocol, and if it is a protocol, checks that it is a "ddk-protocol".
fn filter_protocol<'a>(
    decl: &&'a ast::Decl,
) -> Option<(&'a Ident, &'a Vec<ast::Method>, &'a ast::Attrs)> {
    if let ast::Decl::Protocol { ref name, ref methods, ref attributes, .. } = *decl {
        if let Some(layout) = attributes.get_attribute("BanjoLayout") {
            if layout == "ddk-protocol" {
                return Some((name, methods, attributes));
            }
        } else {
            return Some((name, methods, attributes));
        }
    }
    None
}

impl<'a, W: io::Write> CppBackend<'a, W> {
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

    fn codegen_protocol(
        &self,
        declarations: &Vec<&ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol)
            .map(|(name, methods, _)| {
                Ok(format!(
                    include_str!("templates/cpp/internal_protocol.h"),
                    protocol_name = to_cpp_name(name.name()),
                    decls = self.codegen_decls(name.name(), &methods, ast)?,
                    static_asserts = self.codegen_static_asserts(name.name(), &methods, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_interface(
        &self,
        declarations: &Vec<&ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_interface)
            .map(|(name, methods, _)| {
                Ok(format!(
                    include_str!("templates/cpp/internal_protocol.h"),
                    protocol_name = to_cpp_name(name.name()),
                    decls = self.codegen_decls(name.name(), &methods, ast)?,
                    static_asserts = self.codegen_static_asserts(name.name(), &methods, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

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
                    "        {protocol_name_c}_protocol_ops_.{c_name} = {protocol_name}{cpp_name};",
                    protocol_name_c = to_c_name(&name),
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
            protocol_name = to_c_name(name),
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
                    "        {protocol_name_c}_protocol_ops_.{c_name} = {protocol_name}{cpp_name};",
                    protocol_name_c = to_c_name(&name),
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
                m.out_params.iter().skip(skip_amt).filter_map(|(name, ty, _)| {
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
                        "    {return_param} {function_name}({params}) const {{\n",
                        return_param = return_param,
                        params = params,
                        function_name = to_cpp_name(m.name.as_str())
                    )
                    .as_str(),
                );

                let (out_args, skip) = get_out_args(&m, true, ast)?;
                let in_args = get_in_args(&m, false, ast)?;

                let proto_args = m
                    .in_params
                    .iter()
                    .filter_map(|(name, ty, _)| {
                        if let ast::Ty::Identifier { id, .. } = ty {
                            if ast.id_to_type(id) == ast::Ty::Protocol && not_callback(ast, id) {
                                return Some((
                                    to_c_name(name),
                                    ty_to_cpp_str(ast, true, ty).unwrap(),
                                ));
                            }
                        }
                        None
                    })
                    .collect::<Vec<_>>();
                for (name, ty) in proto_args.iter() {
                    accum.push_str(
                        format!(
                            include_str!("templates/cpp/proto_transform.h"),
                            ty = ty,
                            name = name
                        )
                        .as_str(),
                    );
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
        declarations: &Vec<&ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_interface)
            .map(|(name, methods, attributes)| {
                Ok(format!(
                    include_str!("templates/cpp/interface.h"),
                    protocol_name = to_cpp_name(name.name()),
                    protocol_name_snake = to_c_name(name.name()).as_str(),
                    protocol_docs = get_doc_comment(attributes, 0),
                    constructor_definition = self.codegen_interface_constructor_def(
                        name.name(),
                        attributes,
                        methods,
                        ast
                    )?,
                    protocol_definitions = self.codegen_protocol_defs(name.name(), methods, ast)?,
                    client_definitions = self.codegen_client_defs(name.name(), methods, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_protocols(
        &self,
        declarations: &Vec<&ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol)
            .map(|(name, methods, attributes)| {
                Ok(format!(
                    include_str!("templates/cpp/protocol.h"),
                    protocol_name = to_cpp_name(name.name()),
                    protocol_name_uppercase = to_c_name(name.name()).to_uppercase(),
                    protocol_name_snake = to_c_name(name.name()).as_str(),
                    protocol_docs = get_doc_comment(attributes, 0),
                    constructor_definition = self.codegen_protocol_constructor_def(
                        name.name(),
                        attributes,
                        methods,
                        ast
                    )?,
                    protocol_definitions = self.codegen_protocol_defs(name.name(), methods, ast)?,
                    client_definitions = self.codegen_client_defs(name.name(), methods, ast)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_includes(&self, ast: &BanjoAst) -> Result<String, Error> {
        let mut includes = vec![
            "lib/ddk/device".to_string(),
            "lib/ddk/driver".to_string(),
            "ddktl/device-internal".to_string(),
            "zircon/assert".to_string(),
            "zircon/compiler".to_string(),
            "zircon/types".to_string(),
        ]
        .into_iter()
        .chain(ast.namespaces.iter().filter(|n| n.0 != "zx").map(|n| {
            if is_banjo_namespace(n.0) {
                n.0.replace('.', "/") + "/c/banjo"
            } else {
                n.0.replace('.', "/")
            }
        }))
        .map(|n| format!("#include <{}.h>", n))
        .chain(
            // Include handle headers for zx_handle_t wrapper types used in protocols.
            ast.namespaces[&ast.primary_namespace]
                .iter()
                .filter_map(|decl| {
                    // Find all protocols and extract their methods.
                    if let ast::Decl::Protocol { ref methods, .. } = *decl {
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
                            .filter_map(|(_, ty, _)| match ty {
                                ast::Ty::Handle { ty, .. } => Some(ty),
                                _ => None,
                            })
                            .chain(method.out_params.iter().filter_map(|(_, ty, _)| match ty {
                                ast::Ty::Handle { ty, .. } => Some(ty),
                                _ => None,
                            }))
                            .map(|ty| handle_ty_to_cpp_str(ast, ty).unwrap())
                    })
                })
                // Place into set to remove duplicates.
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
        declarations: &Vec<&ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol)
            .map(|(name, methods, _)| {
                let example_decls = methods
                    .iter()
                    .map(|m| {
                        let (out_params, return_param) =
                            get_out_params(&m, name.name(), true, ast)?;
                        let in_params = get_in_params(&m, true, false, ast)?;

                        let params =
                            in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                        Ok(format!(
                            "//     {return_param} {protocol_name}{function_name}({params});",
                            return_param = return_param,
                            protocol_name = to_cpp_name(name.name()),
                            params = params,
                            function_name = to_cpp_name(m.name.as_str())
                        ))
                    })
                    .collect::<Result<Vec<_>, Error>>()?
                    .join("\n//\n");
                Ok(format!(
                    include_str!("templates/cpp/example.h"),
                    protocol_name = to_cpp_name(name.name()),
                    protocol_name_snake = to_c_name(name.name()),
                    protocol_name_lisp = to_c_name(name.name()).replace('_', "-"),
                    protocol_name_uppercase = to_c_name(name.name()).to_uppercase(),
                    example_decls = example_decls
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_mock_accessors(
        &self,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let text = methods
            .iter()
            .map(|m| {
                Ok(format!(
                    "    mock_function::MockFunction<{param_types}>& mock_{name}() \
                     {{ return mock_{name}_; }}",
                    param_types = get_mock_param_types(&m, ast)?,
                    name = to_c_name(&m.name).as_str(),
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))?;
        Ok(if text.len() > 0 { "\n".to_string() + &text } else { "".to_string() })
    }

    fn codegen_mock_definitions(
        &self,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                Ok(format!(
                    "    mock_function::MockFunction<{param_types}> mock_{name}_;",
                    param_types = get_mock_param_types(&m, ast)?,
                    name = to_c_name(&m.name).as_str(),
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_mock_expects(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let text = methods
            .iter()
            .map(|m| {
                Ok(format!(
                    include_str!("templates/cpp/mock_expect.h"),
                    protocol_name = to_cpp_name(name),
                    method_name = m.name,
                    params = get_mock_params(&m, ast)?,
                    method_name_snake = to_c_name(&m.name).as_str(),
                    args = get_mock_expect_args(&m)?,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))?;
        Ok(if text.len() > 0 { "\n".to_string() + &text } else { "".to_string() })
    }

    fn codegen_mock_verify(&self, methods: &Vec<ast::Method>) -> Result<String, Error> {
        Ok(methods
            .iter()
            .map(|m| format!("        mock_{}_.VerifyAndClear();", to_c_name(m.name.as_str())))
            .collect::<Vec<_>>()
            .join("\n"))
    }

    fn codegen_mock_protocol_out_args(
        &self,
        m: &ast::Method,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let (skip, _) = get_first_param(ast, m)?;
        let skip_amt = if skip { 1 } else { 0 };

        let mut accum = String::new();

        for i in skip_amt..m.out_params.len() {
            match m.out_params[i].1 {
                ast::Ty::Handle { .. } => {
                    accum.push_str(
                        format!(
                            "        *out_{name} = std::move(std::get<{index}>(ret));\n",
                            name = to_c_name(&m.out_params[i].0),
                            index = i,
                        )
                        .as_str(),
                    );
                }
                ast::Ty::Str { .. } => {
                    accum.push_str(
                        format!(
                            "        strncpy(out_{name}, std::get<{index}>(ret).c_str(), \
                             {name}_capacity));\n",
                            name = to_c_name(&m.out_params[i].0),
                            index = i,
                        )
                        .as_str(),
                    );
                }
                ast::Ty::Vector { ref ty, .. } => {
                    let ty_name = ty_to_cpp_str(ast, false, ty).unwrap();
                    accum.push_str(
                        format!(
                            "        *out_{name}_actual = std::min<size_t>(\
                             std::get<{index}>(ret).size(), {name}_{size});\n",
                            name = to_c_name(&m.out_params[i].0),
                            size = name_size(&ty_name, &m.out_params[i].2),
                            index = i,
                        )
                        .as_str(),
                    );
                    accum.push_str(
                        format!(
                            "        std::move(std::get<{index}>(ret).begin(), \
                             std::get<{index}>(ret).begin() + *out_{name}_actual, \
                             out_{name}_{buffer});\n",
                            name = to_c_name(&m.out_params[i].0),
                            buffer = name_buffer(&ty_name, &m.out_params[i].2),
                            index = i,
                        )
                        .as_str(),
                    );
                }
                _ => {
                    accum.push_str(
                        format!(
                            "        *out_{name} = std::get<{index}>(ret);\n",
                            name = to_c_name(&m.out_params[i].0),
                            index = i,
                        )
                        .as_str(),
                    );
                }
            }
        }

        if skip {
            accum.push_str("        return std::get<0>(ret);\n");
        }

        Ok(accum)
    }

    fn codegen_mock_protocol_async_out_args(
        &self,
        m: &ast::Method,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let mut out_args = Vec::new();
        out_args.push("cookie".to_string());

        for i in 0..m.out_params.len() {
            match &m.out_params[i].1 {
                ast::Ty::Handle { .. } => out_args.push(format!("std::move(std::get<{}>(ret))", i)),
                ast::Ty::Str { .. } => out_args.push(format!("std::get<{}>(ret).c_str()", i)),
                ast::Ty::Vector { .. } => {
                    out_args.push(format!("std::get<{}>(ret).data()", i));
                    out_args.push(format!("std::get<{}>(ret).size()", i));
                }
                ast::Ty::Identifier { id, .. } => {
                    if id.is_base_type() {
                        out_args.push(format!("std::get<{}>(ret)", i))
                    } else {
                        match ast.id_to_type(&id) {
                            ast::Ty::Struct | ast::Ty::Union => {
                                out_args.push(format!("&std::get<{}>(ret)", i))
                            }
                            _ => out_args.push(format!("std::get<{}>(ret)", i)),
                        }
                    }
                }
                _ => out_args.push(format!("std::get<{}>(ret)", i)),
            }
        }

        Ok(format!("        callback({});\n", out_args.join(", ")))
    }

    fn codegen_mock_protocol_defs(
        &self,
        name: &str,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let text = methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(&m, name, true, ast)?;
                let in_params = get_in_params(&m, true, true, ast)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                let mut accum = String::new();

                accum.push_str(
                    format!(
                        "    virtual {return_param} {protocol_name}{function_name}({params}) {{\n",
                        return_param = return_param,
                        protocol_name = to_cpp_name(name),
                        params = params,
                        function_name = to_cpp_name(m.name.as_str()),
                    )
                    .as_str(),
                );

                let in_args = m
                    .in_params
                    .iter()
                    .map(|(name, ty, attrs)| match ty {
                        ast::Ty::Handle { .. } => format!("std::move({})", to_c_name(name)),
                        ast::Ty::Str { .. } => format!("std::string({})", to_c_name(name)),
                        ast::Ty::Vector { ref ty, .. } => {
                            let ty_name = ty_to_cpp_str(ast, false, ty).unwrap();
                            format!(
                                "std::vector<{ty}>({name}_{buffer}, \
                                 {name}_{buffer} + {name}_{size})",
                                ty = ty_name,
                                name = to_c_name(name),
                                buffer = name_buffer(&ty_name, &attrs),
                                size = name_size(&ty_name, &attrs),
                            )
                        }
                        ast::Ty::Identifier { id, .. } => {
                            if id.is_base_type() {
                                to_c_name(name)
                            } else if not_callback(ast, id) {
                                match ast.id_to_type(id) {
                                    ast::Ty::Struct | ast::Ty::Union => {
                                        format!("*{}", to_c_name(name))
                                    }
                                    _ => to_c_name(name),
                                }
                            } else {
                                format!("*{}", to_c_name(name))
                            }
                        }
                        _ => to_c_name(name),
                    })
                    .collect::<Vec<_>>()
                    .join(", ");

                accum.push_str("        ");

                if !m.out_params.is_empty() {
                    accum
                        .push_str(format!("{} ret = ", get_mock_out_param_types(m, ast)?).as_str());
                }

                accum.push_str(
                    format!(
                        "mock_{name}_.Call({args});\n",
                        name = to_c_name(m.name.as_str()),
                        args = in_args.as_str(),
                    )
                    .as_str(),
                );

                if m.attributes.has_attribute("Async") {
                    accum.push_str(self.codegen_mock_protocol_async_out_args(&m, ast)?.as_str());
                } else {
                    accum.push_str(self.codegen_mock_protocol_out_args(&m, ast)?.as_str());
                }

                accum.push_str("    }");
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n\n"))?;
        Ok(if text.len() > 0 { "\n".to_string() + &text } else { "".to_string() })
    }

    fn codegen_mock(
        &self,
        declarations: &Vec<&ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol)
            .map(|(name, methods, _attributes)| {
                Ok(format!(
                    include_str!("templates/cpp/mock.h"),
                    protocol_name = to_cpp_name(name.name()),
                    protocol_name_snake = to_c_name(name.name()).as_str(),
                    mock_expects = self.codegen_mock_expects(name.name(), &methods, ast)?,
                    mock_verify = self.codegen_mock_verify(&methods)?,
                    protocol_definitions =
                        self.codegen_mock_protocol_defs(name.name(), &methods, ast)?,
                    mock_accessors = self.codegen_mock_accessors(&methods, ast)?,
                    mock_definitions = self.codegen_mock_definitions(&methods, ast)?,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_mock_includes(
        &self,
        declarations: &Vec<&ast::Decl>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let mut need_c_string_header = false;
        let mut need_cpp_string_header = false;
        let mut need_cpp_tuple_header = false;
        let mut need_cpp_vector_header = false;

        declarations.iter().filter_map(filter_protocol).for_each(
            |(_name, methods, _attributes)| {
                methods.iter().for_each(|m| {
                    m.in_params.iter().for_each(|(_name, ty, _)| match ty {
                        ast::Ty::Str { .. } => need_cpp_string_header = true,
                        ast::Ty::Vector { .. } => need_cpp_vector_header = true,
                        _ => {}
                    });

                    if !m.out_params.is_empty() {
                        need_cpp_tuple_header = true;
                    }

                    m.out_params.iter().for_each(|(_name, ty, _)| match ty {
                        ast::Ty::Str { .. } => {
                            need_cpp_string_header = true;
                            if !m.attributes.has_attribute("Async") {
                                need_c_string_header = true;
                            }
                        }
                        ast::Ty::Vector { .. } => need_cpp_vector_header = true,
                        _ => {}
                    });
                });
            },
        );

        let mut accum = String::new();
        if need_c_string_header {
            accum.push_str("#include <string.h>\n\n");
        }
        if need_cpp_string_header {
            accum.push_str("#include <string>\n");
        }
        if need_cpp_tuple_header {
            accum.push_str("#include <tuple>\n");
        }
        if need_cpp_vector_header {
            accum.push_str("#include <vector>\n");
        }
        if need_cpp_string_header || need_cpp_tuple_header || need_cpp_vector_header {
            accum.push_str("\n");
        }

        let mut includes = vec!["lib/mock-function/mock-function".to_string()]
            .into_iter()
            .chain(ast.namespaces.iter().filter(|n| n.0 != "zx").map(|n| {
                if is_banjo_namespace(n.0) {
                    n.0.replace('.', "/") + "/cpp/banjo"
                } else {
                    n.0.replace('.', "/")
                }
            }))
            .map(|n| format!("#include <{}.h>", n))
            .collect::<Vec<_>>();
        includes.sort();
        accum.push_str(&includes.join("\n"));
        Ok(accum)
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CppBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        let decl_order = ast.validate_declaration_deps()?;
        match &self.subtype {
            CppSubtype::Base => {
                self.w.write_fmt(format_args!(
                    include_str!("templates/cpp/header.h"),
                    includes = self.codegen_includes(&ast)?,
                    examples = self.codegen_examples(&decl_order, &ast)?,
                    namespace = &ast.primary_namespace,
                    primary_namespace = to_c_name(&ast.primary_namespace).as_str()
                ))?;

                self.w
                    .write_fmt(format_args!("{}", self.codegen_interfaces(&decl_order, &ast)?))?;
                self.w.write_fmt(format_args!("{}", self.codegen_protocols(&decl_order, &ast)?))?;
                self.w.write_fmt(format_args!(include_str!("templates/cpp/footer.h")))?;
            }
            CppSubtype::Internal => {
                self.w.write_fmt(format_args!(
                    include_str!("templates/cpp/internal.h"),
                    protocol_static_asserts = self.codegen_protocol(&decl_order, &ast)?,
                    interface_static_asserts = self.codegen_interface(&decl_order, &ast)?,
                    namespace = &ast.primary_namespace,
                ))?;
            }
            CppSubtype::Mock => {
                self.w.write_fmt(format_args!(
                    include_str!("templates/cpp/mock_header.h"),
                    includes = self.codegen_mock_includes(&decl_order, &ast)?,
                    namespace = &ast.primary_namespace,
                ))?;
                self.w.write_fmt(format_args!("{}", self.codegen_mock(&decl_order, &ast)?))?;
                self.w.write_fmt(format_args!(include_str!("templates/cpp/footer.h")))?;
            }
        }

        Ok(())
    }
}
