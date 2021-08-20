// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        util::{
            array_bounds, for_banjo_transport, get_base_type_from_alias, get_declarations,
            get_doc_comment, is_namespaced, is_table_or_bits, name_buffer, name_size, not_callback,
            primitive_type_to_c_str, to_c_name, Decl, ProtocolType,
        },
        Backend,
    },
    crate::fidl::*,
    anyhow::{anyhow, Context, Error},
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

fn integer_type_to_c_str(ty: &IntegerType) -> Result<String, Error> {
    primitive_type_to_c_str(&ty.to_primitive())
}

fn type_to_c_str(ty: &Type, ir: &FidlIr) -> Result<String, Error> {
    match ty {
        Type::Array { ref element_type, .. } => type_to_c_str(element_type, ir),
        Type::Vector { ref element_type, .. } => type_to_c_str(element_type, ir),
        Type::Str { .. } => Ok(String::from("char*")),
        Type::Primitive { ref subtype } => primitive_type_to_c_str(subtype),
        Type::Identifier { identifier, .. } => match ir
            .get_declaration(identifier)
            .expect(&format!("Could not find declaration for {:?}", identifier))
        {
            Declaration::Struct | Declaration::Union | Declaration::Enum => {
                Ok(format!("{}_t", to_c_name(&identifier.get_name())))
            }
            Declaration::Interface => {
                let c_name = to_c_name(&identifier.get_name());
                if not_callback(identifier, ir)? {
                    return Ok(format!("{}_protocol_t", c_name));
                } else {
                    return Ok(format!("{}_t", c_name));
                }
            }
            _ => Err(anyhow!("Identifier type not handled: {:?}", identifier)),
        },
        Type::Handle { .. } => Ok(String::from("zx_handle_t")),
        _ => Err(anyhow!("Type not handled: {:?}", ty)),
    }
}

fn protocol_to_ops_c_str(id: &CompoundIdentifier, ir: &FidlIr) -> Result<String, Error> {
    if ir.is_protocol(id) {
        return Ok(to_c_name(id.get_name()) + "_protocol_ops_t");
    }
    Err(anyhow!("Identifier does not represent a protocol: {:?}", id))
}

fn integer_constant_to_c_str(
    ty: &IntegerType,
    constant: &Constant,
    ir: &FidlIr,
) -> Result<String, Error> {
    constant_to_c_str(&ty.to_type(), constant, ir)
}

fn constant_to_c_str(ty: &Type, constant: &Constant, ir: &FidlIr) -> Result<String, Error> {
    let value = match constant {
        Constant::Identifier { value, .. } => value,
        Constant::Literal { expression, .. } => expression,
        Constant::BinaryOperator { value, .. } => value,
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
        Type::Identifier { identifier, .. } => match ir
            .get_declaration(identifier)
            .expect(&format!("Can't identify: {:?}", identifier))
        {
            Declaration::Enum => {
                let decl = ir.get_enum(identifier)?;
                return integer_constant_to_c_str(&decl._type, constant, ir);
            }
            Declaration::Bits => {
                let decl = ir.get_bits(identifier)?;
                return constant_to_c_str(&decl._type, constant, ir);
            }
            t => Err(anyhow!("Can't handle this constant identifier: {:?}", t)),
        },
        t => Err(anyhow!("Can't handle this constant type: {:?}", t)),
    }
}

