// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fidl::{self, *},
    anyhow::{anyhow, Error},
    heck::SnakeCase,
    std::iter,
};

static ATTR_NAME_DERIVE_DEBUG: &'static str = "derive_debug";
static ATTR_NAME_DOC: &'static str = "doc";
static ATTR_NAME_NAMESPACED: &'static str = "namespaced";
static ATTR_NAME_TRANSPORT: &'static str = "transport";

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

#[derive(PartialEq, Eq)]
pub enum ProtocolType {
    Callback,
    Interface,
    Protocol,
}

impl From<&Option<Vec<Attribute>>> for ProtocolType {
    fn from(maybe_attributes: &Option<Vec<Attribute>>) -> Self {
        if let Some(layout) = maybe_attributes.get("BanjoLayout") {
            return match layout.get_standalone() {
                Ok(constant) => match constant.value_string() {
                    "ddk-callback" => ProtocolType::Callback,
                    "ddk-interface" => ProtocolType::Interface,
                    "ddk-protocol" => ProtocolType::Protocol,
                    value => panic!("Unknown layout attribute: {}", value),
                },
                Err(_) => ProtocolType::Protocol,
            };
        }
        ProtocolType::Protocol
    }
}

pub fn filter_protocol<'b>(declaration: &Decl<'b>) -> Option<&'b Interface> {
    match declaration {
        Decl::Interface { data } => {
            if !for_banjo_transport(&data.maybe_attributes) {
                return None;
            }
            match ProtocolType::from(&data.maybe_attributes) {
                ProtocolType::Protocol => Some(data),
                _ => None,
            }
        }
        _ => None,
    }
}

pub fn filter_interface<'b>(declaration: &Decl<'b>) -> Option<&'b Interface> {
    match declaration {
        Decl::Interface { data } => {
            if !for_banjo_transport(&data.maybe_attributes) {
                return None;
            }
            match ProtocolType::from(&data.maybe_attributes) {
                ProtocolType::Interface => Some(data),
                _ => None,
            }
        }
        _ => None,
    }
}

pub fn get_doc_comment(maybe_attrs: &Option<Vec<Attribute>>, tabs: usize) -> String {
    if let Some(attrs) = maybe_attrs {
        for attr in attrs.iter() {
            if to_lower_snake_case(&attr.name) == ATTR_NAME_DOC {
                return match attr.get_standalone() {
                    Ok(constant) => {
                        let value = constant.value_string();
                        if value.is_empty() {
                            "".to_string()
                        } else {
                            let tabs: String = iter::repeat(' ').take(tabs * 4).collect();
                            value
                                .trim_end()
                                .split("\n")
                                .map(|line| format!("{}//{}\n", tabs, line))
                                .collect()
                        }
                    }
                    Err(_) => "".to_string(),
                };
            }
        }
    }
    "".to_string()
}

/// Returns an `Option` containing the value returned by `func` when applied to
/// the first `attr.name` and `attr.value` where
/// `to_lower_snake_case(attr.name) == attr_name`. Otherwise, returns `None`.
///
/// # Arguments
///
/// * `maybe_attrs` - `Option<Vec<Attribute>>` containing the attributes
///                   associated with a FIDL IR token.
/// * `attr_name`   - `&str` to match against each attribute name appearing in
///                   `maybe_attrs`.
/// * `func`        - `FnOnce<&str, &str> -> R` to apply to attribute name and
///                   value if a matching attribute name is found.
///
/// # EXAMPLES
///
/// ```
/// let maybe_attrs = Some(vec![Attribute { name: "foo".to_string(), value: "value".to_string() }]);
/// let test = apply_to_attr(&maybe_attrs, "foo", |attr_name, attr_value| {
///     format!("{}: {}", attr_name, attr_value)
/// });
///
/// assert_eq!(test, Some("foo: value".to_string()));
/// ```
pub fn apply_to_attr<F, R>(
    maybe_attrs: &Option<Vec<Attribute>>,
    attr_name: &str,
    func: F,
) -> Option<R>
where
    F: FnOnce(&str, &str) -> R,
{
    if let Some(attrs) = maybe_attrs {
        for attr in attrs.iter() {
            if to_lower_snake_case(&attr.name) == attr_name {
                return Some(match &attr.count() {
                    0 => func(&attr.name, ""),
                    _ => func(
                        &attr.name,
                        &match attr.get_standalone() {
                            Ok(constant) => constant.value_string(),
                            Err(_) => "",
                        },
                    ),
                });
            }
        }
    }
    None
}

