// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{util::*, *},
    anyhow::Error,
    fidl_ir_lib::fidl::*,
    std::io,
};

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
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(&m, name, true, ir)?;
                let in_params = get_in_params(&m, true, false, ir)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                Ok(format!(
                    include_str!("templates/cpp/internal_decl.h"),
                    return_param = return_param,
                    params = params,
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name),
                    method_name = to_cpp_name(&m.name.0),
                    method_name_snake = to_c_name(&m.name.0)
                ))
            })
            .collect::<Result<Vec<String>, Error>>()
            .map(|fns| fns.join("\n"))
    }

    fn codegen_static_asserts(
        &self,
        name: &str,
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(&m, name, true, ir)?;
                let in_params = get_in_params(&m, true, false, ir)?;

                let params = in_params.into_iter().chain(out_params).collect::<Vec<_>>().join(", ");

                Ok(format!(
                    include_str!("templates/cpp/internal_static_assert.h"),
                    return_param = return_param,
                    params = params,
                    protocol_name = to_cpp_name(name),
                    protocol_name_snake = to_c_name(name),
                    method_name = to_cpp_name(&m.name.0),
                    method_name_snake = to_c_name(&m.name.0)
                ))
            })
            .collect::<Result<Vec<String>, Error>>()
            .map(|fns| fns.join("\n"))
    }

    fn codegen_protocol(&self, declarations: &Vec<Decl<'_>>, ir: &FidlIr) -> Result<String, Error> {
        declarations
            .iter()
            .filter_map(filter_protocol(ProtocolType::Protocol))
            .map(|data| {
                let name = data.name.get_name();
                Ok(format!(
                    include_str!("templates/cpp/internal_protocol.h"),
                    protocol_name = to_cpp_name(name),
                    decls = self.codegen_decls(name, &data.methods, ir)?,
                    static_asserts = self.codegen_static_asserts(name, &data.methods, ir)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_interface(
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
                    include_str!("templates/cpp/internal_protocol.h"),
                    protocol_name = to_cpp_name(name),
                    decls = self.codegen_decls(name, &data.methods, ir)?,
                    static_asserts = self.codegen_static_asserts(name, &data.methods, ir)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CppInternalBackend<'a, W> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        let decl_order = get_declarations(&ir)?;
        self.w.write_fmt(format_args!(
            include_str!("templates/cpp/internal.h"),
            protocol_static_asserts = self.codegen_protocol(&decl_order, &ir)?,
            interface_static_asserts = self.codegen_interface(&decl_order, &ir)?,
            namespace = ir.name.0,
        ))?;
        Ok(())
    }
}
