// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ast::{self, BanjoAst, Decl},
    crate::backends::util::{to_c_name, ValuedAttributes},
    crate::backends::Backend,
    anyhow::{format_err, Error},
    std::collections::HashMap,
    std::io,
};

pub struct SyzkallerBackend<'a, W: io::Write> {
    w: &'a mut W,
    // These store info about args for containers(methods, structs, unions).
    size_to_buffer: HashMap<String, (String, String)>,
    arg_types: ValuedAttributes,
    array_sizes: ValuedAttributes,
    value_sets: ValuedAttributes,
    type_sets: ValuedAttributes,
}

/// Translates to underlying type(without using resources) in string-format.
/// e.g. HandleTy::Process will be translated to int32 instead of zx_process.
fn ty_to_underlying_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    match ty {
        ast::Ty::Bool => Ok(String::from("bool8")), // type bool8 int8[0:1]
        ast::Ty::Int8 => Ok(String::from("int8")),
        ast::Ty::Int16 => Ok(String::from("int16")),
        ast::Ty::Int32 => Ok(String::from("int32")),
        ast::Ty::Int64 => Ok(String::from("int64")),
        ast::Ty::UInt8 => Ok(String::from("int8")),
        ast::Ty::UInt16 => Ok(String::from("int16")),
        ast::Ty::UInt32 => Ok(String::from("int32")),
        ast::Ty::UInt64 => Ok(String::from("int64")),
        ast::Ty::USize => Ok(String::from("intptr")),
        ast::Ty::Voidptr => Ok(String::from("int8")),
        ast::Ty::Handle { .. } => Ok(String::from("int32")),
        ast::Ty::Identifier { id, .. } => {
            if id.is_base_type() {
                let resolved_type = ast.id_to_type(id);
                ty_to_underlying_str(ast, &resolved_type)
            } else {
                match ast.id_to_type(id) {
                    ast::Ty::Struct | ast::Ty::Union | ast::Ty::Enum => {
                        Ok(format!("{}", to_c_name(id.name())))
                    }
                    _ => {
                        Err(format_err!("unsupported ident_type in ty_to_underlying_str {:?}", id))
                    }
                }
            }
        }
        t => Err(format_err!("unknown type in ty_to_underlying_str {:?}", t)),
    }
}

/// Translates to Syzkaller type(using resources) in string-format.
/// e.g. HandleTy::Process will be translated to zx_process instead of int32.
fn ty_to_syzkaller_str(ast: &ast::BanjoAst, ty: &ast::Ty) -> Result<String, Error> {
    if !ast.is_resource(ty) {
        ty_to_underlying_str(ast, ty)
    } else {
        match ty {
            ast::Ty::Identifier { id, .. } => {
                if id.is_base_type() {
                    let (_ns, name) = id.fq();
                    Ok(format!("zx_{name}", name = name))
                } else {
                    Err(format_err!("unsupported ident_type in ident_to_resource_str {:?}", id))
                }
            }
            ast::Ty::Handle { ty, .. } => {
                let handle_ty_str = match ty {
                    ast::HandleTy::Handle => "zx_handle",
                    ast::HandleTy::Process => "zx_process",
                    ast::HandleTy::Thread => "zx_thread",
                    ast::HandleTy::Vmo => "zx_vmo",
                    ast::HandleTy::Channel => "zx_channel",
                    ast::HandleTy::Event => "zx_event",
                    ast::HandleTy::Port => "zx_port",
                    ast::HandleTy::Interrupt => "zx_interrupt",
                    ast::HandleTy::Log => "zx_log",
                    ast::HandleTy::Socket => "zx_socket",
                    ast::HandleTy::Resource => "zx_resource",
                    ast::HandleTy::EventPair => "zx_eventpair",
                    ast::HandleTy::Job => "zx_job",
                    ast::HandleTy::Vmar => "zx_vmar",
                    ast::HandleTy::Fifo => "zx_fifo",
                    ast::HandleTy::Guest => "zx_guest",
                    ast::HandleTy::Timer => "zx_timer",
                    ast::HandleTy::Bti => "zx_bti",
                    ast::HandleTy::Profile => "zx_profile",
                    ast::HandleTy::DebugLog => "zx_debuglog",
                    ast::HandleTy::VCpu => "zx_vcpu",
                    ast::HandleTy::IoMmu => "zx_iommu",
                    ast::HandleTy::Pager => "zx_pager",
                    ast::HandleTy::Pmt => "zx_pmt",
                    ast::HandleTy::Clock => "zx_clock",
                    ast::HandleTy::MsiAllocation => "zx_msi_allocation",
                    ast::HandleTy::MsiInterrupt => "zx_msi_interrupt",
                };
                Ok(String::from(handle_ty_str))
            }
            t => Err(format_err!("undeclared resource in ty_to_syzkaller_str {:?}", t)),
        }
    }
}