/// Returns `Ok(true)` if `maybe_attrs` contains an attribute with the name
/// "Namespaced" (or any case-insensitive equivalent) and no associated value.
/// If an attribute with that name does not exist, returns `Ok(false)`. If an
/// attribute with that name exists and has a value, returns `Err`.
///
/// # Arguments
///
/// * `maybe_attrs` - `Option<Vec<Attribute>>` containing the attributes
///                   associated with a FIDL IR token.
///
/// # EXAMPLES
///
/// ```
/// let maybe_attrs_with_namespaced =
///     Some(vec![Attribute { name: String::from("Namespaced"), value: String::from("") }]);
/// assert!(is_namespaced(&maybe_attrs_with_namespaced).expect("is_namespaced should not fail"));
/// ```
pub fn is_namespaced(maybe_attrs: &Option<Vec<Attribute>>) -> Result<bool, Error> {
    apply_to_attr(maybe_attrs, ATTR_NAME_NAMESPACED, |_, attr_value| {
        if attr_value != "" {
            return Err(anyhow!("{} attribute cannot have a value.", ATTR_NAME_NAMESPACED));
        }
        Ok(true)
    })
    .unwrap_or(Ok(false))
}

/// Returns `Ok(true)` if `maybe_attrs` contains an attribute with the name
/// "DeriveDebug" (or any case-insensitive equivalent) and no associated value.
/// If an attribute with that name does not exist, returns `Ok(false)`. If an
/// attribute with that name exists and has a value, returns `Err`.
///
/// # Arguments
///
/// * `maybe_attrs` - `Option<Vec<Attribute>>` containing the attributes
///                   associated with a FIDL IR token.
///
/// # EXAMPLES
///
/// ```
/// let maybe_attrs_with_derive_debug =
///     Some(vec![Attribute { name: String::from("DeriveDebug"), value: String::from("") }]);
/// assert!(is_derive_debug(&maybe_attrs_with_derive_debug).expect("is_derive_debug should not fail"));
/// ```
pub fn is_derive_debug(maybe_attrs: &Option<Vec<Attribute>>) -> Result<bool, Error> {
    apply_to_attr(maybe_attrs, ATTR_NAME_DERIVE_DEBUG, |_, attr_value| {
        if attr_value != "" {
            return Err(anyhow!("{} attribute cannot have a value.", ATTR_NAME_DERIVE_DEBUG));
        }
        Ok(true)
    })
    .unwrap_or(Ok(false))
}

pub fn for_banjo_transport(maybe_attrs: &Option<Vec<Attribute>>) -> bool {
    apply_to_attr(maybe_attrs, ATTR_NAME_TRANSPORT, |_, attr_value| attr_value == "Banjo")
        .unwrap_or(false)
}

//---------------------------------------------
// Utilities shared by the four C/C++ backends.

pub fn to_c_name(name: &str) -> String {
    if name.is_empty() {
        name.to_string()
    } else {
        // strip FQN
        let name = name.split(".").last().unwrap();
        // Force uppercase characters the follow one another to lowercase.
        // e.g. GUIDType becomes Guidtype
        let mut iter = name.chars();
        let name = iter::once(iter.next().unwrap())
            .chain(iter.zip(name.chars()).map(|(c1, c2)| {
                if c2.is_ascii_uppercase() {
                    c1.to_ascii_lowercase()
                } else {
                    c1
                }
            }))
            .collect::<String>();
        name.trim().to_snake_case()
    }
}

pub fn primitive_type_to_c_str(ty: &PrimitiveSubtype) -> Result<String, Error> {
    match ty {
        PrimitiveSubtype::Bool => Ok(String::from("bool")),
        PrimitiveSubtype::Float32 => Ok(String::from("float")),
        PrimitiveSubtype::Float64 => Ok(String::from("double")),
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

pub fn not_callback(id: &CompoundIdentifier, ir: &FidlIr) -> Result<bool, Error> {
    if ir.is_external_decl(id)? {
        // This is a workaround for the fact that FidlIr doesn't contain attributes for external
        // libraries.
        if &Declaration::Interface != ir.get_declaration(id)? {
            return Err(anyhow!("Expected an interface an interface"));
        }
        if id.get_name().ends_with("Callback") {
            return Ok(false);
        }
    } else {
        if let Some(layout) = ir.get_protocol_attributes(id)?.get("BanjoLayout") {
            if layout.get_standalone()?.value_string() == "ddk-callback" {
                return Ok(false);
            }
        }
    }
    Ok(true)
}

pub fn array_bounds(ty: &Type) -> Option<String> {
    if let Type::Array { ref element_type, element_count } = ty {
        return if let Some(bounds) = array_bounds(element_type) {
            Some(format!("[{}]{}", element_count.0, bounds))
        } else {
            Some(format!("[{}]", element_count.0))
        };
    }
    None
}

pub fn name_buffer(maybe_attributes: &Option<Vec<Attribute>>) -> &'static str {
    if maybe_attributes.has("Buffer") {
        "buffer"
    } else {
        "list"
    }
}

pub fn name_size(maybe_attributes: &Option<Vec<Attribute>>) -> &'static str {
    if maybe_attributes.has("Buffer") {
        "size"
    } else {
        "count"
    }
}

