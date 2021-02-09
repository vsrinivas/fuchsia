// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Rule,
    anyhow::{format_err, Error},
    itertools::Itertools,
    pest::iterators::{Pair, Pairs},
    serde::Serialize,
    std::cmp::Ordering,
    std::collections::{BTreeMap, HashSet},
    std::fmt,
    std::str::FromStr,
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum ParseError {
    #[error("{0} is not yet support")]
    NotYetSupported(String),
    #[error("{0:?} is not expected in this location")]
    UnexpectedToken(Rule),
    #[error("{0} was not included in the input libraries")]
    UnImported(String),
    #[error("{0} is an unknown type")]
    UnrecognizedType(String),
    #[error("Failed to parse because {0:?} is not an integer")]
    NotAnInteger(Rule),
    #[error("Invalid dependencies: {0}")]
    InvalidDeps(String),
    #[error("Expected {0:?} to resolve to a {1:?}.")]
    InvalidConstType(Constant, Ty),
    #[error("Declaration not found in namespace")]
    UnknownDecl,
}

#[derive(PartialEq, Eq, Serialize, Default, Debug, Clone, Hash, PartialOrd, Ord)]
pub struct Attr {
    pub key: String,
    pub val: Option<String>,
}

#[derive(PartialEq, Eq, Serialize, Default, Debug, Clone, Hash, PartialOrd, Ord)]
pub struct Attrs(pub Vec<Attr>);

// namespace is only populated if it's not the current/default one
// TODO(bwb) consider populating it or renaming to be more explicit
#[derive(PartialEq, Debug, Eq, Serialize, Clone, Hash, PartialOrd, Ord)]
pub struct Ident {
    namespace: Option<String>,
    name: String,
}

impl Ident {
    /// Construct an identity. If the second parameter is a fully qualified identity, use that
    /// if it is not qualified, place it under the passed in namespace
    pub fn new(namespace: &str, raw_name: &str) -> Ident {
        let v: Vec<&str> = raw_name.rsplitn(2, '.').collect();
        if v.len() > 1 {
            Ident { namespace: Some(v[1].trim().to_string()), name: v[0].trim().to_string() }
        } else {
            Ident {
                namespace: Some(namespace.trim().to_string()),
                name: raw_name.trim().to_string(),
            }
        }
    }

    pub fn new_raw(raw_name: &str) -> Ident {
        let v: Vec<&str> = raw_name.rsplitn(2, '.').collect();
        if v.len() > 1 {
            Ident { namespace: Some(v[1].trim().to_string()), name: v[0].trim().to_string() }
        } else {
            Ident { namespace: None, name: raw_name.trim().to_string() }
        }
    }

    pub fn fq(&self) -> (Option<String>, String) {
        (self.namespace.clone(), self.name.clone())
    }

    pub fn name(&self) -> &str {
        self.name.as_str()
    }

    pub fn is_base_type(&self) -> bool {
        // TODO add more of zx.banjo
        match self.namespace {
            Some(ref n) => n == "zx",
            None => false,
        }
    }
}

impl Attrs {
    #[allow(dead_code)]
    pub fn has_attributes(&self) -> bool {
        self.0.len() > 0
    }

    pub fn has_attribute(&self, key: &str) -> bool {
        self.0.iter().any(|attr| attr.key == key)
    }

    pub fn get_attribute<'a>(&'a self, key: &str) -> Option<&'a String> {
        if let Some(attr) = self.0.iter().find(|attr| attr.key == key) {
            if let Some(ref val) = attr.val {
                Some(val)
            } else {
                None
            }
        } else {
            None
        }
    }

    pub fn from_pair(pair: Pair<'_, Rule>) -> Result<Attrs, ParseError> {
        let mut attrs = Attrs::default();
        let mut doc_string: Option<String> = None; // String::default();
        for inner_pair in pair.into_inner() {
            match inner_pair.as_rule() {
                Rule::doc_comment => {
                    if doc_string.is_none() {
                        doc_string = Some(String::default())
                    }
                    if let Some(ref mut doc_string) = doc_string {
                        *doc_string += inner_pair.as_str().split_at(3).1;
                    }
                }
                Rule::attribute_list => {
                    for attrib in inner_pair.into_inner() {
                        let ap = attrib.as_str();
                        if !ap.contains("=") {
                            attrs.0.push(Attr { key: String::from(ap.trim()), val: None });
                        } else {
                            let split: Vec<&str> = ap.split("=").collect();
                            // Strip whitespace and quotes.
                            let val = split[1].trim();
                            let val = val.chars().skip(1).take(val.len() - 2).collect();
                            attrs
                                .0
                                .push(Attr { key: String::from(split[0].trim()), val: Some(val) });
                        }
                    }
                }
                _ => unreachable!(),
            }
        }
        if doc_string.is_some() {
            attrs.0.push(Attr { key: String::from("Doc"), val: doc_string });
        }
        Ok(attrs)
    }
}

#[derive(PartialEq, Eq, Clone, Serialize, Debug, Hash, PartialOrd, Ord)]
pub struct Constant(pub String);

impl Constant {
    pub fn from_str(string: &str) -> Self {
        Constant(string.to_string())
    }
}