/// Handles `dependency` attributes on a method, where the attribute values are
//  formatted as pairs. For example, with an annotation like this on a menthod m:
/// ```
/// [dependency="p0 ABC",
///  dependency="p0 XYZ"]
/// ```
/// where p0 is linked to an enum and ABC, XYZ are linked to unions,
/// a call to get_dependent_arg_names(&m.attributes) would return {p0: [ABC, XYZ]}.
fn get_dependent_arg_names(attrs: &ast::Attrs) -> (String, Vec<String>) {
    let mut key_arg_name = String::default();
    let mut value_arg_names: Vec<String> = Vec::default();
    for attr in attrs.0.iter() {
        if attr.key == "dependency" {
            if let Some(ref val) = attr.val {
                let parts: Vec<&str> = val.split(' ').collect();
                if parts.len() != 2 {
                    panic!("dependency annotation requires two arguments");
                }
                if !key_arg_name.is_empty() && key_arg_name.to_string() != parts[0].to_string() {
                    panic!("`dependency` arg can only have 1 unique key");
                }
                key_arg_name = parts[0].to_string();
                value_arg_names.push(parts[1].to_string());
            }
        }
    }
    (key_arg_name, value_arg_names)
}

impl<'a, W: io::Write> SyzkallerBackend<'a, W> {
    pub fn new(w: &'a mut W) -> Self {
        SyzkallerBackend {
            w: w,
            size_to_buffer: HashMap::new(),
            arg_types: ValuedAttributes::default(),
            array_sizes: ValuedAttributes::default(),
            value_sets: ValuedAttributes::default(),
            type_sets: ValuedAttributes::default(),
        }
    }

    fn update_cache(&mut self, name: &str, ty: &ast::Ty, size: &ast::Constant) -> () {
        match ty {
            ast::Ty::Voidptr => self
                .size_to_buffer
                .insert(size.to_string(), (name.to_string(), String::from("voidptr"))),
            _ => self.size_to_buffer.insert(size.to_string(), (name.to_string(), "".to_string())),
        };
    }