pub fn is_table_or_bits(ty: &Type, ir: &FidlIr) -> bool {
    match ty {
        Type::Identifier { identifier, .. } => match ir
            .get_declaration(identifier)
            .expect(&format!("Could not find declaration for {:?}", identifier))
        {
            Declaration::Bits | Declaration::Table => true,
            _ => false,
        },
        _ => false,
    }
}

//--------------------------------------------
// Utilities shared by the three C++ backends.

pub fn to_cpp_name(name: &str) -> &str {
    // strip FQN
    name.split(".").last().unwrap()
}

pub fn handle_type_to_cpp_str(ty: &HandleSubtype) -> String {
    match ty {
        HandleSubtype::Handle => String::from("zx::handle"),
        HandleSubtype::Bti => String::from("zx::bti"),
        HandleSubtype::Channel => String::from("zx::channel"),
        HandleSubtype::Clock => String::from("zx::clock"),
        HandleSubtype::Debuglog => String::from("zx::handle"),
        HandleSubtype::Event => String::from("zx::event"),
        HandleSubtype::EventPair => String::from("zx::eventpair"),
        HandleSubtype::Exception => String::from("zx::handle"),
        HandleSubtype::Fifo => String::from("zx::fifo"),
        HandleSubtype::Guest => String::from("zx::guest"),
        HandleSubtype::Interrupt => String::from("zx::interrupt"),
        HandleSubtype::Iommu => String::from("zx::iommu"),
        HandleSubtype::Job => String::from("zx::job"),
        HandleSubtype::Msi => String::from("zx::msi"),
        HandleSubtype::Pager => String::from("zx::pager"),
        HandleSubtype::PciDevice => String::from("zx::handle"),
        HandleSubtype::Pmt => String::from("zx::pmt"),
        HandleSubtype::Port => String::from("zx::port"),
        HandleSubtype::Process => String::from("zx::process"),
        HandleSubtype::Profile => String::from("zx::profile"),
        HandleSubtype::Resource => String::from("zx::resource"),
        HandleSubtype::Socket => String::from("zx::socket"),
        HandleSubtype::Stream => String::from("zx::handle"),
        HandleSubtype::SuspendToken => String::from("zx::handle"),
        HandleSubtype::Thread => String::from("zx::thread"),
        HandleSubtype::Timer => String::from("zx::timer"),
        HandleSubtype::Vcpu => String::from("zx::vcpu"),
        HandleSubtype::Vmar => String::from("zx::vmar"),
        HandleSubtype::Vmo => String::from("zx::vmo"),
    }
}

pub fn type_to_cpp_str(ty: &Type, wrappers: bool, ir: &FidlIr) -> Result<String, Error> {
    match ty {
        Type::Array { ref element_type, .. } => type_to_cpp_str(element_type, wrappers, ir),
        Type::Vector { ref element_type, .. } => type_to_cpp_str(element_type, wrappers, ir),
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
        Type::Handle { ref subtype, .. } => {
            if wrappers {
                Ok(handle_type_to_cpp_str(subtype))
            } else {
                Ok(String::from("zx_handle_t"))
            }
        }
        _ => Err(anyhow!("Type not handled in C++: {:?}", ty)),
    }
}

pub fn protocol_to_ops_cpp_str(id: &CompoundIdentifier, ir: &FidlIr) -> Result<String, Error> {
    if ir.is_protocol(id) {
        return Ok(to_c_name(id.get_name()) + "_protocol_ops_t");
    }
    Err(anyhow!("Identifier does not represent a protocol {:?}", id))
}

pub fn get_base_type_from_alias(alias: &Option<&String>) -> Option<String> {
    if let Some(name) = alias {
        if name.starts_with("zx/") {
            return Some(format!("zx_{}_t", &name[3..]));
        }
    }
    None
}

