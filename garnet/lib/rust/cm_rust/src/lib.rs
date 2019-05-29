// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_fidl_validator,
    failure::Fail,
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
    std::collections::HashMap,
    std::convert::{From, TryFrom, TryInto},
    std::fmt,
};

pub mod data;

/// Converts a fidl object into its corresponding native representation.
pub trait FidlIntoNative<T> {
    fn fidl_into_native(self) -> T;
}

pub trait NativeIntoFidl<T> {
    fn native_into_fidl(self) -> T;
}

/// Generates `FidlIntoNative` and `NativeIntoFidl` implementations for a basic type from
/// `Option<type>` that respectively unwraps the Option and wraps the internal value in a `Some()`.
macro_rules! fidl_translations_opt_type {
    ($into_type:ty) => {
        impl FidlIntoNative<$into_type> for Option<$into_type> {
            fn fidl_into_native(self) -> $into_type {
                self.unwrap()
            }
        }
        impl NativeIntoFidl<Option<$into_type>> for $into_type {
            fn native_into_fidl(self) -> Option<$into_type> {
                Some(self)
            }
        }
    };
}

/// Generates `FidlIntoNative` and `NativeIntoFidl` implementations that leaves the input unchanged.
macro_rules! fidl_translations_identical {
    ($into_type:ty) => {
        impl FidlIntoNative<$into_type> for $into_type {
            fn fidl_into_native(self) -> $into_type {
                self
            }
        }
        impl NativeIntoFidl<$into_type> for $into_type {
            fn native_into_fidl(self) -> Self {
                self
            }
        }
    };
}

/// Generates a struct with a `FidlIntoNative` implementation that calls `fidl_into_native()` on each
/// field.
/// - `into_type` is the name of the struct and the into type for the conversion.
/// - `into_ident` must be identical to `into_type`.
/// - `from_type` is the from type for the conversion.
/// - `from_ident` must be identical to `from_type`.
/// - `field: type` form a list of fields and their types for the generated struct.
macro_rules! fidl_into_struct {
    ($into_type:ty, $into_ident:ident, $from_type:ty, $from_path:path,
     { $( $field:ident: $type:ty, )+ } ) => {
        #[derive(Debug, Clone, PartialEq)]
        pub struct $into_ident {
            $(
                pub $field: $type,
            )+
        }

        impl FidlIntoNative<$into_type> for $from_type {
            fn fidl_into_native(self) -> $into_type {
                $into_ident { $( $field: self.$field.fidl_into_native(), )+ }
            }
        }

        impl NativeIntoFidl<$from_type> for $into_type {
            fn native_into_fidl(self) -> $from_type {
                use $from_path as from_ident;
                from_ident { $( $field: self.$field.native_into_fidl(), )+ }
            }
        }
    }
}

/// Generates an enum with a `FidlIntoNative` implementation that calls `fidl_into_native()` on each
/// field.
/// - `into_type` is the name of the enum and the into type for the conversion.
/// - `into_ident` must be identical to `into_type`.
/// - `from_type` is the from type for the conversion.
/// - `from_ident` must be identical to `from_type`.
/// - `variant(type)` form a list of variants and their types for the generated enum.
macro_rules! fidl_into_enum {
    ($into_type:ty, $into_ident:ident, $from_type:ty, $from_path:path,
     { $( $variant:ident($type:ty), )+ } ) => {
        #[derive(Debug, Clone, PartialEq)]
        pub enum $into_ident {
            $(
                $variant($type),
            )+
        }

        impl FidlIntoNative<$into_type> for $from_type {
            fn fidl_into_native(self) -> $into_type {
                use $from_path as from_ident;
                match self {
                    $(
                    from_ident::$variant(e) => $into_ident::$variant(e.fidl_into_native()),
                    )+
                    from_ident::__UnknownVariant {..} => { panic!("invalid variant") }
                }
            }
        }

        impl NativeIntoFidl<$from_type> for $into_type {
            fn native_into_fidl(self) -> $from_type {
                use $from_path as from_ident;
                match self {
                    $(
                        $into_ident::$variant(e) => from_ident::$variant(e.native_into_fidl()),
                    )+
                }
            }
        }
    }
}