    /// Translates an arg(name, type) to a Syzkaller-format string.
    fn arg_to_syzkaller_str(
        &mut self,
        ast: &ast::BanjoAst,
        (name, ty): (&str, &ast::Ty),
        sep: &str,
    ) -> Result<String, Error> {
        let direction = self.arg_types.get_arg(name);
        let value_set = self.value_sets.get_arg(name);
        match ty {
            ast::Ty::UInt32 => match self.size_to_buffer.get(name) {
                Some((assoc_buffer, _)) => {
                    // Rule: buffer array<voidptr>:count with type(count) = uint32 means:
                    // count of elements in the buffer
                    // Example: zx_job_set_policy()
                    Ok(format!("{}{}len[{}]", name, sep, assoc_buffer))
                }
                None => {
                    if !value_set.is_empty() {
                        Ok(format!("{}{}flags[{}]", to_c_name(name), sep, to_c_name(&value_set)))
                    } else {
                        Ok(format!("{}{}{}", to_c_name(name), sep, String::from("int32")))
                    }
                }
            },
            ast::Ty::USize => match self.size_to_buffer.get(name) {
                Some((assoc_buffer, ty)) => match ty.as_str() {
                    // Rule: buffer array<voidptr>:buffer_size with type(buffer_size) = usize means:
                    // bytesize of contents in the buffer
                    // Example: object_get_info()
                    "voidptr" => Ok(format!("{}{}bytesize[{}]", name, sep, assoc_buffer)),
                    _ => Ok(format!("{}{}len[{}]", name, sep, assoc_buffer)),
                },
                None => Ok(format!("{}{}{}", to_c_name(name), sep, String::from("intptr"))),
            },
            ast::Ty::Str { size, nullable } => {
                if *nullable {
                    panic!("string cannot be nullable");
                }
                match size {
                    Some(size) => {
                        if !direction.is_empty() {
                            let resolved_type = match size.to_string().as_str() {
                                "1" | "N" => panic!(
                                    "unsupported length argument {:?} for string",
                                    size.to_string().as_str()
                                ),
                                _ => format!(
                                    "ptr[{direction}, string]",
                                    direction = to_c_name(&direction)
                                ),
                            };
                            self.size_to_buffer
                                .insert(size.to_string(), (name.to_string(), "".to_string()));
                            Ok(format!("{}{}{}", to_c_name(name), sep, resolved_type))
                        } else {
                            panic!(
                                "missing 'argtype' attribute: string direction must be specified"
                            )
                        }
                    }
                    None => panic!("string must have fixed length"),
                }
            }
            ast::Ty::Array { ty, size } => {
                if !direction.is_empty() {
                    let resolved_type = match size.to_string().as_str() {
                        "1" => format!(
                            "ptr[{}, {}]",
                            to_c_name(&direction),
                            ty_to_syzkaller_str(ast, ty).unwrap()
                        ),
                        "N" => {
                            let val = self.array_sizes.get_arg(name); // count*elem_size
                            let parts: Vec<&str> = val.split('*').collect();
                            let count = parts[0];
                            self.update_cache(name, ty, &ast::Constant::from_str(&count));
                            format!(
                                "ptr[{}, array[{}]]",
                                to_c_name(&direction),
                                ty_to_syzkaller_str(ast, ty).unwrap()
                            )
                        }
                        _ => match size.to_string().as_str().parse::<usize>() {
                            // fixed/constant array length
                            Ok(n) => {
                                format!("array[{}, {}]", ty_to_syzkaller_str(ast, ty).unwrap(), n)
                            }
                            _ => {
                                self.update_cache(name, ty, size);
                                format!(
                                    "ptr[{}, array[{}]]",
                                    to_c_name(&direction),
                                    ty_to_syzkaller_str(ast, ty).unwrap()
                                )
                            }
                        },
                    };
                    Ok(format!("{}{}{}", to_c_name(name), sep, resolved_type))
                } else {
                    panic!("missing 'argtype' attribute: array direction must be specified")
                }
            }
            _ => {
                if !value_set.is_empty() {
                    Ok(format!("{}{}flags[{}]", to_c_name(name), sep, to_c_name(&value_set)))
                } else {
                    let translated_type = ty_to_syzkaller_str(ast, ty).unwrap();
                    Ok(format!("{}{}{}", to_c_name(name), sep, translated_type))
                }
            }
        }
    }

    fn get_in_params(&mut self, m: &ast::Method, ast: &BanjoAst) -> Result<Vec<String>, Error> {
        m.in_params
            .iter()
            .map(|(name, ty)| self.arg_to_syzkaller_str(ast, (name, ty), " "))
            .collect()
    }

    fn get_first_param(
        &mut self,
        ast: &BanjoAst,
        method: &ast::Method,
    ) -> Result<(bool, String), Error> {
        if method
            .out_params
            .get(0)
            .map_or(false, |p| p.1.is_primitive(&ast) && ast.is_resource(&p.1))
        {
            // Return parameter if a primitive type and a resource.
            Ok((true, ty_to_syzkaller_str(ast, &method.out_params[0].1)?))
        } else if method.out_params.get(0).map_or(false, |p| p.1.is_primitive(&ast)) {
            // Primitive but not a resource
            Ok((true, String::default()))
        } else {
            Ok((false, String::default()))
        }
    }