pub fn get_first_param(method: &Method, ir: &FidlIr) -> Result<(bool, String), Error> {
    if let Some(response) = &method.maybe_response {
        if let Some(param) = response.get(0) {
            if let Some(arg_type) = get_base_type_from_alias(
                &param.experimental_maybe_from_type_alias.as_ref().map(|t| &t.name),
            ) {
                return Ok((true, arg_type));
            }
            if param._type.is_primitive(ir)? {
                return Ok((true, type_to_cpp_str(&param._type, false, ir)?));
            }
        }
    }
    Ok((false, "void".to_string()))
}

pub fn get_in_params(
    m: &Method,
    wrappers: bool,
    transform: bool,
    ir: &FidlIr,
) -> Result<Vec<String>, Error> {
    if !m.has_request {
        return Ok(vec![]);
    }
    m.maybe_request
        .as_ref()
        .unwrap()
        .iter()
        .map(|param| {
            let name = to_c_name(&param.name.0);
            if let Some(arg_type) = get_base_type_from_alias(
                &param.experimental_maybe_from_type_alias.as_ref().map(|t| &t.name),
            ) {
                return Ok(format!("{} {}", arg_type, name));
            }
            let ty_name = type_to_cpp_str(&param._type, wrappers, ir)?;
            match &param._type {
                Type::Identifier { identifier, .. } => {
                    if identifier.is_base_type() {
                        return Ok(format!("{} {}", ty_name, name));
                    }
                    match ir.get_declaration(identifier).unwrap() {
                        Declaration::Interface => {
                            if transform && not_callback(identifier, ir)? {
                                let ty_name = protocol_to_ops_cpp_str(identifier, ir).unwrap();
                                Ok(format!(
                                    "void* {name}_ctx, {ty_name}* {name}_ops",
                                    ty_name = ty_name,
                                    name = name
                                ))
                            } else {
                                Ok(format!("const {}* {}", ty_name, name))
                            }
                        }
                        Declaration::Struct | Declaration::Union => {
                            let prefix =
                                if param.maybe_attributes.has("InOut") { "" } else { "const " };
                            Ok(format!("{}{}* {}", prefix, ty_name, name))
                        }
                        Declaration::Enum => Ok(format!("{} {}", ty_name, name)),
                        decl => Err(anyhow!("Unsupported declaration: {:?}", decl)),
                    }
                }
                Type::Str { .. } => Ok(format!("const {} {}", ty_name, name)),
                Type::Array { .. } => {
                    let bounds = array_bounds(&param._type).unwrap();
                    Ok(format!(
                        "const {ty} {name}{bounds}",
                        bounds = bounds,
                        ty = ty_name,
                        name = name
                    ))
                }
                Type::Vector { .. } => {
                    // Note: wrappers are explicitly disabled here.
                    let ty_name = type_to_cpp_str(&param._type, false, ir)?;
                    let ptr = if param.maybe_attributes.has("InnerPointer") { "*" } else { "" };
                    Ok(format!(
                        "const {ty}{ptr}* {name}_{buffer}, size_t {name}_{size}",
                        buffer = name_buffer(&param.maybe_attributes),
                        size = name_size(&param.maybe_attributes),
                        ty = ty_name,
                        ptr = ptr,
                        name = name
                    ))
                }
                _ => Ok(format!("{} {}", ty_name, name)),
            }
        })
        .collect()
}

pub fn get_out_params(
    m: &Method,
    name: &str,
    wrappers: bool,
    ir: &FidlIr,
) -> Result<(Vec<String>, String), Error> {
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

    Ok((m.maybe_response.as_ref().map_or(Vec::new(), |response| { response.iter().skip(skip_amt).map(|param| {
        let name = to_c_name(&param.name.0);
        if let Some(arg_type) = get_base_type_from_alias(
            &param.experimental_maybe_from_type_alias.as_ref().map(|t| &t.name),
        ) {
            return format!("{}* out_{}", arg_type, name);
        }
        let ty_name = type_to_cpp_str(&param._type, wrappers, ir).unwrap();
        match &param._type {
            Type::Str { .. } => {
                format!("{} out_{name}, size_t {name}_capacity", ty_name,
                        name=name)
            }
            Type::Array { .. } => {
                let bounds = array_bounds(&param._type).unwrap();
                format!(
                    "{ty} out_{name}{bounds}",
                    bounds = bounds,
                    ty = ty_name,
                    name = name
                )
            }
            Type::Vector { .. } => {
                // Note: wrappers are explicitly disabled here.
                let ty_name = type_to_cpp_str(&param._type, false, ir).unwrap();
                let buffer_name = name_buffer(&param.maybe_attributes);
                let size_name = name_size(&param.maybe_attributes);
                if param.maybe_attributes.has("CalleeAllocated") {
                    format!("{ty}** out_{name}_{buffer}, size_t* {name}_{size}",
                            buffer = buffer_name,
                            size = size_name,
                            ty = ty_name,
                            name = name)
                } else {
                    format!("{ty}* out_{name}_{buffer}, size_t {name}_{size}, size_t* out_{name}_actual",
                            buffer = buffer_name,
                            size = size_name,
                            ty = ty_name,
                            name = name)
                }
            },
            Type::Handle { .. } => format!("{}* out_{}", ty_name, name),
            Type::Identifier { nullable, .. } => {
                let ptr = if *nullable { "*" } else { "" };
                format!("{}{}* out_{}", ty_name, ptr, name)
            },
            _ => format!("{}* out_{}", ty_name, name)
        }
    }).collect()}), return_param))
}

