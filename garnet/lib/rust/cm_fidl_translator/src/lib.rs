// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_fidl_validator,
    cm_json::{self, cm, Error},
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
    serde_json::{Map, Value},
};

/// Converts the contents of a CM file and produces the equivalent FIDL.
/// The mapping between CM-JSON and CM-FIDL is 1-1. The only difference is the language semantics
/// used to express particular data structures.
/// This function also applies cm_fidl_validator to the generated FIDL.
pub fn translate(buffer: &str) -> Result<fsys::ComponentDecl, Error> {
    let json = cm_json::from_json_str(&buffer)?;
    cm_json::validate_json(&json, cm_json::CM_SCHEMA)?;
    let document: cm::Document = serde_json::from_str(&buffer)
        .map_err(|e| Error::parse(format!("Couldn't read input as struct: {}", e)))?;
    let decl = document.cm_into()?;
    cm_fidl_validator::validate(&decl).map_err(|e| Error::validate_fidl(e))?;
    Ok(decl)
}

/// Converts a cm object into its corresponding fidl representation.
trait CmInto<T> {
    fn cm_into(self) -> Result<T, Error>;
}

/// Generates a `CmInto` implementation for `Vec<type>` that calls `cm_into()` on each element.
macro_rules! cm_into_vec {
    ($into_type:ty, $from_type:ty) => {
        impl CmInto<Vec<$into_type>> for Vec<$from_type> {
            fn cm_into(self) -> Result<Vec<$into_type>, Error> {
                let mut out = vec![];
                for e in self.into_iter() {
                    out.push(e.cm_into()?);
                }
                Ok(out)
            }
        }
    };
}

/// Generates a `CmInto` implementation for `Opt<Vec<type>>` that calls `cm_into()` on each element.
macro_rules! cm_into_opt_vec {
    ($into_type:ty, $from_type:ty) => {
        impl CmInto<Option<Vec<$into_type>>> for Option<Vec<$from_type>> {
            fn cm_into(self) -> Result<Option<Vec<$into_type>>, Error> {
                match self {
                    Some(from) => {
                        let mut out = vec![];
                        for e in from.into_iter() {
                            out.push(e.cm_into()?);
                        }
                        Ok(Some(out))
                    }
                    None => Ok(None),
                }
            }
        }
    };
}

cm_into_opt_vec!(fsys::UseDecl, cm::Use);
cm_into_opt_vec!(fsys::ExposeDecl, cm::Expose);
cm_into_opt_vec!(fsys::OfferDecl, cm::Offer);
cm_into_opt_vec!(fsys::ChildDecl, cm::Child);
cm_into_opt_vec!(fsys::CollectionDecl, cm::Collection);
cm_into_opt_vec!(fsys::StorageDecl, cm::Storage);
cm_into_vec!(fsys::OfferTarget, cm::Target);
cm_into_vec!(fsys::Ref, cm::Ref);

impl CmInto<fsys::ComponentDecl> for cm::Document {
    fn cm_into(self) -> Result<fsys::ComponentDecl, Error> {
        Ok(fsys::ComponentDecl {
            program: self.program.cm_into()?,
            uses: self.uses.cm_into()?,
            exposes: self.exposes.cm_into()?,
            offers: self.offers.cm_into()?,
            children: self.children.cm_into()?,
            collections: self.collections.cm_into()?,
            facets: self.facets.cm_into()?,
            storage: self.storage.cm_into()?,
        })
    }
}

impl CmInto<fsys::UseDecl> for cm::Use {
    fn cm_into(self) -> Result<fsys::UseDecl, Error> {
        Ok(match self {
            cm::Use::Service(s) => fsys::UseDecl::Service(s.cm_into()?),
            cm::Use::Directory(d) => fsys::UseDecl::Directory(d.cm_into()?),
            cm::Use::Storage(s) => fsys::UseDecl::Storage(s.cm_into()?),
        })
    }
}

