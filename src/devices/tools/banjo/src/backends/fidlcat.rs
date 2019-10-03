// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst, Decl, Ty},
    crate::backends::Backend,
    failure::Error,
    heck::CamelCase,
    std::io,
};

// Extract is_pointer, cpp_type and fidlcat_type from Ty.
fn extract_type(ty: &Ty) -> (bool, String, String) {
    match ty {
        ast::Ty::Voidptr => (true, "uint8_t".to_string(), "Uint8".to_string()),
        ast::Ty::USize => (false, "size_t".to_string(), "Size".to_string()),
        ast::Ty::Bool => (false, "bool".to_string(), "Bool".to_string()),
        ast::Ty::Int8 => (false, "int8_t".to_string(), "Int8".to_string()),
        ast::Ty::Int16 => (false, "int16_t".to_string(), "Int16".to_string()),
        ast::Ty::Int32 => (false, "int32_t".to_string(), "Int32".to_string()),
        ast::Ty::Int64 => (false, "int64_t".to_string(), "Int64".to_string()),
        ast::Ty::UInt8 => (false, "uint8_t".to_string(), "Uint8".to_string()),
        ast::Ty::UInt16 => (false, "uint16_t".to_string(), "Uint16".to_string()),
        ast::Ty::UInt32 => (false, "uint32_t".to_string(), "Uint32".to_string()),
        ast::Ty::UInt64 => (false, "uint64_t".to_string(), "Uint64".to_string()),
        ast::Ty::Float32 => (false, "float".to_string(), "Float32".to_string()),
        ast::Ty::Float64 => (false, "double".to_string(), "Float64".to_string()),
        ast::Ty::Str { .. } => (true, "char".to_string(), "Char".to_string()),
        ast::Ty::Array { ref ty, .. } => {
            let (_is_pointer, cpp_type, fidlcat_type) = extract_type(ty);
            (true, cpp_type, fidlcat_type)
        }
        ast::Ty::Handle { .. } => (false, "zx_handle_t".to_string(), "Handle".to_string()),
        ast::Ty::Identifier { id, .. } => {
            if id.is_base_type() {
                (false, format!("zx_{}_t", id.name()), id.name().to_camel_case())
            } else {
                (false, id.name().to_string(), "Struct".to_string())
            }
        }
        // For types we don't manage yet, we don't want to stop the generation.
        // Instead, we generate a fake value which will generate an error when
        // the generated file is compiled. This way, it's easy to see which
        // parameters/syscalls have a problem.
        _t => (false, "xxx".to_string(), "Xxx".to_string()),
    }
}

// Get the type of the value directly returned by a syscall.
fn get_return_type(out: &Vec<(String, Ty)>) -> String {
    if out.len() == 0 {
        return "Void".to_string();
    }
    return extract_type(&out[0].1).2;
}

// Fidlcat backend. It generate the info needed by Fidlcat to decode all the
// system calls.
pub struct FidlcatBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> FidlcatBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        FidlcatBackend { w }
    }
}

impl<'a, W: io::Write> FidlcatBackend<'a, W> {
    fn codegen_protocol_def(&self, methods: &Vec<ast::Method>) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let mut accum = String::new();

                accum.push_str("  {\n");

                accum.push_str("    ");
                let fn_name =  m.name.as_str();
                // Only generate a local variable for the syscall if it will be used.
                if (m.in_params.len() > 0) || (m.out_params.len() > 1) {
                    accum.push_str(
                        format!("Syscall* zx_{fn_name} = ", fn_name = fn_name)
                        .as_str(),
                    );
                }
                let return_type = get_return_type(&m.out_params);
                accum.push_str(
                    format!(
                        "Add(\"zx_{fn_name}\", SyscallReturnType::k{return_type});\n",
                        fn_name = fn_name,
                        return_type = return_type,
                    )
                    .as_str()
                );

                // Generates all the arguments of the syscall.
                if (m.in_params.len() > 0) || (m.out_params.len() > 1) {
                    accum.push_str("    // Arguments\n");
                    for (name, ty) in m.in_params.iter() {
                        let (is_pointer, cpp_type, fidlcat_type) = extract_type(ty);
                        let argument = if is_pointer { "PointerArgument" } else { "Argument" };
                        accum.push_str("    ");
                        accum.push_str(format!("auto {name} = ", name = name).as_str());
                        accum.push_str(
                            format!(
                                "zx_{fn_name}->{argument}<{cpp_type}>(SyscallType::k{fidlcat_type});\n",
                                fn_name = fn_name,
                                argument = argument,
                                cpp_type = cpp_type,
                                fidlcat_type = fidlcat_type
                            )
                            .as_str()
                        );
                    }
                    if m.out_params.len() > 1 {
                        for (name, ty) in m.out_params[1..].iter() {
                            let (_is_pointer, cpp_type, fidlcat_type) = extract_type(ty);
                            accum.push_str("    ");
                            accum.push_str(format!("auto {name} = ", name = name).as_str());
                            accum.push_str(
                                format!(
                                    "zx_{fn_name}->PointerArgument<{cpp_type}>(SyscallType::k{fidlcat_type});\n",
                                    fn_name = fn_name,
                                    cpp_type = cpp_type,
                                    fidlcat_type = fidlcat_type
                                )
                                .as_str()
                            );
                        }
                    }
                }

