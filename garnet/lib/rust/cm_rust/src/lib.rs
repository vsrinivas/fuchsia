// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_fidl_validator::{self, Error},
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
    std::collections::HashMap,
    std::convert::TryFrom,
};

/// Converts a fidl object into its corresponding native representation.
trait FidlInto<T> {
    fn fidl_into(self) -> T;
}

/// Generates a `FidlInto` implementation for a basic type from `Option<type>` that simply
/// unwraps the Option.
macro_rules! fidl_into_base {
    ($into_type:ty) => {
        impl FidlInto<$into_type> for Option<$into_type> {
            fn fidl_into(self) -> $into_type {
                self.unwrap()
            }
        }
    };
}

/// Generates a struct with a `FidlInto` implementation that calls `fidl_into()` on each field.
/// - `into_type` is the name of the struct and the into type for the conversion.
/// - `into_ident` must be identical to `into_type`.
/// - `from_type` is the from type for the conversion.
/// - `field: type` form a list of fields and their types for the generated struct.
macro_rules! fidl_into_struct {
    ($into_type:ty, $into_ident:ident, $from_type:ty, { $( $field:ident: $type:ty, )+ } ) => {
        #[derive(Debug, Clone, PartialEq)]
        pub struct $into_ident {
            $(
                $field: $type,
            )+
        }

        impl FidlInto<$into_type> for $from_type {
            fn fidl_into(self) -> $into_type {
                $into_ident { $( $field: self.$field.fidl_into(), )+ }
            }
        }
    }
}

/// Generates a struct with a FidlInto implementation on the `Option<Vec<struct>>` that
/// calls `fidl_into()` on each field.
/// - `into_type` is the name of the struct and the into type for the conversion.
/// - `into_ident` must be identical to `into_type`.
/// - `from_type` is the from type for the conversion.
/// - `field: type` form a list of fields and their types for the generated struct.
macro_rules! fidl_into_vec {
    ($into_type:ty, $into_ident:ident, $from_type:ty, { $( $field:ident: $type:ty, )+ } ) => {
        #[derive(Debug, Clone, PartialEq)]
        pub struct $into_ident {
            $(
                $field: $type,
            )+
        }

        impl FidlInto<Vec<$into_type>> for Option<Vec<$from_type>> {
            fn fidl_into(self) -> Vec<$into_type> {
                if let Some(from) = self {
                    from.into_iter()
                        .map(|e: $from_type|
                            $into_ident { $( $field: e.$field.fidl_into(), )+ }
                        )
                        .collect()
                } else {
                    vec![]
                }
            }
        }
    }
}

fidl_into_struct!(ComponentDecl, ComponentDecl, fsys::ComponentDecl,
                  {
                      program: Option<HashMap<String, Value>>,
                      uses: Vec<UseDecl>,
                      exposes: Vec<ExposeDecl>,
                      offers: Vec<OfferDecl>,
                      children: Vec<ChildDecl>,
                      facets: Option<HashMap<String, Value>>,
                  });
fidl_into_vec!(UseDecl, UseDecl, fsys::UseDecl,
               {
                   type_: fsys::CapabilityType,
                   source_path: String,
                   target_path: String,
               });
fidl_into_vec!(ExposeDecl, ExposeDecl, fsys::ExposeDecl,
               {
                   type_: fsys::CapabilityType,
                   source_path: String,
                   source: RelativeId,
                   target_path: String,
               });
fidl_into_vec!(OfferDecl, OfferDecl, fsys::OfferDecl,
               {
                   type_: fsys::CapabilityType,
                   source_path: String,
                   source: RelativeId,
                   targets: Vec<OfferTarget>,
               });
fidl_into_vec!(ChildDecl, ChildDecl, fsys::ChildDecl,
               {
                   name: String,
                   uri: String,
                   startup: fsys::StartupMode,
               });
fidl_into_vec!(OfferTarget, OfferTarget, fsys::OfferTarget,
               {
                   target_path: String,
                   child_name: String,
               });
fidl_into_base!(String);
fidl_into_base!(fsys::CapabilityType);
fidl_into_base!(fsys::StartupMode);

impl FidlInto<Option<HashMap<String, Value>>> for Option<fdata::Dictionary> {
    fn fidl_into(self) -> Option<HashMap<String, Value>> {
        self.map(|d| from_fidl_dict(d))
    }
}

#[derive(Debug, Clone, PartialEq)]
enum Value {
    Bit(bool),
    Inum(i64),
    Fnum(f64),
    Str(String),
    Vec(Vec<Value>),
    Dict(HashMap<String, Value>),
    Null,
}