/// Generates `FidlIntoNative` and `NativeIntoFidl` implementations between `Vec` and
/// `Option<Vec>`.
/// - `into_type` is the name of the struct and the into type for the conversion.
/// - `from_type` is the from type for the conversion.
macro_rules! fidl_into_vec {
    ($into_type:ty, $from_type:ty) => {
        impl FidlIntoNative<Vec<$into_type>> for Option<Vec<$from_type>> {
            fn fidl_into_native(self) -> Vec<$into_type> {
                if let Some(from) = self {
                    from.into_iter().map(|e: $from_type| e.fidl_into_native()).collect()
                } else {
                    vec![]
                }
            }
        }

        impl NativeIntoFidl<Option<Vec<$from_type>>> for Vec<$into_type> {
            fn native_into_fidl(self) -> Option<Vec<$from_type>> {
                if self.is_empty() {
                    None
                } else {
                    Some(self.into_iter().map(|e: $into_type| e.native_into_fidl()).collect())
                }
            }
        }
    };
}

#[derive(Debug, PartialEq)]
pub struct ComponentDecl {
    pub program: Option<fdata::Dictionary>,
    pub uses: Vec<UseDecl>,
    pub exposes: Vec<ExposeDecl>,
    pub offers: Vec<OfferDecl>,
    pub children: Vec<ChildDecl>,
    pub collections: Vec<CollectionDecl>,
    pub facets: Option<fdata::Dictionary>,
}

impl FidlIntoNative<ComponentDecl> for fsys::ComponentDecl {
    fn fidl_into_native(self) -> ComponentDecl {
        ComponentDecl {
            program: self.program.fidl_into_native(),
            uses: self.uses.fidl_into_native(),
            exposes: self.exposes.fidl_into_native(),
            offers: self.offers.fidl_into_native(),
            children: self.children.fidl_into_native(),
            collections: self.collections.fidl_into_native(),
            facets: self.facets.fidl_into_native(),
        }
    }
}

impl NativeIntoFidl<fsys::ComponentDecl> for ComponentDecl {
    fn native_into_fidl(self) -> fsys::ComponentDecl {
        fsys::ComponentDecl {
            program: self.program.native_into_fidl(),
            uses: self.uses.native_into_fidl(),
            exposes: self.exposes.native_into_fidl(),
            offers: self.offers.native_into_fidl(),
            children: self.children.native_into_fidl(),
            collections: self.collections.native_into_fidl(),
            facets: self.facets.native_into_fidl(),
            storage: None,
        }
    }
}

impl Clone for ComponentDecl {
    fn clone(&self) -> Self {
        ComponentDecl {
            program: data::clone_option_dictionary(&self.program),
            uses: self.uses.clone(),
            exposes: self.exposes.clone(),
            offers: self.offers.clone(),
            children: self.children.clone(),
            collections: self.collections.clone(),
            facets: data::clone_option_dictionary(&self.facets),
        }
    }
}

impl ComponentDecl {
    /// Returns the `ExposeDecl` that exposes `capability`, if it exists.
    pub fn find_expose_source<'a>(&'a self, capability: &Capability) -> Option<&'a ExposeDecl> {
        self.exposes.iter().find(|&e| match (capability, e) {
            (Capability::Service(p), ExposeDecl::Service(d)) => d.target_path == *p,
            (Capability::Directory(p), ExposeDecl::Directory(d)) => d.target_path == *p,
            _ => false,
        })
    }

    /// Returns the `OfferDecl` that offers `capability` to `child_name`, if it exists.
    pub fn find_offer_source<'a>(
        &'a self,
        capability: &Capability,
        child_name: &str,
    ) -> Option<&'a OfferDecl> {
        self.offers.iter().find(|&offer| match (capability, offer) {
            (Capability::Service(p), OfferDecl::Service(d)) => d
                .targets
                .iter()
                .find(|&e| {
                    if let OfferDest::Child(target_child_name) = &e.dest {
                        e.target_path == *p && target_child_name == child_name
                    } else {
                        false
                    }
                })
                .is_some(),
            (Capability::Directory(p), OfferDecl::Directory(d)) => d
                .targets
                .iter()
                .find(|&e| {
                    if let OfferDest::Child(target_child_name) = &e.dest {
                        e.target_path == *p && target_child_name == child_name
                    } else {
                        false
                    }
                })
                .is_some(),
            _ => false,
        })
    }
}

fidl_into_enum!(UseDecl, UseDecl, fsys::UseDecl, fsys::UseDecl,
                {
                    Service(UseServiceDecl),
                    Directory(UseDirectoryDecl),
                });
fidl_into_struct!(UseServiceDecl, UseServiceDecl, fsys::UseServiceDecl, fsys::UseServiceDecl,
                  {
                      source_path: CapabilityPath,
                      target_path: CapabilityPath,
                  });