impl std::fmt::Display for Constant {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

#[derive(PartialEq, Eq, Clone, Serialize, Debug, Hash, PartialOrd, Ord)]
pub enum HandleTy {
    None,
    Process,
    Thread,
    Vmo,
    Channel,
    Event,
    Port,
    Interrupt,
    PciDevice,
    Log,
    Socket,
    Resource,
    EventPair,
    Job,
    Vmar,
    Fifo,
    Guest,
    VCpu,
    Timer,
    IoMmu,
    Bti,
    Profile,
    Pmt,
    SuspendToken,
    Pager,
    Exception,
    Clock,
    Stream,
    MsiAllocation,
    MsiInterrupt,
}

#[derive(PartialEq, Eq, Clone, Serialize, Debug, Hash, PartialOrd, Ord)]
pub enum Ty {
    Voidptr,
    Bool,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float32,
    Float64,
    Str { size: Option<Constant>, nullable: bool },
    Vector { ty: Box<Ty>, size: Option<Constant>, nullable: bool },
    Array { ty: Box<Ty>, size: Constant },
    Protocol,
    Struct,
    Union,
    Enum,
    Handle { ty: HandleTy, reference: bool },
    // TODO rename this to something less confusing
    Identifier { id: Ident, reference: bool },
}

impl fmt::Display for Ty {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Ty::UInt32 => write!(f, "UInt32"),
            Ty::Identifier { .. } => write!(f, "<Ident>"),
            _ => Err(fmt::Error),
        }
    }
}

impl Ty {
    pub fn is_primitive(&self, ast: &BanjoAst) -> bool {
        match self {
            Ty::Identifier { id, .. } => {
                if id.is_base_type() {
                    return true;
                } else {
                    let resolved_type = ast.id_to_type(id);
                    return resolved_type.is_primitive(&ast);
                }
            }
            Ty::Struct { .. } => false,
            Ty::Union { .. } => false,
            Ty::Protocol { .. } => false,
            Ty::Enum { .. } => true,
            Ty::Str { .. } | Ty::Vector { .. } | Ty::Array { .. } | Ty::Handle { .. } => false,
            _ => true,
        }
    }

    pub fn is_reference(&self) -> bool {
        match self {
            Ty::Str { nullable, .. } => *nullable,
            Ty::Vector { nullable, .. } => *nullable,
            Ty::Identifier { reference, .. } => *reference,
            Ty::Handle { reference, .. } => *reference,
            _ => false,
        }
    }

    pub fn from_pair(ns: &str, pair: &Pair<'_, Rule>) -> Result<Self, ParseError> {
        let rule = pair.as_rule();
        match rule {
            Rule::primitive_type => match pair.as_str() {
                "bool" => Ok(Ty::Bool),
                "int8" => Ok(Ty::Int8),
                "int16" => Ok(Ty::Int16),
                "int32" => Ok(Ty::Int32),
                "int64" => Ok(Ty::Int64),
                "uint8" => Ok(Ty::UInt8),
                "uint16" => Ok(Ty::UInt16),
                "uint32" => Ok(Ty::UInt32),
                "uint64" => Ok(Ty::UInt64),
                "float32" => Ok(Ty::Float32),
                "float64" => Ok(Ty::Float64),
                "voidptr" => Ok(Ty::Voidptr),
                _e => Err(ParseError::UnrecognizedType(pair.as_str().to_string())),
            },
            Rule::handle_type => {
                let mut ty = HandleTy::None;
                let mut reference = false;
                for inner_pair in pair.clone().into_inner() {
                    match inner_pair.as_rule() {
                        Rule::handle_subtype => {
                            ty = match inner_pair.as_str() {
                                "NONE" => HandleTy::None,
                                "PROCESS" => HandleTy::Process,
                                "THREAD" => HandleTy::Thread,
                                "VMO" => HandleTy::Vmo,
                                "CHANNEL" => HandleTy::Channel,
                                "EVENT" => HandleTy::Event,
                                "PORT" => HandleTy::Port,
                                "INTERRUPT" => HandleTy::Interrupt,
                                "PCI_DEVICE" => HandleTy::PciDevice,
                                "LOG" => HandleTy::Log,
                                "SOCKET" => HandleTy::Socket,
                                "RESOURCE" => HandleTy::Resource,
                                "EVENTPAIR" => HandleTy::EventPair,
                                "JOB" => HandleTy::Job,
                                "VMAR" => HandleTy::Vmar,
                                "FIFO" => HandleTy::Fifo,
                                "GUEST" => HandleTy::Guest,
                                "VCPU" => HandleTy::VCpu,
                                "TIMER" => HandleTy::Timer,
                                "IOMMU" => HandleTy::IoMmu,
                                "BTI" => HandleTy::Bti,
                                "PROFILE" => HandleTy::Profile,
                                "PMT" => HandleTy::Pmt,
                                "SUSPEND_TOKEN" => HandleTy::SuspendToken,
                                "PAGER" => HandleTy::Pager,
                                "EXCEPTION" => HandleTy::Exception,
                                "CLOCK" => HandleTy::Clock,
                                "STREAM" => HandleTy::Stream,
                                "MSI_ALLOCATION" => HandleTy::MsiAllocation,
                                "MSI_INTERRUPT" => HandleTy::MsiInterrupt,
                                _e => {
                                    return Err(ParseError::UnrecognizedType(
                                        inner_pair.as_str().to_string(),
                                    ));
                                }
                            }
                        }
                        Rule::reference => {
                            reference = true;
                        }
                        _e => {
                            return Err(ParseError::UnrecognizedType(
                                inner_pair.as_str().to_string(),
                            ));
                        }
                    }
                }
                Ok(Ty::Handle { ty, reference })
            }
            Rule::integer_type => match pair.as_str() {
                "int8" => Ok(Ty::Int8),
                "int16" => Ok(Ty::Int16),
                "int32" => Ok(Ty::Int32),
                "int64" => Ok(Ty::Int64),
                "uint8" => Ok(Ty::UInt8),
                "uint16" => Ok(Ty::UInt16),
                "uint32" => Ok(Ty::UInt32),
                "uint64" => Ok(Ty::UInt64),
                _e => Err(ParseError::NotAnInteger(rule)),
            },
            Rule::array_type => {
                let vec_contents: Vec<Pair<'_, Rule>> = pair.clone().into_inner().collect();
                let ty = Box::new(Ty::from_pair(ns, &vec_contents[0])?);
                let size = Constant::from_str(vec_contents[1].as_str());
                Ok(Ty::Array { ty, size })
            }
            Rule::identifier_type => {
                let mut iter = pair.clone().into_inner();
                let id = iter.next().unwrap().as_str();
                let reference = if let Some(pair) = iter.next() {
                    match pair.as_rule() {
                        Rule::reference => true,
                        e => {
                            return Err(ParseError::UnexpectedToken(e));
                        }
                    }
                } else {
                    false
                };
                Ok(Ty::Identifier { id: Ident::new(ns, id), reference })
            }
            Rule::string_type => {
                let mut size = None;
                let mut nullable = false;
                for inner_pair in pair.clone().into_inner() {
                    match inner_pair.as_rule() {
                        Rule::constant => {
                            size = Some(Constant::from_str(inner_pair.as_str()));
                        }
                        Rule::reference => {
                            nullable = true;
                        }
                        e => {
                            return Err(ParseError::UnexpectedToken(e));
                        }
                    }
                }
                Ok(Ty::Str { size, nullable })
            }
            Rule::vector_type => {
                let mut iter = pair.clone().into_inner();
                let ty = Box::new(Ty::from_pair(ns, &iter.next().unwrap())?);
                let mut size = None;
                let mut nullable = false;
                for inner_pair in iter {
                    match inner_pair.as_rule() {
                        Rule::constant => {
                            size = Some(Constant::from_str(inner_pair.as_str()));
                        }
                        Rule::reference => {
                            nullable = true;
                        }
                        e => {
                            return Err(ParseError::UnexpectedToken(e));
                        }
                    }
                }
                Ok(Ty::Vector { ty, size, nullable })
            }
            _e => Err(ParseError::UnrecognizedType(pair.as_str().to_string())),
        }
    }
}