impl CmInto<fsys::ExposeDecl> for cm::Expose {
    fn cm_into(self) -> Result<fsys::ExposeDecl, Error> {
        Ok(match self {
            cm::Expose::Service(s) => fsys::ExposeDecl::Service(s.cm_into()?),
            cm::Expose::Directory(d) => fsys::ExposeDecl::Directory(d.cm_into()?),
        })
    }
}

impl CmInto<fsys::OfferDecl> for cm::Offer {
    fn cm_into(self) -> Result<fsys::OfferDecl, Error> {
        Ok(match self {
            cm::Offer::Service(s) => fsys::OfferDecl::Service(s.cm_into()?),
            cm::Offer::Directory(d) => fsys::OfferDecl::Directory(d.cm_into()?),
            cm::Offer::Storage(s) => fsys::OfferDecl::Storage(s.cm_into()?),
        })
    }
}

impl CmInto<fsys::UseServiceDecl> for cm::UseService {
    fn cm_into(self) -> Result<fsys::UseServiceDecl, Error> {
        Ok(fsys::UseServiceDecl {
            source_path: Some(self.source_path),
            target_path: Some(self.target_path),
        })
    }
}

impl CmInto<fsys::UseDirectoryDecl> for cm::UseDirectory {
    fn cm_into(self) -> Result<fsys::UseDirectoryDecl, Error> {
        Ok(fsys::UseDirectoryDecl {
            source_path: Some(self.source_path),
            target_path: Some(self.target_path),
        })
    }
}

impl CmInto<fsys::UseStorageDecl> for cm::UseStorage {
    fn cm_into(self) -> Result<fsys::UseStorageDecl, Error> {
        Ok(fsys::UseStorageDecl {
            type_: Some(self.type_.cm_into()?),
            target_path: Some(self.target_path),
        })
    }
}

impl CmInto<fsys::ExposeServiceDecl> for cm::ExposeService {
    fn cm_into(self) -> Result<fsys::ExposeServiceDecl, Error> {
        Ok(fsys::ExposeServiceDecl {
            source_path: Some(self.source_path),
            source: Some(self.source.cm_into()?),
            target_path: Some(self.target_path),
        })
    }
}

impl CmInto<fsys::ExposeDirectoryDecl> for cm::ExposeDirectory {
    fn cm_into(self) -> Result<fsys::ExposeDirectoryDecl, Error> {
        Ok(fsys::ExposeDirectoryDecl {
            source_path: Some(self.source_path),
            source: Some(self.source.cm_into()?),
            target_path: Some(self.target_path),
        })
    }
}

impl CmInto<fsys::OfferServiceDecl> for cm::OfferService {
    fn cm_into(self) -> Result<fsys::OfferServiceDecl, Error> {
        Ok(fsys::OfferServiceDecl {
            source_path: Some(self.source_path),
            source: Some(self.source.cm_into()?),
            targets: Some(self.targets.cm_into()?),
        })
    }
}

impl CmInto<fsys::OfferDirectoryDecl> for cm::OfferDirectory {
    fn cm_into(self) -> Result<fsys::OfferDirectoryDecl, Error> {
        Ok(fsys::OfferDirectoryDecl {
            source_path: Some(self.source_path),
            source: Some(self.source.cm_into()?),
            targets: Some(self.targets.cm_into()?),
        })
    }
}

impl CmInto<fsys::OfferStorageDecl> for cm::OfferStorage {
    fn cm_into(self) -> Result<fsys::OfferStorageDecl, Error> {
        Ok(fsys::OfferStorageDecl {
            type_: Some(self.type_.cm_into()?),
            source: Some(self.source.cm_into()?),
            dests: Some(self.dests.cm_into()?),
        })
    }
}

