// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml::{self, CapabilityClause, OneOrMany};
use crate::validate;
use cm_json::{self, cm, Error};
use serde::ser::Serialize;
use serde_json;
use serde_json::ser::{CompactFormatter, PrettyFormatter, Serializer};
use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::PathBuf;
use std::str::from_utf8;

/// Read in a CML file and produce the equivalent CM.
pub fn compile(file: &PathBuf, pretty: bool, output: Option<PathBuf>) -> Result<(), Error> {
    const BAD_IN_EXTENSION: &str = "Input file does not have the component manifest language \
                                    extension (.cml)";
    match file.extension().and_then(|e| e.to_str()) {
        Some("cml") => Ok(()),
        _ => Err(Error::invalid_args(BAD_IN_EXTENSION)),
    }?;
    const BAD_OUT_EXTENSION: &str =
        "Output file does not have the component manifest extension (.cm)";
    if let Some(ref path) = output {
        match path.extension().and_then(|e| e.to_str()) {
            Some("cm") => Ok(()),
            _ => Err(Error::invalid_args(BAD_OUT_EXTENSION)),
        }?;
    }

    let mut buffer = String::new();
    File::open(&file.as_path())?.read_to_string(&mut buffer)?;
    let value = cm_json::from_json5_str(&buffer)?;
    let document = validate::parse_cml(value)?;
    let out = compile_cml(document)?;

    let mut res = Vec::new();
    if pretty {
        let mut ser = Serializer::with_formatter(&mut res, PrettyFormatter::with_indent(b"    "));
        out.serialize(&mut ser)
            .map_err(|e| Error::parse(format!("Couldn't serialize JSON: {}", e)))?;
    } else {
        let mut ser = Serializer::with_formatter(&mut res, CompactFormatter {});
        out.serialize(&mut ser)
            .map_err(|e| Error::parse(format!("Couldn't serialize JSON: {}", e)))?;
    }
    if let Some(output_path) = output {
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(output_path)?
            .write_all(&res)?;
    } else {
        println!("{}", from_utf8(&res)?);
    }
    // Sanity check that output conforms to CM schema.
    serde_json::from_slice::<cm::Document>(&res)
        .map_err(|e| Error::parse(format!("Couldn't read output as JSON: {}", e)))?;
    Ok(())
}

