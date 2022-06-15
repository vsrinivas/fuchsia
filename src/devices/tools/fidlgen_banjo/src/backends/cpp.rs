// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{util::*, *},
    anyhow::Error,
    fidl_ir_lib::fidl::*,
    std::collections::HashSet,
    std::io,
    std::iter,
};

pub struct CppBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> CppBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        CppBackend { w }
    }
}

fn get_in_args(m: &Method, wrappers: bool, ir: &FidlIr) -> Result<Vec<String>, Error> {
    Ok(m.request_parameters(ir)?
        .as_ref()
        .unwrap_or(&Vec::new())
        .iter()
        .map(|param| {
            let name = to_c_name(&param.name.0);
            match &param._type {
                Type::Vector { .. } => format!(
                    "{name}_{buffer}, {name}_{size}",
                    buffer = name_buffer(&param.maybe_attributes),
                    size = name_size(&param.maybe_attributes),
                    name = name
                ),
                Type::Handle { .. } => {
                    if wrappers {
                        format!(
                            "{}({})",
                            type_to_cpp_str(&param._type, wrappers, ir).unwrap(),
                            name
                        )
                    } else {
                        format!("{}.release()", name)
                    }
                }
                _ => format!("{}", name),
            }
        })
        .collect())
}

fn get_out_args(m: &Method, client: bool, ir: &FidlIr) -> Result<(Vec<String>, bool), Error> {
    if m.maybe_attributes.has("Async") {
        return Ok((vec!["callback".to_string(), "cookie".to_string()], false));
    }

    let (skip, _) = get_first_param(m, ir)?;
    let skip_amt = if skip { 1 } else { 0 };
    Ok((
        m.response_parameters(ir)?
            .as_ref()
            .unwrap_or(&Vec::new())
            .iter()
            .skip(skip_amt)
            .map(|param| {
                let name = to_c_name(&param.name.0);
                match &param._type {
                    Type::Str { .. } => format!("out_{name}, {name}_capacity", name = name),
                    Type::Vector { .. } => {
                        if param.maybe_attributes.has("CalleeAllocated") {
                            format!(
                                "out_{name}_{buffer}, {name}_{size}",
                                buffer = name_buffer(&param.maybe_attributes),
                                size = name_size(&param.maybe_attributes),
                                name = name
                            )
                        } else {
                            format!(
                                "out_{name}_{buffer}, {name}_{size}, out_{name}_actual",
                                buffer = name_buffer(&param.maybe_attributes),
                                size = name_size(&param.maybe_attributes),
                                name = name
                            )
                        }
                    }
                    Type::Handle { .. } => {
                        if client {
                            format!("out_{}->reset_and_get_address()", name)
                        } else {
                            format!("&out_{}2", name)
                        }
                    }
                    _ => format!("out_{}", name),
                }
            })
            .collect(),
        skip,
    ))
}