                // Generates the info to display arguments which must be displayed
                // when the syscall is called.
                if m.in_params.len() > 0 {
                    accum.push_str("    // Inputs\n");
                    for (name, ty) in m.in_params.iter() {
                        let (_is_pointer, cpp_type, _fidlcat_type) = extract_type(ty);
                        match ty {
                            ast::Ty::Voidptr => {},
                            ast::Ty::USize => {},
                            ast::Ty::Bool | ast::Ty::Int8 | ast::Ty::Int16 |
                            ast::Ty::Int32 | ast::Ty::Int64 | ast::Ty::UInt8 |
                            ast::Ty::UInt16 | ast::Ty::UInt32 | ast::Ty::UInt64 |
                            ast::Ty::Float32 | ast::Ty::Float64 | ast::Ty::Handle { .. } =>
                                accum.push_str(
                                    format!(
                                        "    zx_{fn_name}->Input<{cpp_type}>(\"{name}\", std::make_unique<ArgumentAccess<{cpp_type}>>({name}));\n",
                                        fn_name = fn_name,
                                        name = name,
                                        cpp_type = cpp_type,
                                    )
                                    .as_str()
                                ),
                            ast::Ty::Str { size, .. } => {
                                if let Some(size) = size {
                                    if let Some(size) = m.in_params.iter().find(|&x| x.0 == size.0) {
                                        if size.1 == ast::Ty::USize {
                                          accum.push_str(
                                              format!(
                                                  "    zx_{fn_name}->InputString(\"{name}\", std::make_unique<ArgumentAccess<char>>({name}), std::make_unique<ArgumentAccess<size_t>>({size_name}));\n",
                                                  fn_name = fn_name,
                                                  name = name,
                                                  size_name = size.0,
                                              )
                                              .as_str()
                                          );
                                        }
                                    }
                                }
                            },
                            ast::Ty::Array { size, .. } => {
                                if let Some(size) = m.in_params.iter().find(|&x| x.0 == size.0) {
                                    if size.1 == ast::Ty::USize {
                                      accum.push_str(
                                          format!(
                                              "    zx_{fn_name}->InputBuffer<{cpp_type}>(\"{name}\", std::make_unique<ArgumentAccess<{cpp_type}>>({name}), std::make_unique<ArgumentAccess<size_t>>({size_name}));\n",
                                              fn_name = fn_name,
                                              name = name,
                                              size_name = size.0,
                                              cpp_type = cpp_type,
                                          )
                                          .as_str()
                                      );
                                    }
                                }
                            },
                            ast::Ty::Identifier { id, .. } => {
                                if id.is_base_type() {
                                    accum.push_str(
                                        format!(
                                            "    zx_{fn_name}->Input<{cpp_type}>(\"{name}\", std::make_unique<ArgumentAccess<{cpp_type}>>({name}));\n",
                                            fn_name = fn_name,
                                            name = name,
                                            cpp_type = cpp_type,
                                        )
                                        .as_str()
                                    );
                                }
                            },
                            _t => {}
                        }
                    }
                }

                // Generates the info to display arguments which must be displayed
                // when the syscall returns.
                if m.out_params.len() > 1 {
                    accum.push_str("    // Outputs\n");
                    for (name, ty) in m.out_params[1..].iter() {
                        let (_is_pointer, cpp_type, _fidlcat_type) = extract_type(ty);
                        match ty {
                            ast::Ty::Voidptr => {},
                            ast::Ty::USize => {},
                            ast::Ty::Bool | ast::Ty::Int8 | ast::Ty::Int16 |
                            ast::Ty::Int32 | ast::Ty::Int64 | ast::Ty::UInt8 |
                            ast::Ty::UInt16 | ast::Ty::UInt32 | ast::Ty::UInt64 |
                            ast::Ty::Float32 | ast::Ty::Float64 | ast::Ty::Handle { .. } =>
                                accum.push_str(
                                    format!(
                                        "    zx_{fn_name}->Output<{cpp_type}>(ZX_OK, \"{name}\", std::make_unique<ArgumentAccess<{cpp_type}>>({name}));\n",
                                        fn_name = fn_name,
                                        name = name,
                                        cpp_type = cpp_type,
                                    )
                                    .as_str()
                                ),
                            ast::Ty::Str { .. } => {},
                            ast::Ty::Array { size, .. } => {
                                if let Some(size) = m.in_params.iter().find(|&x| x.0 == size.0) {
                                    if size.1 == ast::Ty::USize {
                                      accum.push_str(
                                          format!(
                                              "    zx_{fn_name}->OutputBuffer<{cpp_type}>(ZX_OK, \"{name}\", std::make_unique<ArgumentAccess<{cpp_type}>>({name}), std::make_unique<ArgumentAccess<size_t>>({size_name}));\n",
                                              fn_name = fn_name,
                                              name = name,
                                              size_name = size.0,
                                              cpp_type = cpp_type,
                                          )
                                          .as_str()
                                      );
                                    }
                                }
                            },
                            ast::Ty::Identifier { id, .. } => {
                                if id.is_base_type() {
                                    accum.push_str(
                                        format!(
                                            "    zx_{fn_name}->Output<{cpp_type}>(ZX_OK, \"{name}\", std::make_unique<ArgumentAccess<{cpp_type}>>({name}));\n",
                                            fn_name = fn_name,
                                            name = name,
                                            cpp_type = cpp_type,
                                        )
                                        .as_str()
                                    );
                                }
                            },
                            _t => {}
                        }
                    }
                }

                accum.push_str("  }\n");

                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for FidlcatBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/fidlcat/header.fidlcat.in"),
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
                    Some(self.codegen_protocol_def(methods))
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        write!(&mut self.w, "{}", definitions)?;

        write!(&mut self.w, "{}", include_str!("templates/fidlcat/footer.fidlcat.in"))?;

        Ok(())
    }
}
