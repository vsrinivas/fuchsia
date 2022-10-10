// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{util::*, *},
    anyhow::Error,
    fidl_ir_lib::fidl::*,
    std::{io, iter},
};

pub struct CppMockBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> CppMockBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        CppMockBackend { w }
    }
}

fn get_mock_out_param_types(m: &Method, ir: &FidlIr) -> Result<String, Error> {
    if !m.has_response || m.response_parameters(ir)?.as_ref().unwrap().is_empty() {
        Ok("void".to_string())
    } else {
        Ok(format!(
            "std::tuple<{}>",
            m.response_parameters(ir)?
                .as_ref()
                .unwrap()
                .iter()
                .map(|param| {
                    if let Some(arg_type) = get_base_type_from_alias(
                        &param.experimental_maybe_from_alias.as_ref().map(|t| &t.name),
                    ) {
                        return format!("{}", arg_type);
                    }
                    match param._type {
                        Type::Str { .. } => "std::string".to_string(),
                        Type::Vector { ref element_type, .. } => format!(
                            "std::vector<{}>",
                            type_to_cpp_str(element_type, false, ir).unwrap()
                        ),
                        _ => type_to_cpp_str(&param._type, true, ir).unwrap(),
                    }
                })
                .collect::<Vec<_>>()
                .join(", "),
        ))
    }
}

fn get_mock_params(m: &Method, ir: &FidlIr) -> Result<String, Error> {
    // If async, put all output parameters last and add callback and cookie. Otherwise put first
    // output param first, then all input params, then the rest of the output params.
    let (skip, return_param) = get_first_param(m, ir)?;
    let has_return_value = skip && !m.maybe_attributes.has("Async");

    let mut params = Vec::new();
    if has_return_value {
        params.push(format!(
            "{} out_{}",
            return_param,
            m.response_parameters(ir)?.as_ref().unwrap()[0].name.0
        ));
    }

    Ok(params
        .into_iter()
        .chain(m.request_parameters(ir)?.as_ref().unwrap_or(&Vec::new()).iter().map(|param| {
            let name = &to_c_name(&param.name.0);
            if let Some(arg_type) = get_base_type_from_alias(
                &param.experimental_maybe_from_alias.as_ref().map(|t| &t.name),
            ) {
                return format!("{} {}", arg_type, name);
            }
            match param._type {
                Type::Handle { .. } => {
                    format!("const {}& {}", type_to_cpp_str(&param._type, true, ir).unwrap(), name)
                }
                Type::Str { .. } => format!("std::string {}", name),
                Type::Vector { ref element_type, .. } => format!(
                    "std::vector<{ty}> {name}",
                    ty = type_to_cpp_str(element_type, false, ir).unwrap(),
                    name = name,
                ),
                _ => format!("{} {}", type_to_cpp_str(&param._type, true, ir).unwrap(), name),
            }
        }))
        .chain(
            m.response_parameters(ir)?
                .as_ref()
                .unwrap_or(&Vec::new())
                .iter()
                .skip(if has_return_value { 1 } else { 0 })
                .map(|param| {
                    let name = &to_c_name(&param.name.0);
                    if let Some(arg_type) = get_base_type_from_alias(
                        &param.experimental_maybe_from_alias.as_ref().map(|t| &t.name),
                    ) {
                        return format!("{} out_{}", arg_type, name);
                    }
                    match param._type {
                        Type::Str { .. } => format!("std::string {}", name),
                        Type::Vector { ref element_type, .. } => format!(
                            "std::vector<{ty}> out_{name}",
                            ty = type_to_cpp_str(element_type, false, ir).unwrap(),
                            name = name,
                        ),
                        _ => format!(
                            "{} out_{}",
                            type_to_cpp_str(&param._type, true, ir).unwrap(),
                            name
                        ),
                    }
                }),
        )
        .collect::<Vec<_>>()
        .join(", "))
}

