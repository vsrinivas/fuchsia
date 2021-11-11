// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Based on https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/fidl/fidlc/schema.json

use {
    anyhow::{anyhow, Error},
    heck::SnakeCase,
    lazy_static::lazy_static,
    regex::Regex,
    serde::{Deserialize, Deserializer, Serialize},
    std::collections::BTreeMap,
    std::string::ToString,
};

lazy_static! {
    pub static ref IDENTIFIER_RE: Regex =
        Regex::new(r#"^[A-Za-z]([_A-Za-z0-9]*[A-Za-z0-9])?$"#).unwrap();
    pub static ref COMPOUND_IDENTIFIER_RE: Regex =
        Regex::new(r#"([_A-Za-z][_A-Za-z0-9]*-)*[_A-Za-z][_A-Za-z0-9]*/[_A-Za-z][_A-Za-z0-9]*"#)
            .unwrap();
    pub static ref LIBRARY_IDENTIFIER_RE: Regex =
        Regex::new(r#"^[a-z][a-z0-9]*(\.[a-z][a-z0-9]*)*$"#).unwrap();
    pub static ref VERSION_RE: Regex = Regex::new(r#"^[0-9]+\.[0-9]+\.[0-9]+$"#).unwrap();
}

#[derive(Clone, Debug, PartialEq, Eq, Default, Serialize, Deserialize)]
#[serde(transparent)]
pub struct Ordinal(#[serde(deserialize_with = "validate_ordinal")] pub u64);

/// Validates that ordinal is non-zero.
fn validate_ordinal<'de, D>(deserializer: D) -> Result<u64, D::Error>
where
    D: Deserializer<'de>,
{
    let ordinal = u64::deserialize(deserializer)?;
    if ordinal == 0 {
        return Err(serde::de::Error::custom("Ordinal must not be equal to 0"));
    }
    Ok(ordinal)
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[repr(transparent)]
pub struct Count(pub u32);

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct Identifier(#[serde(deserialize_with = "validate_identifier")] pub String);

fn validate_identifier<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let id = String::deserialize(deserializer)?;
    if !IDENTIFIER_RE.is_match(&id) {
        return Err(serde::de::Error::custom(format!("Invalid identifier: {}", id)));
    }
    Ok(id)
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize, Hash)]
#[serde(transparent)]
pub struct CompoundIdentifier(
    #[serde(deserialize_with = "validate_compound_identifier")] pub String,
);

fn validate_compound_identifier<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let id = String::deserialize(deserializer)?;
    if !COMPOUND_IDENTIFIER_RE.is_match(&id) {
        return Err(serde::de::Error::custom(format!("Invalid compound identifier: {}", id)));
    }
    Ok(id)
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct LibraryIdentifier(#[serde(deserialize_with = "validate_library_identifier")] pub String);

fn validate_library_identifier<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let id = String::deserialize(deserializer)?;
    if !LIBRARY_IDENTIFIER_RE.is_match(&id) {
        return Err(serde::de::Error::custom(format!("Invalid library identifier: {}", id)));
    }
    Ok(id)
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct Version(#[serde(deserialize_with = "validate_version")] pub String);

fn validate_version<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let version = String::deserialize(deserializer)?;
    if !VERSION_RE.is_match(&version) {
        return Err(serde::de::Error::custom(format!("Invalid version: {}", version)));
    }
    Ok(version)
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Declaration {
    Bits,
    Const,
    Enum,
    Interface,
    Service,
    ExperimentalResource,
    Struct,
    Table,
    Union,
    TypeAlias,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct DeclarationsMap(pub BTreeMap<CompoundIdentifier, Declaration>);

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ExternalDeclaration {
    Bits,
    Const,
    Enum,
    Interface,
    Service,
    ExperimentalResource,
    Struct { resource: bool },
    Table { resource: bool },
    Union { resource: bool },
    TypeAlias,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct ExternalDeclarationsMap(pub BTreeMap<CompoundIdentifier, ExternalDeclaration>);

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Library {
    pub name: LibraryIdentifier,
    pub declarations: ExternalDeclarationsMap,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Location {
    pub filename: String,
    pub line: u32,
    pub column: u32,
    pub length: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum HandleSubtype {
    Handle,
    Bti,
    Channel,
    Clock,
    Debuglog,
    Event,
    EventPair,
    Exception,
    Fifo,
    Guest,
    Interrupt,
    Iommu,
    Job,
    Msi,
    Pager,
    PciDevice,
    Pmt,
    Port,
    Process,
    Profile,
    Resource,
    Socket,
    Stream,
    SuspendToken,
    Thread,
    Timer,
    Vcpu,
    Vmar,
    Vmo,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum IntegerType {
    Int8,
    Int16,
    Int32,
    Int64,
    Uint8,
    Uint16,
    Uint32,
    Uint64,
}

// TODO(surajmalhotra): Implement conversion between IntegerType and PrimitiveSubtype.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum PrimitiveSubtype {
    Bool,
    Float32,
    Float64,
    Int8,
    Int16,
    Int32,
    Int64,
    Uint8,
    Uint16,
    Uint32,
    Uint64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum Type {
    Array {
        element_type: Box<Type>,
        element_count: Count,
    },
    Vector {
        element_type: Box<Type>,
        maybe_element_count: Option<Count>,
        nullable: bool,
    },
    #[serde(rename = "string")]
    Str {
        maybe_element_count: Option<Count>,
        nullable: bool,
    },
    Handle {
        subtype: HandleSubtype,
        rights: u32,
        nullable: bool,
    },
    Request {
        subtype: CompoundIdentifier,
        nullable: bool,
    },
    Primitive {
        subtype: PrimitiveSubtype,
    },
    Identifier {
        identifier: CompoundIdentifier,
        nullable: bool,
    },
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum Literal {
    #[serde(rename = "string")]
    Str {
        value: String,
        expression: String,
    },
    Numeric {
        value: String,
        expression: String,
    },
    True {
        value: String,
        expression: String,
    },
    False {
        value: String,
        expression: String,
    },
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Constant {
    Identifier { identifier: CompoundIdentifier, value: String, expression: String },
    Literal { literal: Literal, value: String, expression: String },
    BinaryOperator { value: String, expression: String },
}

impl Constant {
    pub fn value_string(&self) -> &str {
        match self {
            Constant::Identifier { value, .. } => value,
            Constant::Literal { value, .. } => value,
            Constant::BinaryOperator { value, .. } => value,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Bits {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    #[serde(rename = "type")]
    pub _type: Type,
    pub mask: String,
    pub members: Vec<BitsMember>,
    pub strict: bool,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct BitsMember {
    pub name: Identifier,
    pub location: Option<Location>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub value: Constant,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Const {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    #[serde(rename = "type")]
    pub _type: Type,
    pub value: Constant,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct EnumMember {
    pub name: Identifier,
    pub location: Option<Location>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub value: Constant,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Enum {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    #[serde(rename = "type")]
    pub _type: IntegerType,
    pub members: Vec<EnumMember>,
    pub strict: bool,
    pub maybe_unknown_value: Option<u32>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ResourceProperty {
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Resource {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    #[serde(rename = "type")]
    pub _type: Type,
    pub properties: Option<Vec<ResourceProperty>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct AttributeArg {
    pub name: String,
    pub value: Constant,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Attribute {
    pub name: String,
    pub arguments: Vec<AttributeArg>,
}

impl Attribute {
    /// Count the number of attribute arguments.
    pub fn count(&self) -> usize {
        self.arguments.len()
    }

    /// Check if an argument of a certain name exists on this attribute.
    pub fn has(&self, name: &str) -> bool {
        self.get(name).is_ok()
    }

    /// Get the value of a specific argument on the attribute.
    pub fn get(&self, name: &str) -> Result<&Constant, Error> {
        let lower_name = to_lower_snake_case(name);
        Ok(&self
            .arguments
            .iter()
            .find(|arg| to_lower_snake_case(arg.name.as_str()) == lower_name)
            .ok_or(anyhow!("argument not found"))?
            .value)
    }

    /// For attributes that only have one argument, retrieve the value of that argument without
    /// naming it.
    pub fn get_standalone(&self) -> Result<&Constant, Error> {
        match self.count() {
            0 => Err(anyhow!("attribute {} has no arguments", self.name)),
            1 => Ok(&self.arguments[0].value),
            _ => Err(anyhow!("attribute {} has multiple arguments", self.name)),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct FieldShape {
    pub offset: Count,
    pub padding: Count,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct MethodParameter {
    pub maybe_attributes: Option<Vec<Attribute>>,
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
    pub location: Option<Location>,
    pub field_shape_v1: FieldShape,
    pub experimental_maybe_from_type_alias: Option<TypeConstructor>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct TypeShape {
    pub inline_size: Count,
    pub alignment: Count,
    pub depth: Count,
    pub max_handles: Count,
    pub max_out_of_line: Count,
    pub has_padding: bool,
    pub has_flexible_envelope: bool,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Method {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub ordinal: Ordinal,
    pub name: Identifier,
    pub location: Option<Location>,
    pub has_request: bool,
    pub maybe_request: Option<Vec<MethodParameter>>,
    pub maybe_request_type_shape_v1: Option<TypeShape>,
    pub has_response: bool,
    pub maybe_response: Option<Vec<MethodParameter>>,
    pub maybe_response_type_shape_v1: Option<TypeShape>,
    pub is_composed: bool,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Interface {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub methods: Vec<Method>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ServiceMember {
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
    pub location: Location,
    pub maybe_attributes: Option<Vec<Attribute>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Service {
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub members: Vec<ServiceMember>,
    pub maybe_attributes: Option<Vec<Attribute>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct StructMember {
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
    pub location: Option<Location>,
    pub field_shape_v1: FieldShape,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub maybe_default_value: Option<Constant>,
    pub experimental_maybe_from_type_alias: Option<FieldTypeConstructor>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Struct {
    pub max_handles: Option<Count>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub anonymous: Option<bool>,
    pub members: Vec<StructMember>,
    pub resource: bool,
    pub type_shape_v1: TypeShape,
    pub is_request_or_response: bool,
}

#[derive(Clone, Debug, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct TableMember {
    pub reserved: bool,
    #[serde(rename = "type")]
    pub _type: Option<Type>,
    pub name: Option<Identifier>,
    pub location: Option<Location>,
    pub ordinal: Ordinal,
    pub size: Option<Count>,
    pub max_out_of_line: Option<Count>,
    pub alignment: Option<Count>,
    pub offset: Option<Count>,
    pub maybe_default_value: Option<Constant>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Table {
    pub max_handles: Option<Count>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub members: Vec<TableMember>,
    pub strict: bool,
    pub resource: bool,
    pub type_shape_v1: TypeShape,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct UnionMember {
    pub ordinal: Ordinal,
    pub reserved: bool,
    pub name: Option<Identifier>,
    #[serde(rename = "type")]
    pub _type: Option<Type>,
    pub location: Option<Location>,
    pub max_out_of_line: Option<Count>,
    pub offset: Option<Count>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub experimental_maybe_from_type_alias: Option<FieldTypeConstructor>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Union {
    pub max_handles: Option<Count>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub members: Vec<UnionMember>,
    pub strict: bool,
    pub resource: bool,
    pub type_shape_v1: TypeShape,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct TypeConstructor {
    pub name: String,
    pub args: Vec<TypeConstructor>,
    pub nullable: bool,
    pub maybe_size: Option<Constant>,
    pub maybe_handle_subtype: Option<HandleSubtype>,
}

// This looks like it should be TypeConstructor, but fidlc does not generate the same data for
// alias information in struct/union's vs. top-level aliases.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct FieldTypeConstructor {
    pub name: String,
    pub args: Vec<TypeConstructor>,
    pub nullable: bool,
    pub maybe_size: Option<String>, // This is the difference with TypeConstructor.
    pub maybe_handle_subtype: Option<HandleSubtype>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct TypeAlias {
    pub name: CompoundIdentifier,
    pub location: Location,
    pub partial_type_ctor: TypeConstructor,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct FidlIr {
    pub version: Version,
    pub name: LibraryIdentifier,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub bits_declarations: Vec<Bits>,
    pub const_declarations: Vec<Const>,
    pub enum_declarations: Vec<Enum>,
    pub experimental_resource_declarations: Vec<Resource>,
    pub interface_declarations: Vec<Interface>,
    pub service_declarations: Vec<Service>,
    pub struct_declarations: Vec<Struct>,
    pub table_declarations: Vec<Table>,
    pub union_declarations: Vec<Union>,
    pub type_alias_declarations: Vec<TypeAlias>,
    pub declaration_order: Vec<CompoundIdentifier>,
    pub declarations: DeclarationsMap,
    pub library_dependencies: Vec<Library>,
}

// Additional methods for IR types.

// Compound identifiers are of the form: my.parent.library/ThisIsMyName
impl CompoundIdentifier {
    pub fn get_name(&self) -> &str {
        self.0.split("/").last().unwrap()
    }

    pub fn is_base_type(&self) -> bool {
        self.0.split("/").next().unwrap() == "zx"
    }
}

macro_rules! fetch_declaration {
    ( $ir: ident, $field: ident, $identifier: ident) => {
        $ir.$field
            .iter()
            .filter(|e| e.name == *$identifier)
            .next()
            .ok_or(anyhow!("Could not find declaration: {:?}", $identifier))
    };
}

impl FidlIr {
    pub fn get_library_name(&self) -> String {
        self.name.0.to_string()
    }

    pub fn get_declaration(&self, identifier: &CompoundIdentifier) -> Result<&Declaration, Error> {
        self.declarations.0.get(identifier).ok_or(anyhow!("~~ error never seen ~~~")).or_else(
            |_| {
                self.library_dependencies
                    .iter()
                    .filter_map(|library| library.declarations.0.get(identifier))
                    .next()
                    .ok_or(anyhow!("Could not find declaration: {:?}", identifier))
                    .map(|decl| match decl {
                        ExternalDeclaration::Bits => &Declaration::Bits,
                        ExternalDeclaration::Const => &Declaration::Const,
                        ExternalDeclaration::Enum => &Declaration::Enum,
                        ExternalDeclaration::Interface => &Declaration::Interface,
                        ExternalDeclaration::Service => &Declaration::Service,
                        ExternalDeclaration::ExperimentalResource => {
                            &Declaration::ExperimentalResource
                        }
                        ExternalDeclaration::Struct { .. } => &Declaration::Struct,
                        ExternalDeclaration::Table { .. } => &Declaration::Table,
                        ExternalDeclaration::Union { .. } => &Declaration::Union,
                        ExternalDeclaration::TypeAlias => &Declaration::TypeAlias,
                    })
            },
        )
    }

    pub fn get_enum(&self, identifier: &CompoundIdentifier) -> Result<&Enum, Error> {
        fetch_declaration!(self, enum_declarations, identifier)
    }

    pub fn get_struct(&self, identifier: &CompoundIdentifier) -> Result<&Struct, Error> {
        fetch_declaration!(self, struct_declarations, identifier)
    }

    pub fn get_const(&self, identifier: &CompoundIdentifier) -> Result<&Const, Error> {
        fetch_declaration!(self, const_declarations, identifier)
    }

    pub fn get_type_alias(&self, identifier: &CompoundIdentifier) -> Result<&TypeAlias, Error> {
        fetch_declaration!(self, type_alias_declarations, identifier)
    }

    pub fn get_bits(&self, identifier: &CompoundIdentifier) -> Result<&Bits, Error> {
        fetch_declaration!(self, bits_declarations, identifier)
    }

    pub fn is_protocol(&self, identifier: &CompoundIdentifier) -> bool {
        self.get_declaration(identifier).map_or(false, |decl| decl == &Declaration::Interface)
    }

    pub fn get_protocol_attributes(
        &self,
        identifier: &CompoundIdentifier,
    ) -> Result<&Option<Vec<Attribute>>, Error> {
        if let Some(Declaration::Interface) = self.declarations.0.get(identifier) {
            return Ok(&self
                .interface_declarations
                .iter()
                .filter(|e| e.name == *identifier)
                .next()
                .expect(&format!("Could not find protocol declaration: {:?}", identifier))
                .maybe_attributes);
        }
        Err(anyhow!("Identifier does not represent a protocol: {:?}", identifier))
    }

    pub fn is_external_decl(&self, identifier: &CompoundIdentifier) -> Result<bool, Error> {
        self.declarations
            .0
            .get(identifier)
            .map(|_| false)
            .ok_or(anyhow!("~~ error never seen ~~~"))
            .or_else(|_| {
                self.library_dependencies
                    .iter()
                    .find(|library| library.declarations.0.get(identifier).is_some())
                    .ok_or(anyhow!("Could not find declaration: {:?}", identifier))
                    .map(|_| true)
            })
    }
}

impl Type {
    pub fn is_primitive(&self, ir: &FidlIr) -> Result<bool, Error> {
        match self {
            Type::Identifier { ref identifier, .. } => {
                if identifier.is_base_type() {
                    Ok(true)
                } else {
                    match ir.get_declaration(identifier).unwrap() {
                        Declaration::Bits => fetch_declaration!(ir, bits_declarations, identifier)?
                            ._type
                            .is_primitive(ir),
                        Declaration::Const => {
                            fetch_declaration!(ir, const_declarations, identifier)?
                                ._type
                                .is_primitive(ir)
                        }
                        Declaration::Enum => Ok(true),
                        _ => Ok(false),
                    }
                }
            }
            Type::Primitive { .. } => Ok(true),
            _ => Ok(false),
        }
    }
}

impl IntegerType {
    pub fn to_primitive(&self) -> PrimitiveSubtype {
        match self {
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

    pub fn to_type(&self) -> Type {
        Type::Primitive { subtype: self.to_primitive() }
    }
}

/// Converts an UpperCamelCased name like "FooBar" into a lower_snake_cased one
/// like "foo_bar."  This is used to normalize attribute names such that names
/// written in either case are synonyms.
pub fn to_lower_snake_case(str: &str) -> String {
    str.to_snake_case().to_lowercase()
}

pub trait AttributeContainer {
    fn has(&self, name: &str) -> bool;
    fn get(&self, name: &str) -> Option<&Attribute>;
}

impl AttributeContainer for Option<Vec<Attribute>> {
    fn has(&self, name: &str) -> bool {
        match self {
            Some(attrs) => {
                attrs.iter().any(|a| to_lower_snake_case(&a.name) == to_lower_snake_case(name))
            }
            None => false,
        }
    }

    fn get(&self, name: &str) -> Option<&Attribute> {
        match self {
            Some(attrs) => attrs
                .iter()
                .filter_map(|a| {
                    if to_lower_snake_case(&a.name) == to_lower_snake_case(name) {
                        Some(a)
                    } else {
                        None
                    }
                })
                .next(),
            None => None,
        }
    }
}