    fn get_out_params(
        &mut self,
        m: &ast::Method,
        ast: &BanjoAst,
    ) -> Result<(Vec<String>, String), Error> {
        let (skip, return_param) = self.get_first_param(ast, m)?;
        let skip_amt = if skip { 1 } else { 0 };

        Ok((
            m.out_params
                .iter()
                .skip(skip_amt)
                .map(|(name, ty)| {
                    let resolved_type = format!(
                        "ptr[{}, {}]",
                        String::from("out"),
                        ty_to_syzkaller_str(ast, ty).unwrap()
                    );
                    format!("{} {}", to_c_name(name), resolved_type)
                })
                .collect(),
            return_param,
        ))
    }

    fn codegen_resource_def(
        &mut self,
        ty: &ast::Ty,
        values: &Vec<ast::Constant>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        let mut special_values = String::new();
        if !values.is_empty() {
            special_values.push_str(": ");
            special_values.push_str(
                values
                    .iter()
                    .map(|cons| match cons {
                        ast::Constant(val) => val.to_owned(),
                    })
                    .collect::<Vec<_>>()
                    .join(", ")
                    .as_str(),
            );
        }
        Ok(format!(
            "resource {identifier}[{underlying_type}]{values}",
            identifier = ty_to_syzkaller_str(ast, &ty).unwrap(),
            underlying_type = ty_to_underlying_str(ast, &ty).unwrap(),
            values = special_values
        ))
    }

    fn codegen_struct_def(
        &mut self,
        attributes: &ast::Attrs,
        name: &ast::Ident,
        fields: &Vec<ast::StructField>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        self.size_to_buffer = HashMap::new();
        self.arg_types = ValuedAttributes::new(attributes, "argtype");
        self.array_sizes = ValuedAttributes::new(attributes, "arraysize");
        self.value_sets = ValuedAttributes::new(attributes, "valueset");
        self.type_sets = ValuedAttributes::new(attributes, "typeset");
        let struct_fields = fields
            .iter()
            .map(|field| match field {
                ast::StructField { attributes: _, ty, ident, val: _ } => format!(
                    "\t{struct_field}",
                    struct_field =
                        self.arg_to_syzkaller_str(ast, (ident.name(), ty), "\t").unwrap()
                ),
            })
            .collect::<Vec<_>>()
            .join("\n");
        Ok(format!(
            "{struct_name} {{\n{struct_fields}\n}}",
            struct_name = to_c_name(name.name()),
            struct_fields = struct_fields
        ))
    }

    fn codegen_union_def(
        &mut self,
        attributes: &ast::Attrs,
        name: &ast::Ident,
        fields: &Vec<ast::UnionField>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        self.size_to_buffer = HashMap::new();
        self.arg_types = ValuedAttributes::new(attributes, "argtype");
        self.array_sizes = ValuedAttributes::new(attributes, "arraysize");
        self.value_sets = ValuedAttributes::new(attributes, "valueset");
        self.type_sets = ValuedAttributes::new(attributes, "typeset");
        let union_fields = fields
            .iter()
            .map(|field| match field {
                ast::UnionField { attributes: _, ty, ident } => format!(
                    "\t{union_field}",
                    union_field = self.arg_to_syzkaller_str(ast, (ident.name(), ty), "\t").unwrap()
                ),
            })
            .collect::<Vec<_>>()
            .join("\n");
        Ok(format!(
            "{union_name} [\n{union_fields}\n]",
            union_name = to_c_name(name.name()),
            union_fields = union_fields
        ))
    }