fn compile_cml(document: cml::Document) -> Result<cm::Document, Error> {
    let mut out = cm::Document::default();
    if let Some(program) = document.program.as_ref() {
        out.program = Some(program.clone());
    }
    if let Some(r#use) = document.r#use.as_ref() {
        out.uses = Some(translate_use(r#use)?);
    }
    if let Some(expose) = document.expose.as_ref() {
        out.exposes = Some(translate_expose(expose)?);
    }
    if let Some(offer) = document.offer.as_ref() {
        let all_children = document.all_children_names().into_iter().collect();
        let all_collections = document.all_collection_names().into_iter().collect();
        out.offers = Some(translate_offer(offer, &all_children, &all_collections)?);
    }
    if let Some(children) = document.children.as_ref() {
        out.children = Some(translate_children(children)?);
    }
    if let Some(collections) = document.collections.as_ref() {
        out.collections = Some(translate_collections(collections)?);
    }
    if let Some(storage) = document.storage.as_ref() {
        out.storage = Some(translate_storage(storage)?);
    }
    if let Some(facets) = document.facets.as_ref() {
        out.facets = Some(facets.clone());
    }
    Ok(out)
}

/// `use` rules consume a single capability from one source (realm|framework).
fn translate_use(use_in: &Vec<cml::Use>) -> Result<Vec<cm::Use>, Error> {
    let mut out_uses = vec![];
    for use_ in use_in {
        if let Some(p) = use_.service() {
            let source = extract_use_source(use_)?;
            let target_id = one_target_capability_id(use_, use_)?;
            out_uses.push(cm::Use::Service(cm::UseService {
                source,
                source_path: cm::Path::new(p.clone())?,
                target_path: cm::Path::new(target_id)?,
            }));
        } else if let Some(p) = use_.legacy_service() {
            let source = extract_use_source(use_)?;
            let target_ids = all_target_capability_ids(use_, use_)
                .ok_or(Error::internal("no capability"))?
                .to_vec();
            let source_ids = p.to_vec();
            for target_id in target_ids {
                let target_path = cm::Path::new(target_id)?;
                // When multiple source paths are provided, there is no way to alias each one, so
                // source_path == target_path.
                // When one source path is provided, source_path may be aliased to a different
                // target_path, so we source_paths[0] to derive the source_path.
                let source_path = if source_ids.len() == 1 {
                    cm::Path::new(source_ids[0].clone())?
                } else {
                    target_path.clone()
                };
                out_uses.push(cm::Use::LegacyService(cm::UseLegacyService {
                    source: source.clone(),
                    source_path,
                    target_path,
                }));
            }
        } else if let Some(p) = use_.directory() {
            let source = extract_use_source(use_)?;
            let target_id = one_target_capability_id(use_, use_)?;
            let rights = extract_use_rights(use_)?;
            out_uses.push(cm::Use::Directory(cm::UseDirectory {
                source,
                source_path: cm::Path::new(p.clone())?,
                target_path: cm::Path::new(target_id)?,
                rights,
            }));
        } else if let Some(p) = use_.storage() {
            let target_path = match all_target_capability_ids(use_, use_) {
                Some(OneOrMany::One(target_path)) => Ok(cm::Path::new(target_path).ok()),
                Some(OneOrMany::Many(_)) => {
                    Err(Error::internal(format!("expecting one capability, but multiple provided")))
                }
                None => Ok(None),
            }?;
            out_uses.push(cm::Use::Storage(cm::UseStorage {
                type_: str_to_storage_type(p.as_str())?,
                target_path,
            }));
        } else if let Some(p) = use_.runner() {
            out_uses.push(cm::Use::Runner(cm::UseRunner { source_name: cm::Name::new(p.clone())? }))
        } else {
            return Err(Error::internal(format!("no capability")));
        };
    }
    Ok(out_uses)
}

/// `expose` rules route a single capability from one source (self|framework) to one target (realm|framework).
/// TODO(vardhan, 41896): Teach `expose` rules to route multiple capabilities using a single clause.
fn translate_expose(expose_in: &Vec<cml::Expose>) -> Result<Vec<cm::Expose>, Error> {
    let mut out_exposes = vec![];
    for expose in expose_in.iter() {
        let source = extract_expose_source(expose)?;
        let target = extract_expose_target(expose)?;
        let target_id = one_target_capability_id(expose, expose)?;
        if let Some(p) = expose.service() {
            out_exposes.push(cm::Expose::Service(cm::ExposeService {
                source,
                source_path: cm::Path::new(p.clone())?,
                target_path: cm::Path::new(target_id)?,
                target,
            }))
        } else if let Some(OneOrMany::One(p)) = expose.legacy_service() {
            out_exposes.push(cm::Expose::LegacyService(cm::ExposeLegacyService {
                source,
                source_path: cm::Path::new(p.clone())?,
                target_path: cm::Path::new(target_id)?,
                target,
            }))
        } else if let Some(p) = expose.directory() {
            let rights = extract_expose_rights(expose)?;
            out_exposes.push(cm::Expose::Directory(cm::ExposeDirectory {
                source,
                source_path: cm::Path::new(p.clone())?,
                target_path: cm::Path::new(target_id)?,
                target,
                rights,
            }))
        } else if let Some(p) = expose.runner() {
            out_exposes.push(cm::Expose::Runner(cm::ExposeRunner {
                source,
                source_name: cm::Name::new(p.clone())?,
                target,
                target_name: cm::Name::new(target_id)?,
            }))
        } else {
            return Err(Error::internal(format!("expose: must specify a known capability")));
        }
    }
    Ok(out_exposes)
}

/// `offer` rules route multiple capabilities from one source to multiple targets.
/// TODO(vardhan): Teach `offer` to route multiple `services`.
fn translate_offer(
    offer_in: &Vec<cml::Offer>,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<cm::Offer>, Error> {
    let mut out_offers = vec![];
    for offer in offer_in.iter() {
        if let Some(p) = offer.service() {
            let source = extract_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            for (target, target_id) in targets {
                out_offers.push(cm::Offer::Service(cm::OfferService {
                    source_path: cm::Path::new(p.clone())?,
                    source: source.clone(),
                    target,
                    target_path: cm::Path::new(target_id)?,
                }));
            }
        } else if let Some(p) = offer.legacy_service() {
            let source = extract_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            let source_ids = p.to_vec();
            for (target, target_id) in targets {
                // When multiple source paths are provided, there is no way to alias each one, so
                // source_path == target_path.
                // When one source path is provided, source_path may be aliased to a different
                // target_path, so we source_ids[0] to derive the source_path.
                let source_path = if source_ids.len() == 1 {
                    cm::Path::new(source_ids[0].clone())?
                } else {
                    cm::Path::new(target_id.clone())?
                };
                out_offers.push(cm::Offer::LegacyService(cm::OfferLegacyService {
                    source_path,
                    source: source.clone(),
                    target,
                    target_path: cm::Path::new(target_id)?,
                }));
            }
        } else if let Some(p) = offer.directory() {
            let source = extract_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            for (target, target_id) in targets {
                out_offers.push(cm::Offer::Directory(cm::OfferDirectory {
                    source_path: cm::Path::new(p.clone())?,
                    source: source.clone(),
                    target,
                    target_path: cm::Path::new(target_id)?,
                    rights: extract_offer_rights(offer)?,
                }));
            }
        } else if let Some(p) = offer.storage() {
            let type_ = str_to_storage_type(p.as_str())?;
            let source = extract_offer_storage_source(offer)?;
            let targets = extract_storage_targets(offer, all_children, all_collections)?;
            for target in targets {
                out_offers.push(cm::Offer::Storage(cm::OfferStorage {
                    type_: type_.clone(),
                    source: source.clone(),
                    target,
                }));
            }
        } else if let Some(p) = offer.runner() {
            let source = extract_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            for (target, target_id) in targets {
                out_offers.push(cm::Offer::Runner(cm::OfferRunner {
                    source: source.clone(),
                    source_name: cm::Name::new(p.clone())?,
                    target,
                    target_name: cm::Name::new(target_id)?,
                }));
            }
        } else {
            return Err(Error::internal(format!("no capability")));
        }
    }
    Ok(out_offers)
}

fn translate_children(children_in: &Vec<cml::Child>) -> Result<Vec<cm::Child>, Error> {
    let mut out_children = vec![];
    for child in children_in.iter() {
        let startup = match child.startup.as_ref().map(|s| s as &str) {
            Some(cml::LAZY) | None => cm::StartupMode::Lazy,
            Some(cml::EAGER) => cm::StartupMode::Eager,
            Some(_) => {
                return Err(Error::internal(format!("invalid startup")));
            }
        };
        out_children.push(cm::Child {
            name: cm::Name::new(child.name.to_string())?,
            url: cm::Url::new(child.url.clone())?,
            startup,
        });
    }
    Ok(out_children)
}

fn translate_collections(
    collections_in: &Vec<cml::Collection>,
) -> Result<Vec<cm::Collection>, Error> {
    let mut out_collections = vec![];
    for collection in collections_in.iter() {
        let durability = match &collection.durability as &str {
            cml::PERSISTENT => cm::Durability::Persistent,
            cml::TRANSIENT => cm::Durability::Transient,
            _ => {
                return Err(Error::internal(format!("invalid durability")));
            }
        };
        out_collections
            .push(cm::Collection { name: cm::Name::new(collection.name.to_string())?, durability });
    }
    Ok(out_collections)
}

fn translate_storage(storage_in: &Vec<cml::Storage>) -> Result<Vec<cm::Storage>, Error> {
    storage_in
        .iter()
        .map(|storage| {
            Ok(cm::Storage {
                name: cm::Name::new(storage.name.to_string())?,
                source_path: cm::Path::new(storage.path.clone())?,
                source: extract_offer_source(storage)?,
            })
        })
        .collect()
}

fn extract_use_source(in_obj: &cml::Use) -> Result<cm::Ref, Error> {
    match in_obj.from.as_ref() {
        Some(cml::Ref::Realm) => Ok(cm::Ref::Realm(cm::RealmRef {})),
        Some(cml::Ref::Framework) => Ok(cm::Ref::Framework(cm::FrameworkRef {})),
        Some(other) => Err(Error::internal(format!("invalid \"from\" for \"use\": {}", other))),
        None => Ok(cm::Ref::Realm(cm::RealmRef {})), // Default value.
    }
}

fn extract_use_rights(in_obj: &cml::Use) -> Result<cm::Rights, Error> {
    match in_obj.rights.as_ref() {
        Some(right_tokens) => match cml::parse_rights(right_tokens) {
            Ok(rights) => Ok(rights),
            _ => Err(Error::internal("Rights provided to use are not well formed.")),
        },
        None => Err(Error::internal("No use rights provided but required for used directories")),
    }
}

fn extract_expose_rights(in_obj: &cml::Expose) -> Result<Option<cm::Rights>, Error> {
    match in_obj.rights.as_ref() {
        Some(rights_tokens) => match cml::parse_rights(rights_tokens) {
            Ok(rights) => Ok(Some(rights)),
            _ => Err(Error::internal("Rights provided to expose are not well formed.")),
        },
        None => Ok(None),
    }
}

fn extract_expose_source<T>(in_obj: &T) -> Result<cm::Ref, Error>
where
    T: cml::FromClause,
{
    match in_obj.from() {
        cml::Ref::Named(name) => {
            Ok(cm::Ref::Child(cm::ChildRef { name: cm::Name::new(name.to_string())? }))
        }
        cml::Ref::Framework => Ok(cm::Ref::Framework(cm::FrameworkRef {})),
        cml::Ref::Self_ => Ok(cm::Ref::Self_(cm::SelfRef {})),
        _ => Err(Error::internal(format!("invalid \"from\" for \"expose\": {}", in_obj.from()))),
    }
}

fn extract_offer_rights(in_obj: &cml::Offer) -> Result<Option<cm::Rights>, Error> {
    match in_obj.rights.as_ref() {
        Some(rights_tokens) => match cml::parse_rights(rights_tokens) {
            Ok(rights) => Ok(Some(rights)),
            _ => Err(Error::internal("Rights provided to offer are not well formed.")),
        },
        None => Ok(None),
    }
}

fn extract_offer_source<T>(in_obj: &T) -> Result<cm::Ref, Error>
where
    T: cml::FromClause,
{
    match in_obj.from() {
        cml::Ref::Named(name) => {
            Ok(cm::Ref::Child(cm::ChildRef { name: cm::Name::new(name.to_string())? }))
        }
        cml::Ref::Framework => Ok(cm::Ref::Framework(cm::FrameworkRef {})),
        cml::Ref::Realm => Ok(cm::Ref::Realm(cm::RealmRef {})),
        cml::Ref::Self_ => Ok(cm::Ref::Self_(cm::SelfRef {})),
    }
}

fn extract_offer_storage_source<T>(in_obj: &T) -> Result<cm::Ref, Error>
where
    T: cml::FromClause,
{
    match in_obj.from() {
        cml::Ref::Realm => Ok(cm::Ref::Realm(cm::RealmRef {})),
        cml::Ref::Named(storage_name) => {
            Ok(cm::Ref::Storage(cm::StorageRef { name: cm::Name::new(storage_name.to_string())? }))
        }
        other => Err(Error::internal(format!("invalid \"from\" for \"offer\": {}", other))),
    }
}

fn translate_child_or_collection_ref(
    reference: &cml::Ref,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<cm::Ref, Error> {
    match reference {
        cml::Ref::Named(name) if all_children.contains(name) => {
            Ok(cm::Ref::Child(cm::ChildRef { name: cm::Name::new(name.to_string())? }))
        }
        cml::Ref::Named(name) if all_collections.contains(name) => {
            Ok(cm::Ref::Collection(cm::CollectionRef { name: cm::Name::new(name.to_string())? }))
        }
        cml::Ref::Named(_) => {
            Err(Error::internal(format!("dangling reference: \"{}\"", reference)))
        }
        _ => Err(Error::internal(format!("invalid child reference: \"{}\"", reference))),
    }
}

fn extract_storage_targets(
    in_obj: &cml::Offer,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<cm::Ref>, Error> {
    in_obj
        .to
        .iter()
        .map(|to| translate_child_or_collection_ref(to, all_children, all_collections))
        .collect()
}

// Return a list of (child, target capability id) expressed in the `offer`.
fn extract_all_targets_for_each_child(
    in_obj: &cml::Offer,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<(cm::Ref, String)>, Error> {
    let mut out_targets = vec![];

    let target_ids = all_target_capability_ids(in_obj, in_obj)
        .ok_or(Error::internal("no capability".to_string()))?
        .to_vec();

    // Validate the "to" references.
    for to in in_obj.to.iter() {
        for target_id in target_ids.clone() {
            let target = translate_child_or_collection_ref(to, all_children, all_collections)?;
            out_targets.push((target, target_id))
        }
    }
    Ok(out_targets)
}

/// Return the target paths (or names) specified in the given capability.
fn all_target_capability_ids<T, U>(in_obj: &T, to_obj: &U) -> Option<OneOrMany<String>>
where
    T: cml::CapabilityClause,
    U: cml::AsClause,
{
    if let Some(as_) = to_obj.r#as() {
        // We've already validated that when `as` is specified, only 1 source id exists.
        Some(OneOrMany::One(as_.clone()))
    } else {
        if let Some(p) = in_obj.service() {
            Some(OneOrMany::One(p.clone()))
        } else if let Some(p) = in_obj.legacy_service() {
            Some(p.clone())
        } else if let Some(p) = in_obj.directory() {
            Some(OneOrMany::One(p.clone()))
        } else if let Some(p) = in_obj.runner() {
            Some(OneOrMany::One(p.clone()))
        } else if let Some(type_) = in_obj.storage() {
            match type_.as_str() {
                "data" => Some(OneOrMany::One("/data".to_string())),
                "cache" => Some(OneOrMany::One("/cache".to_string())),
                _ => None,
            }
        } else {
            None
        }
    }
}

// Return the single path (or name) specified in the given capability.
fn one_target_capability_id<T, U>(in_obj: &T, to_obj: &U) -> Result<String, Error>
where
    T: cml::CapabilityClause,
    U: cml::AsClause,
{
    match all_target_capability_ids(in_obj, to_obj) {
        Some(OneOrMany::One(target_id)) => Ok(target_id),
        Some(OneOrMany::Many(_)) => {
            Err(Error::internal("expecting one capability, but multiple provided"))
        }
        _ => Err(Error::internal("expecting one capability, but none provided")),
    }
}

fn extract_expose_target(in_obj: &cml::Expose) -> Result<cm::ExposeTarget, Error> {
    match &in_obj.to {
        Some(cml::Ref::Realm) => Ok(cm::ExposeTarget::Realm),
        Some(cml::Ref::Framework) => Ok(cm::ExposeTarget::Framework),
        Some(other) => Err(Error::internal(format!("invalid exposed dest: \"{}\"", other))),
        None => Ok(cm::ExposeTarget::Realm),
    }
}

fn str_to_storage_type(s: &str) -> Result<cm::StorageType, Error> {
    match s {
        "data" => Ok(cm::StorageType::Data),
        "cache" => Ok(cm::StorageType::Cache),
        "meta" => Ok(cm::StorageType::Meta),
        t => Err(Error::internal(format!("unknown storage type: {}", t))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::fs::File;
    use std::io;
    use std::io::{Read, Write};
    use tempfile::TempDir;

    macro_rules! test_compile {
        (
            $(
                $(#[$m:meta])*
                $test_name:ident => {
                    input = $input:expr,
                    output = $result:expr,
                },
            )+
        ) => {
            $(
                $(#[$m])*
                #[test]
                fn $test_name() {
                    compile_test($input, $result, true);
                }
            )+
        }
    }

    fn compile_test(input: serde_json::value::Value, expected_output: &str, pretty: bool) {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_in_path = tmp_dir.path().join("test.cml");
        let tmp_out_path = tmp_dir.path().join("test.cm");

        File::create(&tmp_in_path).unwrap().write_all(format!("{}", input).as_bytes()).unwrap();

        compile(&tmp_in_path, pretty, Some(tmp_out_path.clone())).expect("compilation failed");
        let mut buffer = String::new();
        fs::File::open(&tmp_out_path).unwrap().read_to_string(&mut buffer).unwrap();
        assert_eq!(buffer, expected_output);
    }

    // TODO: Consider converting these to a golden test
    test_compile! {
        test_compile_empty => {
            input = json!({}),
            output = "{}",
        },

        test_compile_program => {
            input = json!({
                "program": {
                    "binary": "bin/app"
                }
            }),
            output = r#"{
    "program": {
        "binary": "bin/app"
    }
}"#,
        },

        test_compile_use => {
            input = json!({
                "use": [
                    { "service": "/fonts/CoolFonts", "as": "/svc/fuchsia.fonts.Provider" },
                    { "service": "/svc/fuchsia.sys2.Realm", "from": "framework" },
                    { "legacy_service": "/fonts/LegacyCoolFonts", "as": "/svc/fuchsia.fonts.LegacyProvider" },
                    { "legacy_service": "/svc/fuchsia.sys2.LegacyRealm", "from": "framework" },
                    { "directory": "/data/assets", "rights" : ["read_bytes"]},
                    { "directory": "/data/config", "from": "realm", "rights": ["read_bytes"]},
                    { "storage": "meta" },
                    { "storage": "cache", "as": "/tmp" },
                    { "runner": "elf" },
                    { "runner": "web" }
                ],
            }),
            output = r#"{
    "uses": [
        {
            "service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/fonts/CoolFonts",
                "target_path": "/svc/fuchsia.fonts.Provider"
            }
        },
        {
            "service": {
                "source": {
                    "framework": {}
                },
                "source_path": "/svc/fuchsia.sys2.Realm",
                "target_path": "/svc/fuchsia.sys2.Realm"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/fonts/LegacyCoolFonts",
                "target_path": "/svc/fuchsia.fonts.LegacyProvider"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "framework": {}
                },
                "source_path": "/svc/fuchsia.sys2.LegacyRealm",
                "target_path": "/svc/fuchsia.sys2.LegacyRealm"
            }
        },
        {
            "directory": {
                "source": {
                    "realm": {}
                },
                "source_path": "/data/assets",
                "target_path": "/data/assets",
                "rights": [
                    "read_bytes"
                ]
            }
        },
        {
            "directory": {
                "source": {
                    "realm": {}
                },
                "source_path": "/data/config",
                "target_path": "/data/config",
                "rights": [
                    "read_bytes"
                ]
            }
        },
        {
            "storage": {
                "type": "meta"
            }
        },
        {
            "storage": {
                "type": "cache",
                "target_path": "/tmp"
            }
        },
        {
            "runner": {
                "source_name": "elf"
            }
        },
        {
            "runner": {
                "source_name": "web"
            }
        }
    ]
}"#,
        },

        test_compile_expose => {
            input = json!({
                "expose": [
                    {
                      "service": "/loggers/fuchsia.logger.Log",
                      "from": "#logger",
                      "as": "/svc/fuchsia.logger.Log"
                    },
                    {
                      "legacy_service": "/loggers/fuchsia.logger.LegacyLog",
                      "from": "#logger",
                      "as": "/svc/fuchsia.logger.LegacyLog",
                      "to": "realm"
                    },
                    { "directory": "/volumes/blobfs", "from": "self", "to": "framework", "rights": ["r*"]},
                    { "directory": "/hub", "from": "framework" },
                    { "runner": "web", "from": "self" },
                    { "runner": "web", "from": "#logger", "to": "realm", "as": "web-rename" }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                ]
            }),
            output = r#"{
    "exposes": [
        {
            "service": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_path": "/loggers/fuchsia.logger.Log",
                "target_path": "/svc/fuchsia.logger.Log",
                "target": "realm"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_path": "/loggers/fuchsia.logger.LegacyLog",
                "target_path": "/svc/fuchsia.logger.LegacyLog",
                "target": "realm"
            }
        },
        {
            "directory": {
                "source": {
                    "self": {}
                },
                "source_path": "/volumes/blobfs",
                "target_path": "/volumes/blobfs",
                "target": "framework",
                "rights": [
                    "connect",
                    "enumerate",
                    "traverse",
                    "read_bytes",
                    "get_attributes"
                ]
            }
        },
        {
            "directory": {
                "source": {
                    "framework": {}
                },
                "source_path": "/hub",
                "target_path": "/hub",
                "target": "realm"
            }
        },
        {
            "runner": {
                "source": {
                    "self": {}
                },
                "source_name": "web",
                "target": "realm",
                "target_name": "web"
            }
        },
        {
            "runner": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_name": "web",
                "target": "realm",
                "target_name": "web-rename"
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
}"#,
        },

        test_compile_offer => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#netstack" ]
                    },
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#modular" ],
                        "as": "/svc/fuchsia.logger.SysLog"
                    },
                    {
                        "legacy_service": "/svc/fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#netstack" ]
                    },
                    {
                        "legacy_service": "/svc/fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#modular" ],
                        "as": "/svc/fuchsia.logger.LegacySysLog"
                    },
                    {
                        "legacy_service": [
                            "/svc/fuchsia.setui.SetUiService",
                            "/svc/fuchsia.wlan.service.Wlan"
                        ],
                        "from": "realm",
                        "to": [ "#modular" ]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "realm",
                        "to": [ "#netstack" ]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "realm",
                        "to": [ "#modular" ],
                        "as": "/data"
                    },
                    {
                        "directory": "/hub",
                        "from": "framework",
                        "to": [ "#modular" ],
                        "as": "/hub",
                    },
                    {
                        "storage": "data",
                        "from": "#logger-storage",
                        "to": [
                            "#netstack",
                            "#modular"
                        ],
                    },
                    {
                        "runner": "web",
                        "from": "realm",
                        "to": [ "#modular" ],
                    },
                    {
                        "runner": "elf",
                        "from": "realm",
                        "to": [ "#modular" ],
                        "as": "elf-renamed",
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "netstack",
                        "url": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm"
                    },
                ],
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent",
                    },
                ],
                "storage": [
                    {
                        "name": "logger-storage",
                        "path": "/minfs",
                        "from": "#logger",
                    },
                ],
            }),
            output = r#"{
    "offers": [
        {
            "service": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_path": "/svc/fuchsia.logger.Log",
                "target": {
                    "child": {
                        "name": "netstack"
                    }
                },
                "target_path": "/svc/fuchsia.logger.Log"
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
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/svc/fuchsia.logger.SysLog"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_path": "/svc/fuchsia.logger.LegacyLog",
                "target": {
                    "child": {
                        "name": "netstack"
                    }
                },
                "target_path": "/svc/fuchsia.logger.LegacyLog"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_path": "/svc/fuchsia.logger.LegacyLog",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/svc/fuchsia.logger.LegacySysLog"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/svc/fuchsia.setui.SetUiService",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/svc/fuchsia.setui.SetUiService"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/svc/fuchsia.wlan.service.Wlan",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/svc/fuchsia.wlan.service.Wlan"
            }
        },
        {
            "directory": {
                "source": {
                    "realm": {}
                },
                "source_path": "/data/assets",
                "target": {
                    "child": {
                        "name": "netstack"
                    }
                },
                "target_path": "/data/assets"
            }
        },
        {
            "directory": {
                "source": {
                    "realm": {}
                },
                "source_path": "/data/assets",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/data"
            }
        },
        {
            "directory": {
                "source": {
                    "framework": {}
                },
                "source_path": "/hub",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/hub"
            }
        },
        {
            "storage": {
                "type": "data",
                "source": {
                    "storage": {
                        "name": "logger-storage"
                    }
                },
                "target": {
                    "child": {
                        "name": "netstack"
                    }
                }
            }
        },
        {
            "storage": {
                "type": "data",
                "source": {
                    "storage": {
                        "name": "logger-storage"
                    }
                },
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                }
            }
        },
        {
            "runner": {
                "source": {
                    "realm": {}
                },
                "source_name": "web",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_name": "web"
            }
        },
        {
            "runner": {
                "source": {
                    "realm": {}
                },
                "source_name": "elf",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_name": "elf-renamed"
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
            "startup": "lazy"
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
            "name": "logger-storage",
            "source_path": "/minfs",
            "source": {
                "child": {
                    "name": "logger"
                }
            }
        }
    ]
}"#,
        },

        test_compile_children => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                    {
                        "name": "gmail",
                        "url": "https://www.google.com/gmail",
                        "startup": "eager",
                    },
                    {
                        "name": "echo",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm",
                        "startup": "lazy",
                    },
                ]
            }),
            output = r#"{
    "children": [
        {
            "name": "logger",
            "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
            "startup": "lazy"
        },
        {
            "name": "gmail",
            "url": "https://www.google.com/gmail",
            "startup": "eager"
        },
        {
            "name": "echo",
            "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm",
            "startup": "lazy"
        }
    ]
}"#,
        },

        test_compile_collections => {
            input = json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent",
                    },
                    {
                        "name": "tests",
                        "durability": "transient",
                    },
                ]
            }),
            output = r#"{
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
}"#,
        },

        test_compile_storage => {
            input = json!({
                "storage": [
                    {
                        "name": "mystorage",
                        "path": "/storage",
                        "from": "#minfs",
                    }
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm",
                    },
                ]
            }),
            output = r#"{
    "children": [
        {
            "name": "minfs",
            "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm",
            "startup": "lazy"
        }
    ],
    "storage": [
        {
            "name": "mystorage",
            "source_path": "/storage",
            "source": {
                "child": {
                    "name": "minfs"
                }
            }
        }
    ]
}"#,
        },

        test_compile_facets => {
            input = json!({
                "facets": {
                    "metadata": {
                        "title": "foo",
                        "authors": [ "me", "you" ],
                        "year": 2018
                    }
                }
            }),
            output = r#"{
    "facets": {
        "metadata": {
            "authors": [
                "me",
                "you"
            ],
            "title": "foo",
            "year": 2018
        }
    }
}"#,
        },

        test_compile_all_sections => {
            input = json!({
                "program": {
                    "binary": "bin/app",
                },
                "use": [
                    { "service": "/fonts/CoolFonts", "as": "/svc/fuchsia.fonts.Provider" },
                    { "legacy_service": "/fonts/LegacyCoolFonts", "as": "/svc/fuchsia.fonts.LegacyProvider" },
                    { "legacy_service": [ "/fonts/ReallyGoodFonts", "/fonts/IWouldNeverUseTheseFonts"]},
                ],
                "expose": [
                    { "directory": "/volumes/blobfs", "from": "self", "rights": ["r*"]},
                ],
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#netstack", "#modular" ]
                    },
                    {
                        "legacy_service": "/svc/fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#netstack", "#modular" ]
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                    {
                        "name": "netstack",
                        "url": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm",
                    },
                ],
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent",
                    },
                ],
                "facets": {
                    "author": "Fuchsia",
                    "year": 2018,
                },
            }),
            output = r#"{
    "program": {
        "binary": "bin/app"
    },
    "uses": [
        {
            "service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/fonts/CoolFonts",
                "target_path": "/svc/fuchsia.fonts.Provider"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/fonts/LegacyCoolFonts",
                "target_path": "/svc/fuchsia.fonts.LegacyProvider"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/fonts/ReallyGoodFonts",
                "target_path": "/fonts/ReallyGoodFonts"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "realm": {}
                },
                "source_path": "/fonts/IWouldNeverUseTheseFonts",
                "target_path": "/fonts/IWouldNeverUseTheseFonts"
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
                "target_path": "/volumes/blobfs",
                "target": "realm",
                "rights": [
                    "connect",
                    "enumerate",
                    "traverse",
                    "read_bytes",
                    "get_attributes"
                ]
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
                "target": {
                    "child": {
                        "name": "netstack"
                    }
                },
                "target_path": "/svc/fuchsia.logger.Log"
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
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/svc/fuchsia.logger.Log"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_path": "/svc/fuchsia.logger.LegacyLog",
                "target": {
                    "child": {
                        "name": "netstack"
                    }
                },
                "target_path": "/svc/fuchsia.logger.LegacyLog"
            }
        },
        {
            "legacy_service": {
                "source": {
                    "child": {
                        "name": "logger"
                    }
                },
                "source_path": "/svc/fuchsia.logger.LegacyLog",
                "target": {
                    "collection": {
                        "name": "modular"
                    }
                },
                "target_path": "/svc/fuchsia.logger.LegacyLog"
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
            "startup": "lazy"
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
    }
}"#,
        },
    }

    #[test]
    fn test_compile_compact() {
        let input = json!({
            "use": [
                { "service": "/fonts/CoolFonts", "as": "/svc/fuchsia.fonts.Provider" },
                { "legacy_service": "/fonts/LegacyCoolFonts", "as": "/svc/fuchsia.fonts.LegacyProvider" },
                { "directory": "/data/assets", "rights": ["read_bytes"] }
            ]
        });
        let output = r#"{"uses":[{"service":{"source":{"realm":{}},"source_path":"/fonts/CoolFonts","target_path":"/svc/fuchsia.fonts.Provider"}},{"legacy_service":{"source":{"realm":{}},"source_path":"/fonts/LegacyCoolFonts","target_path":"/svc/fuchsia.fonts.LegacyProvider"}},{"directory":{"source":{"realm":{}},"source_path":"/data/assets","target_path":"/data/assets","rights":["read_bytes"]}}]}"#;
        compile_test(input, &output, false);
    }

    #[test]
    fn test_invalid_json() {
        use cm_json::CML_SCHEMA;

        let tmp_dir = TempDir::new().unwrap();
        let tmp_in_path = tmp_dir.path().join("test.cml");
        let tmp_out_path = tmp_dir.path().join("test.cm");

        let input = json!({
            "expose": [
                { "directory": "/volumes/blobfs", "from": "realm" }
            ]
        });
        File::create(&tmp_in_path).unwrap().write_all(format!("{}", input).as_bytes()).unwrap();
        {
            let result = compile(&tmp_in_path, false, Some(tmp_out_path.clone()));
            let expected_result: Result<(), Error> = Err(Error::validate_schema(
                CML_SCHEMA,
                "Pattern condition is not met at /expose/0/from",
            ));
            assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
        }
        // Compilation failed so output should not exist.
        {
            let result = fs::File::open(&tmp_out_path);
            assert_eq!(result.unwrap_err().kind(), io::ErrorKind::NotFound);
        }
    }
}