fn get_mock_expect_args(m: &Method, ir: &FidlIr) -> Result<String, Error> {
    let mut args = Vec::new();
    if m.has_response && !m.response_parameters(ir)?.as_ref().unwrap().is_empty() {
        args.push(format!(
            "{{{}}}",
            m.response_parameters(ir)?
                .as_ref()
                .unwrap()
                .iter()
                .map(|param| {
                    let name = &to_c_name(&param.name.0);
                    match param._type {
                        Type::Handle { .. } => format!("std::move(out_{})", name),
                        Type::Str { .. } => format!("std::move(out_{})", name),
                        Type::Vector { .. } => format!("std::move(out_{})", name),
                        _ => format!("out_{}", name),
                    }
                })
                .collect::<Vec<_>>()
                .join(", "),
        ));
    }

    Ok(args
        .into_iter()
        .chain(m.request_parameters(ir)?.as_ref().unwrap_or(&Vec::new()).iter().map(|param| {
            let name = &to_c_name(&param.name.0);
            match param._type {
                Type::Handle { .. } => format!("{}.get()", name),
                Type::Str { .. } => format!("std::move({})", name),
                Type::Vector { .. } => format!("std::move({})", name),
                _ => name.to_string(),
            }
        }))
        .collect::<Vec<_>>()
        .join(", "))
}

fn get_mock_param_types(m: &Method, ir: &FidlIr) -> Result<String, Error> {
    Ok(iter::once(get_mock_out_param_types(m, ir)?)
        .chain(m.request_parameters(ir)?.as_ref().unwrap_or(&Vec::new()).iter().map(|param| {
            if let Some(arg_type) = get_base_type_from_alias(
                &param.experimental_maybe_from_alias.as_ref().map(|t| &t.name),
            ) {
                return format!("{}", arg_type);
            }
            match param._type {
                Type::Str { .. } => "std::string".to_string(),
                Type::Vector { ref element_type, .. } => {
                    format!("std::vector<{}>", type_to_cpp_str(element_type, false, ir).unwrap())
                }
                _ => type_to_cpp_str(&param._type, true, ir).unwrap(),
            }
        }))
        .collect::<Vec<_>>()
        .join(", "))
}