    fn codegen_enum_def(
        &self,
        name: &ast::Ident,
        variants: &Vec<ast::EnumVariant>,
        _ast: &BanjoAst,
    ) -> Result<String, Error> {
        let enum_variants = variants
            .iter()
            .map(|variant| match variant {
                ast::EnumVariant { attributes: _, name, value: _ } => {
                    format!("{enum_variant}", enum_variant = name)
                }
            })
            .collect::<Vec<_>>()
            .join(", ");
        Ok(format!(
            "{enum_name} = {enum_variants}",
            enum_name = to_c_name(name.name()),
            enum_variants = enum_variants
        ))
    }

    fn codegen_protocol_def(
        &mut self,
        methods: &Vec<ast::Method>,
        ast: &BanjoAst,
    ) -> Result<String, Error> {
        methods
            .iter()
            .map(|m| {
                self.size_to_buffer = HashMap::new();
                self.arg_types = ValuedAttributes::new(&m.attributes, "argtype");
                self.array_sizes = ValuedAttributes::new(&m.attributes, "arraysize");
                self.value_sets = ValuedAttributes::new(&m.attributes, "valueset");
                self.type_sets = ValuedAttributes::new(&m.attributes, "typeset");

                let mut accum = String::new();
                let mut in_params = self.get_in_params(&m, ast)?;
                let (out_params, return_param) = self.get_out_params(&m, ast)?;
                in_params.extend(out_params);
                let params = in_params.join(", ");

                // Generate specialized syscalls.
                if m.attributes.has_attribute("dependency") {
                    let (key_arg_name, value_arg_names) = get_dependent_arg_names(&m.attributes);

                    // Get info about linked enum (enum variants).
                    let enum_name = self.value_sets.get_arg(&key_arg_name);
                    if enum_name.is_empty() {
                        panic!(
                            "Arg {:?} needs to be linked to an enum with `valueset` attribute",
                            key_arg_name
                        );
                    }
                    let mut enum_variants: Vec<ast::EnumVariant> = Vec::default();
                    match ast.id_to_decl(&ast::Ident::new_raw(&enum_name)).unwrap() {
                        ast::Decl::Enum { variants, .. } => {
                            enum_variants = variants.to_vec();
                        }
                        _ => {}
                    }

                    // Get info about linked unions (union fields and attributes).
                    let union_names: Vec<String> = value_arg_names
                        .iter()
                        .map(|name| {
                            let union_name = self.type_sets.get_arg(&name);
                            if union_name.is_empty() {
                                panic!(
                                    "Arg {:?} needs to be linked to a union with `typeset` attribute",
                                    name
                                );
                            }
                            union_name
                        })
                        .collect();
                    let union_info: Vec<(Vec<ast::UnionField>, &ast::Attrs)> = union_names
                        .iter()
                        .filter_map(|union_name| {
                            match ast.id_to_decl(&ast::Ident::new_raw(&union_name)).unwrap() {
                                ast::Decl::Union { attributes, name: _, fields } => {
                                    if fields.len() != enum_variants.len() {
                                        panic!(
                                            "Unequal length between {:?} and {:?}",
                                            enum_name, union_name
                                        );
                                    }
                                    Some((fields.to_vec(), attributes))
                                }
                                _ => None,
                            }
                        })
                        .collect();

                    // `enum_arg` and `union_args[]` are substrings in the syscall
                    // which will be replaced
                    let mut comb_idx = 0;
                    let mut specialized_syscalls: Vec<String> = Vec::default();
                    let enum_arg = self
                        .arg_to_syzkaller_str(
                            ast,
                            (&key_arg_name, m.name_to_ty(&key_arg_name).unwrap()),
                            " ",
                        )
                        .unwrap();
                    let union_args: Vec<String> = value_arg_names
                        .iter()
                        .map(|arg_name| {
                            self.arg_to_syzkaller_str(
                                ast,
                                (&arg_name, m.name_to_ty(&arg_name).unwrap()),
                                " ",
                            )
                            .unwrap()
                        })
                        .collect();

                    // Each iteration generates a specialized syscall by:
                    // replacing enum_arg substring with variant_str from linked enum
                    // replacing union_args[j] substring with field_str from linked unions
                    while comb_idx < enum_variants.len() {
                        let mut tmp_accum = String::default();
                        let variant = &enum_variants[comb_idx];
                        let variant_str = format!("{} const[{}]", key_arg_name, variant.name);
                        let mod_params = params.replace(&enum_arg, &variant_str);
                        let mod_params = union_info
                            .iter()
                            .enumerate()
                            .fold(mod_params, |accum, (i, (union_fields, attributes))| {
                                self.arg_types = ValuedAttributes::new(attributes, "argtype");
                                let field = &union_fields[comb_idx];
                                let field_str = self
                                    .arg_to_syzkaller_str(ast, (&field.ident.name(), &field.ty), " ")
                                    .unwrap()
                                    .to_string()
                                    .replace(&field.ident.name(), &value_arg_names[i]);
                                accum.replace(&union_args[i], &field_str)
                            });
                        let mod_fn_name = format!(
                            "{name}${variant}",
                            name = to_c_name(m.name.as_str()),
                            variant = variant.name
                        );
                        tmp_accum.push_str(
                            format!(
                                "{fn_name}({params})",
                                fn_name = format!("zx_{}", mod_fn_name),
                                params = mod_params,
                            )
                            .as_str(),
                        );
                        if !return_param.is_empty() {
                            tmp_accum.push_str(
                                format!(" {return_param}", return_param = return_param).as_str(),
                            );
                        }
                        specialized_syscalls.push(tmp_accum);
                        comb_idx += 1;
                    }
                    accum.push_str(&specialized_syscalls.join("\n"));
                } else {
                    accum.push_str(
                        format!(
                            "{fn_name}({params})",
                            fn_name = format!("zx_{}", to_c_name(m.name.as_str())),
                            params = params,
                        )
                        .as_str(),
                    );
                    if !return_param.is_empty() {
                        accum.push_str(
                            format!(" {return_param}", return_param = return_param).as_str(),
                        );
                    }
                }
                Ok(accum)
            })
            .collect::<Result<Vec<_>, Error>>()
            .map(|x| x.join("\n"))
    }
}