#[derive(PartialEq, Eq, Serialize, Debug, Hash, PartialOrd, Ord)]
pub struct StructField {
    pub attributes: Attrs,
    pub ty: Ty,
    pub ident: Ident,
    pub val: Option<Constant>,
}

impl StructField {
    pub fn from_pair(ns: &str, pair: Pair<'_, Rule>) -> Result<Self, ParseError> {
        let mut attributes = Attrs::default();
        let mut ty = None;
        let mut ident = String::default();
        let mut val = None;
        for inner_pair in pair.into_inner() {
            match inner_pair.as_rule() {
                Rule::attributes => attributes = Attrs::from_pair(inner_pair)?,
                Rule::ident => ident = String::from(inner_pair.as_str()),
                Rule::constant => val = Some(Constant::from_str(inner_pair.as_str())),
                _ => ty = Some(Ty::from_pair(ns, &inner_pair)?),
            };
        }
        Ok(StructField {
            attributes: attributes,
            ty: ty.unwrap(),
            ident: Ident::new_raw(ident.as_str()),
            val: val,
        })
    }
}

#[derive(PartialEq, Eq, Serialize, Debug, Hash, PartialOrd, Ord, Clone)]
pub struct UnionField {
    pub attributes: Attrs,
    pub ty: Ty,
    pub ident: Ident,
}
impl UnionField {
    pub fn from_pair(ns: &str, pair: Pair<'_, Rule>) -> Result<Self, ParseError> {
        let fields: Vec<Pair<'_, Rule>> = pair.into_inner().collect();
        let ty = &fields[1];
        Ok(UnionField {
            attributes: Attrs::from_pair(fields[0].clone())?,
            ty: Ty::from_pair(ns, ty)?,
            ident: Ident::new_raw(fields[2].as_str()),
        })
    }
}

#[derive(PartialEq, Eq, Serialize, Debug, Hash, PartialOrd, Ord, Clone)]
pub struct EnumVariant {
    pub attributes: Attrs,
    pub name: String,
    pub value: Constant,
}

impl EnumVariant {
    pub fn from_pair(pair: Pair<'_, Rule>) -> Result<Self, ParseError> {
        let fields: Vec<Pair<'_, Rule>> = pair.into_inner().collect();
        Ok(EnumVariant {
            attributes: Attrs::from_pair(fields[0].clone())?,
            name: String::from(fields[1].as_str()),
            value: Constant::from_str(fields[2].as_str()),
        })
    }
}

#[derive(PartialEq, Eq, Serialize, Debug, Hash, PartialOrd, Ord)]
pub struct Method {
    pub attributes: Attrs,
    pub name: String,
    pub in_params: Vec<(String, Ty, Attrs)>,
    pub out_params: Vec<(String, Ty, Attrs)>,
}

impl Method {
    #[allow(dead_code)]
    pub fn name_to_ty(&self, arg_name: &str) -> Result<&Ty, Error> {
        for (name, ty, _) in self.in_params.iter() {
            if arg_name == name {
                return Ok(ty);
            }
        }
        Err(format_err!("`{:?}` arg not found in method `{:?}`", arg_name, self.name))
    }