#[cfg(test)]
mod tests {
    use super::*;

    // A helper function that creates a string literal attribute arg, given t args name and value.
    fn string_literal_attribute_arg(name: &str, value: &str) -> AttributeArg {
        AttributeArg {
            name: String::from(name),
            value: Constant::Literal {
                literal: Literal::Str {
                    value: String::from(value),
                    expression: format!("\"{}\"", value.to_string()),
                },
                value: String::from(value),
                expression: format!("\"{}\"", value.to_string()),
            },
        }
    }

    // A helper function that creates an attribute with a single string literal argument.
    fn string_literal_attribute(name: &str, arg_name: &str, arg_value: &str) -> Attribute {
        Attribute {
            name: String::from(name),
            arguments: vec![string_literal_attribute_arg(arg_name, arg_value)],
        }
    }

    #[test]
    fn apply_to_attr_with_name() {
        let maybe_attrs_simple = Some(vec![string_literal_attribute("foo", "foo", "value")]);
        let test = apply_to_attr(&maybe_attrs_simple, "foo", |attr_name, attr_value| {
            format!("{}: {}", attr_name, attr_value)
        });
        assert_eq!(test, Some("foo: value".to_string()));
    }

    #[test]
    fn apply_to_attr_without_name() {
        let maybe_attrs_simple = Some(vec![string_literal_attribute("foo", "foo", "value")]);
        let test = apply_to_attr(&maybe_attrs_simple, "bar", |attr_name, attr_value| {
            format!("{}: {}", attr_name, attr_value)
        });
        assert_eq!(test, None);
    }

    #[test]
    fn apply_to_attr_with_duplicate_name() {
        let maybe_attrs_duplicate_name = Some(vec![
            string_literal_attribute("foo", "value", "value"),
            string_literal_attribute("foo", "value", "eulav"),
        ]);
        let test = apply_to_attr(&maybe_attrs_duplicate_name, "foo", |attr_name, attr_value| {
            format!("{}: {}", attr_name, attr_value)
        });
        assert_eq!(test, Some("foo: value".to_string()));
    }

    #[test]
    fn apply_to_attr_with_arg_not_named_value() {
        let maybe_attrs_duplicate_name =
            Some(vec![string_literal_attribute("foo", "other_name", "bar")]);
        let test = apply_to_attr(&maybe_attrs_duplicate_name, "foo", |attr_name, attr_value| {
            format!("{}: {}", attr_name, attr_value)
        });
        assert_eq!(test, Some("foo: bar".to_string()));
    }

    #[test]
    fn is_namespaced_ok_true() {
        let maybe_attrs_with_namespaced =
            Some(vec![string_literal_attribute("Namespaced", "value", "")]);
        assert!(is_namespaced(&maybe_attrs_with_namespaced).expect("is_namespaced should not fail"));
    }
    #[test]
    fn is_namespaced_ok_false() {
        let maybe_attrs_without_namespaced =
            Some(vec![string_literal_attribute("NotNamespaced", "value", "")]);
        assert!(
            !is_namespaced(&maybe_attrs_without_namespaced).expect("is_namespaced should not fail")
        );
    }

    #[test]
    fn is_namespaced_err() {
        let maybe_attrs_with_namespaced =
            Some(vec![string_literal_attribute("Namespaced", "value", "foo")]);
        is_namespaced(&maybe_attrs_with_namespaced).expect_err("is_namespaced should fail");
    }
}
