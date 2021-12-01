// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        util::{get_declarations, is_table_or_bits, name_buffer, name_size, to_c_name, Decl},
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
            // FIDL IR doesn't have enough type information, so we conservatively assume we cannot
            // derive ParitalEq.
            if ir.is_external_decl(type_id)? {
                return Ok(false);
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

fn type_to_rust_str(
    ty: &Type,
    maybe_attributes: &Option<Vec<Attribute>>,
    ir: &FidlIr,
) -> Result<String, Error> {
    match ty {
        Type::Array { element_type, element_count } => Ok(format!(
            "[{ty}; {size} as usize]",
            ty = type_to_rust_str(element_type, maybe_attributes, ir)?,
            size = element_count.0.to_string().to_uppercase()
        )),
        Type::Vector { ref element_type, .. } => {
            type_to_rust_str(element_type, maybe_attributes, ir)
        }
        Type::Str { maybe_element_count, .. } => match maybe_element_count {
            Some(count) => Ok(format!("[u8; {count} as usize]", count = count.0)),
            None => {
                let mutable = if maybe_attributes.has("InOut") || maybe_attributes.has("Mutable") {
                    "mut"
                } else {
                    "const"
                };
                Ok(format!("*{mutable} std::os::raw::c_char", mutable = mutable))
            }
        },
        Type::Primitive { ref subtype } => primitive_type_to_rust_str(subtype),
        Type::Identifier { identifier, nullable } => {
            if identifier.is_base_type() {
                return Ok(format!("zircon_types::zx_{}_t", identifier.get_name()));
            }
            match ir.get_declaration(identifier)? {
                Declaration::Const => {
                    let decl = ir.get_const(identifier)?;
                    type_to_rust_str(&decl._type, maybe_attributes, ir)
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
        Type::Handle { .. } => Ok(format!("zircon_types::zx_handle_t")),
        _ => Err(anyhow!("Can't handle type {:?}", ty)),
    }
}

fn field_to_rust_str(field: &StructMember, ir: &FidlIr) -> Result<String, Error> {
    let c_name = &field.name.0;
    let maybe_attributes = &field.maybe_attributes;

    match field._type {
        Type::Array { .. }
        | Type::Str { .. }
        | Type::Primitive { .. }
        | Type::Identifier { .. }
        | Type::Handle { .. } => Ok(format!(
            "    pub {c_name}: {ty},",
            c_name = c_name,
            ty = type_to_rust_str(&field._type, maybe_attributes, ir)?
        )),
        Type::Vector { ref element_type, .. } => {
            let out_of_line = if maybe_attributes.has("OutOfLineContents") { "*mut " } else { "" };
            let mutable = if maybe_attributes.has("Mutable") { "mut" } else { "const" };
            Ok(format!(
                "{indent}pub {c_name}_{buffer}: *{mutable} {out_of_line}{ty},\
                 \n{indent}pub {c_name}_{size}: usize,",
                indent = "    ",
                buffer = name_buffer(&maybe_attributes),
                size = name_size(&maybe_attributes),
                mutable = mutable,
                out_of_line = out_of_line,
                c_name = c_name,
                ty = type_to_rust_str(element_type, maybe_attributes, ir)?
            ))
        }
        _ => Err(anyhow!("Can't handle type {:?}", field._type)),
    }
}

fn get_base_type_from_alias(alias: &Option<&String>) -> Option<String> {
    if let Some(name) = alias {
        if name.starts_with("zx/") {
            return Some(format!("zircon_types::zx_{}_t", &name[3..]));
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
                let ty = type_to_rust_str(&data._type.to_type(), &data.maybe_attributes, ir)?;
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
                    ty = type_to_rust_str(&data._type, &data.maybe_attributes, ir)?,
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
                let mut parents = HashSet::new();
                for field in &data.members {
                    if is_table_or_bits(&field._type, ir) {
                        field_str.push(format!(
                            "    // Skipping type {:?}, see http:://fxbug.dev/82088",
                            &field._type
                        ));
                        continue;
                    }
                    parents.clear();
                    parents.insert(data.name.clone());
                    parents.clear();
                    parents.insert(data.name.clone());
                    if !can_derive_partialeq(&field._type, &mut parents, ir)? {
                        partial_eq = false;
                    }
                    if let Some(arg_type) = get_base_type_from_alias(
                        &field.experimental_maybe_from_type_alias.as_ref().map(|a| &a.name),
                    ) {
                        field_str.push(format!(
                            "    pub {c_name}: {ty},",
                            c_name = field.name.0,
                            ty = arg_type
                        ));
                    } else {
                        field_str.push(field_to_rust_str(&field, ir)?);
                    };
                }
                Ok(format!(
                    include_str!("templates/rust/struct.rs"),
                    debug = ", Debug",
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
                            type_to_rust_str(
                                &field._type.as_ref().unwrap(),
                                &field.maybe_attributes,
                                ir,
                            )?
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

fn has_zircon_dep(ir: &FidlIr) -> bool {
    ir.library_dependencies.iter().find(|library| library.name.0.as_str() == "zx").is_some()
}

impl<'a, W: io::Write> Backend<'a, W> for RustBackend<'a, W> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        let decl_order = get_declarations(&ir)?;

        let zircon_include =
            if has_zircon_dep(&ir) { "use fuchsia_zircon_types as zircon_types;" } else { "" };

        self.w.write_fmt(format_args!(
            include_str!("templates/rust/header.rs"),
            zircon_include = zircon_include,
            includes = self.codegen_includes(&ir)?,
            primary_namespace = ir.name.0,
        ))?;

        if ir.name.0 != "zx" {
            self.w.write_fmt(format_args!(
                include_str!("templates/rust/body.rs"),
                enum_decls = self.codegen_enum_decl(&decl_order, &ir)?,
                constant_decls = self.codegen_const_decl(&decl_order, &ir)?,
                struct_decls = self.codegen_struct_decl(&decl_order, &ir)?,
                union_decls = self.codegen_union_decl(&decl_order, &ir)?,
            ))?;
        }

        Ok(())
    }
}
