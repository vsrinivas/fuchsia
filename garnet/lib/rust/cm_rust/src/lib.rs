// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_fidl_validator,
    failure::Fail,
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
    std::collections::HashMap,
    std::convert::{TryFrom, TryInto},
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
    ($into_type:ty, $into_ident:ident, $from_type:ty, $from_ident:path, { $( $field:ident: $type:ty, )+ } ) => {
        #[derive(Debug, PartialEq)]
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

        use $from_ident as from_ident;
        impl NativeIntoFidl<$from_type> for $into_type {
            fn native_into_fidl(self) -> $from_type {
                from_ident { $( $field: self.$field.native_into_fidl(), )+ }
            }
        }
    }
}

/// Generates a struct with a FidlIntoNative implementation on the `Option<Vec<struct>>` that
/// calls `fidl_into_native()` on each field.
/// - `into_type` is the name of the struct and the into type for the conversion.
/// - `into_ident` must be identical to `into_type`.
/// - `from_type` is the from type for the conversion.
/// - `from_ident` must be identical to `from_type`.
/// - `field: type` form a list of fields and their types for the generated struct.
macro_rules! fidl_into_vec {
    ($into_type:ty, $into_ident:ident, $from_type:ty, $from_ident:path, { $( $field:ident: $type:ty, )+ } ) => {
        #[derive(Debug, PartialEq, Clone)]
        pub struct $into_ident {
            $(
                pub $field: $type,
            )+
        }

        impl FidlIntoNative<Vec<$into_type>> for Option<Vec<$from_type>> {
            fn fidl_into_native(self) -> Vec<$into_type> {
                if let Some(from) = self {
                    from.into_iter()
                        .map(|e: $from_type|
                            $into_ident { $( $field: e.$field.fidl_into_native(), )+ }
                        )
                        .collect()
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
                    Some(self
                         .into_iter()
                         .map(|e: $into_type| {
                             use $from_ident as from_ident;
                             from_ident { $( $field: e.$field.native_into_fidl(), )+ }
                         })
                         .collect())
                }
            }
        }
    }
}

fidl_into_struct!(ComponentDecl, ComponentDecl, fsys::ComponentDecl, fsys::ComponentDecl,
                  {
                      program: Option<fdata::Dictionary>,
                      uses: Vec<UseDecl>,
                      exposes: Vec<ExposeDecl>,
                      offers: Vec<OfferDecl>,
                      children: Vec<ChildDecl>,
                      facets: Option<fdata::Dictionary>,
                  });

impl Clone for ComponentDecl {
    fn clone(&self) -> Self {
        ComponentDecl {
            program: data::clone_option_dictionary(&self.program),
            uses: self.uses.clone(),
            exposes: self.exposes.clone(),
            offers: self.offers.clone(),
            children: self.children.clone(),
            facets: data::clone_option_dictionary(&self.facets),
        }
    }
}

impl ComponentDecl {
    /// Returns the ExposeDecl that exposes `capability`, if it exists.
    pub fn find_expose_source<'a>(
        &'a self,
        capability: &Capability,
    ) -> Option<&'a ExposeDecl> {
        self.exposes.iter().find(|&e| {
            e.target_path == *capability.path()
                && match (capability, &e.capability) {
                    (Capability::Service(_), Capability::Service(_)) => true,
                    (Capability::Directory(_), Capability::Directory(_)) => true,
                    _ => false,
                }
        })
    }

    /// Returns the OfferDecl that offers a capability under `target_path` to `child_name` with
    /// the given `type_`, if it exists.
    pub fn find_offer_source<'a>(
        &'a self,
        capability: &Capability,
        child_name: &str,
    ) -> Option<&'a OfferDecl> {
        for offer in self.offers.iter() {
            match (capability, &offer.capability) {
                (Capability::Service(_), Capability::Service(_)) => {}
                (Capability::Directory(_), Capability::Directory(_)) => {}
                _ => {
                    continue;
                }
            }
            if let Some(_) = offer
                .targets
                .iter()
                .find(|&e| e.target_path == *capability.path() && e.child_name == *child_name)
            {
                return Some(offer);
            }
        }
        None
    }
}

fidl_into_vec!(UseDecl, UseDecl, fsys::UseDecl, fsys::UseDecl,
               {
                   capability: Capability,
                   target_path: CapabilityPath,
               });