impl FidlInto<Value> for Option<Box<fdata::Value>> {
    fn fidl_into(self) -> Value {
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
    vec.values.into_iter().map(|v| v.fidl_into()).collect()
}

fn from_fidl_dict(dict: fdata::Dictionary) -> HashMap<String, Value> {
    dict.entries.into_iter().map(|e| (e.key, e.value.fidl_into())).collect()
}

#[derive(Debug, Clone, PartialEq)]
enum RelativeId {
    Realm,
    Myself,
    Child(String),
}

impl FidlInto<RelativeId> for Option<fsys::RelativeId> {
    fn fidl_into(self) -> RelativeId {
        let from = self.unwrap();
        match from.relation.unwrap() {
            fsys::Relation::Realm => RelativeId::Realm,
            fsys::Relation::Myself => RelativeId::Myself,
            fsys::Relation::Child => RelativeId::Child(from.child_name.unwrap()),
        }
    }
}

/// Converts the contents of a CM-FIDL declaration and produces the equivalent CM-Rust
/// struct.
/// This function applies cm_fidl_validator to check correctness.
impl TryFrom<fsys::ComponentDecl> for ComponentDecl {
    type Error = Vec<Error>;

    fn try_from(decl: fsys::ComponentDecl) -> Result<Self, Self::Error> {
        cm_fidl_validator::validate(&decl)?;
        Ok(decl.fidl_into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn try_from_test(input: fsys::ComponentDecl, expected_res: ComponentDecl) {
        let res = ComponentDecl::try_from(input).expect("try_from failed");
        assert_eq!(res, expected_res);
    }

    fn fidl_into_test<T, U>(input: T, expected_res: U)
    where
        T: FidlInto<U>,
        U: std::cmp::PartialEq + std::fmt::Debug,
    {
        let res: U = input.fidl_into();
        assert_eq!(res, expected_res);
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
                    try_from_test($input, $result);
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
                    fidl_into_test($input, $result);
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
                       type_: Some(fsys::CapabilityType::Directory),
                       source_path: Some("/data/dir".to_string()),
                       target_path: Some("/data".to_string()),
                   },
               ]),
               exposes: Some(vec![
                    fsys::ExposeDecl {
                       type_: Some(fsys::CapabilityType::Service),
                       source_path: Some("/svc/mynetstack".to_string()),
                       source: Some(fsys::RelativeId {
                           relation: Some(fsys::Relation::Child),
                           child_name: Some("netstack".to_string()),
                       }),
                       target_path: Some("/svc/netstack".to_string()),
                    },
               ]),
               offers: Some(vec![
                    fsys::OfferDecl {
                        type_: Some(fsys::CapabilityType::Service),
                        source_path: Some("/svc/sys_logger".to_string()),
                        source: Some(fsys::RelativeId {
                            relation: Some(fsys::Relation::Realm),
                            child_name: None,
                        }),
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
            result = ComponentDecl {
                program: Some({
                    let mut dict = HashMap::new();
                    dict.insert("binary".to_string(), Value::Str("bin/app".to_string()));
                    dict
                }),
                uses: vec![
                    UseDecl {
                        type_: fsys::CapabilityType::Directory,
                        source_path: "/data/dir".to_string(),
                        target_path: "/data".to_string(),
                    },
                ],
                exposes: vec![
                    ExposeDecl {
                        type_: fsys::CapabilityType::Service,
                        source_path: "/svc/mynetstack".to_string(),
                        source: RelativeId::Child("netstack".to_string()),
                        target_path: "/svc/netstack".to_string(),
                    },
                ],
                offers: vec![
                    OfferDecl {
                        type_: fsys::CapabilityType::Service,
                        source_path: "/svc/sys_logger".to_string(),
                        source: RelativeId::Realm,
                        targets: vec![
                            OfferTarget{
                                target_path: "/svc/logger".to_string(),
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
                facets: Some({
                    let mut dict = HashMap::new();
                    dict.insert("author".to_string(), Value::Str("Fuchsia".to_string()));
                    dict
                }),
            },
        },
    }

    test_fidl_into! {
        fidl_into_relative_id_realm => {
            input = Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Realm),
                child_name: None
            }),
            result = RelativeId::Realm,
        },
        fidl_into_relative_id_self => {
            input = Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None
            }),
            result = RelativeId::Myself,
        },
        fidl_into_relative_id_child => {
            input = Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Child),
                child_name: Some("foo".to_string()),
            }),
            result = RelativeId::Child("foo".to_string()),
        },
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
                        key: "float".to_string(),
                        value: Some(Box::new(fdata::Value::Fnum(3.14))),
                    },
                    fdata::Entry {
                        key: "int".to_string(),
                        value: Some(Box::new(fdata::Value::Inum(-42))),
                    },
                    fdata::Entry {
                        key: "string".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bar".to_string()))),
                    },
                    fdata::Entry {
                        key: "dict".to_string(),
                        value: Some(Box::new(fdata::Value::Dict(dict_outer))),
                    },
                    fdata::Entry {
                        key: "null".to_string(),
                        value: None,
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
