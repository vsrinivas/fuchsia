// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        util::{get_declarations, to_c_name, Decl},
        Backend,
    },
    crate::fidl::*,
    anyhow::{anyhow, Error},
    std::collections::HashSet,
    std::io,
};

pub struct RustBackend<'a, W: io::Write> {
    w: &'a mut W,
}

impl<'a, W: io::Write> RustBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        RustBackend { w }
    }
}

fn can_derive_partialeq(
    ty: &Type,
    parents: &mut HashSet<CompoundIdentifier>,
    ir: &FidlIr,
) -> Result<bool, Error> {
    match ty {
        Type::Array { ref element_type, ref element_count } => {
            if element_count.0 <= 32 {
                can_derive_partialeq(element_type, parents, ir)
            } else {
                Ok(false)
            }
        }
        Type::Vector { ref element_type, .. } => can_derive_partialeq(element_type, parents, ir),
        Type::Str { .. } => Ok(true),
        Type::Handle { .. } => Ok(true),
        Type::Request { .. } => Ok(true),
        Type::Primitive { .. } => Ok(true),
        Type::Identifier { identifier: type_id, .. } => {
            if type_id.is_base_type() {
                return Ok(true);
            }
            match ir.get_declaration(type_id)? {
                Declaration::Const => {
                    let decl = ir.get_const(type_id)?;
                    can_derive_partialeq(&decl._type, parents, ir)
                }
                // enum.rs template always derive PartialEq
                Declaration::Enum { .. } => Ok(true),
                // Protocols are not generated, but this supports some tests.
                Declaration::Interface { .. } => Ok(true),
                Declaration::Struct => {
                    let decl = ir.get_struct(type_id)?;
                    for field in &decl.members {
                        if let Type::Identifier { identifier: field_id, .. } = &field._type {
                            // Circular reference. Skip the check on this field to prevent stack
                            // overflow. It's still possible to derive PartialEq as long as other
                            // fields do not prevent the derive.
                            if field_id == type_id || parents.contains(field_id) {
                                continue;
                            }
                        }
                        parents.insert(type_id.clone());
                        if !can_derive_partialeq(&field._type, parents, ir)? {
                            return Ok(false);
                        }
                        parents.remove(type_id);
                    }
                    Ok(true)
                }
                // Union is never PartialEq.
                Declaration::Union { .. } => Ok(false),
                Declaration::TypeAlias { .. } => {
                    let decl = ir.get_type_alias(type_id)?;
                    let ident = CompoundIdentifier(decl.partial_type_ctor.name.clone());
                    can_derive_partialeq(
                        &Type::Identifier { identifier: ident, nullable: false },
                        parents,
                        ir,
                    )
                }
                _ => Err(anyhow!("Can't evaluate PartialEq for {:?}", type_id)),
            }
        }
    }
}

// This is not the same as partialeq because we derive opaque Debugs for unions
fn can_derive_debug(
    ty: &Type,
    parents: &mut HashSet<CompoundIdentifier>,
    ir: &FidlIr,
) -> Result<bool, Error> {
    match ty {
        Type::Array { ref element_type, ref element_count } => {
            if element_count.0 <= 32 {
                can_derive_debug(element_type, parents, ir)
            } else {
                Ok(false)
            }
        }
        Type::Vector { ref element_type, .. } => can_derive_debug(element_type, parents, ir),
        Type::Str { .. } => Ok(true),
        Type::Handle { .. } => Ok(true),
        Type::Request { .. } => Ok(true),
        Type::Primitive { .. } => Ok(true),
        Type::Identifier { identifier: type_id, .. } => {
            if type_id.is_base_type() {
                return Ok(true);
            }
            match ir.get_declaration(type_id)? {
                Declaration::Const => {
                    let decl = ir.get_const(type_id)?;
                    can_derive_debug(&decl._type, parents, ir)
                }
                // enum.rs template always derive Debug
                Declaration::Enum { .. } => Ok(true),
                // Protocols are not generated, but this supports some tests.
                Declaration::Interface { .. } => Ok(true),
                Declaration::Struct => {
                    let decl = ir.get_struct(type_id)?;
                    for field in &decl.members {
                        if let Type::Identifier { identifier: field_id, .. } = &field._type {
                            // Circular reference. Skip the check on this field to prevent stack
                            // overflow. It's still possible to derive PartialEq as long as other
                            // fields do not prevent the derive.
                            if field_id == type_id || parents.contains(field_id) {
                                continue;
                            }
                        }
                        parents.insert(type_id.clone());
                        if !can_derive_debug(&field._type, parents, ir)? {
                            return Ok(false);
                        }
                        parents.remove(type_id);
                    }
                    Ok(true)
                }
                // union.rs template manually implements Debug.
                Declaration::Union { .. } => Ok(true),
                Declaration::TypeAlias { .. } => {
                    let decl = ir.get_type_alias(type_id)?;
                    let ident = CompoundIdentifier(decl.partial_type_ctor.name.clone());
                    can_derive_debug(
                        &Type::Identifier { identifier: ident, nullable: false },
                        parents,
                        ir,
                    )
                }
                _ => Err(anyhow!("Don't know how to evaluate Debug for {:?}", type_id)),
            }
        }
    }
}

