// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fidl::{self, Declaration, FidlIr},
    anyhow::Error,
    std::io,
};

pub use self::c::CBackend;
pub use self::rust::RustBackend;

mod c;
mod rust;
mod util;

pub trait Backend<'a, W: io::Write> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error>;
}

pub enum Decl<'a> {
    Const { data: &'a fidl::Const },
    Enum { data: &'a fidl::Enum },
    Interface { data: &'a fidl::Interface },
    Struct { data: &'a fidl::Struct },
    TypeAlias { data: &'a fidl::TypeAlias },
    Union { data: &'a fidl::Union },
}

pub fn get_declarations<'b>(ir: &'b FidlIr) -> Result<Vec<Decl<'b>>, Error> {
    Ok(ir
        .declaration_order
        .iter()
        .filter_map(|ident| match ir.get_declaration(ident).ok()? {
            Declaration::Const => Some(Decl::Const {
                data: ir.const_declarations.iter().filter(|c| c.name == *ident).next()?,
            }),
            Declaration::Enum => Some(Decl::Enum {
                data: ir.enum_declarations.iter().filter(|e| e.name == *ident).next()?,
            }),
            Declaration::Interface => Some(Decl::Interface {
                data: ir.interface_declarations.iter().filter(|e| e.name == *ident).next()?,
            }),
            Declaration::Struct => Some(Decl::Struct {
                data: ir.struct_declarations.iter().filter(|e| e.name == *ident).next()?,
            }),
            Declaration::TypeAlias => Some(Decl::TypeAlias {
                data: ir.type_alias_declarations.iter().filter(|e| e.name == *ident).next()?,
            }),
            Declaration::Union => Some(Decl::Union {
                data: ir.union_declarations.iter().filter(|e| e.name == *ident).next()?,
            }),
            _ => None,
        })
        .collect())
}