impl<'a, W: io::Write> CppBackend<'a, W> {
    fn codegen_protocol_constructor_def(
        &self,
        name: &str,
        _attributes: &Option<Vec<Attribute>>,
        methods: &Vec<Method>,
        _ir: &FidlIr,
    ) -> Result<String, Error> {
        let assignments = methods
            .into_iter()
            .map(|m| {
                format!(
                    "        {protocol_name_c}_protocol_ops_.{c_name} = {protocol_name}{cpp_name};",
                    protocol_name_c = to_c_name(name),
                    protocol_name = to_cpp_name(name),
                    c_name = to_c_name(&m.name.0),
                    cpp_name = to_cpp_name(&m.name.0)
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
        _attributes: &Option<Vec<Attribute>>,
        methods: &Vec<Method>,
        _ir: &FidlIr,
    ) -> Result<String, Error> {
        let assignments = methods
            .into_iter()
            .map(|m| {
                format!(
                    "        {protocol_name_c}_protocol_ops_.{c_name} = {protocol_name}{cpp_name};",
                    protocol_name_c = to_c_name(name),
                    protocol_name = to_cpp_name(name),
                    c_name = to_c_name(&m.name.0),
                    cpp_name = to_cpp_name(&m.name.0)
                )
            })
            .collect::<Vec<_>>()
            .join("\n");

        Ok(assignments)
    }

    fn codegen_protocol_defs(
        &self,
        name: &str,
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        methods.iter().map(|m| {
            let mut accum = String::new();
            accum.push_str(get_doc_comment(&m.maybe_attributes, 1).as_str());

            let (out_params, return_param) = get_out_params(&m, name, false, ir)?;
            let in_params = get_in_params(&m, false, false, ir)?;

            let params = iter::once("void* ctx".to_string()).chain(in_params)
                                                            .chain(out_params)
                                                            .collect::<Vec<_>>()
                                                            .join(", ");

            accum.push_str(format!("    static {return_param} {protocol_name}{function_name}({params}) {{\n",
                                   return_param = return_param,
                                   protocol_name = to_cpp_name(name),
                                   params = params,
                                   function_name = to_cpp_name(&m.name.0)).as_str());

            let (out_args, skip) = get_out_args(&m, false, ir)?;
            let in_args = get_in_args(&m, true, ir)?;

            let handle_args = if m.maybe_attributes.has("Async") {
                vec![]
            } else {
                let skip_amt = if skip { 1 } else { 0 };
                m.response_parameters(ir)?.as_ref().unwrap_or(&Vec::new()).iter().skip(skip_amt).filter_map(|param| {
                    match &param._type {
                        Type::Handle {..} => Some((to_c_name(&param.name.0), type_to_cpp_str(&param._type, true, ir).unwrap())),
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
                                   function_name = to_cpp_name(&m.name.0)).as_str());
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
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let mut accum = String::new();
                accum.push_str(get_doc_comment(&m.maybe_attributes, 1).as_str());

                let (out_params, return_param) = get_out_params(&m, name, true, ir)?;
                let in_params = get_in_params(&m, true, true, ir)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                accum.push_str(
                    format!(
                        "    {return_param} {function_name}({params}) const {{\n",
                        return_param = return_param,
                        params = params,
                        function_name = to_cpp_name(&m.name.0)
                    )
                    .as_str(),
                );

                let (out_args, skip) = get_out_args(&m, true, ir)?;
                let in_args = get_in_args(&m, false, ir)?;

                let proto_args = m
                    .request_parameters(ir)?
                    .as_ref()
                    .unwrap_or(&Vec::new())
                    .iter()
                    .filter_map(|param| {
                        if let Type::Identifier { ref identifier, .. } = param._type {
                            match ir.get_declaration(identifier).unwrap() {
                                Declaration::Protocol => {
                                    if not_callback(identifier, ir).unwrap() {
                                        return Some((
                                            to_c_name(&param.name.0),
                                            type_to_cpp_str(&param._type, true, ir).unwrap(),
                                        ));
                                    }
                                }
                                _ => {}
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
                        function_name_snake = to_c_name(&m.name.0)
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
        declarations: &Vec<Decl<'_>>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol(ProtocolType::Interface))
            .map(|data| {
                let name = data.name.get_name();
                Ok(format!(
                    include_str!("templates/cpp/interface.h"),
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name),
                    protocol_docs = get_doc_comment(&data.maybe_attributes, 0),
                    constructor_definition = self.codegen_interface_constructor_def(
                        name,
                        &data.maybe_attributes,
                        &data.methods,
                        ir
                    )?,
                    protocol_definitions = self.codegen_protocol_defs(name, &data.methods, ir)?,
                    client_definitions = self.codegen_client_defs(name, &data.methods, ir)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_protocols(
        &self,
        declarations: &Vec<Decl<'_>>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol(ProtocolType::Protocol))
            .map(|data| {
                let name = data.name.get_name();
                Ok(format!(
                    include_str!("templates/cpp/protocol.h"),
                    protocol_name = to_cpp_name(name),
                    protocol_name_uppercase = to_c_name(name).to_uppercase(),
                    protocol_name_snake = to_c_name(name),
                    protocol_docs = get_doc_comment(&data.maybe_attributes, 0),
                    constructor_definition = self.codegen_protocol_constructor_def(
                        name,
                        &data.maybe_attributes,
                        &data.methods,
                        ir
                    )?,
                    protocol_definitions = self.codegen_protocol_defs(name, &data.methods, ir)?,
                    client_definitions = self.codegen_client_defs(name, &data.methods, ir)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join(""))
    }

    fn codegen_includes(&self, declarations: &Vec<Decl<'_>>, ir: &FidlIr) -> Result<String, Error> {
        let empty_params: Vec<MethodParameter<'_>> = vec![];
        let mut includes = vec![
            "lib/ddk/device".to_string(),
            "lib/ddk/driver".to_string(),
            "ddktl/device-internal".to_string(),
            "zircon/assert".to_string(),
            "zircon/compiler".to_string(),
            "zircon/types".to_string(),
            ir.name.0.replace('.', "/") + "/c/banjo",
        ]
        .into_iter()
        .chain(
            ir.library_dependencies
                .iter()
                .map(|l| &l.name.0)
                .filter(|n| *n != "zx")
                .map(|n| n.replace('.', "/") + "/c/banjo"),
        )
        .map(|n| format!("#include <{}.h>", n))
        .chain(
            // Include handle headers for zx_handle_t wrapper types used in protocols.
            declarations
                .iter()
                .filter_map(|decl| {
                    // Find all protocols and extract their methods.
                    if let Decl::Protocol { ref data } = *decl {
                        Some(&data.methods)
                    } else {
                        None
                    }
                })
                .flat_map(|methods| {
                    // Find all handle in/out params in each method.
                    methods.iter().flat_map(|method| {
                        method
                            .request_parameters(ir)
                            .as_ref()
                            .unwrap()
                            .as_ref()
                            .unwrap_or(&empty_params)
                            .iter()
                            .filter_map(|param| match &param._type {
                                Type::Handle { subtype, .. } => Some(subtype),
                                _ => None,
                            })
                            .chain(
                                method
                                    .response_parameters(ir)
                                    .as_ref()
                                    .unwrap()
                                    .as_ref()
                                    .unwrap_or(&empty_params)
                                    .iter()
                                    .filter_map(|param| match &param._type {
                                        Type::Handle { subtype, .. } => Some(subtype),
                                        _ => None,
                                    }),
                            )
                            .map(|ty| handle_type_to_cpp_str(ty))
                            .collect::<Vec<_>>()
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

    fn codegen_examples(&self, declarations: &Vec<Decl<'_>>, ir: &FidlIr) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol(ProtocolType::Protocol))
            .map(|data| {
                let name = data.name.get_name();
                let example_decls = data
                    .methods
                    .iter()
                    .map(|m| {
                        let (out_params, return_param) = get_out_params(&m, name, true, ir)?;
                        let in_params = get_in_params(&m, true, false, ir)?;

                        let params =
                            in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                        Ok(format!(
                            "//     {return_param} {protocol_name}{function_name}({params});",
                            return_param = return_param,
                            protocol_name = to_cpp_name(name),
                            params = params,
                            function_name = to_cpp_name(&m.name.0)
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
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        let decl_order = get_declarations(&ir)?;
        self.w.write_fmt(format_args!(
            include_str!("templates/cpp/header.h"),
            includes = self.codegen_includes(&decl_order, &ir)?,
            examples = self.codegen_examples(&decl_order, &ir)?,
            namespace = ir.name.0,
            primary_namespace = to_c_name(&ir.name.0)
        ))?;

        self.w.write_fmt(format_args!("{}", self.codegen_interfaces(&decl_order, &ir)?))?;
        self.w.write_fmt(format_args!("{}", self.codegen_protocols(&decl_order, &ir)?))?;
        self.w.write_fmt(format_args!(include_str!("templates/cpp/footer.h")))?;
        Ok(())
    }
}