fn primitive_type_to_rust_str(ty: &PrimitiveSubtype) -> Result<String, Error> {
    match ty {
        PrimitiveSubtype::Bool => Ok(String::from("bool")),
        PrimitiveSubtype::Float32 => Ok(String::from("f32")),
        PrimitiveSubtype::Float64 => Ok(String::from("f64")),
        PrimitiveSubtype::Int8 => Ok(String::from("i8")),
        PrimitiveSubtype::Int16 => Ok(String::from("i16")),
        PrimitiveSubtype::Int32 => Ok(String::from("i32")),
        PrimitiveSubtype::Int64 => Ok(String::from("i64")),
        PrimitiveSubtype::Uint8 => Ok(String::from("u8")),
        PrimitiveSubtype::Uint16 => Ok(String::from("u16")),
        PrimitiveSubtype::Uint32 => Ok(String::from("u32")),
        PrimitiveSubtype::Uint64 => Ok(String::from("u64")),
    }
}

fn type_to_rust_str(ty: &Type, ir: &FidlIr) -> Result<String, Error> {
    match ty {
        Type::Array { element_type, element_count } => Ok(format!(
            "[{ty}; {size} as usize]",
            ty = type_to_rust_str(element_type, ir)?,
            size = element_count.0.to_string().to_uppercase()
        )),
        Type::Vector { ref element_type, .. } => type_to_rust_str(element_type, ir),
        Type::Str { maybe_element_count, .. } => match maybe_element_count {
            Some(count) => Ok(format!("[u8; {count} as usize]", count = count.0)),
            None => Ok(String::from("*mut std::ffi::c_void /* String */")),
        },
        Type::Primitive { ref subtype } => primitive_type_to_rust_str(subtype),
        Type::Identifier { identifier, nullable } => {
            if identifier.is_base_type() {
                return Ok(format!("zircon::sys::zx_{}_t", identifier.get_name()));
            }
            match ir.get_declaration(identifier)? {
                Declaration::Const => {
                    let decl = ir.get_const(identifier)?;
                    type_to_rust_str(&decl._type, ir)
                }
                Declaration::Enum => Ok(format!("{}", name = identifier.get_name())),
                // Protocols are not generated, but this supports some tests.
                Declaration::Interface => return Ok(to_c_name(identifier.get_name())),
                Declaration::Struct | Declaration::Union => {
                    if *nullable {
                        Ok(format!("*mut {name}", name = identifier.get_name()))
                    } else {
                        Ok(format!("{name}", name = identifier.get_name()))
                    }
                }
                _ => Err(anyhow!("Can't handle declaration of {:?}", identifier)),
            }
        }
        Type::Handle { .. } => Ok(format!("zircon::sys::zx_handle_t")),
        _ => Err(anyhow!("Can't handle type {:?}", ty)),
    }
}

fn get_base_type_from_alias(alias: &Option<&String>) -> Option<String> {
    if let Some(name) = alias {
        if name.starts_with("zx/") {
            return Some(format!("zircon::sys::zx_{}_t", &name[3..]));
        }
    }
    None
}