impl<'a, W: io::Write> CppMockBackend<'a, W> {
    fn codegen_mock_accessors(&self, methods: &Vec<Method>, ir: &FidlIr) -> Result<String, Error> {
        let text = methods
            .iter()
            .map(|m| {
                Ok(format!(
                    "    mock_function::MockFunction<{param_types}>& mock_{name}() \
                     {{ return mock_{name}_; }}",
                    param_types = get_mock_param_types(&m, ir)?,
                    name = to_c_name(&m.name.0).as_str(),
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))?;
        Ok(if text.len() > 0 { "\n".to_string() + &text } else { "".to_string() })
    }

    fn codegen_mock_definitions(
        &self,
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                Ok(format!(
                    "    mock_function::MockFunction<{param_types}> mock_{name}_;",
                    param_types = get_mock_param_types(&m, ir)?,
                    name = to_c_name(&m.name.0).as_str(),
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_mock_expects(
        &self,
        name: &str,
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        let text = methods
            .iter()
            .map(|m| {
                Ok(format!(
                    include_str!("templates/cpp/mock_expect.h"),
                    protocol_name = to_cpp_name(name),
                    method_name = m.name.0,
                    params = get_mock_params(&m, ir)?,
                    method_name_snake = to_c_name(&m.name.0).as_str(),
                    args = get_mock_expect_args(&m, ir)?,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))?;
        Ok(if text.len() > 0 { "\n".to_string() + &text } else { "".to_string() })
    }

    fn codegen_mock_verify(&self, methods: &Vec<Method>) -> Result<String, Error> {
        Ok(methods
            .iter()
            .map(|m| format!("        mock_{}_.VerifyAndClear();", to_c_name(&m.name.0)))
            .collect::<Vec<_>>()
            .join("\n"))
    }

    fn codegen_mock_protocol_out_args(&self, m: &Method, ir: &FidlIr) -> Result<String, Error> {
        let (skip, _) = get_first_param(m, ir)?;
        let skip_amt = if skip { 1 } else { 0 };

        let mut accum = String::new();

        if m.has_response {
            let params = m.response_parameters(ir)?;
            let response = params.as_ref().unwrap();
            for i in skip_amt..response.len() {
                let name = to_c_name(&response[i].name.0);
                match &response[i]._type {
                    Type::Handle { .. } => {
                        accum.push_str(
                            format!(
                                "        *out_{name} = std::move(std::get<{index}>(ret));\n",
                                name = name,
                                index = i,
                            )
                            .as_str(),
                        );
                    }
                    Type::Str { .. } => {
                        accum.push_str(
                            format!(
                                "        strncpy(out_{name}, std::get<{index}>(ret).c_str(), \
                                 {name}_capacity));\n",
                                name = name,
                                index = i,
                            )
                            .as_str(),
                        );
                    }
                    Type::Vector { .. } => {
                        accum.push_str(
                            format!(
                                "        *out_{name}_actual = std::min<size_t>(\
                                 std::get<{index}>(ret).size(), {name}_{size});\n",
                                name = name,
                                size = name_size(&response[i].maybe_attributes),
                                index = i,
                            )
                            .as_str(),
                        );
                        accum.push_str(
                            format!(
                                "        std::move(std::get<{index}>(ret).begin(), \
                                 std::get<{index}>(ret).begin() + *out_{name}_actual, \
                                 out_{name}_{buffer});\n",
                                name = name,
                                buffer = name_buffer(&response[i].maybe_attributes),
                                index = i,
                            )
                            .as_str(),
                        );
                    }
                    _ => {
                        accum.push_str(
                            format!(
                                "        *out_{name} = std::get<{index}>(ret);\n",
                                name = name,
                                index = i,
                            )
                            .as_str(),
                        );
                    }
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
        m: &Method,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        let mut out_args = Vec::new();
        out_args.push("cookie".to_string());

        if m.has_response {
            let params = m.response_parameters(ir)?;
            let response = params.as_ref().unwrap();
            for i in 0..response.len() {
                match &response[i]._type {
                    Type::Handle { .. } => {
                        out_args.push(format!("std::move(std::get<{}>(ret))", i))
                    }
                    Type::Str { .. } => out_args.push(format!("std::get<{}>(ret).c_str()", i)),
                    Type::Vector { .. } => {
                        out_args.push(format!("std::get<{}>(ret).data()", i));
                        out_args.push(format!("std::get<{}>(ret).size()", i));
                    }
                    Type::Identifier { identifier, .. } => {
                        if identifier.is_base_type() {
                            out_args.push(format!("std::get<{}>(ret)", i))
                        } else {
                            match ir.get_declaration(identifier).unwrap() {
                                Declaration::Struct | Declaration::Union => {
                                    out_args.push(format!("&std::get<{}>(ret)", i))
                                }
                                _ => out_args.push(format!("std::get<{}>(ret)", i)),
                            }
                        }
                    }
                    _ => out_args.push(format!("std::get<{}>(ret)", i)),
                }
            }
        }

        Ok(format!("        callback({});\n", out_args.join(", ")))
    }

    fn codegen_mock_protocol_defs(
        &self,
        name: &str,
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        let text = methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(&m, name, true, ir)?;
                let in_params = get_in_params(&m, true, false, ir)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                let mut accum = String::new();

                accum.push_str(
                    format!(
                        "    virtual {return_param} {protocol_name}{function_name}({params}) {{\n",
                        return_param = return_param,
                        protocol_name = to_cpp_name(name),
                        params = params,
                        function_name = to_cpp_name(&m.name.0),
                    )
                    .as_str(),
                );

                let in_args = m
                    .request_parameters(ir)?
                    .as_ref()
                    .unwrap_or(&Vec::new())
                    .iter()
                    .map(|param| {
                        let name = to_c_name(&param.name.0);
                        match &param._type {
                            Type::Handle { .. } => format!("std::move({})", name),
                            Type::Str { .. } => format!("std::string({})", name),
                            Type::Vector { .. } => {
                                let ty_name = type_to_cpp_str(&param._type, false, ir).unwrap();
                                format!(
                                    "std::vector<{ty}>({name}_{buffer}, \
                                     {name}_{buffer} + {name}_{size})",
                                    ty = ty_name,
                                    name = name,
                                    buffer = name_buffer(&param.maybe_attributes),
                                    size = name_size(&param.maybe_attributes),
                                )
                            }
                            Type::Identifier { identifier, .. } => {
                                if identifier.is_base_type() {
                                    name
                                } else {
                                    match ir.get_declaration(identifier).unwrap() {
                                        Declaration::Protocol
                                        | Declaration::Struct
                                        | Declaration::Union => {
                                            format!("*{}", name)
                                        }
                                        _ => name,
                                    }
                                }
                            }
                            _ => name,
                        }
                    })
                    .collect::<Vec<_>>()
                    .join(", ");

                accum.push_str("        ");

                if m.has_response && !m.response_parameters(ir)?.as_ref().unwrap().is_empty() {
                    accum
                        .push_str(format!("{} ret = ", get_mock_out_param_types(&m, ir)?).as_str());
                }

                accum.push_str(
                    format!(
                        "mock_{name}_.Call({args});\n",
                        name = to_c_name(&m.name.0),
                        args = in_args.as_str(),
                    )
                    .as_str(),
                );

                if m.maybe_attributes.has("Async") {
                    accum.push_str(self.codegen_mock_protocol_async_out_args(&m, ir)?.as_str());
                } else {
                    accum.push_str(self.codegen_mock_protocol_out_args(&m, ir)?.as_str());
                }

                accum.push_str("    }");
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n\n"))?;
        Ok(if text.len() > 0 { "\n".to_string() + &text } else { "".to_string() })
    }

    fn codegen_mock(&self, declarations: &Vec<Decl<'_>>, ir: &FidlIr) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol(ProtocolType::Protocol))
            .map(|data| {
                Ok(format!(
                    include_str!("templates/cpp/mock.h"),
                    protocol_name = to_cpp_name(data.name.get_name()),
                    protocol_name_snake = to_c_name(data.name.get_name()).as_str(),
                    mock_expects =
                        self.codegen_mock_expects(data.name.get_name(), &data.methods, ir)?,
                    mock_verify = self.codegen_mock_verify(&data.methods)?,
                    protocol_definitions =
                        self.codegen_mock_protocol_defs(data.name.get_name(), &data.methods, ir)?,
                    mock_accessors = self.codegen_mock_accessors(&data.methods, ir)?,
                    mock_definitions = self.codegen_mock_definitions(&data.methods, ir)?,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_mock_includes(
        &self,
        declarations: &Vec<Decl<'_>>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        let mut need_c_string_header = false;
        let mut need_cpp_string_header = false;
        let mut need_cpp_tuple_header = false;
        let mut need_cpp_vector_header = false;

        declarations.iter().filter_map(filter_protocol(ProtocolType::Protocol)).for_each(|data| {
            data.methods.iter().for_each(|method| {
                if method.has_request {
                    method.request_parameters(ir).unwrap().as_ref().unwrap().iter().for_each(
                        |param| match param._type {
                            Type::Str { .. } => need_cpp_string_header = true,
                            Type::Vector { .. } => need_cpp_vector_header = true,
                            _ => {}
                        },
                    );
                }

                if method.has_response
                    && !method.response_parameters(ir).unwrap().as_ref().unwrap().is_empty()
                {
                    need_cpp_tuple_header = true;
                    method.response_parameters(ir).unwrap().as_ref().unwrap().iter().for_each(
                        |param| match param._type {
                            Type::Str { .. } => {
                                need_cpp_string_header = true;
                                if !method.maybe_attributes.has("Async") {
                                    need_c_string_header = true;
                                }
                            }
                            Type::Vector { .. } => need_cpp_vector_header = true,
                            _ => {}
                        },
                    );
                }
            })
        });

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

        let mut base = vec!["lib/mock-function/mock-function".to_string()];
        base.push(ir.name.0.replace('.', "/") + "/cpp/banjo");
        let mut includes = base
            .into_iter()
            .chain(
                ir.library_dependencies
                    .iter()
                    .map(|l| &l.name.0)
                    .filter(|n| *n != "zx")
                    .map(|n| n.replace('.', "/") + "/cpp/banjo"),
            )
            .map(|n| format!("#include <{}.h>", n))
            .collect::<Vec<_>>();
        includes.sort();
        accum.push_str(&includes.join("\n"));

        Ok(accum)
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CppMockBackend<'a, W> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        let decl_order = get_declarations(&ir)?;
        self.w.write_fmt(format_args!(
            include_str!("templates/cpp/mock_header.h"),
            includes = self.codegen_mock_includes(&decl_order, &ir)?,
            namespace = ir.name.0,
        ))?;
        self.w.write_fmt(format_args!("{}", self.codegen_mock(&decl_order, &ir)?))?;
        self.w.write_fmt(format_args!(include_str!("templates/cpp/footer.h")))?;
        Ok(())
    }
}