fn struct_attrs_to_c_str(maybe_attributes: &Option<Vec<Attribute>>) -> String {
    if let Some(attributes) = maybe_attributes {
        attributes
            .iter()
            .filter_map(|a| match to_lower_snake_case(a.name.as_ref()).as_str() {
                "packed" => Some("__attribute__ ((packed))"),
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
    alias: &Option<FieldTypeConstructor>,
    ir: &FidlIr,
) -> Result<String, Error> {
    let mut accum = String::new();

    if is_table_or_bits(ty, ir) {
        return Ok(format!(
            "{indent}// Skipping type {ty:?}, see http:://fxbug.dev/82088",
            indent = indent,
            ty = ty,
        ));
    }

    accum.push_str(get_doc_comment(maybe_attributes, 1).as_str());
    let c_name = if preserve_names { String::from(&ident.0) } else { to_c_name(&ident.0) };
    if let Some(arg_type) = get_base_type_from_alias(&alias.as_ref().map(|t| &t.name)) {
        accum.push_str(
            format!("{indent}{ty} {c_name};", indent = indent, ty = arg_type, c_name = c_name)
                .as_str(),
        );
        return Ok(accum);
    }
    let prefix = if maybe_attributes.has("Mutable") { "" } else { "const " };
    let ty_name = type_to_c_str(&ty, ir)?;
    match ty {
        Type::Str { maybe_element_count, .. } => {
            if let Some(count) = maybe_element_count {
                accum.push_str(
                    format!(
                        "{indent}char {c_name}[{size}];",
                        indent = indent,
                        c_name = c_name,
                        size = count.0,
                    )
                    .as_str(),
                );
            } else {
                accum.push_str(
                    format!(
                        "{indent}{prefix}{ty} {c_name};",
                        indent = indent,
                        c_name = c_name,
                        prefix = prefix,
                        ty = ty_name
                    )
                    .as_str(),
                );
            }
        }
        Type::Vector { .. } => {
            let ptr = if maybe_attributes.has("OutOfLineContents") { "*" } else { "" };
            accum.push_str(
                format!(
                    "{indent}{prefix}{ty}{ptr}* {c_name}_{buffer};\n\
                     {indent}size_t {c_name}_{size};",
                    indent = indent,
                    buffer = name_buffer(&maybe_attributes),
                    size = name_size(&maybe_attributes),
                    c_name = c_name,
                    prefix = prefix,
                    ty = ty_name,
                    ptr = ptr,
                )
                .as_str(),
            );
        }
        Type::Array { .. } => {
            let bounds = array_bounds(&ty).unwrap();
            accum.push_str(
                format!(
                    "{indent}{ty} {c_name}{bounds};",
                    indent = indent,
                    c_name = c_name,
                    bounds = bounds,
                    ty = ty_name,
                )
                .as_str(),
            );
        }
        _ => {
            accum.push_str(
                format!("{indent}{ty} {c_name};", indent = indent, c_name = c_name, ty = ty_name)
                    .as_str(),
            );
        }
    }
    Ok(accum)
}

fn get_first_param(method: &Method, ir: &FidlIr) -> Result<(bool, String), Error> {
    if let Some(response) = &method.maybe_response {
        if let Some(param) = response.get(0) {
            if let Some(arg_type) = get_base_type_from_alias(
                &param.experimental_maybe_from_type_alias.as_ref().map(|t| &t.name),
            ) {
                return Ok((true, arg_type));
            }
            if param._type.is_primitive(ir)? {
                return Ok((true, type_to_c_str(&param._type, ir)?));
            }
        }
    }
    Ok((false, "void".to_string()))
}

fn get_in_params(m: &Method, transform: bool, ir: &FidlIr) -> Result<Vec<String>, Error> {
    if m.maybe_request == None {
        return Ok(vec![]);
    }
    m.maybe_request
        .as_ref()
        .unwrap()
        .iter()
        .map(|param| {
            let c_name = to_c_name(&param.name.0);
            if let Some(arg_type) = get_base_type_from_alias(
                &param.experimental_maybe_from_type_alias.as_ref().map(|t| &t.name),
            ) {
                return Ok(format!("{} {}", arg_type, c_name));
            }
            let ty_name = type_to_c_str(&param._type, ir)?;
            match &param._type {
                Type::Identifier { identifier, .. } => {
                    if identifier.is_base_type() {
                        return Ok(format!("{} {}", ty_name, c_name));
                    }
                    match ir.get_declaration(identifier).unwrap() {
                        Declaration::Interface => {
                            if transform && not_callback(identifier, ir)? {
                                let ty_name = protocol_to_ops_c_str(identifier, ir).unwrap();
                                Ok(format!(
                                    "void* {name}_ctx, {ty_name}* {name}_ops",
                                    ty_name = ty_name,
                                    name = c_name
                                ))
                            } else {
                                Ok(format!("const {}* {}", ty_name, c_name))
                            }
                        }
                        Declaration::Struct | Declaration::Union => {
                            let prefix = if param.maybe_attributes.has("InOut")
                                || param.maybe_attributes.has("Mutable")
                            {
                                ""
                            } else {
                                "const "
                            };
                            Ok(format!("{}{}* {}", prefix, ty_name, c_name))
                        }
                        Declaration::Enum => Ok(format!("{} {}", ty_name, c_name)),
                        decl => Err(anyhow!("Unsupported declaration: {:?}", decl)),
                    }
                }
                Type::Str { .. } => Ok(format!("const {} {}", ty_name, c_name)),
                Type::Array { .. } => {
                    let bounds = array_bounds(&param._type).unwrap();
                    Ok(format!(
                        "const {ty} {name}{bounds}",
                        bounds = bounds,
                        ty = ty_name,
                        name = c_name
                    ))
                }
                Type::Vector { .. } => {
                    let ptr = if param.maybe_attributes.has("InnerPointer") { "*" } else { "" };
                    Ok(format!(
                        "const {ty}{ptr}* {name}_{buffer}, size_t {name}_{size}",
                        buffer = name_buffer(&param.maybe_attributes),
                        size = name_size(&param.maybe_attributes),
                        ty = ty_name,
                        ptr = ptr,
                        name = c_name
                    ))
                }
                _ => Ok(format!("{} {}", ty_name, c_name)),
            }
        })
        .collect()
}

fn get_out_params(name: &str, m: &Method, ir: &FidlIr) -> Result<(Vec<String>, String), Error> {
    let protocol_name = to_c_name(name);
    let method_name = to_c_name(&m.name.0);
    if m.maybe_attributes.has("Async") {
        return Ok((
            vec![
                format!(
                    "{protocol_name}_{method_name}_callback callback",
                    protocol_name = protocol_name,
                    method_name = method_name
                ),
                "void* cookie".to_string(),
            ],
            "void".to_string(),
        ));
    }

    let (skip, return_param) = get_first_param(m, ir)?;
    let skip_amt = if skip { 1 } else { 0 };

    Ok((
        m.maybe_response.as_ref()
            .map_or(Vec::new(), |response| {
                response.iter().skip(skip_amt)
                .map(|param| {
                    let c_name = to_c_name(&param.name.0);
                    if let Some(arg_type) = get_base_type_from_alias(
                        &param.experimental_maybe_from_type_alias.as_ref().map(|t| &t.name),
                    ) {
                        return format!("{}* out_{}", arg_type, c_name);
                    }
                    let ty_name = type_to_c_str(&param._type, ir).unwrap();
                    match &param._type {
                        Type::Identifier { nullable, .. } => {
                            let nullable_str = if *nullable { "*" } else { "" };
                            format!("{}{}* out_{}", ty_name, nullable_str, c_name)
                        }
                        Type::Array { .. } => {
                            let bounds = array_bounds(&param._type).unwrap();
                            format!(
                                "{ty} out_{name}{bounds}",
                                bounds = bounds,
                                ty = ty_name,
                                name = c_name
                            )
                        }
                        Type::Vector { .. } => {
                            let buffer_name = name_buffer(&param.maybe_attributes);
                            let size_name = name_size(&param.maybe_attributes);
                            if param.maybe_attributes.has("CalleeAllocated") {
                                format!("{ty}** out_{name}_{buffer}, size_t* {name}_{size}",
                                        buffer = buffer_name,
                                        size = size_name,
                                        ty = ty_name,
                                        name = c_name)
                            } else {
                                format!("{ty}* out_{name}_{buffer}, size_t {name}_{size}, size_t* out_{name}_actual",
                                        buffer = buffer_name,
                                        size = size_name,
                                        ty = ty_name,
                                        name = c_name)
                            }
                        },
                        Type::Str {..} => {
                            format!("{ty} out_{c_name}, size_t {c_name}_capacity",
                                    ty = ty_name, c_name = c_name)
                        }
                        Type::Handle {..} => format!("{}* out_{}", ty_name, c_name),
                        _ => format!("{}* out_{}", ty_name, c_name),
                    }
                }).collect()
            }), return_param))
}

fn get_in_args(m: &Method, _ir: &FidlIr) -> Result<Vec<String>, Error> {
    if let Some(request) = &m.maybe_request {
        return request
            .iter()
            .map(|param| match &param._type {
                Type::Vector { .. } => Ok(format!(
                    "{name}_{buffer}, {name}_{size}",
                    buffer = name_buffer(&param.maybe_attributes),
                    size = name_size(&param.maybe_attributes),
                    name = to_c_name(&param.name.0)
                )),
                _ => Ok(format!("{}", to_c_name(&param.name.0))),
            })
            .collect();
    }
    Ok(Vec::new())
}

fn get_out_args(m: &Method, ir: &FidlIr) -> Result<(Vec<String>, bool), Error> {
    if m.maybe_attributes.has("Async") {
        return Ok((vec!["callback".to_string(), "cookie".to_string()], false));
    }

    let (skip, _) = get_first_param(m, ir)?;
    let skip_amt = if skip { 1 } else { 0 };
    Ok((
        m.maybe_response.as_ref().map_or(Vec::new(), |response| {
            response
                .iter()
                .skip(skip_amt)
                .map(|param| {
                    let c_name = to_c_name(&param.name.0);
                    match &param._type {
                        Type::Vector { .. } => {
                            let buffer_name = name_buffer(&param.maybe_attributes);
                            let size_name = name_size(&param.maybe_attributes);
                            if param.maybe_attributes.has("CalleeAllocated") {
                                format!(
                                    "out_{name}_{buffer}, {name}_{size}",
                                    buffer = buffer_name,
                                    size = size_name,
                                    name = c_name
                                )
                            } else {
                                format!(
                                    "out_{name}_{buffer}, {name}_{size}, out_{name}_actual",
                                    buffer = buffer_name,
                                    size = size_name,
                                    name = c_name
                                )
                            }
                        }
                        Type::Str { .. } => {
                            format!("out_{c_name}, {c_name}_capacity", c_name = c_name)
                        }
                        _ => format!("out_{}", c_name),
                    }
                })
                .collect()
        }),
        skip,
    ))
}

impl<'a, W: io::Write> CBackend<'a, W> {
    fn codegen_enum_decl(&self, data: &Enum, ir: &FidlIr) -> Result<String, Error> {
        let name = &data.name.get_name();
        let enum_defines = data
            .members
            .iter()
            .map(|v| {
                Ok(format!(
                    "#define {c_name}_{v_name} {c_size}",
                    c_name = to_c_name(&name).to_uppercase(),
                    v_name = v.name.0.to_uppercase().trim(),
                    c_size = integer_constant_to_c_str(&data._type, &v.value, ir)?
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

    fn codegen_constant_decl(&self, data: &Const, ir: &FidlIr) -> Result<String, Error> {
        let mut accum = String::new();
        accum.push_str(get_doc_comment(&data.maybe_attributes, 0).as_str());

        let name = data.name.get_name().to_string();
        let namespaced = is_namespaced(&data.maybe_attributes)
            .context(format!("Looking for namespaced attribute on constant {}", name))?;
        let name = if namespaced {
            format!("{}_{}", ir.get_library_name().replace(".", "_"), name)
        } else {
            name
        };

        accum.push_str(
            format!(
                "#define {name} {value}",
                name = name,
                value = constant_to_c_str(&data._type, &data.value, ir)?
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_union_decl(&self, data: &Union) -> Result<String, Error> {
        Ok(format!("typedef union {c_name} {c_name}_t;", c_name = to_c_name(&data.name.get_name())))
    }

    fn codegen_union_def(&self, data: &Union, ir: &FidlIr) -> Result<String, Error> {
        let attrs = struct_attrs_to_c_str(&data.maybe_attributes);
        let members = data
            .members
            .iter()
            .filter_map(|f| {
                if let Some(ty) = &f._type {
                    match ty {
                        Type::Vector { .. } => {
                            Some(Err(anyhow!("unsupported for UnionField: {:?}", f)))
                        }
                        _ => Some(field_to_c_str(
                            &f.maybe_attributes,
                            &ty,
                            &f.name.as_ref().unwrap(),
                            "    ",
                            false,
                            &f.experimental_maybe_from_type_alias,
                            ir,
                        )),
                    }
                } else {
                    None
                }
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        let mut accum = String::new();
        accum.push_str(get_doc_comment(&data.maybe_attributes, 0).as_str());
        accum.push_str(
            format!(
                include_str!("templates/c/struct.h"),
                c_name = to_c_name(&data.name.get_name()),
                decl = "union",
                attrs = if attrs.is_empty() { "".to_string() } else { format!(" {}", attrs) },
                members = members
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_struct_decl(&self, data: &Struct) -> Result<String, Error> {
        Ok(format!(
            "typedef struct {c_name} {c_name}_t;",
            c_name = to_c_name(&data.name.get_name())
        ))
    }

    fn codegen_struct_def(&self, data: &Struct, ir: &FidlIr) -> Result<String, Error> {
        let attrs = struct_attrs_to_c_str(&data.maybe_attributes);
        let preserve_names = data.maybe_attributes.has("PreserveCNames");
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
                    &f.experimental_maybe_from_type_alias,
                    ir,
                )
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        let mut accum = String::new();
        accum.push_str(get_doc_comment(&data.maybe_attributes, 0).as_str());
        accum.push_str(
            format!(
                include_str!("templates/c/struct.h"),
                c_name = to_c_name(&data.name.get_name()),
                decl = "struct",
                attrs = if attrs.is_empty() { "".to_string() } else { format!(" {}", attrs) },
                members = members
            )
            .as_str(),
        );
        Ok(accum)
    }

    fn codegen_protocol_def2(
        &self,
        name: &str,
        methods: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        let fns = methods
            .iter()
            .map(|m| {
                let (out_params, return_param) = get_out_params(name, &m, ir)?;
                let in_params = get_in_params(&m, false, ir)?;

                let params = iter::once("void* ctx".to_string())
                    .chain(in_params)
                    .chain(out_params)
                    .collect::<Vec<_>>()
                    .join(", ");
                Ok(format!(
                    "    {return_param} (*{fn_name})({params});",
                    return_param = return_param,
                    params = params,
                    fn_name = to_c_name(&m.name.0)
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        Ok(format!(include_str!("templates/c/protocol_ops.h"), c_name = to_c_name(name), fns = fns))
    }

    fn codegen_protocol_helper(&self, data: &'a Interface, ir: &FidlIr) -> Result<String, Error> {
        if ProtocolType::from(&data.maybe_attributes) == ProtocolType::Callback
            || !for_banjo_transport(&data.maybe_attributes)
        {
            return Ok("".to_string());
        }
        let name = data.name.get_name();
        data.methods
            .iter()
            .map(|m| {
                let mut accum = String::new();
                accum.push_str(get_doc_comment(&m.maybe_attributes, 0).as_str());

                let (out_params, return_param) = get_out_params(name, &m, ir)?;
                let in_params = get_in_params(&m, true, ir)?;

                let first_param = format!("const {}_protocol_t* proto", to_c_name(name));

                let params = iter::once(first_param)
                    .chain(in_params)
                    .chain(out_params)
                    .collect::<Vec<_>>()
                    .join(", ");

                accum.push_str(
                    format!(
                        "static inline {return_param} {protocol_name}_{fn_name}({params}) {{\n",
                        return_param = return_param,
                        params = params,
                        protocol_name = to_c_name(name),
                        fn_name = to_c_name(&m.name.0)
                    )
                    .as_str(),
                );

                let (out_args, skip) = get_out_args(&m, ir)?;
                let in_args = get_in_args(&m, ir)?;

                if let Some(request) = &m.maybe_request {
                    let proto_args = request
                        .iter()
                        .filter_map(|param| {
                            if let Type::Identifier { ref identifier, .. } = param._type {
                                if not_callback(identifier, ir).ok()? {
                                    return Some((
                                        to_c_name(&param.name.0),
                                        type_to_c_str(&param._type, ir).unwrap(),
                                    ));
                                }
                            }
                            None
                        })
                        .collect::<Vec<_>>();
                    for (name, ty) in proto_args.iter() {
                        accum.push_str(
                            format!(
                                include_str!("templates/c/proto_transform.h"),
                                ty = ty,
                                name = name
                            )
                            .as_str(),
                        );
                    }
                }

                let args = iter::once("proto->ctx".to_string())
                    .chain(in_args)
                    .chain(out_args)
                    .collect::<Vec<_>>()
                    .join(", ");

                let return_statement = if skip { "return " } else { "" };

                accum.push_str(
                    format!(
                        "    {return_statement}proto->ops->{fn_name}({args});\n",
                        return_statement = return_statement,
                        args = args,
                        fn_name = to_c_name(&m.name.0)
                    )
                    .as_str(),
                );
                accum.push_str("}\n");
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }

    fn codegen_protocol_def(&self, data: &'a Interface, ir: &FidlIr) -> Result<String, Error> {
        if !for_banjo_transport(&data.maybe_attributes) {
            return Ok("".to_string());
        }
        let name = data.name.get_name();
        Ok(match ProtocolType::from(&data.maybe_attributes) {
            ProtocolType::Interface | ProtocolType::Protocol => format!(
                include_str!("templates/c/protocol.h"),
                protocol_name = to_c_name(name),
                protocol_def = self.codegen_protocol_def2(name, &data.methods, ir)?,
            ),
            ProtocolType::Callback => {
                let m = data.methods.get(0).ok_or(anyhow!("callback has no methods"))?;
                let (out_params, return_param) = get_out_params(name, &m, ir)?;
                let in_params = get_in_params(&m, false, ir)?;

                let params = iter::once("void* ctx".to_string())
                    .chain(in_params)
                    .chain(out_params)
                    .collect::<Vec<_>>()
                    .join(", ");
                let method = format!(
                    "{return_param} (*{fn_name})({params})",
                    return_param = return_param,
                    params = params,
                    fn_name = to_c_name(&m.name.0)
                );
                format!(
                    include_str!("templates/c/callback.h"),
                    callback_name = to_c_name(name),
                    callback = method,
                )
            }
        })
    }

    fn codegen_async_decls(
        &self,
        name: &String,
        data: &Vec<Method>,
        ir: &FidlIr,
    ) -> Result<String, Error> {
        Ok(data
            .iter()
            .filter(|method| method.maybe_attributes.has("Async"))
            .map(|method| {
                let mut temp_method = method.clone();
                temp_method.maybe_request = method.maybe_response.clone();
                temp_method.maybe_response = None;
                let in_params = get_in_params(&temp_method, true, ir)?;
                let params = iter::once("void* ctx".to_string())
                    .chain(in_params)
                    .collect::<Vec<_>>()
                    .join(", ");
                Ok(format!(
                    "typedef void (*{protocol_name}_{method_name}_callback)({params});\n",
                    protocol_name = name,
                    method_name = to_c_name(&method.name.0),
                    params = params
                ))
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join(""))
    }

    fn codegen_protocol_decl(&self, data: &'a Interface, ir: &FidlIr) -> Result<String, Error> {
        if !for_banjo_transport(&data.maybe_attributes) {
            return Ok("".to_string());
        }
        let name = to_c_name(&data.name.get_name());
        Ok(match ProtocolType::from(&data.maybe_attributes) {
            ProtocolType::Interface | ProtocolType::Protocol => format!(
                "{async_decls}typedef struct {c_name}_protocol {c_name}_protocol_t;\n\
                 typedef struct {c_name}_protocol_ops {c_name}_protocol_ops_t;",
                async_decls = self.codegen_async_decls(&name, &data.methods, ir)?,
                c_name = name
            ),
            ProtocolType::Callback => format!("typedef struct {c_name} {c_name}_t;", c_name = name),
        })
    }

    fn codegen_alias_decl(&self, data: &TypeAlias, _ir: &FidlIr) -> Result<String, Error> {
        match data.partial_type_ctor.name.as_str() {
            "array" => Ok("".to_string()),
            "vector" => Ok("".to_string()),
            _ => Ok(format!(
                "typedef {from}_t {to}_t;",
                to = to_c_name(&data.name.get_name()),
                from =
                    to_c_name(&CompoundIdentifier(data.partial_type_ctor.name.clone()).get_name()),
            )),
        }
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
}

impl<'a, W: io::Write> Backend<'a, W> for CBackend<'a, W> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/c/header.h"),
            includes = self.codegen_includes(&ir)?,
            primary_namespace = ir.name.0
        ))?;

        let decl_order = get_declarations(&ir)?;

        let declarations = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Const { data } => Some(self.codegen_constant_decl(data, &ir)),
                Decl::Enum { data } => Some(self.codegen_enum_decl(data, &ir)),
                Decl::Interface { data } => Some(self.codegen_protocol_decl(data, &ir)),
                Decl::Struct { data } => Some(self.codegen_struct_decl(data)),
                Decl::TypeAlias { data } => Some(self.codegen_alias_decl(data, &ir)),
                Decl::Union { data } => Some(self.codegen_union_decl(data)),
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        let definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Interface { data } => Some(self.codegen_protocol_def(data, &ir)),
                Decl::Struct { data } => Some(self.codegen_struct_def(data, &ir)),
                Decl::Union { data } => Some(self.codegen_union_def(data, &ir)),
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        let helpers = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Interface { data } => Some(self.codegen_protocol_helper(data, &ir)),
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        self.w.write_fmt(format_args!(
            include_str!("templates/c/body.h"),
            declarations = declarations,
            definitions = definitions,
            helpers = helpers,
        ))?;
        Ok(())
    }
}