fidl_into_vec!(ExposeDecl, ExposeDecl, fsys::ExposeDecl, fsys::ExposeDecl,
               {
                   capability: Capability,
                   source: RelativeId,
                   target_path: CapabilityPath,
               });
fidl_into_vec!(OfferDecl, OfferDecl, fsys::OfferDecl, fsys::OfferDecl,
               {
                   capability: Capability,
                   source: RelativeId,
                   targets: Vec<OfferTarget>,
               });
fidl_into_vec!(ChildDecl, ChildDecl, fsys::ChildDecl, fsys::ChildDecl,
               {
                   name: String,
                   uri: String,
                   startup: fsys::StartupMode,
               });
fidl_into_vec!(OfferTarget, OfferTarget, fsys::OfferTarget, fsys::OfferTarget,
               {
                   target_path: CapabilityPath,
                   child_name: String,
               });
fidl_translations_opt_type!(String);
fidl_translations_opt_type!(fsys::StartupMode);
fidl_translations_opt_type!(fdata::Dictionary);
fidl_translations_identical!(Option<fdata::Dictionary>);

#[derive(Debug, Clone, PartialEq)]
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
            Capability::Service(s) => {
                write!(f, "service at {}", s)
            }
            Capability::Directory(d) => {
                write!(f, "directory at {}", d)
            }
        }
    }
}