impl CmInto<fsys::StorageType> for cm::StorageType {
    fn cm_into(self) -> Result<fsys::StorageType, Error> {
        match self {
            cm::StorageType::Data => Ok(fsys::StorageType::Data),
            cm::StorageType::Cache => Ok(fsys::StorageType::Cache),
            cm::StorageType::Meta => Ok(fsys::StorageType::Meta),
        }
    }
}

impl CmInto<fsys::ChildDecl> for cm::Child {
    fn cm_into(self) -> Result<fsys::ChildDecl, Error> {
        Ok(fsys::ChildDecl {
            name: Some(self.name),
            url: Some(self.url),
            startup: Some(startup_from_str(&self.startup)?),
        })
    }
}

impl CmInto<fsys::CollectionDecl> for cm::Collection {
    fn cm_into(self) -> Result<fsys::CollectionDecl, Error> {
        Ok(fsys::CollectionDecl {
            name: Some(self.name),
            durability: Some(durability_from_str(&self.durability)?),
        })
    }
}

impl CmInto<fsys::StorageDecl> for cm::Storage {
    fn cm_into(self) -> Result<fsys::StorageDecl, Error> {
        Ok(fsys::StorageDecl {
            name: Some(self.name),
            source_path: Some(self.source_path),
            source: Some(self.source.cm_into()?),
        })
    }
}

impl CmInto<fsys::RealmRef> for cm::RealmRef {
    fn cm_into(self) -> Result<fsys::RealmRef, Error> {
        Ok(fsys::RealmRef {})
    }
}

impl CmInto<fsys::SelfRef> for cm::SelfRef {
    fn cm_into(self) -> Result<fsys::SelfRef, Error> {
        Ok(fsys::SelfRef {})
    }
}

impl CmInto<fsys::ChildRef> for cm::ChildRef {
    fn cm_into(self) -> Result<fsys::ChildRef, Error> {
        Ok(fsys::ChildRef { name: Some(self.name), collection: None })
    }
}

impl CmInto<fsys::CollectionRef> for cm::CollectionRef {
    fn cm_into(self) -> Result<fsys::CollectionRef, Error> {
        Ok(fsys::CollectionRef { name: Some(self.name) })
    }
}

impl CmInto<fsys::StorageRef> for cm::StorageRef {
    fn cm_into(self) -> Result<fsys::StorageRef, Error> {
        Ok(fsys::StorageRef { name: Some(self.name) })
    }
}

impl CmInto<fsys::OfferTarget> for cm::Target {
    fn cm_into(self) -> Result<fsys::OfferTarget, Error> {
        Ok(fsys::OfferTarget {
            target_path: Some(self.target_path),
            dest: Some(self.dest.cm_into()?),
        })
    }
}

impl CmInto<fsys::Ref> for cm::Ref {
    fn cm_into(self) -> Result<fsys::Ref, Error> {
        Ok(match self {
            cm::Ref::Realm(r) => fsys::Ref::Realm(r.cm_into()?),
            cm::Ref::Self_(s) => fsys::Ref::Self_(s.cm_into()?),
            cm::Ref::Child(c) => fsys::Ref::Child(c.cm_into()?),
            cm::Ref::Collection(c) => fsys::Ref::Collection(c.cm_into()?),
            cm::Ref::Storage(r) => fsys::Ref::Storage(r.cm_into()?),
        })
    }
}

impl CmInto<Option<fdata::Dictionary>> for Option<Map<String, Value>> {
    fn cm_into(self) -> Result<Option<fdata::Dictionary>, Error> {
        match self {
            Some(from) => {
                let dict = dictionary_from_map(from)?;
                Ok(Some(dict))
            }
            None => Ok(None),
        }
    }
}

fn dictionary_from_map(in_obj: Map<String, Value>) -> Result<fdata::Dictionary, Error> {
    let mut out = fdata::Dictionary { entries: vec![] };
    for (k, v) in in_obj {
        if let Some(value) = convert_value(v)? {
            out.entries.push(fdata::Entry { key: k, value: Some(value) });
        }
    }
    Ok(out)
}