impl<'a, W: io::Write> Backend<'a, W> for SyzkallerBackend<'a, W> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error> {
        self.w.write_fmt(format_args!(
            include_str!("templates/syzkaller/header.syzkaller.inc"),
            primary_namespace = ast.primary_namespace
        ))?;

        let decl_order = ast.validate_declaration_deps()?;

        let mut resource_definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Resource { attributes: _, ty, values } => {
                    Some(self.codegen_resource_def(ty, values, &ast))
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");
        if !resource_definitions.is_empty() {
            resource_definitions.push_str("\n\n");
        }

        let mut struct_definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Struct { attributes, name, fields } => {
                    Some(self.codegen_struct_def(attributes, name, fields, &ast))
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n\n");
        if !struct_definitions.is_empty() {
            struct_definitions.push_str("\n\n");
        }

        let mut union_definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Union { attributes, name, fields } => {
                    Some(self.codegen_union_def(attributes, name, fields, &ast))
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n\n");
        if !union_definitions.is_empty() {
            union_definitions.push_str("\n\n");
        }

        let mut enum_definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Enum { attributes: _, name, ty: _, variants } => {
                    Some(self.codegen_enum_def(name, variants, &ast))
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n\n");
        if !enum_definitions.is_empty() {
            enum_definitions.push_str("\n\n");
        }

        let protocol_definitions = decl_order
            .iter()
            .filter_map(|decl| match decl {
                Decl::Protocol { attributes: _, name, methods } => {
                    if name.name() != "Api" {
                        panic!("Expecting single protocol 'Api'");
                    }
                    Some(self.codegen_protocol_def(methods, &ast))
                }
                _ => None,
            })
            .collect::<Result<Vec<_>, Error>>()?
            .join("\n");

        write!(
            &mut self.w,
            "{}{}{}{}{}",
            resource_definitions,
            struct_definitions,
            union_definitions,
            enum_definitions,
            protocol_definitions
        )?;
        Ok(())
    }
}
