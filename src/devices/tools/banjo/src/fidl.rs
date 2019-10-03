// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Based on https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/tools/fidl/schema.json

use {
    lazy_static::lazy_static,
    regex::Regex,
    serde::{Deserialize, Deserializer},
    serde_derive::{Deserialize, Serialize},
    std::collections::BTreeMap,
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
pub struct Ordinal(#[serde(deserialize_with = "validate_ordinal")] pub u32);

/// Validates that ordinal is non-zero.
fn validate_ordinal<'de, D>(deserializer: D) -> Result<u32, D::Error>
where
    D: Deserializer<'de>,
{
    let ordinal = u32::deserialize(deserializer)?;
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

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
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
#[serde(rename_all = "lowercase")]
pub enum Declaration {
    Const,
    Enum,
    Interface,
    Struct,
    Table,
    Union,
    XUnion,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct DeclarationsMap(pub BTreeMap<CompoundIdentifier, Declaration>);

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Library {
    pub name: LibraryIdentifier,
    pub declarations: DeclarationsMap,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Location {
    pub filename: String,
    pub line: u32,
    pub column: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum HandleSubtype {
    Handle,
    Process,
    Thread,
    Vmo,
    Channel,
    Event,
    Port,
    Interrupt,
    Debuglog,
    Socket,
    Resource,
    Eventpair,
    Job,
    Vmar,
    Fifo,
    Guest,
    Timer,
    Bti,
    Profile,
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

// TODO(surajmalhotra): Implement conversion begtween IntegerType and PrimitiveSubtype.
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
    },
    Numeric {
        value: String,
    },
    True,
    False,
    #[serde(rename = "default")]
    _Default,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum Constant {
    Identifier { identifier: CompoundIdentifier },
    Literal { literal: Literal },
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Const {
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
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Attribute {
    pub name: String,
    pub value: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct MethodParameter {
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
    pub location: Option<Location>,
    pub size: Count,
    pub max_out_of_line: Count,
    pub alignment: Count,
    pub offset: Count,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Method {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub ordinal: Ordinal,
    pub generated_ordinal: Ordinal,
    pub name: Identifier,
    pub location: Option<Location>,
    pub has_request: bool,
    pub maybe_request: Option<Vec<MethodParameter>>,
    pub maybe_request_size: Option<Count>,
    pub maybe_request_alignment: Option<Count>,
    pub has_response: bool,
    pub maybe_response: Option<Vec<MethodParameter>>,
    pub maybe_response_size: Option<Count>,
    pub maybe_response_alignment: Option<Count>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Interface {
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub methods: Vec<Method>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct StructMember {
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
    pub location: Option<Location>,
    pub size: Count,
    pub max_out_of_line: Count,
    pub alignment: Count,
    pub offset: Count,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub maybe_default_value: Option<Constant>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Struct {
    pub max_handles: Option<Count>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub anonymous: Option<bool>,
    pub members: Vec<StructMember>,
    pub size: Count,
    pub max_out_of_line: Count,
}

#[derive(Clone, Debug, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct TableMember {
    pub ordinal: Ordinal,
    pub reserved: bool,
    #[serde(rename = "type")]
    pub _type: Option<Type>,
    pub name: Option<Identifier>,
    pub location: Option<Location>,
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
    pub size: Count,
    pub max_out_of_line: Count,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct UnionMember {
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
    pub location: Option<Location>,
    pub size: Count,
    pub max_out_of_line: Count,
    pub alignment: Count,
    pub offset: Count,
    pub maybe_attributes: Option<Vec<Attribute>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Union {
    pub max_handles: Option<Count>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub members: Vec<UnionMember>,
    pub size: Count,
    pub max_out_of_line: Count,
    pub alignment: Count,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct XUnionMember {
    pub ordinal: Ordinal,
    #[serde(rename = "type")]
    pub _type: Type,
    pub name: Identifier,
    pub location: Option<Location>,
    pub size: Count,
    pub max_out_of_line: Count,
    pub alignment: Count,
    pub offset: Count,
    pub maybe_attributes: Option<Vec<Attribute>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct XUnion {
    pub max_handles: Option<Count>,
    pub maybe_attributes: Option<Vec<Attribute>>,
    pub name: CompoundIdentifier,
    pub location: Option<Location>,
    pub members: Vec<XUnionMember>,
    pub size: Count,
    pub max_out_of_line: Count,
    pub alignment: Count,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Ir {
    pub version: Version,
    pub name: LibraryIdentifier,
    pub library_dependencies: Vec<Library>,
    pub const_declarations: Vec<Const>,
    pub enum_declarations: Vec<Enum>,
    pub interface_declarations: Vec<Interface>,
    pub struct_declarations: Vec<Struct>,
    pub table_declarations: Vec<Table>,
    pub union_declarations: Vec<Union>,
    pub xunion_declarations: Vec<XUnion>,
    pub declaration_order: Vec<CompoundIdentifier>,
    pub declarations: DeclarationsMap,
}