fn convert_value(v: Value) -> Result<Option<Box<fdata::Value>>, Error> {
    Ok(match v {
        Value::Null => None,
        Value::Bool(b) => Some(Box::new(fdata::Value::Bit(b))),
        Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                Some(Box::new(fdata::Value::Inum(i)))
            } else if let Some(f) = n.as_f64() {
                Some(Box::new(fdata::Value::Fnum(f)))
            } else {
                return Err(Error::Parse(format!("Number is out of range: {}", n)));
            }
        }
        Value::String(s) => Some(Box::new(fdata::Value::Str(s.clone()))),
        Value::Array(a) => {
            let mut values = vec![];
            for v in a {
                if let Some(value) = convert_value(v)? {
                    values.push(Some(value));
                }
            }
            let vector = fdata::Vector { values };
            Some(Box::new(fdata::Value::Vec(vector)))
        }
        Value::Object(o) => {
            let dict = dictionary_from_map(o)?;
            Some(Box::new(fdata::Value::Dict(dict)))
        }
    })
}

fn startup_from_str(value: &str) -> Result<fsys::StartupMode, Error> {
    match value {
        cm::LAZY => Ok(fsys::StartupMode::Lazy),
        cm::EAGER => Ok(fsys::StartupMode::Eager),
        _ => Err(Error::parse(format!("Unknown startup mode: {}", value))),
    }
}