    pub fn from_pair(ns: &str, pair: Pair<'_, Rule>) -> Result<Self, ParseError> {
        let mut attributes = Attrs::default();
        let mut name = String::default();
        let mut in_params = Vec::new();
        let mut out_params = Vec::new();
        for inner_pair in pair.into_inner() {
            match inner_pair.as_rule() {
                Rule::attributes => {
                    attributes = Attrs::from_pair(inner_pair)?;
                }
                Rule::protocol_parameters => {
                    let mut fields: Vec<Pair<'_, Rule>> = inner_pair.into_inner().collect();
                    name = String::from(fields[0].as_str());
                    // TODO cleaner way of getting in/out params
                    let inner_params = fields.remove(1);
                    for in_pair in inner_params.into_inner() {
                        match in_pair.as_rule() {
                            Rule::parameter => {
                                let mut param_fields: Vec<Pair<'_, Rule>> =
                                    in_pair.into_inner().collect();
                                let mut attributes = Attrs::default();
                                if param_fields.len() > 2 {
                                    attributes = Attrs::from_pair(param_fields.remove(0))?;
                                }
                                let ty = Ty::from_pair(ns, &param_fields.get(0).unwrap())?;
                                let name = String::from(param_fields.get(1).unwrap().as_str());
                                in_params.push((name, ty, attributes));
                            }
                            e => return Err(ParseError::UnexpectedToken(e)),
                        }
                    }
                    // might not have any return paramaters
                    if fields.len() > 1 {
                        let inner_params = fields.remove(1);
                        for in_pair in inner_params.into_inner() {
                            match in_pair.as_rule() {
                                Rule::parameter => {
                                    let mut param_fields: Vec<Pair<'_, Rule>> =
                                        in_pair.into_inner().collect();
                                    let mut attributes = Attrs::default();
                                    if param_fields.len() > 2 {
                                        attributes = Attrs::from_pair(param_fields.remove(0))?;
                                    }
                                    let ty = Ty::from_pair(ns, &param_fields.get(0).unwrap())?;
                                    let name = String::from(param_fields.get(1).unwrap().as_str());
                                    out_params.push((name, ty, attributes));
                                }
                                e => return Err(ParseError::UnexpectedToken(e)),
                            }
                        }
                    }
                }
                e => return Err(ParseError::UnexpectedToken(e)),
            }
        }
        Ok(Method { attributes, name, in_params, out_params })
    }
}

#[derive(PartialEq, Eq, Serialize, Debug, Hash)]
pub enum Decl {
    Struct { attributes: Attrs, name: Ident, fields: Vec<StructField> },
    Union { attributes: Attrs, name: Ident, fields: Vec<UnionField> },
    Enum { attributes: Attrs, name: Ident, ty: Ty, variants: Vec<EnumVariant> },
    Constant { attributes: Attrs, name: Ident, ty: Ty, value: Constant },
    Protocol { attributes: Attrs, name: Ident, methods: Vec<Method>, placeholders: Vec<Ty> },
    Resource { attributes: Attrs, ty: Ty, values: Vec<Constant> },
    Alias(Ident, Ident),
}

impl Decl {
    // Returns an object used to order declaration instances.
    //
    // Currently a tuple of optional namespace + required name.
    fn get_order(&self) -> Option<(Option<String>, String)> {
        // Declarations are alphabetically ordered here. Note however that the list of 0-dependency
        // declarations is processed from the back in `validate_declaration_deps`, resulting in
        // descending alhpabetical order.
        match self {
            Decl::Protocol { name, .. } => Some(name.fq()),
            Decl::Struct { name, .. } => Some(name.fq()),
            Decl::Union { name, .. } => Some(name.fq()),
            Decl::Enum { name, .. } => Some(name.fq()),
            Decl::Alias(to, _from) => Some(to.fq()),
            Decl::Constant { name, .. } => Some(name.fq()),
            Decl::Resource { .. } => None,
        }
    }
}

impl Ord for Decl {
    fn cmp(&self, other: &Self) -> Ordering {
        self.get_order().cmp(&other.get_order())
    }
}

impl PartialOrd for Decl {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(PartialEq, Serialize, Debug)]
pub struct BanjoAst {
    pub primary_namespace: String,
    pub namespaces: BTreeMap<String, Vec<Decl>>,
}

// Placeholder declarations are necessary to achieve an ordering identical to FIDL's.
// The name of these generated declarations is the same as in FIDL.

#[derive(Debug)]
pub struct PlaceholderGenerator {
    counter: u32,
}

const ANONYMOUS_PREFIX: &str = "SomeLongAnonymousPrefix";

impl PlaceholderGenerator {
    fn new() -> Self {
        Self { counter: 0 }
    }

    fn get(&mut self, ns: &str, params: &Vec<(String, Ty, Attrs)>) -> (Decl, Ty) {
        let ident = Ident::new(ns, format!("{}{}", ANONYMOUS_PREFIX, self.counter).as_str());
        let fields: Vec<StructField> = params
            .iter()
            .map(|t| {
                let (name, ty, _) = t;
                return StructField {
                    attributes: Attrs(Vec::new()),
                    ty: ty.clone(),
                    ident: Ident::new(ns, name),
                    val: None,
                };
            })
            .collect();
        let result =
            Decl::Struct { attributes: Attrs(Vec::new()), name: ident.clone(), fields: fields };
        self.counter += 1;
        (result, Ty::Identifier { id: ident, reference: false })
    }
}

pub fn is_placeholder_struct(decl: &Decl) -> bool {
    match decl {
        Decl::Struct { name, .. } => name.name.starts_with(ANONYMOUS_PREFIX),
        _ => false,
    }
}

impl BanjoAst {
    pub fn id_to_decl(&self, fq_ident: &Ident) -> Result<&Decl, ParseError> {
        self.maybe_id_to_decl(fq_ident).ok_or(ParseError::UnknownDecl)
    }