impl FidlIntoNative<Capability> for Option<fsys::Capability> {
    fn fidl_into_native(self) -> Capability {
        match self.unwrap() {
            fsys::Capability::Service(s) => Capability::Service(s.path.fidl_into_native()),
            fsys::Capability::Directory(d) => Capability::Directory(d.path.fidl_into_native()),
            fsys::Capability::__UnknownVariant { .. } => panic!("unknown Capability variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Capability>> for Capability {
    fn native_into_fidl(self) -> Option<fsys::Capability> {
        Some(match self {
            Capability::Service(p) => {
                fsys::Capability::Service(fsys::ServiceCapability { path: p.native_into_fidl() })
            }
            Capability::Directory(p) => fsys::Capability::Directory(fsys::DirectoryCapability {
                path: p.native_into_fidl(),
            }),
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum RelativeId {
    Realm,
    Myself,
    Child(String),
}

impl FidlIntoNative<RelativeId> for Option<fsys::RelativeId> {
    fn fidl_into_native(self) -> RelativeId {
        match self.unwrap() {
            fsys::RelativeId::Realm(_) => RelativeId::Realm,
            fsys::RelativeId::Myself(_) => RelativeId::Myself,
            fsys::RelativeId::Child(c) => RelativeId::Child(c.name.unwrap()),
            fsys::RelativeId::__UnknownVariant { .. } => panic!("unknown RelativeId variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::RelativeId>> for RelativeId {
    fn native_into_fidl(self) -> Option<fsys::RelativeId> {
        Some(match self {
            RelativeId::Realm => fsys::RelativeId::Realm(fsys::RealmId {}),
            RelativeId::Myself => fsys::RelativeId::Myself(fsys::SelfId {}),
            RelativeId::Child(child_name) => {
                fsys::RelativeId::Child(fsys::ChildId { name: Some(child_name) })
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

    fn try_from_fidl_test(input: fsys::ComponentDecl, expected_res: ComponentDecl) {
        let res = ComponentDecl::try_from(input).expect("try_from failed");
        assert_eq!(res, expected_res);
    }

    fn try_from_rust_test(input: ComponentDecl, expected_res: fsys::ComponentDecl) {
        let res = fsys::ComponentDecl::try_from(input).expect("try_from failed");
        assert_eq!(res, expected_res);
    }

    fn fidl_into_rust_test<T, U>(input: T, expected_res: U)
    where
        T: FidlIntoNative<U>,
        U: std::cmp::PartialEq + std::fmt::Debug,
    {
        let res: U = input.fidl_into_native();
        assert_eq!(res, expected_res);
    }

    fn rust_into_fidl_test<T, U>(input: T, expected_res: U)
    where
        T: NativeIntoFidl<U>,
        U: std::cmp::PartialEq + std::fmt::Debug,
    {
        let res: U = input.native_into_fidl();
        assert_eq!(res, expected_res);
    }

    fn capability_path_test(input: &str, expected_res: Result<CapabilityPath, Error>) {
        let res = CapabilityPath::try_from(input);
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        if let Ok(p) = res {
            assert_eq!(&p.to_string(), input);
        }
    }

    macro_rules! test_try_from {
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
                    try_from_fidl_test($input, $result);
                    try_from_rust_test($result, $input);
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
                    fidl_into_rust_test($input, $result);
                    rust_into_fidl_test($result, $input);
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
                    fidl_into_rust_test($input, $result);
                }
            )+
        }
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
                    capability_path_test($input, $result);
                }
            )+
        }
    }

    test_try_from! {
        try_from_empty => {
            input = fsys::ComponentDecl {
                program: None,
                uses: None,
                exposes: None,
                offers: None,
                children: None,
                facets: None,
            },
            result = ComponentDecl {
                program: None,
                uses: vec![],
                exposes: vec![],
                offers: vec![],
                children: vec![],
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
                   fsys::UseDecl{
                       capability: Some(fsys::Capability::Directory(fsys::DirectoryCapability {
                           path: Some("/data/dir".to_string()),
                       })),
                       target_path: Some("/data".to_string()),
                   },
               ]),
               exposes: Some(vec![
                   fsys::ExposeDecl {
                       capability: Some(fsys::Capability::Service(fsys::ServiceCapability {
                           path: Some("/svc/mynetstack".to_string()),
                       })),
                       source: Some(fsys::RelativeId::Child(fsys::ChildId {
                           name: Some("netstack".to_string()),
                       })),
                       target_path: Some("/svc/netstack".to_string()),
                   },
               ]),
               offers: Some(vec![
                   fsys::OfferDecl {
                       capability: Some(fsys::Capability::Service(fsys::ServiceCapability {
                           path: Some("/svc/sys_logger".to_string()),
                       })),
                       source: Some(fsys::RelativeId::Realm(fsys::RealmId {})),
                       targets: Some(vec![
                           fsys::OfferTarget{
                               target_path: Some("/svc/logger".to_string()),
                               child_name: Some("echo".to_string()),
                           },
                       ]),
                   },
               ]),
               children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        uri: Some("fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                    },
                    fsys::ChildDecl {
                        name: Some("echo".to_string()),
                        uri: Some("fuchsia-pkg://fuchsia.com/echo#meta/echo.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                    },
               ]),
               facets: Some(fdata::Dictionary{entries: vec![
                   fdata::Entry{
                       key: "author".to_string(),
                       value: Some(Box::new(fdata::Value::Str("Fuchsia".to_string()))),
                   },
               ]}),
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
                        UseDecl {
                            capability: Capability::Directory("/data/dir".try_into().unwrap()),
                            target_path: "/data".try_into().unwrap(),
                        },
                    ],
                    exposes: vec![
                        ExposeDecl {
                            capability: Capability::Service("/svc/mynetstack".try_into().unwrap()),
                            source: RelativeId::Child("netstack".to_string()),
                            target_path: "/svc/netstack".try_into().unwrap(),
                        },
                    ],
                    offers: vec![
                        OfferDecl {
                            capability: Capability::Service("/svc/sys_logger".try_into().unwrap()),
                            source: RelativeId::Realm,
                            targets: vec![
                                OfferTarget {
                                    target_path: "/svc/logger".try_into().unwrap(),
                                    child_name: "echo".to_string(),
                                },
                            ],
                        },
                    ],
                    children: vec![
                        ChildDecl {
                            name: "netstack".to_string(),
                            uri: "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm".to_string(),
                            startup: fsys::StartupMode::Lazy,
                        },
                        ChildDecl {
                            name: "echo".to_string(),
                            uri: "fuchsia-pkg://fuchsia.com/echo#meta/echo.cm".to_string(),
                            startup: fsys::StartupMode::Eager,
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

    test_fidl_into_and_from! {
        fidl_into_relative_id_realm => {
            input = Some(fsys::RelativeId::Realm(fsys::RealmId {})),
            result = RelativeId::Realm,
        },
        fidl_into_relative_id_myself => {
            input = Some(fsys::RelativeId::Myself(fsys::SelfId {})),
            result = RelativeId::Myself,
        },
        fidl_into_relative_id_child => {
            input = Some(fsys::RelativeId::Child(fsys::ChildId {
                name: Some("foo".to_string()),
            })),
            result = RelativeId::Child("foo".to_string()),
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