impl<'a, W: io::Write> RustBackend<'a, W> {
    fn codegen_enum_decl(
        &self,
        declarations: &Vec<Decl<'_>>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        Ok(declarations
            .iter()
            .filter_map(|decl| match decl {
                Decl::Enum { data } => Some(data),
                _ => None,
            })
            .map(|data| {
                let mut enum_defines = Vec::new();
                let ty = type_to_rust_str(&data._type.to_type(), ir)?;
                for v in &data.members {
                    let c_name = v.name.0.as_str().to_uppercase();
                    let name = if c_name.chars().next().unwrap().is_numeric() {
                        "_".to_string() + c_name.as_str()
                    } else {
                        c_name
                    };
                    let value = match v.value {
                        Constant::Identifier { ref expression, .. } => expression,
                        Constant::Literal { ref expression, .. } => expression,
                        Constant::BinaryOperator { ref expression, .. } => expression,
                    };
                    enum_defines.push(format!(
                        "    pub const {name}: Self = Self({val});",
                        name = name,
                        val = value,
                    ));
                }

                Ok(format!(
                    include_str!("templates/rust/enum.rs"),
                    ty = ty,
                    name = data.name.get_name(),
                    enum_decls = enum_defines.join("\n")
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join(""))
    }

    fn codegen_const_decl(
        &self,
        declarations: &Vec<Decl<'_>>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        Ok(declarations
            .iter()
            .filter_map(|decl| match decl {
                Decl::Const { data } => Some(data),
                _ => None,
            })
            .map(|data| {
                let value = match &data.value {
                    Constant::Identifier { expression, .. } => expression,
                    Constant::Literal { expression, .. } => expression,
                    Constant::BinaryOperator { expression, .. } => expression,
                };
                Ok(format!(
                    "pub const {name}: {ty} = {val};",
                    name = data.name.get_name().to_uppercase(),
                    ty = type_to_rust_str(&data._type, ir)?,
                    val = value,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n"))
    }

    fn codegen_struct_decl(
        &self,
        declarations: &Vec<Decl<'_>>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        Ok(declarations
            .iter()
            .filter_map(|decl| match decl {
                Decl::Struct { data } => Some(data),
                _ => None,
            })
            .map(|data| {
                let mut field_str = Vec::new();
                let alignment = if data.maybe_attributes.has("Packed") { "C, packed" } else { "C" };
                let mut partial_eq = true;
                let mut debug = true;
                let mut parents = HashSet::new();
                for field in &data.members {
                    parents.clear();
                    parents.insert(data.name.clone());
                    if !can_derive_debug(&field._type, &mut parents, ir)? {
                        debug = false;
                    }
                    parents.clear();
                    parents.insert(data.name.clone());
                    if !can_derive_partialeq(&field._type, &mut parents, ir)? {
                        partial_eq = false;
                    }
                    let ty = if let Some(arg_type) = get_base_type_from_alias(
                        &field.experimental_maybe_from_type_alias.as_ref().map(|a| &a.name),
                    ) {
                        arg_type
                    } else {
                        type_to_rust_str(&field._type, ir)?
                    };
                    field_str.push(format!(
                        "    pub {c_name}: {ty},",
                        c_name = field.name.0,
                        ty = ty
                    ));
                }
                Ok(format!(
                    include_str!("templates/rust/struct.rs"),
                    debug = if debug { ", Debug" } else { "" },
                    partial_eq = if partial_eq { ", PartialEq" } else { "" },
                    name = data.name.get_name(),
                    struct_fields = field_str.join("\n"),
                    alignment = alignment,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n"))
    }

    fn codegen_union_decl(
        &self,
        declarations: &Vec<Decl<'_>>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        Ok(declarations
            .iter()
            .filter_map(|decl| match decl {
                Decl::Union { data } => Some(data),
                _ => None,
            })
            .map(|data| {
                let alignment = if data.maybe_attributes.has("Packed") { "C, packed" } else { "C" };

                let field_str = data
                    .members
                    .iter()
                    .filter(|f| f._type != None)
                    .map(|field| {
                        let ty = if let Some(arg_type) = get_base_type_from_alias(
                            &field.experimental_maybe_from_type_alias.as_ref().map(|a| &a.name),
                        ) {
                            arg_type
                        } else {
                            type_to_rust_str(&field._type.as_ref().unwrap(), ir)?
                        };
                        Ok(format!(
                            "    pub {c_name}: {ty},",
                            c_name = to_c_name(&field.name.as_ref().unwrap().0),
                            ty = ty
                        ))
                    })
                    .collect::<Result<Vec<_>, Error>>()?
                    .join("\n");

                Ok(format!(
                    include_str!("templates/rust/union.rs"),
                    name = data.name.get_name(),
                    union_fields = field_str,
                    alignment = alignment,
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n"))
    }

    fn codegen_includes(&self, ir: &FidlIr) -> Result<String, Error> {
        Ok(ir
            .library_dependencies
            .iter()
            .map(|l| &l.name.0)
            .filter(|n| *n != "zx")
            .map(|n| n.replace('.', "_"))
            .map(|n| format!("use banjo_{name} as {name};\nuse {name}::*;\n", name = n))
            .collect::<Vec<_>>()
            .join(""))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for RustBackend<'a, W> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        let decl_order = get_declarations(&ir)?;

        self.w.write_fmt(format_args!(
            include_str!("templates/rust/header.rs"),
            includes = self.codegen_includes(&ir)?,
            primary_namespace = ir.name.0,
        ))?;

        self.w.write_fmt(format_args!(
            include_str!("templates/rust/body.rs"),
            enum_decls = self.codegen_enum_decl(&decl_order, &ir)?,
            constant_decls = self.codegen_const_decl(&decl_order, &ir)?,
            struct_decls = self.codegen_struct_decl(&decl_order, &ir)?,
            union_decls = self.codegen_union_decl(&decl_order, &ir)?,
        ))?;

        Ok(())
    }
}
