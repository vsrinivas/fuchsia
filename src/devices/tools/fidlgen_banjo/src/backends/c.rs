// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        util::{extract_name, to_c_name},
        Backend,
    },
    crate::fidl::{self, *},
    anyhow::{anyhow, Error},
    std::io,
    std::iter,
};

pub struct CBackend<'a, W: io::Write> {
    // Note: a mutable reference is used here instead of an owned object in
    // order to facilitate testing.
    w: &'a mut W,
}

impl<'a, W: io::Write> CBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        CBackend { w }
    }
}

enum Decl<'a> {
    Const { data: &'a fidl::Const },
    Enum { data: &'a fidl::Enum },
    Struct { data: &'a fidl::Struct },
}

fn get_doc_comment(maybe_attrs: &Option<Vec<Attribute>>, tabs: usize) -> String {
    if let Some(attrs) = maybe_attrs {
        for attr in attrs.iter() {
            if attr.name == "Doc" {
                if attr.value.is_empty() {
                    continue;
                }
                let tabs: String = iter::repeat(' ').take(tabs * 4).collect();
                return attr
                    .value
                    .trim_end()
                    .split("\n")
                    .map(|line| format!("{}//{}\n", tabs, line))
                    .collect();
            }
        }
    }
    "".to_string()
}

fn integer_type_to_primitive(ty: &IntegerType) -> PrimitiveSubtype {
    match ty {
        IntegerType::Int8 => PrimitiveSubtype::Int8,
        IntegerType::Int16 => PrimitiveSubtype::Int16,
        IntegerType::Int32 => PrimitiveSubtype::Int32,
        IntegerType::Int64 => PrimitiveSubtype::Int64,
        IntegerType::Uint8 => PrimitiveSubtype::Uint8,
        IntegerType::Uint16 => PrimitiveSubtype::Uint16,
        IntegerType::Uint32 => PrimitiveSubtype::Uint32,
        IntegerType::Uint64 => PrimitiveSubtype::Uint64,
    }
}

fn primitive_type_to_c_str(ty: &PrimitiveSubtype) -> Result<String, Error> {
    match ty {
        PrimitiveSubtype::Bool => Ok(String::from("bool")),
        PrimitiveSubtype::Float64 => Ok(String::from("double")),
        PrimitiveSubtype::Float32 => Ok(String::from("float")),
        PrimitiveSubtype::Int8 => Ok(String::from("int8_t")),
        PrimitiveSubtype::Int16 => Ok(String::from("int16_t")),
        PrimitiveSubtype::Int32 => Ok(String::from("int32_t")),
        PrimitiveSubtype::Int64 => Ok(String::from("int64_t")),
        PrimitiveSubtype::Uint8 => Ok(String::from("uint8_t")),
        PrimitiveSubtype::Uint16 => Ok(String::from("uint16_t")),
        PrimitiveSubtype::Uint32 => Ok(String::from("uint32_t")),
        PrimitiveSubtype::Uint64 => Ok(String::from("uint64_t")),
    }
}

fn integer_type_to_c_str(ty: &IntegerType) -> Result<String, Error> {
    primitive_type_to_c_str(&integer_type_to_primitive(ty))
}

fn type_to_c_str(ty: &Type, decl_map: &DeclarationsMap) -> Result<String, Error> {
    match ty {
        Type::Array { ref element_type, .. } => type_to_c_str(element_type, decl_map),
        Type::Vector { ref element_type, .. } => type_to_c_str(element_type, decl_map),
        Type::Str { .. } => Ok(String::from("char*")),
        Type::Primitive { ref subtype } => primitive_type_to_c_str(subtype),
        Type::Identifier { identifier, .. } => match decl_map.0.get(identifier).unwrap() {
            Declaration::Struct | Declaration::Union | Declaration::Enum => {
                Ok(format!("{}_t", to_c_name(&extract_name(identifier))))
            }
            _ => Err(anyhow!("Identifier type not handled: {:?}", identifier)),
        },
        _ => Err(anyhow!("Type not handled: {:?}", ty)),
    }
}

fn integer_constant_to_c_str(ty: &IntegerType, constant: &Constant) -> Result<String, Error> {
    constant_to_c_str(&Type::Primitive { subtype: integer_type_to_primitive(ty) }, constant)
}

fn constant_to_c_str(ty: &Type, constant: &Constant) -> Result<String, Error> {
    let value = match constant {
        Constant::Identifier { expression, .. } => expression,
        Constant::Literal { expression, .. } => expression,
        Constant::BinaryOperator { expression, .. } => expression,
    };
    match ty {
        Type::Primitive { subtype } => match subtype {
            PrimitiveSubtype::Bool => Ok(value.clone()),
            PrimitiveSubtype::Int8 => Ok(String::from(format!("INT8_C({})", value))),
            PrimitiveSubtype::Int16 => Ok(String::from(format!("INT16_C({})", value))),
            PrimitiveSubtype::Int32 => Ok(String::from(format!("INT32_C({})", value))),
            PrimitiveSubtype::Int64 => Ok(String::from(format!("INT64_C({})", value))),
            PrimitiveSubtype::Uint8 => Ok(String::from(format!("UINT8_C({})", value))),
            PrimitiveSubtype::Uint16 => Ok(String::from(format!("UINT16_C({})", value))),
            PrimitiveSubtype::Uint32 => Ok(String::from(format!("UINT32_C({})", value))),
            PrimitiveSubtype::Uint64 => Ok(String::from(format!("UINT64_C({})", value))),
            t => Err(anyhow!("Can't handle this primitive type: {:?}", t)),
        },
        t => Err(anyhow!("Can't handle this constant type: {:?}", t)),
    }
}

fn has_attribute(maybe_attributes: &Option<Vec<Attribute>>, name: &str) -> bool {
    match maybe_attributes {
        Some(attrs) => attrs.iter().any(|a| a.name == name),
        None => false,
    }
}

fn struct_attrs_to_c_str(maybe_attributes: &Option<Vec<Attribute>>) -> String {
    if let Some(attributes) = maybe_attributes {
        attributes
            .iter()
            .filter_map(|a| match a.name.as_ref() {
                "Packed" => Some("__attribute__ ((packed))"),
                _ => None,
            })
            .collect::<Vec<_>>()
            .join(" ")
    } else {
        String::from("")
    }
}

fn field_to_c_str(
    maybe_attributes: &Option<Vec<Attribute>>,
    ty: &Type,
    ident: &Identifier,
    indent: &str,
    preserve_names: bool,
    decl_map: &DeclarationsMap,
) -> Result<String, Error> {
    let mut accum = String::new();
    accum.push_str(get_doc_comment(maybe_attributes, 1).as_str());
    let _prefix = if has_attribute(maybe_attributes, "Mutable") { "" } else { "const " };
    let c_name = if preserve_names { String::from(&ident.0) } else { to_c_name(&ident.0) };
    match ty {
        _ => {
            accum.push_str(
                format!(
                    "{indent}{ty} {c_name};",
                    indent = indent,
                    c_name = c_name,
                    ty = type_to_c_str(&ty, decl_map)?
                )
                .as_str(),
            );
        }
    }
    Ok(accum)
}

impl<'a, W: io::Write> CBackend<'a, W> {
    fn codegen_enum_decl(&self, data: &Enum) -> Result<String, Error> {
        let name = extract_name(&data.name);
        let enum_defines = data
            .members
            .iter()
            .map(|v| {
                Ok(format!(
                    "#define {c_name}_{v_name} {c_size}",
                    c_name = to_c_name(&name).to_uppercase(),
                    v_name = v.name.0.to_uppercase().trim(),
                    c_size = integer_constant_to_c_str(&data._type, &v.value)?
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        Ok(format!(
            "typedef {ty} {c_name}_t;\n{enum_defines}",
            c_name = to_c_name(&name),
            ty = integer_type_to_c_str(&data._type)?,
            enum_defines = enum_defines
        ))
    }

    fn codegen_constant_decl(&self, data: &Const) -> Result<String, Error> {
        let mut accum = String::new();
        accum.push_str(get_doc_comment(&data.maybe_attributes, 0).as_str());
        accum.push_str(
            format!(
                "#define {name} {value}",
                name = extract_name(&data.name),
                value = constant_to_c_str(&data._type, &data.value)?
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_struct_decl(&self, data: &Struct) -> Result<String, Error> {
        Ok(format!(
            "typedef struct {c_name} {c_name}_t;",
            c_name = to_c_name(&extract_name(&data.name))
        ))
    }

    fn codegen_struct_def(
        &self,
        data: &Struct,
        decl_map: &DeclarationsMap,
    ) -> Result<String, Error> {
        let attrs = struct_attrs_to_c_str(&data.maybe_attributes);
        let preserve_names = has_attribute(&data.maybe_attributes, "PreserveCNames");
        let members = data
            .members
            .iter()
            .map(|f| {
                field_to_c_str(
                    &f.maybe_attributes,
                    &f._type,
                    &f.name,
                    "    ",
                    preserve_names,
                    decl_map,
                )
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        let mut accum = String::new();
        accum.push_str(get_doc_comment(&data.maybe_attributes, 0).as_str());
        accum.push_str(
            format!(
                include_str!("templates/c/struct.h"),
                c_name = to_c_name(&extract_name(&data.name)),
                decl = "struct",
                attrs = if attrs.is_empty() { "".to_string() } else { format!(" {}", attrs) },
                members = members
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_includes(&self, ir: &FidlIr) -> Result<String, Error> {
        Ok(ir
            .library_dependencies
            .iter()
            .map(|l| &l.name.0)
            .filter(|n| *n != "zx")
            .map(|n| n.replace('.', "/") + "/c/banjo")
            .map(|n| format!("#include <{}.h>", n))
            .collect::<Vec<_>>()
            .join("\n"))
    }

    fn get_declarations<'b>(&self, ir: &'b FidlIr) -> Result<Vec<Decl<'b>>, Error> {
        Ok(ir
            .declaration_order
            .iter()
            .filter_map(|ident| {
                let decl = ir.declarations.0.get(ident)?;
                match decl {
                    Declaration::Const => Some(Decl::Const {
                        data: ir.const_declarations.iter().filter(|c| c.name == *ident).nth(0)?,
                    }),
                    Declaration::Enum => Some(Decl::Enum {
                        data: ir.enum_declarations.iter().filter(|e| e.name == *ident).nth(0)?,
                    }),
                    Declaration::Struct => Some(Decl::Struct {
                        data: ir.struct_declarations.iter().filter(|e| e.name == *ident).nth(0)?,
                    }),
                    _ => None,
                }
            })
            .collect())
    }
}

impl<'a, W: io::Write> Backend<'a, W> for CBackend<'a, W> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/c/header.h"),
            includes = self.codegen_includes(&ir)?,
            primary_namespace = ir.name.0
        ))?;

        let decl_order = self.get_declarations(&ir)?;

        let declarations = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Const { data } => Some(self.codegen_constant_decl(data)),
                Decl::Enum { data } => Some(self.codegen_enum_decl(data)),
                Decl::Struct { data } => Some(self.codegen_struct_decl(data)),
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        let definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Struct { data } => Some(self.codegen_struct_def(data, &ir.declarations)),
                _ => None,
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