fidl_into_struct!(UseDirectoryDecl, UseDirectoryDecl, fsys::UseDirectoryDecl,
                  fsys::UseDirectoryDecl,
                  {
                      source_path: CapabilityPath,
                      target_path: CapabilityPath,
                  });

fidl_into_enum!(ExposeDecl, ExposeDecl, fsys::ExposeDecl, fsys::ExposeDecl,
                {
                    Service(ExposeServiceDecl),
                    Directory(ExposeDirectoryDecl),
                });
fidl_into_struct!(ExposeServiceDecl, ExposeServiceDecl, fsys::ExposeServiceDecl,
                  fsys::ExposeServiceDecl,
                  {
                      source: ExposeSource,
                      source_path: CapabilityPath,
                      target_path: CapabilityPath,
                  });
fidl_into_struct!(ExposeDirectoryDecl, ExposeDirectoryDecl, fsys::ExposeDirectoryDecl,
                  fsys::ExposeDirectoryDecl,
                  {
                      source: ExposeSource,
                      source_path: CapabilityPath,
                      target_path: CapabilityPath,
                  });

fidl_into_enum!(OfferDecl, OfferDecl, fsys::OfferDecl, fsys::OfferDecl,
                {
                    Service(OfferServiceDecl),
                    Directory(OfferDirectoryDecl),
                });
fidl_into_struct!(OfferServiceDecl, OfferServiceDecl, fsys::OfferServiceDecl,
                  fsys::OfferServiceDecl,
                  {
                      source: OfferSource,
                      source_path: CapabilityPath,
                      targets: Vec<OfferTarget>,
                  });
fidl_into_struct!(OfferDirectoryDecl, OfferDirectoryDecl, fsys::OfferDirectoryDecl,
                  fsys::OfferDirectoryDecl,
                  {
                      source: OfferSource,
                      source_path: CapabilityPath,
                      targets: Vec<OfferTarget>,
                  });
fidl_into_struct!(OfferTarget, OfferTarget, fsys::OfferTarget, fsys::OfferTarget,
                  {
                      target_path: CapabilityPath,
                      dest: OfferDest,
                  });

fidl_into_struct!(ChildDecl, ChildDecl, fsys::ChildDecl, fsys::ChildDecl,
                  {
                      name: String,
                      url: String,
                      startup: fsys::StartupMode,
                  });

fidl_into_struct!(CollectionDecl, CollectionDecl, fsys::CollectionDecl, fsys::CollectionDecl,
                  {
                      name: String,
                      durability: fsys::Durability,
                  });

fidl_into_vec!(UseDecl, fsys::UseDecl);
fidl_into_vec!(ExposeDecl, fsys::ExposeDecl);
fidl_into_vec!(OfferDecl, fsys::OfferDecl);
fidl_into_vec!(ChildDecl, fsys::ChildDecl);
fidl_into_vec!(CollectionDecl, fsys::CollectionDecl);
fidl_into_vec!(OfferTarget, fsys::OfferTarget);
fidl_translations_opt_type!(String);
fidl_translations_opt_type!(fsys::StartupMode);
fidl_translations_opt_type!(fsys::Durability);
fidl_translations_opt_type!(fdata::Dictionary);
fidl_translations_identical!(Option<fdata::Dictionary>);

#[derive(Debug, Clone, PartialEq, Hash)]
pub struct CapabilityPath {
    pub dirname: String,
    pub basename: String,
}

impl CapabilityPath {
    pub fn to_string(&self) -> String {
        format!("{}", self)
    }
}

impl TryFrom<&str> for CapabilityPath {
    type Error = Error;
    fn try_from(path: &str) -> Result<CapabilityPath, Error> {
        if !path.starts_with("/") {
            // Paths must be absolute.
            return Err(Error::InvalidCapabilityPath { raw: path.to_string() });
        }
        let mut parts = vec![];
        let mut parts_iter = path.split("/");
        // Skip empty component generated by first '/'.
        parts_iter.next();
        for part in parts_iter {
            if part.is_empty() {
                // Paths may not have empty components.
                return Err(Error::InvalidCapabilityPath { raw: path.to_string() });
            }
            parts.push(part);
        }
        if parts.is_empty() {
            // Root paths are not allowed.
            return Err(Error::InvalidCapabilityPath { raw: path.to_string() });
        }
        Ok(CapabilityPath {
            dirname: format!("/{}", parts[..parts.len() - 1].join("/")),
            basename: parts.last().unwrap().to_string(),
        })
    }
}

impl fmt::Display for CapabilityPath {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if &self.dirname == "/" {
            write!(f, "/{}", self.basename)
        } else {
            write!(f, "{}/{}", self.dirname, self.basename)
        }
    }
}