fn durability_from_str(value: &str) -> Result<fsys::Durability, Error> {
    match value {
        cm::PERSISTENT => Ok(fsys::Durability::Persistent),
        cm::TRANSIENT => Ok(fsys::Durability::Transient),
        _ => Err(Error::parse(format!("Unknown durability: {}", value))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cm_json::CM_SCHEMA;
    use serde_json::json;

    fn translate_test(input: serde_json::value::Value, expected_output: fsys::ComponentDecl) {
        let component_decl = translate(&format!("{}", input)).expect("translation failed");
        assert_eq!(component_decl, expected_output);
    }

    fn new_component_decl() -> fsys::ComponentDecl {
        fsys::ComponentDecl {
            program: None,
            uses: None,
            exposes: None,
            offers: None,
            facets: None,
            children: None,
            collections: None,
            storage: None,
        }
    }

    macro_rules! test_translate {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    output = $output:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    translate_test($input, $output);
                }
            )+
        }
    }

    #[test]
    fn test_translate_invalid_cm_fails() {
        let input = json!({
            "exposes": [
                {
                }
            ]
        });

        let expected_res: Result<fsys::ComponentDecl, Error> =
            Err(Error::validate_schema(CM_SCHEMA, "OneOf conditions are not met at /exposes/0"));
        let res = translate(&format!("{}", input));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    }

    test_translate! {
        test_translate_empty => {
            input = json!({}),
            output = new_component_decl(),
        },
        test_translate_program => {
            input = json!({
                "program": {
                    "binary": "bin/app"
                }
            }),
            output = {
                let program = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bin/app".to_string()))),
                    }
                ]};
                let mut decl = new_component_decl();
                decl.program = Some(program);
                decl
            },
        },
        test_translate_dictionary_primitive => {
            input = json!({
                "program": {
                    "string": "bar",
                    "int": -42,
                    "float": 3.14,
                    "bool": true,
                    "ignore": null
                }
            }),
            output = {
                let program = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "bool".to_string(),
                        value: Some(Box::new(fdata::Value::Bit(true))),
                    },
                    fdata::Entry{
                        key: "float".to_string(),
                        value: Some(Box::new(fdata::Value::Fnum(3.14))),
                    },
                    fdata::Entry{
                        key: "int".to_string(),
                        value: Some(Box::new(fdata::Value::Inum(-42))),
                    },
                    fdata::Entry{
                        key: "string".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bar".to_string()))),
                    },
                ]};
                let mut decl = new_component_decl();
                decl.program = Some(program);
                decl
            },
        },
        test_translate_dictionary_nested => {
            input = json!({
                "program": {
                    "obj": {
                        "array": [
                            {
                                "string": "bar"
                            },
                            -42
                        ],
                    },
                    "bool": true
                }
            }),
            output = {
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
                let program = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "bool".to_string(),
                        value: Some(Box::new(fdata::Value::Bit(true))),
                    },
                    fdata::Entry{
                        key: "obj".to_string(),
                        value: Some(Box::new(fdata::Value::Dict(dict_outer))),
                    },
                ]};
                let mut decl = new_component_decl();
                decl.program = Some(program);
                decl
            },
        },
        test_translate_uses => {
            input = json!({
                "uses": [
                    {
                        "service": {
                            "source_path": "/fonts/CoolFonts",
                            "target_path": "/svc/fuchsia.fonts.Provider"
                        }
                    },
                    {
                        "service": {
                            "source_path": "/svc/fuchsia.sys2.Realm",
                            "target_path": "/svc/fuchsia.sys2.Realm"
                        }
                    },
                    {
                        "directory": {
                            "source_path": "/data/assets",
                            "target_path": "/data"
                        }
                    },
                    {
                        "storage": {
                            "type": "cache",
                            "target_path": "/cache"
                        }
                    }
                ]
            }),
            output = {
                let uses = vec![
                    fsys::UseDecl::Service(fsys::UseServiceDecl {
                        source_path: Some("/fonts/CoolFonts".to_string()),
                        target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                    }),
                    fsys::UseDecl::Service(fsys::UseServiceDecl {
                        source_path: Some("/svc/fuchsia.sys2.Realm".to_string()),
                        target_path: Some("/svc/fuchsia.sys2.Realm".to_string()),
                    }),
                    fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                        source_path: Some("/data/assets".to_string()),
                        target_path: Some("/data".to_string()),
                    }),
                    fsys::UseDecl::Storage(fsys::UseStorageDecl {
                        type_: Some(fsys::StorageType::Cache),
                        target_path: Some("/cache".to_string()),
                    }),
                ];
                let mut decl = new_component_decl();
                decl.uses = Some(uses);
                decl
            },
        },
        test_translate_exposes => {
            input = json!({
                "exposes": [
                    {
                        "service": {
                            "source": {
                                "child": {
                                    "name": "logger"
                                }
                            },
                            "source_path": "/loggers/fuchsia.logger.Log",
                            "target_path": "/svc/fuchsia.logger.Log"
                        }
                    },
                    {
                        "directory": {
                            "source": {
                                "self": {}
                            },
                            "source_path": "/volumes/blobfs",
                            "target_path": "/volumes/blobfs"
                        }
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
                    }
                ]
            }),
            output = {
                let exposes = vec![
                    fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        source: Some(fsys::Ref::Child(fsys::ChildRef {
                            name: Some("logger".to_string()),
                            collection: None,
                        })),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    fsys::ExposeDecl::Directory(fsys::ExposeDirectoryDecl {
                        source_path: Some("/volumes/blobfs".to_string()),
                        source: Some(fsys::Ref::Self_(fsys::SelfRef{})),
                        target_path: Some("/volumes/blobfs".to_string()),
                    }),
                ];
                let children = vec![
                    fsys::ChildDecl{
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                    },
                ];
                let mut decl = new_component_decl();
                decl.exposes = Some(exposes);
                decl.children = Some(children);
                decl
            },
        },
        test_translate_offers => {
            input = json!({
                "offers": [
                    {
                        "directory": {
                            "source": {
                                "realm": {}
                            },
                            "source_path": "/data/assets",
                            "targets": [
                                {
                                    "target_path": "/data/realm_assets",
                                    "dest": {
                                        "child": {
                                            "name": "logger"
                                        }
                                    }
                                },
                                {
                                    "target_path": "/data/assets",
                                    "dest": {
                                        "collection": {
                                            "name": "modular"
                                        }
                                    }
                                }
                            ]
                        }
                    },
                    {
                        "directory": {
                            "source": {
                                "self": {}
                            },
                            "source_path": "/data/config",
                            "targets": [
                                {
                                    "target_path": "/data/config",
                                    "dest": {
                                        "child": {
                                            "name": "netstack"
                                        }
                                    }
                                }
                            ]
                        }
                    },
                    {
                        "service": {
                            "source": {
                                "child": {
                                    "name": "logger"
                                }
                            },
                            "source_path": "/svc/fuchsia.logger.Log",
                            "targets": [
                                {
                                    "target_path": "/svc/fuchsia.logger.SysLog",
                                    "dest": {
                                        "collection": {
                                            "name": "modular"
                                        }
                                    }
                                }
                            ]
                        }
                    },
                    {
                        "storage": {
                            "type": "data",
                            "source": {
                                "storage": {
                                    "name": "memfs"
                                }
                            },
                            "dests": [
                                {
                                    "collection": {
                                        "name": "modular"
                                    }
                                },
                                {
                                    "child": {
                                        "name": "logger"
                                    }
                                }
                            ]
                        }
                    },
                    {
                        "storage": {
                            "type": "meta",
                            "source": {
                                "realm": {}
                            },
                            "dests": [
                                {
                                    "collection": {
                                        "name": "modular"
                                    }
                                },
                                {
                                    "child": {
                                        "name": "logger"
                                    }
                                }
                            ]
                        }
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
                    },
                    {
                        "name": "netstack",
                        "url": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm",
                        "startup": "eager"
                    }
                ],
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent"
                    }
                ],
                "storage": [
                    {
                        "name": "memfs",
                        "source_path": "/memfs",
                        "source": {
                            "self": {}
                        }
                    }
                ],
            }),
            output = {
                let offers = vec![
                    fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                        source_path: Some("/data/assets".to_string()),
                        source: Some(fsys::Ref::Realm(fsys::RealmRef{})),
                        targets: Some(vec![
                            fsys::OfferTarget {
                                target_path: Some("/data/realm_assets".to_string()),
                                dest: Some(fsys::Ref::Child(
                                   fsys::ChildRef {
                                       name: Some("logger".to_string()),
                                       collection: None,
                                   }
                                )),
                            },
                            fsys::OfferTarget {
                                target_path: Some("/data/assets".to_string()),
                                dest: Some(fsys::Ref::Collection(
                                   fsys::CollectionRef { name: Some("modular".to_string()) }
                                )),
                            },
                        ]),
                    }),
                    fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                        source_path: Some("/data/config".to_string()),
                        source: Some(fsys::Ref::Self_(fsys::SelfRef{})),
                        targets: Some(vec![
                            fsys::OfferTarget{
                                target_path: Some("/data/config".to_string()),
                                dest: Some(fsys::Ref::Child(
                                   fsys::ChildRef {
                                       name: Some("netstack".to_string()),
                                       collection: None,
                                   }
                                )),
                            },
                        ]),
                    }),
                    fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                        source_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        source: Some(fsys::Ref::Child(fsys::ChildRef {
                            name: Some("logger".to_string()),
                            collection: None,
                        })),
                        targets: Some(vec![
                            fsys::OfferTarget{
                                target_path: Some("/svc/fuchsia.logger.SysLog".to_string()),
                                dest: Some(fsys::Ref::Collection(
                                   fsys::CollectionRef { name: Some("modular".to_string()) }
                                )),
                            },
                        ]),
                    }),
                    fsys::OfferDecl::Storage(fsys::OfferStorageDecl {
                        type_: Some(fsys::StorageType::Data),
                        source: Some(fsys::Ref::Storage(fsys::StorageRef {
                            name: Some("memfs".to_string()),
                        })),
                        dests: Some(vec![
                            fsys::Ref::Collection(
                               fsys::CollectionRef { name: Some("modular".to_string()) }
                            ),
                            fsys::Ref::Child(
                               fsys::ChildRef { name: Some("logger".to_string()), collection: None }
                            ),
                        ]),
                    }),
                    fsys::OfferDecl::Storage(fsys::OfferStorageDecl {
                        type_: Some(fsys::StorageType::Meta),
                        source: Some(fsys::Ref::Realm(fsys::RealmRef { })),
                        dests: Some(vec![
                            fsys::Ref::Collection(
                               fsys::CollectionRef { name: Some("modular".to_string()) }
                            ),
                            fsys::Ref::Child(
                               fsys::ChildRef { name: Some("logger".to_string()), collection: None }
                            ),
                        ]),
                    }),
                ];
                let children = vec![
                    fsys::ChildDecl{
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                    },
                    fsys::ChildDecl{
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                    },
                ];
                let collections = vec![
                    fsys::CollectionDecl{
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                    },
                ];
                let storages = vec![
                    fsys::StorageDecl {
                        name: Some("memfs".to_string()),
                        source_path: Some("/memfs".to_string()),
                        source: Some(fsys::Ref::Self_(fsys::SelfRef{})),
                    },
                ];
                let mut decl = new_component_decl();
                decl.offers = Some(offers);
                decl.children = Some(children);
                decl.collections = Some(collections);
                decl.storage = Some(storages);
                decl
            },
        },
        test_translate_children => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
                    },
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo_server/stable#meta/echo_server.cm",
                        "startup": "eager"
                    }
                ]
            }),
            output = {
                let children = vec![
                    fsys::ChildDecl{
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                    },
                    fsys::ChildDecl{
                        name: Some("echo_server".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/echo_server/stable#meta/echo_server.cm".to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                    },
                ];
                let mut decl = new_component_decl();
                decl.children = Some(children);
                decl
            },
        },
        test_translate_collections => {
            input = json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent"
                    },
                    {
                        "name": "tests",
                        "durability": "transient"
                    }
                ]
            }),
            output = {
                let collections = vec![
                    fsys::CollectionDecl{
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                    },
                    fsys::CollectionDecl{
                        name: Some("tests".to_string()),
                        durability: Some(fsys::Durability::Transient),
                    },
                ];
                let mut decl = new_component_decl();
                decl.collections = Some(collections);
                decl
            },
        },
        test_translate_facets => {
            input = json!({
                "facets": {
                    "authors": [
                        "me",
                        "you"
                    ],
                    "title": "foo",
                    "year": 2018
                }
            }),
            output = {
                let vector = fdata::Vector{values: vec![
                    Some(Box::new(fdata::Value::Str("me".to_string()))),
                    Some(Box::new(fdata::Value::Str("you".to_string()))),
                ]};
                let facets = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "authors".to_string(),
                        value: Some(Box::new(fdata::Value::Vec(vector))),
                    },
                    fdata::Entry{
                        key: "title".to_string(),
                        value: Some(Box::new(fdata::Value::Str("foo".to_string()))),
                    },
                    fdata::Entry{
                        key: "year".to_string(),
                        value: Some(Box::new(fdata::Value::Inum(2018))),
                    },
                ]};
                let mut decl = new_component_decl();
                decl.facets = Some(facets);
                decl
            },
        },
        test_translate_storage => {
            input = json!({
                "storage": [
                    {
                        "name": "memfs",
                        "source_path": "/memfs",
                        "source": {
                            "self": {}
                        }
                    }
                ]
            }),
            output = {
                let storages = vec![
                    fsys::StorageDecl {
                        name: Some("memfs".to_string()),
                        source_path: Some("/memfs".to_string()),
                        source: Some(fsys::Ref::Self_(fsys::SelfRef{})),
                    },
                ];
                let mut decl = new_component_decl();
                decl.storage = Some(storages);
                decl

            },
        },
        test_translate_all_sections => {
            input = json!({
                "program": {
                    "binary": "bin/app"
                },
                "uses": [
                    {
                        "service": {
                            "source_path": "/fonts/CoolFonts",
                            "target_path": "/svc/fuchsia.fonts.Provider"
                        }
                    }
                ],
                "exposes": [
                    {
                        "directory": {
                            "source": {
                                "self": {}
                            },
                            "source_path": "/volumes/blobfs",
                            "target_path": "/volumes/blobfs"
                        }
                    }
                ],
                "offers": [
                    {
                        "service": {
                            "source": {
                                "child": {
                                    "name": "logger"
                                }
                            },
                            "source_path": "/svc/fuchsia.logger.Log",
                            "targets": [
                                {
                                    "target_path": "/svc/fuchsia.logger.Log",
                                    "dest": {
                                        "child": {
                                            "name": "netstack"
                                        }
                                    },
                                },
                                {
                                    "target_path": "/svc/fuchsia.logger.Log",
                                    "dest": {
                                        "collection": {
                                            "name": "modular"
                                        }
                                    },
                                }
                            ]
                        }
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
                    },
                    {
                        "name": "netstack",
                        "url": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm",
                        "startup": "eager"
                    }
                ],
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent"
                    }
                ],
                "facets": {
                    "author": "Fuchsia",
                    "year": 2018
                },
                "storage": [
                    {
                        "name": "memfs",
                        "source_path": "/memfs",
                        "source": {
                            "self": {}
                        }
                    }
                ]
            }),
            output = {
                let program = fdata::Dictionary {entries: vec![
                    fdata::Entry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bin/app".to_string()))),
                    },
                ]};
                let uses = vec![
                    fsys::UseDecl::Service(fsys::UseServiceDecl {
                        source_path: Some("/fonts/CoolFonts".to_string()),
                        target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                    }),
                ];
                let exposes = vec![
                    fsys::ExposeDecl::Directory(fsys::ExposeDirectoryDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef{})),
                        source_path: Some("/volumes/blobfs".to_string()),
                        target_path: Some("/volumes/blobfs".to_string()),
                    }),
                ];
                let offers = vec![
                    fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                        source: Some(fsys::Ref::Child(fsys::ChildRef {
                            name: Some("logger".to_string()),
                            collection: None,
                        })),
                        source_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        targets: Some(vec![
                            fsys::OfferTarget{
                                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                                dest: Some(fsys::Ref::Child(
                                   fsys::ChildRef {
                                       name: Some("netstack".to_string()),
                                       collection: None,
                                   }
                                )),
                            },
                            fsys::OfferTarget{
                                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                                dest: Some(fsys::Ref::Collection(
                                   fsys::CollectionRef { name: Some("modular".to_string()) }
                                )),
                            },
                        ]),
                    }),
                ];
                let children = vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                    },
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                    },
                ];
                let collections = vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                    },
                ];
                let facets = fdata::Dictionary{entries: vec![
                    fdata::Entry {
                        key: "author".to_string(),
                        value: Some(Box::new(fdata::Value::Str("Fuchsia".to_string()))),
                    },
                    fdata::Entry {
                        key: "year".to_string(),
                        value: Some(Box::new(fdata::Value::Inum(2018))),
                    },
                ]};
                let storages = vec![
                    fsys::StorageDecl {
                        name: Some("memfs".to_string()),
                        source_path: Some("/memfs".to_string()),
                        source: Some(fsys::Ref::Self_(fsys::SelfRef{})),
                    },
                ];
                fsys::ComponentDecl {
                    program: Some(program),
                    uses: Some(uses),
                    exposes: Some(exposes),
                    offers: Some(offers),
                    children: Some(children),
                    collections: Some(collections),
                    facets: Some(facets),
                    storage: Some(storages),
                }
            },
        },
    }
}