    pub fn maybe_id_to_decl(&self, fq_ident: &Ident) -> Option<&Decl> {
        let (namespace, ident) = fq_ident.fq();
        for decl in self.namespaces[&namespace.unwrap_or(self.primary_namespace.clone())].iter() {
            match decl {
                Decl::Protocol { name, .. } => {
                    if name.name() == ident {
                        return Some(decl);
                    }
                }
                Decl::Struct { name, .. } => {
                    if name.name() == ident {
                        return Some(decl);
                    }
                }
                Decl::Union { name, .. } => {
                    if name.name() == ident {
                        return Some(decl);
                    }
                }
                Decl::Enum { name, .. } => {
                    if name.name() == ident {
                        return Some(decl);
                    }
                }
                Decl::Alias(to, _from) => {
                    if to == fq_ident {
                        return Some(decl);
                    }
                }
                Decl::Constant { name, .. } => {
                    if name.name() == ident {
                        return Some(decl);
                    }
                }
                Decl::Resource { .. } => {}
            }
        }
        None
    }

    pub fn id_to_type(&self, fq_ident: &Ident) -> Ty {
        match self.maybe_id_to_type(fq_ident) {
            Some(ty) => ty,
            None => panic!("Unidentified {:?}", fq_ident),
        }
    }

    pub fn maybe_id_to_type(&self, fq_ident: &Ident) -> Option<Ty> {
        let (ns, ident) = fq_ident.fq();
        match ident.as_str() {
            "bool" => return Some(Ty::Bool),
            "int8" => return Some(Ty::Int8),
            "int16" => return Some(Ty::Int16),
            "int32" => return Some(Ty::Int32),
            "int64" => return Some(Ty::Int64),
            "uint8" => return Some(Ty::UInt8),
            "uint16" => return Some(Ty::UInt16),
            "uint32" => return Some(Ty::UInt32),
            "uint64" => return Some(Ty::UInt64),
            "float32" => return Some(Ty::Float32),
            "float64" => return Some(Ty::Float64),
            "voidptr" => return Some(Ty::Voidptr),
            _ => {}
        };

        let namespace = match ns {
            Some(ref n) => n,
            None => &self.primary_namespace,
        };

        for decl in self.namespaces[namespace].iter() {
            match decl {
                Decl::Protocol { name, .. } => {
                    if name.name() == ident {
                        return Some(Ty::Protocol);
                    }
                }
                Decl::Struct { name, .. } => {
                    if name.name() == ident {
                        return Some(Ty::Struct);
                    }
                }
                Decl::Union { name, .. } => {
                    if name.name() == ident {
                        return Some(Ty::Union);
                    }
                }
                Decl::Enum { name, variants, .. } => {
                    if name.name() == ident {
                        return Some(Ty::Enum);
                    }
                    for variant in variants.iter() {
                        if variant.name == ident {
                            return Some(Ty::Identifier { id: name.clone(), reference: false });
                        }
                    }
                }
                Decl::Alias(to, from) => {
                    if to.name == ident {
                        return self.maybe_id_to_type(from);
                    }
                }
                Decl::Constant { name, ty, .. } => {
                    if name.name() == ident {
                        return Some((*ty).clone());
                    }
                }
                Decl::Resource { .. } => {}
            }
        }
        None
    }

    pub fn id_to_attributes(&self, fq_ident: &Ident) -> Option<&Attrs> {
        let (ns, ident) = fq_ident.fq();

        let namespace = match ns {
            Some(ref n) => n,
            None => &self.primary_namespace,
        };

        for decl in self.namespaces[namespace].iter() {
            match decl {
                Decl::Protocol { name, attributes, .. } => {
                    if name.name() == ident {
                        return Some(attributes);
                    }
                }
                Decl::Struct { name, attributes, .. } => {
                    if name.name() == ident {
                        return Some(attributes);
                    }
                }
                Decl::Union { name, attributes, .. } => {
                    if name.name() == ident {
                        return Some(attributes);
                    }
                }
                Decl::Enum { name, attributes, .. } => {
                    if name.name() == ident {
                        return Some(attributes);
                    }
                }
                Decl::Alias(to, from) => {
                    if to.name == ident {
                        return self.id_to_attributes(from);
                    }
                }
                Decl::Constant { name, attributes, .. } => {
                    if name.name() == ident {
                        return Some(attributes);
                    }
                }
                Decl::Resource { .. } => {}
            }
        }
        None
    }