// TODO: Runners and third parties can use this to parse `program` or `facets`.
impl FidlIntoNative<Option<HashMap<String, Value>>> for Option<fdata::Dictionary> {
    fn fidl_into_native(self) -> Option<HashMap<String, Value>> {
        self.map(|d| from_fidl_dict(d))
    }
}

impl FidlIntoNative<CapabilityPath> for Option<String> {
    fn fidl_into_native(self) -> CapabilityPath {
        let s: &str = &self.unwrap();
        s.try_into().expect("invalid capability path")
    }
}

impl NativeIntoFidl<Option<String>> for CapabilityPath {
    fn native_into_fidl(self) -> Option<String> {
        Some(self.to_string())
    }
}

#[derive(Debug, PartialEq)]
pub enum Value {
    Bit(bool),
    Inum(i64),
    Fnum(f64),
    Str(String),
    Vec(Vec<Value>),
    Dict(HashMap<String, Value>),
    Null,
}

impl FidlIntoNative<Value> for Option<Box<fdata::Value>> {
    fn fidl_into_native(self) -> Value {
        match self {
            Some(v) => match *v {
                fdata::Value::Bit(b) => Value::Bit(b),
                fdata::Value::Inum(i) => Value::Inum(i),
                fdata::Value::Fnum(f) => Value::Fnum(f),
                fdata::Value::Str(s) => Value::Str(s),
                fdata::Value::Vec(v) => Value::Vec(from_fidl_vec(v)),
                fdata::Value::Dict(d) => Value::Dict(from_fidl_dict(d)),
            },
            None => Value::Null,
        }
    }
}

fn from_fidl_vec(vec: fdata::Vector) -> Vec<Value> {
    vec.values.into_iter().map(|v| v.fidl_into_native()).collect()
}

fn from_fidl_dict(dict: fdata::Dictionary) -> HashMap<String, Value> {
    dict.entries.into_iter().map(|e| (e.key, e.value.fidl_into_native())).collect()
}

/// Generic container for any capability type. Doesn't map onto any fidl declaration, but useful as
/// an intermediate representation.
#[derive(Debug, Clone, PartialEq)]
pub enum Capability {
    Service(CapabilityPath),
    Directory(CapabilityPath),
}

impl Capability {
    pub fn path(&self) -> &CapabilityPath {
        match self {
            Capability::Service(s) => s,
            Capability::Directory(d) => d,
        }
    }
}

impl fmt::Display for Capability {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Capability::Service(s) => write!(f, "service at {}", s),
            Capability::Directory(d) => write!(f, "directory at {}", d),
        }
    }
}

impl From<UseDecl> for Capability {
    fn from(d: UseDecl) -> Self {
        match d {
            UseDecl::Service(d) => d.into(),
            UseDecl::Directory(d) => d.into(),
        }
    }
}

impl From<UseServiceDecl> for Capability {
    fn from(d: UseServiceDecl) -> Self {
        Capability::Service(d.source_path)
    }
}

impl From<UseDirectoryDecl> for Capability {
    fn from(d: UseDirectoryDecl) -> Self {
        Capability::Directory(d.source_path)
    }
}

impl From<ExposeDecl> for Capability {
    fn from(d: ExposeDecl) -> Self {
        match d {
            ExposeDecl::Service(d) => d.into(),
            ExposeDecl::Directory(d) => d.into(),
        }
    }
}

impl From<ExposeServiceDecl> for Capability {
    fn from(d: ExposeServiceDecl) -> Self {
        Capability::Service(d.source_path)
    }
}

impl From<ExposeDirectoryDecl> for Capability {
    fn from(d: ExposeDirectoryDecl) -> Self {
        Capability::Directory(d.source_path)
    }
}

impl From<OfferDecl> for Capability {
    fn from(d: OfferDecl) -> Self {
        match d {
            OfferDecl::Service(d) => d.into(),
            OfferDecl::Directory(d) => d.into(),
        }
    }
}

impl From<OfferServiceDecl> for Capability {
    fn from(d: OfferServiceDecl) -> Self {
        Capability::Service(d.source_path)
    }
}