    /// Parses a declaration represented by a `Rule` object generated by the parser.
    ///
    /// The declaration is attached to the given namespace `ns`.
    /// `placeholder_generator` is used to provide anonymous types which participate in recreating
    /// a declaration ordering identical to FIDL's.
    pub fn parse_decl(
        pair: Pair<'_, Rule>,
        ns: &str,
        _namespaces: &BTreeMap<String, Vec<Decl>>,
        placeholder_generator: &mut PlaceholderGenerator,
    ) -> Result<Vec<Decl>, ParseError> {
        match pair.as_rule() {
            Rule::struct_declaration => {
                let mut attributes = Attrs::default();
                let mut name = String::default();
                let mut fields = Vec::default();
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::attributes => {
                            attributes = Attrs::from_pair(inner_pair)?;
                        }
                        Rule::ident => {
                            name = String::from(inner_pair.as_str().trim());
                        }
                        Rule::struct_field => fields.push(StructField::from_pair(ns, inner_pair)?),
                        e => return Err(ParseError::UnexpectedToken(e)),
                    }
                }
                Ok(vec![Decl::Struct { attributes, name: Ident::new(ns, &name), fields }])
            }
            Rule::enum_declaration => {
                let mut attributes = Attrs::default();
                let mut name = String::default();
                let mut variants = Vec::default();
                let mut ty = Ty::UInt32;
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::attributes => {
                            attributes = Attrs::from_pair(inner_pair)?;
                        }
                        Rule::ident => {
                            name = String::from(inner_pair.as_str().trim());
                        }
                        Rule::integer_type | Rule::identifier_type => {
                            ty = Ty::from_pair(ns, &inner_pair)?;
                        }
                        Rule::enum_field => variants.push(EnumVariant::from_pair(inner_pair)?),
                        e => return Err(ParseError::UnexpectedToken(e)),
                    }
                }
                Ok(vec![Decl::Enum {
                    attributes,
                    name: Ident::new(ns, name.as_str()),
                    ty,
                    variants,
                }])
            }
            Rule::union_declaration => {
                let mut attributes = Attrs::default();
                let mut name = String::default();
                let mut fields = Vec::default();
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::attributes => {
                            attributes = Attrs::from_pair(inner_pair)?;
                        }
                        Rule::ident => {
                            name = String::from(inner_pair.as_str().trim());
                        }
                        Rule::union_field => fields.push(UnionField::from_pair(ns, inner_pair)?),
                        e => return Err(ParseError::UnexpectedToken(e)),
                    }
                }
                Ok(vec![Decl::Union { attributes, name: Ident::new(ns, name.as_str()), fields }])
            }
            // TODO extend to be more expressive for banjo
            Rule::protocol_declaration => {
                let mut attributes = Attrs::default();
                let mut name = String::default();
                let mut methods = Vec::default();
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::attributes => {
                            attributes = Attrs::from_pair(inner_pair)?;
                        }
                        Rule::ident => {
                            name = String::from(inner_pair.as_str());
                        }
                        Rule::protocol_method => methods.push(Method::from_pair(ns, inner_pair)?),
                        e => return Err(ParseError::UnexpectedToken(e)),
                    }
                }
                let mut decls: Vec<Decl> = Vec::new();
                let mut placeholders: Vec<Ty> = Vec::new();
                // Generate an anonymous type for each method.
                // This ensures the protocol is properly ordered in the list of declarations.
                for method in &methods {
                    let (in_decl, in_ty) = placeholder_generator.get(ns, &method.in_params);
                    placeholders.push(in_ty);
                    decls.push(in_decl);
                    let (out_decl, out_ty) = placeholder_generator.get(ns, &method.out_params);
                    placeholders.push(out_ty);
                    decls.push(out_decl);
                }
                decls.push(Decl::Protocol {
                    attributes,
                    name: Ident::new(ns, name.as_str()),
                    methods,
                    placeholders,
                });
                Ok(decls)
            }
            Rule::const_declaration => {
                let mut attributes = Attrs::default();
                let mut name = String::default();
                let mut ty = Ty::UInt32;
                let mut value = Constant(String::from(""));
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::attributes => {
                            attributes = Attrs::from_pair(inner_pair)?;
                        }
                        Rule::ident => {
                            name = String::from(inner_pair.as_str());
                        }
                        Rule::identifier_type | Rule::string_type | Rule::primitive_type => {
                            ty = Ty::from_pair(ns, &inner_pair)?;
                        }
                        Rule::constant => {
                            value = Constant::from_str(inner_pair.clone().as_span().as_str());
                        }
                        e => return Err(ParseError::UnexpectedToken(e)),
                    }
                }
                Ok(vec![Decl::Constant {
                    attributes,
                    name: Ident::new(ns, name.as_str()),
                    ty,
                    value,
                }])
            }
            Rule::resource_declaration => {
                let mut attributes = Attrs::default();
                let mut ty = Ty::UInt32;
                let mut values = Vec::default();
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::attributes => {
                            attributes = Attrs::from_pair(inner_pair)?;
                        }
                        Rule::handle_type | Rule::identifier_type => {
                            ty = Ty::from_pair(ns, &inner_pair)?;
                        }
                        Rule::constant => {
                            values.push(Constant::from_str(inner_pair.clone().as_span().as_str()));
                        }
                        e => return Err(ParseError::UnexpectedToken(e)),
                    }
                }
                Ok(vec![Decl::Resource { attributes, ty, values }])
            }
            Rule::using_decl => {
                let contents: Vec<&str> = pair.clone().into_inner().map(|p| p.as_str()).collect();
                Ok(vec![Decl::Alias(Ident::new(ns, contents[0]), Ident::new(ns, contents[1]))])
            }
            Rule::alias_declaration => {
                let mut to = String::default();
                let mut from = String::default();
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::ident => {
                            if to.is_empty() {
                                to = String::from(inner_pair.as_str());
                            } else {
                                from = String::from(inner_pair.as_str());
                            }
                        }
                        Rule::attributes => {}
                        e => return Err(ParseError::UnexpectedToken(e)),
                    }
                }
                Ok(vec![Decl::Alias(Ident::new(ns, to.as_str()), Ident::new(ns, from.as_str()))])
            }
            e => Err(ParseError::UnexpectedToken(e)),
        }
    }

    /// Finds the `Decl` in the AST for a `Ty` found inside of another `Decl`.
    /// If |ignore_ref| is true and |ty| is a reference to an identifier, `None`
    /// will be returned instead of the appropriate `Decl`.
    pub fn type_to_decl(&self, ty: &Ty, ignore_ref: bool) -> Option<&Decl> {
        match ty {
            Ty::Array { ref ty, .. } => self.type_to_decl(ty, ignore_ref),
            Ty::Vector { ref ty, .. } => self.type_to_decl(ty, ignore_ref),
            Ty::Identifier { id, reference } => {
                // don't add edge for a reference
                if ignore_ref && *reference {
                    return None;
                }

                let (_, ident) = id.fq();
                for decl in self.namespaces[&self.primary_namespace].iter() {
                    match decl {
                        Decl::Union { name, .. }
                        | Decl::Constant { name, .. }
                        | Decl::Protocol { name, .. }
                        | Decl::Enum { name, .. }
                        | Decl::Struct { name, .. } => {
                            if name.name() == ident {
                                return Some(decl);
                            }
                        }
                        Decl::Alias(to, from) => {
                            if to == id {
                                return self.type_to_decl(&self.id_to_type(from), ignore_ref);
                            }
                        }
                        Decl::Resource { .. } => {}
                    }
                }

                if id.is_base_type() {
                    for decl in self.namespaces["zx"].iter() {
                        match decl {
                            Decl::Alias(to, _) => {
                                if to == id {
                                    return Some(decl);
                                }
                            }
                            _ => {}
                        }
                    }
                }

                None
            }
            Ty::Handle { .. } => Some(
                self.id_to_decl(&Ident::new("zx", "handle")).expect("Could not find zx.handle"),
            ),
            _ => None,
        }
    }

    #[allow(dead_code)]
    pub fn is_resource(&self, ty: &Ty) -> bool {
        match ty {
            Ty::Identifier { id, .. } => {
                let target_id = id;
                for decl in self.namespaces[&self.primary_namespace].iter() {
                    match decl {
                        Decl::Resource { ty, .. } => match ty {
                            Ty::Identifier { id, .. } => {
                                if id == target_id {
                                    return true;
                                }
                            }
                            _ => {}
                        },
                        _ => {}
                    }
                }
                false
            }
            Ty::Handle { ty, .. } => {
                let handle_type = ty;
                for decl in self.namespaces[&self.primary_namespace].iter() {
                    match decl {
                        Decl::Resource { ty, .. } => match ty {
                            Ty::Handle { ty, .. } => {
                                if ty == handle_type {
                                    return true;
                                }
                            }
                            _ => {}
                        },
                        _ => {}
                    }
                }
                false
            }
            _ => false,
        }
    }

    // An edge from D1 to D2 means that a C needs to see the declaration
    // of D1 before the declaration of D2. For instance, given the banjo
    //     struct D2 { D1 d; };
    //     struct D1 { int32 x; };
    // D1 has an edge pointing to D2. Note that struct and union pointers,
    // unlike inline structs or unions, do not have dependency edges.
    fn decl_dependencies(&self, decl: &Decl) -> Result<HashSet<&Decl>, ParseError> {
        let mut edges = HashSet::new();

        let mut maybe_add_decl = |ty, ignore_ref| {
            if let Some(type_decl) = self.type_to_decl(ty, ignore_ref) {
                match type_decl {
                    // FIDL omits dependencies on protocols.
                    Decl::Protocol { .. } => (),
                    _ => {
                        edges.insert(type_decl);
                    }
                }
            }
        };

        match decl {
            Decl::Protocol { placeholders, .. } => {
                for placeholder in placeholders {
                    maybe_add_decl(&placeholder, false);
                }
            }
            Decl::Struct { fields, .. } => {
                for field in fields {
                    maybe_add_decl(&field.ty, true);
                }
            }
            Decl::Union { fields, .. } => {
                for field in fields {
                    maybe_add_decl(&field.ty, true);
                }
            }
            Decl::Alias(_to, from) => {
                maybe_add_decl(&self.id_to_type(from), false);
            }
            Decl::Constant { ty, .. } => {
                maybe_add_decl(&ty, false);
            }
            // Enum cannot have dependencies.
            Decl::Enum { .. } => (),
            Decl::Resource { .. } => (),
        };

        Ok(edges)
    }

    /// Validates that the declarations are cycle free and returns declaration ordering.
    pub fn validate_declaration_deps(&self) -> Result<Vec<&Decl>, ParseError> {
        // The number of undelcared dependencies for each decl.
        let mut degrees: BTreeMap<&Decl, u32> = BTreeMap::new();
        // Records the decls that depend on each other.
        let mut inverse_dependencies: BTreeMap<&Decl, Vec<&Decl>> = BTreeMap::new();

        for decl in self.namespaces.iter().flat_map(|(_, decls)| decls.iter()) {
            degrees.insert(&decl, 0);
        }

        for decl in self.namespaces.iter().flat_map(|(_, decls)| decls.iter()) {
            let deps = self.decl_dependencies(&decl)?;
            for dep in deps.iter().filter(|&dep| dep != &decl) {
                let entry = degrees.get_mut(&decl).unwrap();
                *entry += 1;
                let entry = inverse_dependencies.entry(&dep).or_insert(Vec::new());
                entry.push(&decl);
            }
        }

        // Remove mutability.
        let inverse_dependencies = inverse_dependencies;

        // Start with all decls that have no incoming edges.
        // The list is initially sorted in ascending order and newly-available declarations are
        // simply added to the back of the list.
        let mut decls_without_deps = degrees
            .iter()
            .filter(|(_, &degrees)| degrees == 0)
            .map(|(&decl, _)| decl)
            .sorted()
            .collect::<Vec<_>>();

        let mut decl_order = Vec::new();
        // Pull one out of the queue.
        while let Some(decl) = decls_without_deps.pop() {
            assert_eq!(degrees.get(decl), Some(&0));
            decl_order.push(decl);

            // Decrement the incoming degree of all other decls it points to.
            if let Some(inverse_deps) = inverse_dependencies.get(decl) {
                for inverse_dep in inverse_deps.iter().sorted() {
                    let degree = degrees.get_mut(inverse_dep).unwrap();
                    assert_ne!(*degree, 0);
                    *degree -= 1;
                    if *degree == 0 {
                        decls_without_deps.push(inverse_dep);
                    }
                }
            }
        }

        if decl_order.len() != degrees.len() {
            // We didn't visit all the edges! There was a cycle.
            return Err(ParseError::InvalidDeps(String::from(
                "There is a cycle in the declarations",
            )));
        }

        Ok(decl_order
            .into_iter()
            .filter(|decl| {
                if is_placeholder_struct(decl) {
                    return false;
                }
                let ident: Option<&Ident> = match decl {
                    Decl::Protocol { name, .. } => Some(name),
                    Decl::Struct { name, .. } => Some(name),
                    Decl::Union { name, .. } => Some(name),
                    Decl::Enum { name, .. } => Some(name),
                    Decl::Alias(to, _from) => Some(to),
                    Decl::Constant { name, .. } => Some(name),
                    Decl::Resource { .. } => None,
                };
                match ident {
                    Some(ident) => {
                        if let Some(ref ns) = ident.fq().0 {
                            ns == &self.primary_namespace
                        } else {
                            true
                        }
                    }
                    None => true,
                }
            })
            .collect())
    }

    // Validates that the constants are of the right type.
    fn validate_constants(&self) -> Result<(), ParseError> {
        // Search ast for constants.
        for (ty, constant) in
            self.namespaces.iter().flat_map(|(_, decls)| decls.iter()).filter_map(|decl| match decl
            {
                Decl::Constant { ty, value, .. } => Some((ty, value)),
                _ => None,
            })
        {
            let Constant(string) = constant;
            if string.len() > 2
                && (string.get(0..2) == Some("0x") || string.get(0..2) == Some("0b"))
            {
                // TODO(bwb): validate constants if hex as well
                return Ok(());
            }
            match ty {
                Ty::Int8 => {
                    i8::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::Int16 => {
                    i16::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::Int32 => {
                    i32::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::Int64 => {
                    i64::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::UInt8 => {
                    u8::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::UInt16 => {
                    u16::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::UInt32 | Ty::UInt64 => {
                    u32::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::Bool => {
                    bool::from_str(string)
                        .map_err(|_| ParseError::InvalidConstType(constant.clone(), ty.clone()))?;
                }
                Ty::Str { .. } => {
                    if !string.starts_with("\"") || !string.ends_with("\"") {
                        return Err(ParseError::InvalidConstType(constant.clone(), ty.clone()));
                    }
                }
                Ty::Identifier { id, .. } => {
                    let ident = Ident::new_raw(string);
                    // Special case: enum references "Enum.foo".
                    if let Some(ns) = &ident.namespace {
                        if ns == &id.name {
                            // The constant is a reference to an enum variant.
                            continue;
                        }
                    }
                    return Err(ParseError::InvalidConstType(constant.clone(), ty.clone()));
                }
                _ => {
                    return Err(ParseError::InvalidConstType(constant.clone(), ty.clone()));
                }
            };
        }

        // TODO(surajmalhotra): Find every bound array, string, and validate their bound is a valid
        // usize.
        Ok(())
    }

    pub fn parse(pair_vec: Vec<Pairs<'_, Rule>>) -> Result<Self, ParseError> {
        let mut primary_namespace = None;
        let mut namespaces = BTreeMap::default();

        let mut placeholder_generator = PlaceholderGenerator::new();

        for pairs in pair_vec {
            for pair in pairs {
                let mut current_namespace = String::default();
                for inner_pair in pair.into_inner() {
                    match inner_pair.as_rule() {
                        Rule::library_header => {
                            for token in inner_pair.into_inner() {
                                if Rule::compound_ident == token.as_rule() {
                                    current_namespace = String::from(token.as_str());
                                    // Keep updating the primary namespace as it should be the last
                                    // one we encounter.
                                    primary_namespace = Some(String::from(token.as_str()));
                                    if !namespaces.contains_key(&current_namespace) {
                                        namespaces
                                            .insert(current_namespace.clone(), Vec::default());
                                    }
                                }
                            }
                        }
                        Rule::using => {
                            for (cnt, pair) in inner_pair.into_inner().enumerate() {
                                if cnt == 0 {
                                    if !namespaces.contains_key(pair.as_str()) {
                                        return Err(ParseError::UnImported(format!(
                                            "{}",
                                            pair.as_str()
                                        )));
                                    }
                                } else {
                                    return Err(ParseError::NotYetSupported(String::from(
                                        "'as' in library imports",
                                    )));
                                }
                            }
                        }
                        Rule::struct_declaration
                        | Rule::union_declaration
                        | Rule::enum_declaration
                        | Rule::protocol_declaration
                        | Rule::const_declaration
                        | Rule::resource_declaration
                        | Rule::using_decl
                        | Rule::alias_declaration => {
                            let mut decls = Self::parse_decl(
                                inner_pair,
                                &current_namespace,
                                &namespaces,
                                &mut placeholder_generator,
                            )?;
                            namespaces.get_mut(&current_namespace).unwrap().append(&mut decls)
                        }
                        Rule::EOI => (),
                        e => return Err(ParseError::UnexpectedToken(e)),
                    };
                }
            }
        }

        let ast =
            BanjoAst { primary_namespace: primary_namespace.unwrap(), namespaces: namespaces };

        let _ = ast.validate_declaration_deps()?;
        ast.validate_constants()?;

        Ok(ast)
    }
}