impl From<OfferDirectoryDecl> for Capability {
    fn from(d: OfferDirectoryDecl) -> Self {
        Capability::Directory(d.source_path)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum ExposeSource {
    Myself,
    Child(String),
}

impl FidlIntoNative<ExposeSource> for Option<fsys::ExposeSource> {
    fn fidl_into_native(self) -> ExposeSource {
        match self.unwrap() {
            fsys::ExposeSource::Myself(_) => ExposeSource::Myself,
            fsys::ExposeSource::Child(c) => ExposeSource::Child(c.name.unwrap()),
            fsys::ExposeSource::__UnknownVariant { .. } => panic!("unknown ExposeSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::ExposeSource>> for ExposeSource {
    fn native_into_fidl(self) -> Option<fsys::ExposeSource> {
        Some(match self {
            ExposeSource::Myself => fsys::ExposeSource::Myself(fsys::SelfRef {}),
            ExposeSource::Child(child_name) => fsys::ExposeSource::Child(fsys::ChildRef {
                name: Some(child_name),
                collection: None,
            }),
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferSource {
    Realm,
    Myself,
    Child(String),
}

impl FidlIntoNative<OfferSource> for Option<fsys::OfferSource> {
    fn fidl_into_native(self) -> OfferSource {
        match self.unwrap() {
            fsys::OfferSource::Realm(_) => OfferSource::Realm,
            fsys::OfferSource::Myself(_) => OfferSource::Myself,
            fsys::OfferSource::Child(c) => OfferSource::Child(c.name.unwrap()),
            fsys::OfferSource::__UnknownVariant { .. } => panic!("unknown OfferSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::OfferSource>> for OfferSource {
    fn native_into_fidl(self) -> Option<fsys::OfferSource> {
        Some(match self {
            OfferSource::Realm => fsys::OfferSource::Realm(fsys::RealmRef {}),
            OfferSource::Myself => fsys::OfferSource::Myself(fsys::SelfRef {}),
            OfferSource::Child(child_name) => fsys::OfferSource::Child(fsys::ChildRef {
                name: Some(child_name),
                collection: None,
            }),
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferDest {
    Child(String),
    Collection(String),
}

impl FidlIntoNative<OfferDest> for Option<fsys::OfferDest> {
    fn fidl_into_native(self) -> OfferDest {
        match self.unwrap() {
            fsys::OfferDest::Child(c) => OfferDest::Child(c.name.unwrap()),
            fsys::OfferDest::Collection(c) => OfferDest::Collection(c.name.unwrap()),
            fsys::OfferDest::__UnknownVariant { .. } => panic!("unknown OfferDest variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::OfferDest>> for OfferDest {
    fn native_into_fidl(self) -> Option<fsys::OfferDest> {
        Some(match self {
            OfferDest::Child(child_name) => {
                fsys::OfferDest::Child(fsys::ChildRef { name: Some(child_name), collection: None })
            }
            OfferDest::Collection(collection_name) => {
                fsys::OfferDest::Collection(fsys::CollectionRef { name: Some(collection_name) })
            }
        })
    }
}

/// Converts the contents of a CM-FIDL declaration and produces the equivalent CM-Rust
/// struct.
/// This function applies cm_fidl_validator to check correctness.
impl TryFrom<fsys::ComponentDecl> for ComponentDecl {
    type Error = Error;

    fn try_from(decl: fsys::ComponentDecl) -> Result<Self, Self::Error> {
        cm_fidl_validator::validate(&decl).map_err(|err| Error::Validate { err })?;
        Ok(decl.fidl_into_native())
    }
}

// Converts the contents of a CM-Rust declaration into a CM_FIDL declaration
impl TryFrom<ComponentDecl> for fsys::ComponentDecl {
    type Error = Error;
    fn try_from(decl: ComponentDecl) -> Result<Self, Self::Error> {
        Ok(decl.native_into_fidl())
    }
}

/// Errors produced by cm_rust.
#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "Fidl validation failed: {}", err)]
    Validate {
        #[fail(cause)]
        err: cm_fidl_validator::ErrorList,
    },
    #[fail(display = "Invalid capability path: {}", raw)]
    InvalidCapabilityPath { raw: String },
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! test_from {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    test_from_helper($input, $result);
                }
            )+
        }
    }

    fn test_from_helper<T, U>(input: Vec<T>, result: Vec<U>)
    where
        U: From<T> + std::cmp::PartialEq + std::fmt::Debug,
    {
        let res: Vec<U> = input.into_iter().map(|e| e.into()).collect();
        assert_eq!(res, result);
    }

    macro_rules! test_try_from_decl {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    {
                        let res = ComponentDecl::try_from($input).expect("try_from failed");
                        assert_eq!(res, $result);
                    }
                    {
                        let res = fsys::ComponentDecl::try_from($result).expect("try_from failed");
                        assert_eq!(res, $input);
                    }
                }
            )+
        }
    }

    macro_rules! test_fidl_into_and_from {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    {
                        let res: Vec<_> =
                            $input.into_iter().map(|e| e.fidl_into_native()).collect();
                        assert_eq!(res, $result);
                    }
                    {
                        let res: Vec<_> =
                            $result.into_iter().map(|e| e.native_into_fidl()).collect();
                        assert_eq!(res, $input);
                    }
                }
            )+
        }
    }

    macro_rules! test_fidl_into {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    test_fidl_into_helper($input, $result);
                }
            )+
        }
    }

    fn test_fidl_into_helper<T, U>(input: T, expected_res: U)
    where
        T: FidlIntoNative<U>,
        U: std::cmp::PartialEq + std::fmt::Debug,
    {
        let res: U = input.fidl_into_native();
        assert_eq!(res, expected_res);
    }

    macro_rules! test_capability_path {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    test_capability_path_helper($input, $result);
                }
            )+
        }
    }

    fn test_capability_path_helper(input: &str, result: Result<CapabilityPath, Error>) {
        let res = CapabilityPath::try_from(input);
        assert_eq!(format!("{:?}", res), format!("{:?}", result));
        if let Ok(p) = res {
            assert_eq!(&p.to_string(), input);
        }
    }

    test_try_from_decl! {
        try_from_empty => {
            input = fsys::ComponentDecl {
                program: None,
                uses: None,
                exposes: None,
                offers: None,
                children: None,
                collections: None,
                facets: None,
                storage: None,
            },
            result = ComponentDecl {
                program: None,
                uses: vec![],
                exposes: vec![],
                offers: vec![],
                children: vec![],
                collections: vec![],
                facets: None,
            },
        },
        try_from_all => {
            input = fsys::ComponentDecl {
               program: Some(fdata::Dictionary{entries: vec![
                   fdata::Entry{
                       key: "binary".to_string(),
                       value: Some(Box::new(fdata::Value::Str("bin/app".to_string()))),
                   },
               ]}),
               uses: Some(vec![
                   fsys::UseDecl::Service(fsys::UseServiceDecl {
                       source_path: Some("/svc/netstack".to_string()),
                       target_path: Some("/svc/mynetstack".to_string()),
                   }),
                   fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                       source_path: Some("/data/dir".to_string()),
                       target_path: Some("/data".to_string()),
                   }),
               ]),
               exposes: Some(vec![
                   fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                       source: Some(fsys::ExposeSource::Child(fsys::ChildRef {
                           name: Some("netstack".to_string()),
                           collection: None,
                       })),
                       source_path: Some("/svc/netstack".to_string()),
                       target_path: Some("/svc/mynetstack".to_string()),
                   }),
                   fsys::ExposeDecl::Directory(fsys::ExposeDirectoryDecl {
                       source: Some(fsys::ExposeSource::Child(fsys::ChildRef {
                           name: Some("netstack".to_string()),
                           collection: None,
                       })),
                       source_path: Some("/data/dir".to_string()),
                       target_path: Some("/data".to_string()),
                   }),
               ]),
               offers: Some(vec![
                   fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                       source: Some(fsys::OfferSource::Realm(fsys::RealmRef {})),
                       source_path: Some("/svc/netstack".to_string()),
                       targets: Some(vec![
                           fsys::OfferTarget{
                               target_path: Some("/svc/mynetstack".to_string()),
                               dest: Some(fsys::OfferDest::Child(
                                  fsys::ChildRef {
                                      name: Some("echo".to_string()),
                                      collection: None,
                                  }
                               )),
                           },
                       ]),
                   }),
                   fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                       source: Some(fsys::OfferSource::Realm(fsys::RealmRef {})),
                       source_path: Some("/data/dir".to_string()),
                       targets: Some(vec![
                           fsys::OfferTarget{
                               target_path: Some("/data".to_string()),
                               dest: Some(fsys::OfferDest::Collection(
                                   fsys::CollectionRef { name: Some("modular".to_string()) }
                               )),
                           },
                       ]),
                   }),
               ]),
               children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                    },
                    fsys::ChildDecl {
                        name: Some("echo".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/echo#meta/echo.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                    },
               ]),
               collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                    },
                    fsys::CollectionDecl {
                        name: Some("tests".to_string()),
                        durability: Some(fsys::Durability::Transient),
                    },
               ]),
               facets: Some(fdata::Dictionary{entries: vec![
                   fdata::Entry{
                       key: "author".to_string(),
                       value: Some(Box::new(fdata::Value::Str("Fuchsia".to_string()))),
                   },
               ]}),
               storage: None,
            },
            result = {
                ComponentDecl {
                    program: Some(fdata::Dictionary{entries: vec![
                        fdata::Entry{
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::Value::Str("bin/app".to_string()))),
                        },
                    ]}),
                    uses: vec![
                        UseDecl::Service(UseServiceDecl {
                            source_path: "/svc/netstack".try_into().unwrap(),
                            target_path: "/svc/mynetstack".try_into().unwrap(),
                        }),
                        UseDecl::Directory(UseDirectoryDecl {
                            source_path: "/data/dir".try_into().unwrap(),
                            target_path: "/data".try_into().unwrap(),
                        }),
                    ],
                    exposes: vec![
                        ExposeDecl::Service(ExposeServiceDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_path: "/svc/netstack".try_into().unwrap(),
                            target_path: "/svc/mynetstack".try_into().unwrap(),
                        }),
                        ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_path: "/data/dir".try_into().unwrap(),
                            target_path: "/data".try_into().unwrap(),
                        }),
                    ],
                    offers: vec![
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Realm,
                            source_path: "/svc/netstack".try_into().unwrap(),
                            targets: vec![
                                OfferTarget {
                                    target_path: "/svc/mynetstack".try_into().unwrap(),
                                    dest: OfferDest::Child("echo".to_string()),
                                },
                            ],
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::Realm,
                            source_path: "/data/dir".try_into().unwrap(),
                            targets: vec![
                                OfferTarget {
                                    target_path: "/data".try_into().unwrap(),
                                    dest: OfferDest::Collection("modular".to_string()),
                                },
                            ],
                        }),
                    ],
                    children: vec![
                        ChildDecl {
                            name: "netstack".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm".to_string(),
                            startup: fsys::StartupMode::Lazy,
                        },
                        ChildDecl {
                            name: "echo".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/echo#meta/echo.cm".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                    ],
                    collections: vec![
                        CollectionDecl {
                            name: "modular".to_string(),
                            durability: fsys::Durability::Persistent,
                        },
                        CollectionDecl {
                            name: "tests".to_string(),
                            durability: fsys::Durability::Transient,
                        },
                    ],
                    facets: Some(fdata::Dictionary{entries: vec![
                       fdata::Entry{
                           key: "author".to_string(),
                           value: Some(Box::new(fdata::Value::Str("Fuchsia".to_string()))),
                       },
                    ]}),
                }
            },
        },
    }

    test_capability_path! {
        capability_path_one_part => {
            input = "/foo",
            result = Ok(CapabilityPath{dirname: "/".to_string(), basename: "foo".to_string()}),
        },
        capability_path_two_parts => {
            input = "/foo/bar",
            result = Ok(CapabilityPath{dirname: "/foo".to_string(), basename: "bar".to_string()}),
        },
        capability_path_many_parts => {
            input = "/foo/bar/long/path",
            result = Ok(CapabilityPath{
                dirname: "/foo/bar/long".to_string(),
                basename: "path".to_string()
            }),
        },
        capability_path_invalid_empty_part => {
            input = "/foo/bar//long/path",
            result = Err(Error::InvalidCapabilityPath{raw: "/foo/bar//long/path".to_string()}),
        },
        capability_path_invalid_empty => {
            input = "",
            result = Err(Error::InvalidCapabilityPath{raw: "".to_string()}),
        },
        capability_path_invalid_root => {
            input = "/",
            result = Err(Error::InvalidCapabilityPath{raw: "/".to_string()}),
        },
        capability_path_invalid_relative => {
            input = "foo/bar",
            result = Err(Error::InvalidCapabilityPath{raw: "foo/bar".to_string()}),
        },
        capability_path_invalid_trailing => {
            input = "/foo/bar/",
            result = Err(Error::InvalidCapabilityPath{raw: "/foo/bar/".to_string()}),
        },
    }

    test_from! {
        from_use_capability => {
            input = vec![
                UseDecl::Service(UseServiceDecl {
                    source_path: CapabilityPath::try_from("/foo/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/blah").unwrap(),
                }),
                UseDecl::Directory(UseDirectoryDecl {
                    source_path: CapabilityPath::try_from("/foo/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/blah").unwrap(),
                }),
            ],
            result = vec![
                Capability::Service(CapabilityPath::try_from("/foo/bar").unwrap()),
                Capability::Directory(CapabilityPath::try_from("/foo/bar").unwrap()),
            ],
        },
        from_expose_capability => {
            input = vec![
                ExposeDecl::Service(ExposeServiceDecl {
                    source: ExposeSource::Myself,
                    source_path: CapabilityPath::try_from("/foo/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/blah").unwrap(),
                }),
                ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Myself,
                    source_path: CapabilityPath::try_from("/foo/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/blah").unwrap(),
                }),
            ],
            result = vec![
                Capability::Service(CapabilityPath::try_from("/foo/bar").unwrap()),
                Capability::Directory(CapabilityPath::try_from("/foo/bar").unwrap()),
            ],
        },
        from_offer_capability => {
            input = vec![
                OfferDecl::Service(OfferServiceDecl {
                    source: OfferSource::Myself,
                    source_path: CapabilityPath::try_from("/foo/bar").unwrap(),
                    targets: vec![
                        OfferTarget {
                            target_path: CapabilityPath::try_from("/blah").unwrap(),
                            dest: OfferDest::Child("child".to_string()),
                        }
                    ],
                }),
                OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferSource::Myself,
                    source_path: CapabilityPath::try_from("/foo/bar").unwrap(),
                    targets: vec![
                        OfferTarget {
                            target_path: CapabilityPath::try_from("/blah").unwrap(),
                            dest: OfferDest::Child("child".to_string()),
                        }
                    ],
                }),
            ],
            result = vec![
                Capability::Service(CapabilityPath::try_from("/foo/bar").unwrap()),
                Capability::Directory(CapabilityPath::try_from("/foo/bar").unwrap()),
            ],
        },
    }

    test_fidl_into_and_from! {
        fidl_into_and_from_expose_source => {
            input = vec![
                Some(fsys::ExposeSource::Myself(fsys::SelfRef {})),
                Some(fsys::ExposeSource::Child(fsys::ChildRef {
                    name: Some("foo".to_string()),
                    collection: None,
                })),
            ],
            result = vec![
                ExposeSource::Myself,
                ExposeSource::Child("foo".to_string()),
            ],
        },
        fidl_into_and_from_offer_source => {
            input = vec![
                Some(fsys::OfferSource::Realm(fsys::RealmRef {})),
                Some(fsys::OfferSource::Myself(fsys::SelfRef {})),
                Some(fsys::OfferSource::Child(fsys::ChildRef {
                    name: Some("foo".to_string()),
                    collection: None,
                })),
            ],
            result = vec![
                OfferSource::Realm,
                OfferSource::Myself,
                OfferSource::Child("foo".to_string()),
            ],
        },
    }

    test_fidl_into! {
        fidl_into_dictionary => {
            input = {
                let dict_inner = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "string".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bar".to_string()))),
                    },
                ]};
                let vector = fdata::Vector{values: vec![
                    Some(Box::new(fdata::Value::Dict(dict_inner))),
                    Some(Box::new(fdata::Value::Inum(-42)))
                ]};
                let dict_outer = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "array".to_string(),
                        value: Some(Box::new(fdata::Value::Vec(vector))),
                    },
                ]};
                let dict = fdata::Dictionary {entries: vec![
                    fdata::Entry {
                        key: "bool".to_string(),
                        value: Some(Box::new(fdata::Value::Bit(true))),
                    },
                    fdata::Entry {
                        key: "dict".to_string(),
                        value: Some(Box::new(fdata::Value::Dict(dict_outer))),
                    },
                    fdata::Entry {
                        key: "float".to_string(),
                        value: Some(Box::new(fdata::Value::Fnum(3.14))),
                    },
                    fdata::Entry {
                        key: "int".to_string(),
                        value: Some(Box::new(fdata::Value::Inum(-42))),
                    },
                    fdata::Entry {
                        key: "null".to_string(),
                        value: None,
                    },
                    fdata::Entry {
                        key: "string".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bar".to_string()))),
                    },
                ]};
                Some(dict)
            },
            result = {
                let mut dict_inner = HashMap::new();
                dict_inner.insert("string".to_string(), Value::Str("bar".to_string()));
                let mut dict_outer = HashMap::new();
                let vector = vec![Value::Dict(dict_inner), Value::Inum(-42)];
                dict_outer.insert("array".to_string(), Value::Vec(vector));

                let mut dict: HashMap<String, Value> = HashMap::new();
                dict.insert("bool".to_string(), Value::Bit(true));
                dict.insert("float".to_string(), Value::Fnum(3.14));
                dict.insert("int".to_string(), Value::Inum(-42));
                dict.insert("string".to_string(), Value::Str("bar".to_string()));
                dict.insert("dict".to_string(), Value::Dict(dict_outer));
                dict.insert("null".to_string(), Value::Null);
                Some(dict)
            },
        },
    }
}
