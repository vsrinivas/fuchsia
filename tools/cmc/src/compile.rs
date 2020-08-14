// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml::{self, CapabilityClause};
use crate::error::Error;
use crate::one_or_many::OneOrMany;
use crate::validate;
use cm_types as cm;
use fidl::encoding::encode_persistent;
use fidl_fuchsia_data as fdata;
use fidl_fuchsia_io2 as fio2;
use fidl_fuchsia_sys2 as fsys;
use serde_json::{self, value::Value, Map};
use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::PathBuf;

/// Read in a CML file and produce the equivalent CM.
pub fn compile(file: &PathBuf, output: &PathBuf) -> Result<(), Error> {
    match file.extension().and_then(|e| e.to_str()) {
        Some("cml") => Ok(()),
        _ => Err(Error::invalid_args(format!(
            "Input file {:?} does not have the component manifest language extension (.cml)",
            file
        ))),
    }?;
    match output.extension().and_then(|e| e.to_str()) {
        Some("cm") => Ok(()),
        _ => Err(Error::invalid_args(format!(
            "Output file {:?} does not have the component manifest extension (.cm)",
            output
        ))),
    }?;

    let mut buffer = String::new();
    File::open(&file.as_path())?.read_to_string(&mut buffer)?;
    let document = validate::parse_cml(&buffer, file.as_path())?;
    let mut out_data = compile_cml(document)?;

    let mut out_file =
        fs::OpenOptions::new().create(true).truncate(true).write(true).open(output)?;
    out_file.write(&encode_persistent(&mut out_data)?)?;

    Ok(())
}

fn compile_cml(document: cml::Document) -> Result<fsys::ComponentDecl, Error> {
    Ok(fsys::ComponentDecl {
        program: document.program.as_ref().map(translate_program).transpose()?,
        uses: document.r#use.as_ref().map(translate_use).transpose()?,
        exposes: document.expose.as_ref().map(translate_expose).transpose()?,
        offers: document
            .offer
            .as_ref()
            .map(|offer| {
                let all_children = document.all_children_names().into_iter().collect();
                let all_collections = document.all_collection_names().into_iter().collect();
                translate_offer(offer, &all_children, &all_collections)
            })
            .transpose()?,
        capabilities: document.capabilities.as_ref().map(translate_capabilities).transpose()?,
        children: document.children.as_ref().map(translate_children).transpose()?,
        collections: document.collections.as_ref().map(translate_collections).transpose()?,
        environments: document.environments.as_ref().map(translate_environments).transpose()?,
        facets: document.facets.clone().map(fsys_object_from_map).transpose()?,
    })
}

// Converts a Map<String, serde_json::Value> to a fuchsia Object.
fn fsys_object_from_map(dictionary: Map<String, Value>) -> Result<fsys::Object, Error> {
    let mut out = fsys::Object { entries: vec![] };
    for (k, v) in dictionary {
        if let Some(value) = convert_value(v)? {
            out.entries.push(fsys::Entry { key: k, value: Some(value) });
        }
    }
    Ok(out)
}

// Converts a serde_json::Value into a fuchsia fidl Value. Used by `fsys_object_from_map`.
fn convert_value(v: Value) -> Result<Option<Box<fsys::Value>>, Error> {
    Ok(match v {
        Value::Null => None,
        Value::Bool(b) => Some(Box::new(fsys::Value::Bit(b))),
        Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                Some(Box::new(fsys::Value::Inum(i)))
            } else if let Some(f) = n.as_f64() {
                Some(Box::new(fsys::Value::Fnum(f)))
            } else {
                return Err(Error::validate(format!("Number is out of range: {}", n)));
            }
        }
        Value::String(s) => Some(Box::new(fsys::Value::Str(s.clone()))),
        Value::Array(a) => {
            let vector = fsys::Vector {
                values: a.into_iter().map(convert_value).collect::<Result<Vec<_>, Error>>()?,
            };
            Some(Box::new(fsys::Value::Vec(vector)))
        }
        Value::Object(o) => {
            let obj = fsys_object_from_map(o)?;
            Some(Box::new(fsys::Value::Obj(obj)))
        }
    })
}

// Converts a Map<String, serde_json::Value> to a fuchsia Dictionary.
fn dictionary_from_map(in_obj: Map<String, Value>) -> Result<fdata::Dictionary, Error> {
    let mut entries = vec![];
    for (key, v) in in_obj {
        let value = value_to_dictionary_value(v)?;
        entries.push(fdata::DictionaryEntry { key, value });
    }
    Ok(fdata::Dictionary { entries: Some(entries) })
}

// Converts a serde_json::Value into a fuchsia DictionaryValue. Used by `dictionary_from_map`.
fn value_to_dictionary_value(value: Value) -> Result<Option<Box<fdata::DictionaryValue>>, Error> {
    match value {
        Value::Null => Ok(None),
        Value::String(s) => Ok(Some(Box::new(fdata::DictionaryValue::Str(s.clone())))),
        Value::Array(arr) => {
            let mut strs = vec![];
            for val in arr {
                match val {
                    Value::String(s) => strs.push(s),
                    _ => return Err(Error::validate("Value must be string")),
                };
            }
            Ok(Some(Box::new(fdata::DictionaryValue::StrVec(strs))))
        }
        _ => return Err(Error::validate("Value must be string or list of strings")),
    }
}

// Translates a cm::StorageType to a fsys::StorageType.
fn translate_storage_type_to_fidl(storage_type: &cm::StorageType) -> fsys::StorageType {
    match storage_type {
        cm::StorageType::Data => fsys::StorageType::Data,
        cm::StorageType::Cache => fsys::StorageType::Cache,
        cm::StorageType::Meta => fsys::StorageType::Meta,
    }
}

// Translates a optional cm::DependencyType to a fsys::DependencyType, defaulting to Strong if the input is None.
fn translate_dependency_type_to_fidl(
    dependency_type: Option<cm::DependencyType>,
) -> fsys::DependencyType {
    match dependency_type {
        None | Some(cm::DependencyType::Strong) => fsys::DependencyType::Strong,
        Some(cm::DependencyType::WeakForMigration) => fsys::DependencyType::WeakForMigration,
    }
}

// Translates a cm::StartupMode to a fsys::StartupMode.
fn translate_startup_mode_to_fidl(startup_mode: &cm::StartupMode) -> fsys::StartupMode {
    match startup_mode {
        cm::StartupMode::Lazy => fsys::StartupMode::Lazy,
        cm::StartupMode::Eager => fsys::StartupMode::Eager,
    }
}

// Translates a cm::Durability to a fsys::Durability.
fn translate_durability_to_fidl(durability: &cm::Durability) -> fsys::Durability {
    match durability {
        cm::Durability::Persistent => fsys::Durability::Persistent,
        cm::Durability::Transient => fsys::Durability::Transient,
    }
}

// 'program' rules denote a set of dictionary entries that may specify lifestyle events with a "lifecycle" prefix key.
// All other entries are copied as is.
pub fn translate_program(program: &Map<String, Value>) -> Result<fdata::Dictionary, Error> {
    let mut entries = Vec::new();
    for (k, v) in program {
        match (&k[..], v) {
            ("lifecycle", Value::Object(events)) => {
                for (event, subscription) in events {
                    entries.push(fdata::DictionaryEntry {
                        key: format!("lifecycle.{}", event),
                        value: value_to_dictionary_value(subscription.clone())?,
                    });
                }
            }
            ("lifecycle", _) => {
                return Err(Error::validate(format!(
                    "Unexpected entry in lifecycle section: {}",
                    v
                )));
            }
            _ => {
                entries.push(fdata::DictionaryEntry {
                    key: k.clone(),
                    value: value_to_dictionary_value(v.clone())?,
                });
            }
        }
    }

    Ok(fdata::Dictionary { entries: Some(entries) })
}

/// `use` rules consume a single capability from one source (parent|framework).
fn translate_use(use_in: &Vec<cml::Use>) -> Result<Vec<fsys::UseDecl>, Error> {
    let mut out_uses = vec![];
    for use_ in use_in {
        if let Some(n) = use_.service() {
            let source = extract_use_source(use_)?;
            let target_path = one_target_capability_id(use_, use_, cml::RoutingClauseType::Use)?
                .extract_path()?;
            out_uses.push(fsys::UseDecl::Service(fsys::UseServiceDecl {
                source: Some(source),
                source_name: Some(n.clone().into()),
                target_path: Some(target_path.into()),
            }));
        } else if let Some(p) = use_.protocol() {
            let source = extract_use_source(use_)?;
            let target_ids = all_target_capability_ids(use_, use_, cml::RoutingClauseType::Use)
                .ok_or_else(|| Error::internal("no capability"))?;
            let source_ids = p.to_vec();
            for (source_id, target_id) in source_ids.into_iter().zip(target_ids.into_iter()) {
                let target_path = cml::NameOrPath::Path(target_id.extract_path()?);
                out_uses.push(fsys::UseDecl::Protocol(fsys::UseProtocolDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_path: Some(source_id.clone().into()),
                    target_path: Some(target_path.into()),
                }));
            }
        } else if let Some(p) = use_.directory() {
            let source = extract_use_source(use_)?;
            let target_path = cml::NameOrPath::Path(
                one_target_capability_id(use_, use_, cml::RoutingClauseType::Use)?
                    .extract_path()?,
            );
            let rights = extract_required_rights(use_, "use")?;
            let subdir = extract_use_subdir(use_);
            out_uses.push(fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                source: Some(source),
                source_path: Some(p.clone().into()), // Q: Why do we need to clone here as opposed to with source_path and target_path elsewhere, which are also &Path
                target_path: Some(target_path.into()),
                rights: Some(rights),
                subdir: subdir.map(|s| s.into()),
            }));
        } else if let Some(s) = use_.storage_type() {
            let target_path =
                match all_target_capability_ids(use_, use_, cml::RoutingClauseType::Use) {
                    Some(OneOrMany::One(target_id)) => {
                        let target_path = target_id.extract_path()?;
                        Ok(Some(target_path))
                    }
                    Some(OneOrMany::Many(_)) => Err(Error::internal(format!(
                        "expecting one capability, but multiple provided"
                    ))),
                    None => Ok(None),
                }?;
            out_uses.push(fsys::UseDecl::Storage(fsys::UseStorageDecl {
                type_: Some(translate_storage_type_to_fidl(s)),
                target_path: target_path.map(|path| path.into()),
            }));
        } else if let Some(n) = use_.runner() {
            out_uses.push(fsys::UseDecl::Runner(fsys::UseRunnerDecl {
                source_name: Some(n.clone().into()),
            }))
        } else if let Some(n) = use_.event() {
            let source = extract_use_event_source(use_)?;
            let target_ids = all_target_capability_ids(use_, use_, cml::RoutingClauseType::Use)
                .ok_or_else(|| Error::internal("no capability"))?;
            let source_ids = n.to_vec();
            for target_id in target_ids {
                let target_name = target_id.extract_name()?;
                // When multiple source names are provided, there is no way to alias each one, so
                // source_name == target_name,
                // When one source name is provided, source_name may be aliased to a different
                // target_name, so we use source_names[0] to derive the source_name.
                let source_name =
                    if source_ids.len() == 1 { source_ids[0].clone() } else { target_name.clone() };
                out_uses.push(fsys::UseDecl::Event(fsys::UseEventDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.into()),
                    target_name: Some(target_name.into()),
                    // We have already validated that none will be present if we were using many
                    // events.
                    filter: match use_.filter.clone() {
                        Some(dict) => Some(dictionary_from_map(dict)?),
                        None => None,
                    },
                }));
            }
        } else if let Some(p) = use_.event_stream() {
            let target_path = one_target_capability_id(use_, use_, cml::RoutingClauseType::Use)?
                .extract_path()?;
            out_uses.push(fsys::UseDecl::EventStream(fsys::UseEventStreamDecl {
                target_path: Some(target_path.into()),
                events: Some(p.to_vec().into_iter().map(|name| name.clone().into()).collect()),
            }));
        } else {
            return Err(Error::internal(format!("no capability in use declaration")));
        };
    }
    Ok(out_uses)
}

/// `expose` rules route a single capability from one or more sources (self|framework|#<child>) to
/// one or more targets (parent|framework).
fn translate_expose(expose_in: &Vec<cml::Expose>) -> Result<Vec<fsys::ExposeDecl>, Error> {
    let mut out_exposes = vec![];
    for expose in expose_in.iter() {
        let target = extract_expose_target(expose)?;
        if let Some(n) = expose.service() {
            let sources = extract_all_expose_sources(expose)?;
            let target_name =
                one_target_capability_id(expose, expose, cml::RoutingClauseType::Expose)?
                    .extract_name()?;
            for source in sources {
                out_exposes.push(fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(n.clone().into()),
                    target_name: Some(target_name.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                }))
            }
        } else if let Some(p) = expose.protocol() {
            let source = extract_single_expose_source(expose)?;
            let source_ids: Vec<_> = p.to_vec();
            let target_ids =
                all_target_capability_ids(expose, expose, cml::RoutingClauseType::Expose)
                    .ok_or_else(|| Error::internal("no capability"))?;
            for (source_id, target_id) in source_ids.into_iter().zip(target_ids.into_iter()) {
                out_exposes.push(fsys::ExposeDecl::Protocol(fsys::ExposeProtocolDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_path: Some(source_id.clone().into()),
                    target_path: Some(target_id.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                }))
            }
        } else if let Some(p) = expose.directory() {
            let source = extract_single_expose_source(expose)?;
            let target_id =
                one_target_capability_id(expose, expose, cml::RoutingClauseType::Expose)?;
            let rights = extract_expose_rights(expose)?;
            let subdir = extract_expose_subdir(expose);
            out_exposes.push(fsys::ExposeDecl::Directory(fsys::ExposeDirectoryDecl {
                source: Some(clone_fsys_ref(&source)?),
                source_path: Some(p.clone().into()),
                target_path: Some(target_id.clone().into()),
                target: Some(clone_fsys_ref(&target)?),
                rights: rights,
                subdir: subdir.map(|s| s.into()),
            }))
        } else if let Some(n) = expose.runner() {
            let source = extract_single_expose_source(expose)?;
            let target_name =
                one_target_capability_id(expose, expose, cml::RoutingClauseType::Expose)?
                    .extract_name()?;
            out_exposes.push(fsys::ExposeDecl::Runner(fsys::ExposeRunnerDecl {
                source: Some(clone_fsys_ref(&source)?),
                source_name: Some(n.clone().into()),
                target: Some(clone_fsys_ref(&target)?),
                target_name: Some(target_name.clone().into()),
            }))
        } else if let Some(n) = expose.resolver() {
            let source = extract_single_expose_source(expose)?;
            let target_name =
                one_target_capability_id(expose, expose, cml::RoutingClauseType::Expose)?
                    .extract_name()?;
            out_exposes.push(fsys::ExposeDecl::Resolver(fsys::ExposeResolverDecl {
                source: Some(clone_fsys_ref(&source)?),
                source_name: Some(n.clone().into()),
                target: Some(clone_fsys_ref(&target)?),
                target_name: Some(target_name.clone().into()),
            }))
        } else {
            return Err(Error::internal(format!("expose: must specify a known capability")));
        }
    }
    Ok(out_exposes)
}

/// `offer` rules route multiple capabilities from multiple sources to multiple targets.
fn translate_offer(
    offer_in: &Vec<cml::Offer>,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<fsys::OfferDecl>, Error> {
    let mut out_offers = vec![];
    for offer in offer_in.iter() {
        if let Some(n) = offer.service() {
            let sources = extract_all_offer_sources(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            for (target, target_id) in targets {
                let target_name = target_id.extract_name()?;
                for source in &sources {
                    out_offers.push(fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                        source_name: Some(n.clone().into()),
                        source: Some(clone_fsys_ref(&source)?),
                        target: Some(clone_fsys_ref(&target)?),
                        target_name: Some(target_name.clone().into()),
                    }));
                }
            }
        } else if let Some(p) = offer.protocol() {
            let source = extract_single_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            let source_ids = p.to_vec();
            for (target, target_id) in targets {
                // When multiple source paths are provided, there is no way to alias each one, so
                // source_path == target_path.
                // When one source path is provided, source_path may be aliased to a different
                // target_path, so we source_ids[0] to derive the source_path.
                //
                // TODO: This logic could be simplified to use iter::zip() if
                // extract_all_targets_for_each_child returned separate vectors for targets and
                // target_ids instead of the cross product of them.
                let source_id =
                    if source_ids.len() == 1 { source_ids[0].clone() } else { target_id.clone() };
                out_offers.push(fsys::OfferDecl::Protocol(fsys::OfferProtocolDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_path: Some(source_id.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                    target_path: Some(target_id.clone().into()),
                    dependency_type: Some(translate_dependency_type_to_fidl(
                        offer.dependency.clone(),
                    )),
                }));
            }
        } else if let Some(p) = offer.directory() {
            let source = extract_single_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            for (target, target_id) in targets {
                out_offers.push(fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                    source_path: Some(p.clone().into()),
                    source: Some(clone_fsys_ref(&source)?),
                    target: Some(clone_fsys_ref(&target)?),
                    target_path: Some(target_id.into()),
                    rights: extract_offer_rights(offer)?,
                    subdir: extract_offer_subdir(offer).map(|s| s.into()),
                    dependency_type: Some(translate_dependency_type_to_fidl(
                        offer.dependency.clone(),
                    )),
                }));
            }
        } else if let Some(s) = offer.storage_type() {
            let source = extract_single_offer_storage_source(offer)?;
            let targets = extract_storage_targets(offer, all_children, all_collections)?;
            for target in targets {
                out_offers.push(fsys::OfferDecl::Storage(fsys::OfferStorageDecl {
                    type_: Some(translate_storage_type_to_fidl(s)),
                    source: Some(clone_fsys_ref(&source)?),
                    target: Some(clone_fsys_ref(&target)?),
                }));
            }
        } else if let Some(n) = offer.runner() {
            let source = extract_single_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            for (target, target_id) in targets {
                let target_name = target_id.extract_name()?;
                out_offers.push(fsys::OfferDecl::Runner(fsys::OfferRunnerDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(n.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                    target_name: Some(target_name.clone().into()),
                }));
            }
        } else if let Some(n) = offer.resolver() {
            let source = extract_single_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            for (target, target_id) in targets {
                let target_name = target_id.extract_name()?;
                out_offers.push(fsys::OfferDecl::Resolver(fsys::OfferResolverDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(n.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                    target_name: Some(target_name.clone().into()),
                }));
            }
        } else if let Some(p) = offer.event() {
            let source = extract_single_offer_source(offer)?;
            let targets = extract_all_targets_for_each_child(offer, all_children, all_collections)?;
            let source_ids = p.to_vec();
            for (target, target_id) in targets {
                // When multiple source names are provided, there is no way to alias each one, so
                // source_name == target_name.
                // When one source name is provided, source_name may be aliased to a different
                // source_name, so we source_ids[0] to derive the source_name.
                let target_name = target_id.extract_name()?;
                let source_name =
                    if source_ids.len() == 1 { source_ids[0].clone() } else { target_name.clone() };
                out_offers.push(fsys::OfferDecl::Event(fsys::OfferEventDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                    target_name: Some(target_name.clone().into()),
                    // We have already validated that none will be present if we were using many
                    // events.
                    filter: match offer.filter.clone() {
                        Some(dict) => Some(dictionary_from_map(dict)?),
                        None => None,
                    },
                }));
            }
        } else {
            return Err(Error::internal(format!("no capability")));
        }
    }
    Ok(out_offers)
}

fn translate_children(children_in: &Vec<cml::Child>) -> Result<Vec<fsys::ChildDecl>, Error> {
    let mut out_children = vec![];
    for child in children_in.iter() {
        out_children.push(fsys::ChildDecl {
            name: Some(child.name.clone().into()),
            url: Some(child.url.clone().into()),
            startup: Some(translate_startup_mode_to_fidl(&child.startup)),
            environment: extract_environment_ref(child.environment.as_ref()).map(|e| e.into()),
        });
    }
    Ok(out_children)
}

fn translate_collections(
    collections_in: &Vec<cml::Collection>,
) -> Result<Vec<fsys::CollectionDecl>, Error> {
    let mut out_collections = vec![];
    for collection in collections_in.iter() {
        out_collections.push(fsys::CollectionDecl {
            name: Some(collection.name.clone().into()),
            durability: Some(translate_durability_to_fidl(&collection.durability)),
            environment: extract_environment_ref(collection.environment.as_ref()).map(|e| e.into()),
        });
    }
    Ok(out_collections)
}

fn translate_capabilities(
    capabilities_in: &Vec<cml::Capability>,
) -> Result<Vec<fsys::CapabilityDecl>, Error> {
    let mut out_capabilities = vec![];
    for capability in capabilities_in {
        if let Some(n) = &capability.service {
            let source_path =
                capability.path.clone().unwrap_or_else(|| format!("/svc/{}", n).parse().unwrap());
            out_capabilities.push(fsys::CapabilityDecl::Service(fsys::ServiceDecl {
                name: Some(n.clone().into()),
                source_path: Some(source_path.into()),
            }));
        } else if let Some(protocol) = &capability.protocol {
            for n in protocol.to_vec() {
                let source_path = capability
                    .path
                    .clone()
                    .unwrap_or_else(|| format!("/svc/{}", n).parse().unwrap());
                out_capabilities.push(fsys::CapabilityDecl::Protocol(fsys::ProtocolDecl {
                    name: Some(n.clone().into()),
                    source_path: Some(source_path.into()),
                }));
            }
        } else if let Some(n) = &capability.directory {
            let source_path =
                capability.path.clone().unwrap_or_else(|| format!("/svc/{}", n).parse().unwrap());
            let rights = extract_required_rights(capability, "capability")?;
            out_capabilities.push(fsys::CapabilityDecl::Directory(fsys::DirectoryDecl {
                name: Some(n.clone().into()),
                source_path: Some(source_path.into()),
                rights: Some(rights),
            }));
        } else if let Some(n) = &capability.storage {
            let source_path = if let Some(source_path) = capability.path.as_ref() {
                source_path.clone().into()
            } else {
                capability
                    .backing_dir
                    .as_ref()
                    .expect("storage has no path or backing_dir")
                    .clone()
                    .into()
            };
            out_capabilities.push(fsys::CapabilityDecl::Storage(fsys::StorageDecl {
                name: Some(n.clone().into()),
                source_path: Some(source_path),
                source: Some(offer_source_from_ref(capability.from.as_ref().unwrap().into())?),
            }));
        } else if let Some(n) = &capability.runner {
            out_capabilities.push(fsys::CapabilityDecl::Runner(fsys::RunnerDecl {
                name: Some(n.clone().into()),
                source_path: Some(capability.path.clone().expect("missing path").into()),
                source: Some(offer_source_from_ref(capability.from.as_ref().unwrap().into())?),
            }));
        } else if let Some(n) = &capability.resolver {
            out_capabilities.push(fsys::CapabilityDecl::Resolver(fsys::ResolverDecl {
                name: Some(n.clone().into()),
                source_path: Some(capability.path.clone().expect("missing path").into()),
            }));
        } else {
            return Err(Error::internal(format!("no capability in use declaration")));
        }
    }
    Ok(out_capabilities)
}

fn translate_environments(
    envs_in: &Vec<cml::Environment>,
) -> Result<Vec<fsys::EnvironmentDecl>, Error> {
    envs_in
        .iter()
        .map(|env| {
            Ok(fsys::EnvironmentDecl {
                name: Some(env.name.clone().into()),
                extends: match env.extends {
                    Some(cml::EnvironmentExtends::Realm) => Some(fsys::EnvironmentExtends::Realm),
                    Some(cml::EnvironmentExtends::None) => Some(fsys::EnvironmentExtends::None),
                    None => Some(fsys::EnvironmentExtends::None),
                },
                runners: env
                    .runners
                    .as_ref()
                    .map(|runners| {
                        runners
                            .iter()
                            .map(translate_runner_registration)
                            .collect::<Result<Vec<_>, Error>>()
                    })
                    .transpose()?,
                resolvers: env
                    .resolvers
                    .as_ref()
                    .map(|resolvers| {
                        resolvers
                            .iter()
                            .map(translate_resolver_registration)
                            .collect::<Result<Vec<_>, Error>>()
                    })
                    .transpose()?,
                stop_timeout_ms: env.stop_timeout_ms.map(|s| s.0),
            })
        })
        .collect()
}

fn translate_runner_registration(
    reg: &cml::RunnerRegistration,
) -> Result<fsys::RunnerRegistration, Error> {
    Ok(fsys::RunnerRegistration {
        source_name: Some(reg.runner.clone().into()),
        source: Some(extract_single_offer_source(reg)?),
        target_name: Some(reg.r#as.as_ref().unwrap_or(&reg.runner).clone().into()),
    })
}

fn translate_resolver_registration(
    reg: &cml::ResolverRegistration,
) -> Result<fsys::ResolverRegistration, Error> {
    Ok(fsys::ResolverRegistration {
        resolver: Some(reg.resolver.clone().into()),
        source: Some(extract_single_offer_source(reg)?),
        scheme: Some(
            reg.scheme
                .as_str()
                .parse::<cm_types::UrlScheme>()
                .map_err(|e| Error::internal(format!("invalid URL scheme: {}", e)))?
                .into(),
        ),
    })
}

fn extract_use_source(in_obj: &cml::Use) -> Result<fsys::Ref, Error> {
    match in_obj.from.as_ref() {
        Some(cml::UseFromRef::Parent) => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
        Some(cml::UseFromRef::Framework) => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        None => Ok(fsys::Ref::Parent(fsys::ParentRef {})), // Default value.
    }
}

// Since fsys::Ref is not cloneable, write our own clone function.
fn clone_fsys_ref(fsys_ref: &fsys::Ref) -> Result<fsys::Ref, Error> {
    match fsys_ref {
        fsys::Ref::Parent(parent_ref) => Ok(fsys::Ref::Parent(parent_ref.clone())),
        fsys::Ref::Self_(self_ref) => Ok(fsys::Ref::Self_(self_ref.clone())),
        fsys::Ref::Child(child_ref) => Ok(fsys::Ref::Child(child_ref.clone())),
        fsys::Ref::Collection(collection_ref) => Ok(fsys::Ref::Collection(collection_ref.clone())),
        fsys::Ref::Storage(storage_ref) => Ok(fsys::Ref::Storage(storage_ref.clone())),
        fsys::Ref::Framework(framework_ref) => Ok(fsys::Ref::Framework(framework_ref.clone())),
        _ => Err(Error::internal("Unknown fsys::Ref found.")),
    }
}

fn extract_use_event_source(in_obj: &cml::Use) -> Result<fsys::Ref, Error> {
    match in_obj.from.as_ref() {
        Some(cml::UseFromRef::Parent) => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
        Some(cml::UseFromRef::Framework) => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        None => Err(Error::internal(format!("No source \"from\" provided for \"use\""))),
    }
}

fn extract_required_rights<T>(in_obj: &T, keyword: &str) -> Result<fio2::Operations, Error>
where
    T: cml::RightsClause,
{
    match in_obj.rights() {
        Some(rights_tokens) => {
            let mut rights = Vec::new();
            for token in rights_tokens.0.iter() {
                rights.append(&mut token.expand())
            }
            if rights.is_empty() {
                return Err(Error::missing_rights(format!(
                    "Rights provided to `{}` are not well formed.",
                    keyword
                )));
            }
            let mut seen_rights = HashSet::with_capacity(rights.len());
            let mut operations: fio2::Operations = fio2::Operations::empty();
            for right in rights.iter() {
                if seen_rights.contains(&right) {
                    return Err(Error::duplicate_rights(format!(
                        "Rights provided to `{}` are not well formed.",
                        keyword
                    )));
                }
                seen_rights.insert(right);
                operations |= *right;
            }

            Ok(operations)
        }
        None => Err(Error::internal(format!(
            "No `{}` rights provided but required for directories",
            keyword
        ))),
    }
}

fn extract_use_subdir(in_obj: &cml::Use) -> Option<cm::RelativePath> {
    in_obj.subdir.clone()
}

fn extract_expose_subdir(in_obj: &cml::Expose) -> Option<cm::RelativePath> {
    in_obj.subdir.clone()
}

fn extract_offer_subdir(in_obj: &cml::Offer) -> Option<cm::RelativePath> {
    in_obj.subdir.clone()
}

fn extract_expose_rights(in_obj: &cml::Expose) -> Result<Option<fio2::Operations>, Error> {
    match in_obj.rights.as_ref() {
        Some(rights_tokens) => {
            let mut rights = Vec::new();
            for token in rights_tokens.0.iter() {
                rights.append(&mut token.expand())
            }
            if rights.is_empty() {
                return Err(Error::missing_rights(
                    "Rights provided to expose are not well formed.",
                ));
            }
            let mut seen_rights = HashSet::with_capacity(rights.len());
            let mut operations: fio2::Operations = fio2::Operations::empty();
            for right in rights.iter() {
                if seen_rights.contains(&right) {
                    return Err(Error::duplicate_rights(
                        "Rights provided to expose are not well formed.",
                    ));
                }
                seen_rights.insert(right);
                operations |= *right;
            }

            Ok(Some(operations))
        }
        // Unlike use rights, expose rights can take a None value
        None => Ok(None),
    }
}

fn expose_source_from_ref(reference: &cml::ExposeFromRef) -> Result<fsys::Ref, Error> {
    match reference {
        cml::ExposeFromRef::Named(name) => {
            Ok(fsys::Ref::Child(fsys::ChildRef { name: name.clone().into(), collection: None }))
        }
        cml::ExposeFromRef::Framework => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        cml::ExposeFromRef::Self_ => Ok(fsys::Ref::Self_(fsys::SelfRef {})),
    }
}

fn extract_single_expose_source(in_obj: &cml::Expose) -> Result<fsys::Ref, Error> {
    match &in_obj.from {
        OneOrMany::One(reference) => expose_source_from_ref(&reference),
        OneOrMany::Many(many) => {
            return Err(Error::internal(format!(
                "multiple unexpected \"from\" clauses for \"expose\": {:?}",
                many
            )))
        }
    }
}

fn extract_all_expose_sources(in_obj: &cml::Expose) -> Result<Vec<fsys::Ref>, Error> {
    in_obj.from.to_vec().into_iter().map(expose_source_from_ref).collect()
}

fn extract_offer_rights(in_obj: &cml::Offer) -> Result<Option<fio2::Operations>, Error> {
    match in_obj.rights.as_ref() {
        Some(rights_tokens) => {
            let mut rights = Vec::new();
            for token in rights_tokens.0.iter() {
                rights.append(&mut token.expand())
            }
            if rights.is_empty() {
                return Err(Error::missing_rights("Rights provided to offer are not well formed."));
            }
            let mut seen_rights = HashSet::with_capacity(rights.len());
            let mut operations: fio2::Operations = fio2::Operations::empty();
            for right in rights.iter() {
                if seen_rights.contains(&right) {
                    return Err(Error::duplicate_rights(
                        "Rights provided to offer are not well formed.",
                    ));
                }
                seen_rights.insert(right);
                operations |= *right;
            }

            Ok(Some(operations))
        }
        // Unlike use rights, offer rights can take a None value
        None => Ok(None),
    }
}

fn offer_source_from_ref(reference: cml::AnyRef) -> Result<fsys::Ref, Error> {
    match reference {
        cml::AnyRef::Named(name) => {
            Ok(fsys::Ref::Child(fsys::ChildRef { name: name.clone().into(), collection: None }))
        }
        cml::AnyRef::Framework => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        cml::AnyRef::Parent => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
        cml::AnyRef::Self_ => Ok(fsys::Ref::Self_(fsys::SelfRef {})),
    }
}

fn extract_single_offer_source<T>(in_obj: &T) -> Result<fsys::Ref, Error>
where
    T: cml::FromClause,
{
    match in_obj.from_() {
        OneOrMany::One(reference) => offer_source_from_ref(reference),
        many => {
            return Err(Error::internal(format!(
                "multiple unexpected \"from\" clauses for \"offer\": {}",
                many
            )))
        }
    }
}

fn extract_all_offer_sources(in_obj: &cml::Offer) -> Result<Vec<fsys::Ref>, Error> {
    in_obj.from.to_vec().into_iter().map(|r| offer_source_from_ref(r.into())).collect()
}

fn extract_single_offer_storage_source(in_obj: &cml::Offer) -> Result<fsys::Ref, Error> {
    let reference = match &in_obj.from {
        OneOrMany::One(r) => r,
        OneOrMany::Many(_) => {
            return Err(Error::internal(format!(
                "multiple unexpected \"from\" clauses for \"offer\": {:?}",
                in_obj.from
            )));
        }
    };
    match reference {
        cml::OfferFromRef::Parent => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
        cml::OfferFromRef::Named(name) => {
            Ok(fsys::Ref::Storage(fsys::StorageRef { name: name.clone().into() }))
        }
        other => Err(Error::internal(format!("invalid \"from\" for \"offer\": {:?}", other))),
    }
}

fn translate_child_or_collection_ref(
    reference: cml::AnyRef,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<fsys::Ref, Error> {
    match reference {
        cml::AnyRef::Named(name) if all_children.contains(name) => {
            Ok(fsys::Ref::Child(fsys::ChildRef { name: name.clone().into(), collection: None }))
        }
        cml::AnyRef::Named(name) if all_collections.contains(name) => {
            Ok(fsys::Ref::Collection(fsys::CollectionRef { name: name.clone().into() }))
        }
        cml::AnyRef::Named(_) => {
            Err(Error::internal(format!("dangling reference: \"{}\"", reference)))
        }
        _ => Err(Error::internal(format!("invalid child reference: \"{}\"", reference))),
    }
}

fn extract_storage_targets(
    in_obj: &cml::Offer,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<fsys::Ref>, Error> {
    in_obj
        .to
        .0
        .iter()
        .map(|to| translate_child_or_collection_ref(to.into(), all_children, all_collections))
        .collect()
}

// Return a list of (child, target capability id) expressed in the `offer`.
fn extract_all_targets_for_each_child(
    in_obj: &cml::Offer,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<(fsys::Ref, cml::NameOrPath)>, Error> {
    let mut out_targets = vec![];

    let target_ids = all_target_capability_ids(in_obj, in_obj, cml::RoutingClauseType::Offer)
        .ok_or_else(|| Error::internal("no capability".to_string()))?;

    // Validate the "to" references.
    for to in &in_obj.to.0 {
        for target_id in &target_ids {
            let target =
                translate_child_or_collection_ref(to.into(), all_children, all_collections)?;
            out_targets.push((target, target_id.clone()))
        }
    }
    Ok(out_targets)
}

/// Return the target names or paths specified in the given capability.
fn all_target_capability_ids<T, U>(
    in_obj: &T,
    to_obj: &U,
    clause_type: cml::RoutingClauseType,
) -> Option<OneOrMany<cml::NameOrPath>>
where
    T: cml::CapabilityClause,
    U: cml::AsClause + cml::PathClause,
{
    if let Some(as_) = to_obj.r#as() {
        // We've already validated that when `as` is specified, only 1 source id exists.
        Some(OneOrMany::One(as_.clone()))
    } else {
        if let Some(n) = in_obj.service() {
            if let cml::RoutingClauseType::Use = clause_type {
                if let Some(path) = to_obj.path() {
                    Some(OneOrMany::One(cml::NameOrPath::Path(path.clone())))
                } else {
                    Some(OneOrMany::One(cml::NameOrPath::Path(
                        format!("/svc/{}", n).parse().unwrap(),
                    )))
                }
            } else {
                Some(OneOrMany::One(cml::NameOrPath::Name(n.clone())))
            }
        } else if let Some(p) = in_obj.protocol() {
            Some(match p {
                OneOrMany::One(cml::NameOrPath::Name(n))
                    if clause_type == cml::RoutingClauseType::Use =>
                {
                    if let Some(path) = to_obj.path() {
                        OneOrMany::One(cml::NameOrPath::Path(path.clone()))
                    } else {
                        OneOrMany::One(cml::NameOrPath::Path(
                            format!("/svc/{}", n).parse().unwrap(),
                        ))
                    }
                }
                OneOrMany::One(cml::NameOrPath::Name(n))
                    if clause_type == cml::RoutingClauseType::Use =>
                {
                    OneOrMany::One(cml::NameOrPath::Name(n.clone()))
                }
                OneOrMany::One(path) => OneOrMany::One(path.clone()),
                OneOrMany::Many(v) => {
                    let many = v
                        .iter()
                        .map(|p| match p {
                            cml::NameOrPath::Name(n) => {
                                if let cml::RoutingClauseType::Use = clause_type {
                                    cml::NameOrPath::Path(format!("/svc/{}", n).parse().unwrap())
                                } else {
                                    cml::NameOrPath::Name(n.clone())
                                }
                            }
                            path => path.clone(),
                        })
                        .collect();
                    OneOrMany::Many(many)
                }
            })
        } else if let Some(d) = in_obj.directory() {
            Some(match d {
                cml::NameOrPath::Name(_) if clause_type == cml::RoutingClauseType::Use => {
                    let path = to_obj.path().expect("no path on use directory");
                    OneOrMany::One(cml::NameOrPath::Path(path.clone()))
                }
                cml::NameOrPath::Name(n) => OneOrMany::One(cml::NameOrPath::Name(n.clone())),
                path => OneOrMany::One(path.clone()),
            })
        } else if let Some(n) = in_obj.storage() {
            Some(OneOrMany::One(cml::NameOrPath::Name(n.clone())))
        } else if let Some(n) = in_obj.runner() {
            Some(OneOrMany::One(cml::NameOrPath::Name(n.clone())))
        } else if let Some(n) = in_obj.resolver() {
            Some(OneOrMany::One(cml::NameOrPath::Name(n.clone())))
        } else if let Some(OneOrMany::One(event)) = in_obj.event() {
            Some(OneOrMany::One(cml::NameOrPath::Name(event.clone())))
        } else if let Some(OneOrMany::Many(events)) = in_obj.event() {
            Some(OneOrMany::Many(events.iter().map(|e| cml::NameOrPath::Name(e.clone())).collect()))
        } else if let Some(type_) = in_obj.storage_type() {
            match type_ {
                cml::StorageType::Data => Some(OneOrMany::One("/data".parse().unwrap())),
                cml::StorageType::Cache => Some(OneOrMany::One("/cache".parse().unwrap())),
                _ => None,
            }
        } else {
            None
        }
    }
}

// Return the single name or path specified in the given capability.
fn one_target_capability_id<T, U>(
    in_obj: &T,
    to_obj: &U,
    clause_type: cml::RoutingClauseType,
) -> Result<cml::NameOrPath, Error>
where
    T: cml::CapabilityClause,
    U: cml::AsClause + cml::PathClause,
{
    match all_target_capability_ids(in_obj, to_obj, clause_type) {
        Some(OneOrMany::One(target_id)) => Ok(target_id),
        Some(OneOrMany::Many(_)) => {
            Err(Error::internal("expecting one capability, but multiple provided"))
        }
        _ => Err(Error::internal("expecting one capability, but none provided")),
    }
}

fn extract_expose_target(in_obj: &cml::Expose) -> Result<fsys::Ref, Error> {
    match &in_obj.to {
        Some(cml::ExposeToRef::Parent) => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
        Some(cml::ExposeToRef::Framework) => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        None => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
    }
}

fn extract_environment_ref(r: Option<&cml::EnvironmentRef>) -> Option<cm::Name> {
    r.map(|r| {
        let cml::EnvironmentRef::Named(name) = r;
        name.clone()
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::encoding::decode_persistent;
    use matches::assert_matches;
    use serde_json::json;
    use std::io;
    use std::io::Write;
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
                    compile_test($input, $result);
                }
            )+
        }
    }

    fn compile_test(input: serde_json::value::Value, expected_output: fsys::ComponentDecl) {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_in_path = tmp_dir.path().join("test.cml");
        let tmp_out_path = tmp_dir.path().join("test.cm");

        File::create(&tmp_in_path).unwrap().write_all(format!("{}", input).as_bytes()).unwrap();

        compile(&tmp_in_path, &tmp_out_path.clone()).expect("compilation failed");
        let mut buffer = Vec::new();
        fs::File::open(&tmp_out_path).unwrap().read_to_end(&mut buffer).unwrap();
        let output: fsys::ComponentDecl = decode_persistent(&buffer).unwrap();
        assert_eq!(output, expected_output);
    }

    fn default_component_decl() -> fsys::ComponentDecl {
        fsys::ComponentDecl {
            program: None,
            uses: None,
            exposes: None,
            offers: None,
            capabilities: None,
            children: None,
            collections: None,
            environments: None,
            facets: None,
        }
    }

    test_compile! {
        test_compile_empty => {
            input = json!({}),
            output = default_component_decl(),
        },

        test_compile_program => {
            input = json!({
                "program": {
                    "binary": "bin/app"
                },
                "use": [
                    { "runner": "elf" }
                ]
            }),
            output = fsys::ComponentDecl {
                program: Some(fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                    }]),
                }),
                uses: Some(vec![fsys::UseDecl::Runner (
                    fsys::UseRunnerDecl {
                        source_name: Some("elf".to_string()),
                    }
                )]),
                ..default_component_decl()
            },
        },
        test_compile_program_with_lifecycle => {
            input = json!({
                "program": {
                    "binary": "bin/app",
                    "lifecycle": {
                        "stop_event": "notify",
                    }
                },
                "use": [
                    { "runner": "elf" }
                ]
            }),
            output = fsys::ComponentDecl {
                program: Some(fdata::Dictionary {
                    entries: Some(vec![
                        fdata::DictionaryEntry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                        },
                        fdata::DictionaryEntry {
                            key: "lifecycle.stop_event".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("notify".to_string()))),
                        },
                    ]),
                }),
                uses: Some(vec![fsys::UseDecl::Runner (
                    fsys::UseRunnerDecl {
                        source_name: Some("elf".to_string()),
                    }
                )]),
                ..default_component_decl()
            },
        },

        test_compile_use => {
            input = json!({
                "use": [
                    { "service": "CoolFonts", "path": "/svc/fuchsia.fonts.Provider" },
                    { "service": "fuchsia.sys2.Realm", "from": "framework" },
                    { "protocol": "LegacyCoolFonts", "path": "/svc/fuchsia.fonts.LegacyProvider" },
                    { "protocol": "/fonts/LegacyCoolFonts", "as": "/svc/fuchsia.fonts.LegacyProvider2" },
                    { "protocol": "fuchsia.sys2.LegacyRealm", "from": "framework" },
                    { "protocol": "/svc/fuchsia.sys2.LegacyRealm2", "from": "framework" },
                    { "directory": "assets", "rights" : ["read_bytes"], "path": "/data/assets" },
                    { "directory": "/data/assets2", "rights" : ["read_bytes"]},
                    {
                        "directory": "config",
                        "path": "/data/config",
                        "from": "parent",
                        "rights": ["read_bytes"],
                        "subdir": "fonts",
                    },
                    {
                        "directory": "/data/config2",
                        "from": "parent",
                        "rights": ["read_bytes"],
                        "subdir": "fonts",
                    },
                    { "storage": "meta" },
                    { "storage": "cache", "as": "/tmp" },
                    { "runner": "elf" },
                    { "runner": "web" },
                    { "event": "destroyed", "from": "parent" },
                    { "event": ["started", "stopped"], "from": "framework" },
                    {
                        "event": "capability_ready",
                        "as": "diagnostics",
                        "from": "parent",
                        "filter": { "path": "/diagnostics" }
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                uses: Some(vec![
                    fsys::UseDecl::Service (
                        fsys::UseServiceDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("CoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                        }
                    ),
                    fsys::UseDecl::Service (
                        fsys::UseServiceDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("fuchsia.sys2.Realm".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.Realm".to_string()),
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("LegacyCoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("/fonts/LegacyCoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.LegacyProvider2".to_string()),
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_path: Some("fuchsia.sys2.LegacyRealm".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.LegacyRealm".to_string()),
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_path: Some("/svc/fuchsia.sys2.LegacyRealm2".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.LegacyRealm2".to_string()),
                        }
                    ),
                    fsys::UseDecl::Directory (
                        fsys::UseDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("assets".to_string()),
                            target_path: Some("/data/assets".to_string()),
                            rights: Some(fio2::Operations::ReadBytes),
                            subdir: None,
                        }
                    ),
                    fsys::UseDecl::Directory (
                        fsys::UseDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("/data/assets2".to_string()),
                            target_path: Some("/data/assets2".to_string()),
                            rights: Some(fio2::Operations::ReadBytes),
                            subdir: None,
                        }
                    ),
                    fsys::UseDecl::Directory (
                        fsys::UseDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("config".to_string()),
                            target_path: Some("/data/config".to_string()),
                            rights: Some(fio2::Operations::ReadBytes),
                            subdir: Some("fonts".to_string()),
                        }
                    ),
                    fsys::UseDecl::Directory (
                        fsys::UseDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("/data/config2".to_string()),
                            target_path: Some("/data/config2".to_string()),
                            rights: Some(fio2::Operations::ReadBytes),
                            subdir: Some("fonts".to_string()),
                        }
                    ),
                    fsys::UseDecl::Storage (
                        fsys::UseStorageDecl {
                            type_: Some(fsys::StorageType::Meta),
                            target_path: None,
                        }
                    ),
                    fsys::UseDecl::Storage (
                        fsys::UseStorageDecl {
                            type_: Some(fsys::StorageType::Cache),
                            target_path: Some("/tmp".to_string()),
                        }
                    ),
                    fsys::UseDecl::Runner (
                        fsys::UseRunnerDecl {
                            source_name: Some("elf".to_string())
                        }
                    ),
                    fsys::UseDecl::Runner (
                        fsys::UseRunnerDecl {
                            source_name: Some("web".to_string())
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("destroyed".to_string()),
                            target_name: Some("destroyed".to_string()),
                            filter: None,
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("started".to_string()),
                            target_name: Some("started".to_string()),
                            filter: None,
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("stopped".to_string()),
                            target_name: Some("stopped".to_string()),
                            filter: None,
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("capability_ready".to_string()),
                            target_name: Some("diagnostics".to_string()),
                            filter: Some(fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: "path".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str("/diagnostics".to_string()))),
                                    },
                                ]),
                            }),
                        }
                    ),
                ]),
                ..default_component_decl()
            },
        },

        test_compile_expose => {
            input = json!({
                "expose": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#logger",
                        "as": "fuchsia.logger.Log2",
                    },
                    {
                        "service": "my.service.Service",
                        "from": ["#logger", "self"],
                    },
                    {
                        "protocol": "fuchsia.logger.Log",
                        "from": "#logger",
                        "as": "fuchsia.logger.LegacyLog",
                        "to": "parent"
                    },
                    {
                        "protocol": "/loggers/fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "as": "/svc/fuchsia.logger.LegacyLog2",
                        "to": "parent"
                    },
                    {
                        "protocol": [ "A", "B" ],
                        "from": "self",
                        "to": "parent"
                    },
                    {
                        "protocol": [ "/A", "/B" ],
                        "from": "self",
                        "to": "parent"
                    },
                    {
                        "directory": "blob",
                        "from": "self",
                        "to": "framework",
                        "rights": ["r*"],
                    },
                    {
                        "directory": "/volumes/blobfs/blob",
                        "from": "self",
                        "to": "framework",
                        "rights": ["r*"],
                    },
                    { "directory": "hub", "from": "framework" },
                    { "directory": "/hub", "from": "framework" },
                    { "runner": "web", "from": "self" },
                    { "runner": "web", "from": "#logger", "to": "parent", "as": "web-rename" },
                    { "resolver": "my_resolver", "from": "#logger", "to": "parent", "as": "pkg_resolver" }
                ],
                "capabilities": [
                    { "service": "my.service.Service" },
                    { "protocol": "A" },
                    { "protocol": "B" },
                    {
                        "directory": "blob",
                        "path": "/volumes/blobfs/blob",
                        "rights": ["r*"],
                    },
                    {
                        "runner": "web",
                        "path": "/svc/fuchsia.component.ComponentRunner",
                        "from": "self",
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                ]
            }),
            output = fsys::ComponentDecl {
                exposes: Some(vec![
                    fsys::ExposeDecl::Service (
                        fsys::ExposeServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.Log".to_string()),
                            target_name: Some("fuchsia.logger.Log2".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        }
                    ),
                    fsys::ExposeDecl::Service (
                        fsys::ExposeServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("my.service.Service".to_string()),
                            target_name: Some("my.service.Service".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        }
                    ),
                    fsys::ExposeDecl::Service (
                        fsys::ExposeServiceDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("my.service.Service".to_string()),
                            target_name: Some("my.service.Service".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("fuchsia.logger.Log".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("fuchsia.logger.LegacyLog".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("/svc/fuchsia.logger.LegacyLog2".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("A".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("A".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("B".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("B".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("/A".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("/A".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("/B".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("/B".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("blob".to_string()),
                            target: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            target_path: Some("blob".to_string()),
                            rights: Some(
                                fio2::Operations::Connect | fio2::Operations::Enumerate |
                                fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                fio2::Operations::GetAttributes
                            ),
                            subdir: None,
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("/volumes/blobfs/blob".to_string()),
                            target: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            target_path: Some("/volumes/blobfs/blob".to_string()),
                            rights: Some(
                                fio2::Operations::Connect | fio2::Operations::Enumerate |
                                fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                fio2::Operations::GetAttributes
                            ),
                            subdir: None,
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_path: Some("hub".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("hub".to_string()),
                            rights: None,
                            subdir: None,
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_path: Some("/hub".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("/hub".to_string()),
                            rights: None,
                            subdir: None,
                        }
                    ),
                    fsys::ExposeDecl::Runner (
                        fsys::ExposeRunnerDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("web".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("web".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Runner (
                        fsys::ExposeRunnerDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("web".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("web-rename".to_string()),
                        }
                    ),
                    fsys::ExposeDecl::Resolver (
                        fsys::ExposeResolverDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("my_resolver".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("pkg_resolver".to_string()),
                        }
                    ),
                ]),
                offers: None,
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("my.service.Service".to_string()),
                            source_path: Some("/svc/my.service.Service".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("A".to_string()),
                            source_path: Some("/svc/A".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("B".to_string()),
                            source_path: Some("/svc/B".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Directory (
                        fsys::DirectoryDecl {
                            name: Some("blob".to_string()),
                            source_path: Some("/volumes/blobfs/blob".to_string()),
                            rights: Some(fio2::Operations::Connect | fio2::Operations::Enumerate |
                                fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                fio2::Operations::GetAttributes
                            ),
                        }
                    ),
                    fsys::CapabilityDecl::Runner (
                        fsys::RunnerDecl {
                            name: Some("web".to_string()),
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("/svc/fuchsia.component.ComponentRunner".to_string()),
                        }
                    ),
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    }
                ]),
                ..default_component_decl()
            },
        },

        test_compile_offer => {
            input = json!({
                "offer": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#netstack" ]
                    },
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#modular" ],
                        "as": "fuchsia.logger.Log2",
                    },
                    {
                        "service": "my.service.Service",
                        "from": ["#logger", "self"],
                        "to": [ "#netstack" ]
                    },
                    {
                        "protocol": "fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#netstack" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "protocol": "/svc/fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#netstack" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "protocol": "fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#modular" ],
                        "as": "fuchsia.logger.LegacySysLog",
                        "dependency": "strong"
                    },
                    {
                        "protocol": "/svc/fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#modular" ],
                        "as": "/svc/fuchsia.logger.LegacySysLog",
                        "dependency": "strong"
                    },
                    {
                        "protocol": [
                            "fuchsia.setui.SetUiService",
                            "fuchsia.wlan.service.Wlan"
                        ],
                        "from": "parent",
                        "to": [ "#modular" ]
                    },
                    {
                        "protocol": [
                            "/svc/fuchsia.setui.SetUiService",
                            "/svc/fuchsia.wlan.service.Wlan"
                        ],
                        "from": "parent",
                        "to": [ "#modular" ]
                    },
                    {
                        "directory": "assets",
                        "from": "parent",
                        "to": [ "#netstack" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "directory": "/data/assets",
                        "from": "parent",
                        "to": [ "#netstack" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "directory": "data",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "as": "assets",
                        "subdir": "index/file",
                        "dependency": "strong"
                    },
                    {
                        "directory": "/data/assets",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "as": "/data",
                        "subdir": "index/file",
                        "dependency": "strong"
                    },
                    {
                        "directory": "hub",
                        "from": "framework",
                        "to": [ "#modular" ],
                        "as": "hub",
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
                        "from": "parent",
                        "to": [ "#modular" ],
                    },
                    {
                        "runner": "elf",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "as": "elf-renamed",
                    },
                    {
                        "event": "destroyed",
                        "from": "framework",
                        "to": [ "#netstack"],
                        "as": "destroyed_net"
                    },
                    {
                        "event": [ "stopped", "started" ],
                        "from": "parent",
                        "to": [ "#modular" ],
                    },
                    {
                        "event": "capability_ready",
                        "from": "parent",
                        "to": [ "#netstack" ],
                        "as": "net-ready",
                        "filter": {
                            "path": [
                                "/diagnostics",
                                "/foo/bar"
                            ],
                        }
                    },
                    {
                        "resolver": "my_resolver",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "as": "pkg_resolver",
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
                "capabilities": [
                    { "service": "my.service.Service" },
                    {
                        "storage": "logger-storage",
                        "path": "/minfs",
                        "from": "#logger",
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                offers: Some(vec![
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.Log".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("fuchsia.logger.Log".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.Log".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("fuchsia.logger.Log2".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("my.service.Service".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("my.service.Service".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("my.service.Service".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("my.service.Service".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_path: Some("fuchsia.logger.LegacyLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::WeakForMigration),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::WeakForMigration),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("fuchsia.logger.LegacySysLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("/svc/fuchsia.logger.LegacySysLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("fuchsia.setui.SetUiService".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("fuchsia.setui.SetUiService".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("fuchsia.wlan.service.Wlan".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("fuchsia.wlan.service.Wlan".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("/svc/fuchsia.setui.SetUiService".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("/svc/fuchsia.setui.SetUiService".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("/svc/fuchsia.wlan.service.Wlan".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("/svc/fuchsia.wlan.service.Wlan".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("assets".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_path: Some("assets".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::WeakForMigration),
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("/data/assets".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_path: Some("/data/assets".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::WeakForMigration),
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("data".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("assets".to_string()),
                            rights: None,
                            subdir: Some("index/file".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("/data/assets".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("/data".to_string()),
                            rights: None,
                            subdir: Some("index/file".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_path: Some("hub".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("hub".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_path: Some("/hub".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("/hub".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::Strong),
                        }
                    ),
                    fsys::OfferDecl::Storage (
                        fsys::OfferStorageDecl {
                            type_: Some(fsys::StorageType::Data),
                            source: Some(fsys::Ref::Storage(fsys::StorageRef {
                                name: "logger-storage".to_string(),
                            })),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                        }
                    ),
                    fsys::OfferDecl::Storage (
                        fsys::OfferStorageDecl {
                            type_: Some(fsys::StorageType::Data),
                            source: Some(fsys::Ref::Storage(fsys::StorageRef {
                                name: "logger-storage".to_string(),
                            })),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                        }
                    ),
                    fsys::OfferDecl::Runner (
                        fsys::OfferRunnerDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("web".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("web".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Runner (
                        fsys::OfferRunnerDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("elf".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("elf-renamed".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Event (
                        fsys::OfferEventDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("destroyed".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("destroyed_net".to_string()),
                            filter: None,
                        }
                    ),
                    fsys::OfferDecl::Event (
                        fsys::OfferEventDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("stopped".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("stopped".to_string()),
                            filter: None,
                        }
                    ),
                    fsys::OfferDecl::Event (
                        fsys::OfferEventDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("started".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("started".to_string()),
                            filter: None,
                        }
                    ),
                    fsys::OfferDecl::Event (
                        fsys::OfferEventDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("capability_ready".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("net-ready".to_string()),
                            filter: Some(fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: "path".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::StrVec(
                                            vec!["/diagnostics".to_string(), "/foo/bar".to_string()]
                                        ))),
                                    },
                                ]),
                            }),
                        }
                    ),
                    fsys::OfferDecl::Resolver (
                        fsys::OfferResolverDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("my_resolver".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("pkg_resolver".to_string()),
                        }
                    ),
                ]),
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("my.service.Service".to_string()),
                            source_path: Some("/svc/my.service.Service".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Storage (
                        fsys::StorageDecl {
                            name: Some("logger-storage".to_string()),
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("/minfs".to_string()),
                        }
                    )
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    },
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    },
                ]),
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                        environment: None,
                    }
                ]),
                ..default_component_decl()
            },
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
                        "environment": "#myenv",
                    },
                ],
                "environments": [
                    {
                        "name": "myenv",
                        "extends": "realm",
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    },
                    fsys::ChildDecl {
                        name: Some("gmail".to_string()),
                        url: Some("https://www.google.com/gmail".to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                        environment: None,
                    },
                    fsys::ChildDecl {
                        name: Some("echo".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: Some("myenv".to_string()),
                    }
                ]),
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                    }
                ]),
                ..default_component_decl()
            },
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
                        "environment": "#myenv",
                    },
                ],
                "environments": [
                    {
                        "name": "myenv",
                        "extends": "realm",
                    }
                ],
            }),
            output = fsys::ComponentDecl {
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                        environment: None,
                    },
                    fsys::CollectionDecl {
                        name: Some("tests".to_string()),
                        durability: Some(fsys::Durability::Transient),
                        environment: Some("myenv".to_string()),
                    }
                ]),
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                    }
                ]),
                ..default_component_decl()
            },
        },

        test_compile_capabilities => {
            input = json!({
                "capabilities": [
                    {
                        "service": "myservice",
                        "path": "/service",
                    },
                    {
                        "service": "myservice2",
                    },
                    {
                        "protocol": "myprotocol",
                        "path": "/protocol",
                    },
                    {
                        "protocol": "myprotocol2",
                    },
                    {
                        "protocol": [ "myprotocol3", "myprotocol4" ],
                    },
                    {
                        "directory": "mydirectory",
                        "path": "/directory",
                        "rights": [ "connect" ],
                    },
                    {
                        "storage": "mystorage",
                        "backing_dir": "storage",
                        "from": "#minfs",
                    },
                    {
                        "storage": "mystorage2",
                        "path": "/storage2",
                        "from": "#minfs",
                    },
                    {
                        "runner": "myrunner",
                        "path": "/runner",
                        "from": "self"
                    },
                    {
                        "resolver": "myresolver",
                        "path": "/resolver"
                    },
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm",
                    },
                ]
            }),
            output = fsys::ComponentDecl {
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("myservice".to_string()),
                            source_path: Some("/service".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("myservice2".to_string()),
                            source_path: Some("/svc/myservice2".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol".to_string()),
                            source_path: Some("/protocol".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol2".to_string()),
                            source_path: Some("/svc/myprotocol2".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol3".to_string()),
                            source_path: Some("/svc/myprotocol3".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol4".to_string()),
                            source_path: Some("/svc/myprotocol4".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Directory (
                        fsys::DirectoryDecl {
                            name: Some("mydirectory".to_string()),
                            source_path: Some("/directory".to_string()),
                            rights: Some(fio2::Operations::Connect),
                        }
                    ),
                    fsys::CapabilityDecl::Storage (
                        fsys::StorageDecl {
                            name: Some("mystorage".to_string()),
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "minfs".to_string(),
                                collection: None,
                            })),
                            source_path: Some("storage".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Storage (
                        fsys::StorageDecl {
                            name: Some("mystorage2".to_string()),
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "minfs".to_string(),
                                collection: None,
                            })),
                            source_path: Some("/storage2".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Runner (
                        fsys::RunnerDecl {
                            name: Some("myrunner".to_string()),
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("/runner".to_string()),
                        }
                    ),
                    fsys::CapabilityDecl::Resolver (
                        fsys::ResolverDecl {
                            name: Some("myresolver".to_string()),
                            source_path: Some("/resolver".to_string()),
                        }
                    )
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("minfs".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    }
                ]),
                ..default_component_decl()
            },
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
            output = fsys::ComponentDecl {
                facets: Some(fsys::Object {
                    entries: vec![
                        fsys::Entry {
                            key: "metadata".to_string(),
                            value: Some(Box::new(fsys::Value::Obj(fsys::Object {
                                entries: vec![
                                    fsys::Entry {
                                        key: "authors".to_string(),
                                        value: Some(Box::new(fsys::Value::Vec (
                                            fsys::Vector {
                                                values: vec![
                                                    Some(Box::new(fsys::Value::Str("me".to_string()))),
                                                    Some(Box::new(fsys::Value::Str("you".to_string()))),
                                                ]
                                            }
                                        ))),
                                    },
                                    fsys::Entry {
                                        key: "title".to_string(),
                                        value: Some(Box::new(fsys::Value::Str("foo".to_string()))),
                                    },
                                    fsys::Entry {
                                        key: "year".to_string(),
                                        value: Some(Box::new(fsys::Value::Inum(2018))),
                                    },
                                ],
                            }))),
                        },
                    ],
                }),
                ..default_component_decl()
            },
        },

        test_compile_environment => {
            input = json!({
                "environments": [
                    {
                        "name": "myenv",
                    },
                    {
                        "name": "myenv2",
                        "extends": "realm",
                    },
                    {
                        "name": "myenv3",
                        "extends": "none",
                        "__stop_timeout_ms": 8000,
                    }
                ],
            }),
            output = fsys::ComponentDecl {
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::None),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                    },
                    fsys::EnvironmentDecl {
                        name: Some("myenv2".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                    },
                    fsys::EnvironmentDecl {
                        name: Some("myenv3".to_string()),
                        extends: Some(fsys::EnvironmentExtends::None),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: Some(8000),
                    },
                ]),
                ..default_component_decl()
            },
        },

        test_compile_environment_with_runner_and_resolver => {
            input = json!({
                "environments": [
                    {
                        "name": "myenv",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "parent",
                            }
                        ],
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "parent",
                                "scheme": "fuchsia-pkg",
                            }
                        ],
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::None),
                        runners: Some(vec![
                            fsys::RunnerRegistration {
                                source_name: Some("dart".to_string()),
                                source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                                target_name: Some("dart".to_string()),
                            }
                        ]),
                        resolvers: Some(vec![
                            fsys::ResolverRegistration {
                                resolver: Some("pkg_resolver".to_string()),
                                source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                                scheme: Some("fuchsia-pkg".to_string()),
                            }
                        ]),
                        stop_timeout_ms: None,
                    },
                ]),
                ..default_component_decl()
            },
        },

        test_compile_environment_with_runner_alias => {
            input = json!({
                "environments": [
                    {
                        "name": "myenv",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "parent",
                                "as": "my-dart",
                            }
                        ],
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::None),
                        runners: Some(vec![
                            fsys::RunnerRegistration {
                                source_name: Some("dart".to_string()),
                                source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                                target_name: Some("my-dart".to_string()),
                            }
                        ]),
                        resolvers: None,
                        stop_timeout_ms: None,
                    },
                ]),
                ..default_component_decl()
            },
        },

        test_compile_all_sections => {
            input = json!({
                "program": {
                    "binary": "bin/app",
                },
                "use": [
                    { "service": "CoolFonts", "path": "/svc/fuchsia.fonts.Provider" },
                    { "protocol": "LegacyCoolFonts", "path": "/svc/fuchsia.fonts.LegacyProvider" },
                    { "protocol": [ "ReallyGoodFonts", "IWouldNeverUseTheseFonts"]},
                    { "runner": "elf" },
                ],
                "expose": [
                    { "directory": "blobfs", "from": "self", "rights": ["r*"]},
                ],
                "offer": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#netstack", "#modular" ]
                    },
                    {
                        "protocol": "fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#netstack", "#modular" ],
                        "dependency": "weak_for_migration"
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
                "capabilities": [
                    {
                        "directory": "blobfs",
                        "path": "/volumes/blobfs",
                        "rights": [ "r*" ],
                    },
                    {
                        "runner": "myrunner",
                        "path": "/runner",
                        "from": "self",
                    },
                ],
                "facets": {
                    "author": "Fuchsia",
                    "year": 2018,
                },
                "environments": [
                    {
                        "name": "myenv",
                        "extends": "realm"
                    }
                ],
            }),
            output = fsys::ComponentDecl {
                program: Some(fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                    }]),
                }),
                uses: Some(vec![
                    fsys::UseDecl::Service (
                        fsys::UseServiceDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("CoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("LegacyCoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("ReallyGoodFonts".to_string()),
                            target_path: Some("/svc/ReallyGoodFonts".to_string()),
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_path: Some("IWouldNeverUseTheseFonts".to_string()),
                            target_path: Some("/svc/IWouldNeverUseTheseFonts".to_string()),
                        }
                    ),
                    fsys::UseDecl::Runner (
                        fsys::UseRunnerDecl {
                            source_name: Some("elf".to_string()),
                        }
                    ),
                ]),
                exposes: Some(vec![
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("blobfs".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_path: Some("blobfs".to_string()),
                            rights: Some(
                                fio2::Operations::Connect | fio2::Operations::Enumerate |
                                fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                fio2::Operations::GetAttributes
                            ),
                            subdir: None,
                        }
                    ),
                ]),
                offers: Some(vec![
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.Log".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("fuchsia.logger.Log".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.Log".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("fuchsia.logger.Log".to_string()),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_path: Some("fuchsia.logger.LegacyLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::WeakForMigration),
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_path: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_path: Some("fuchsia.logger.LegacyLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::WeakForMigration),
                        }
                    ),
                ]),
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Directory (
                        fsys::DirectoryDecl {
                            name: Some("blobfs".to_string()),
                            source_path: Some("/volumes/blobfs".to_string()),
                            rights: Some(fio2::Operations::Connect | fio2::Operations::Enumerate |
                                fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                fio2::Operations::GetAttributes
                            ),
                        }
                    ),
                    fsys::CapabilityDecl::Runner (
                        fsys::RunnerDecl {
                            name: Some("myrunner".to_string()),
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_path: Some("/runner".to_string()),
                        }
                    ),
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    },
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    },
                ]),
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                        environment: None,
                    }
                ]),
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                    }
                ]),
                facets: Some(fsys::Object {
                    entries: vec![
                        fsys::Entry {
                            key: "author".to_string(),
                            value: Some(Box::new(fsys::Value::Str("Fuchsia".to_string()))),
                        },
                        fsys::Entry {
                            key: "year".to_string(),
                            value: Some(Box::new(fsys::Value::Inum(2018))),
                        },
                    ],
                }),
            },
        },
    }

    #[test]
    fn test_compile_compact() {
        let input = json!({
            "use": [
                { "service": "CoolFonts", "path": "/svc/fuchsia.fonts.Provider" },
                { "protocol": "/fonts/LegacyCoolFonts", "as": "/svc/fuchsia.fonts.LegacyProvider" },
                { "directory": "/data/assets", "rights": ["read_bytes"] }
            ]
        });
        let output = fsys::ComponentDecl {
            uses: Some(vec![
                fsys::UseDecl::Service(fsys::UseServiceDecl {
                    source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                    source_name: Some("CoolFonts".to_string()),
                    target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                }),
                fsys::UseDecl::Protocol(fsys::UseProtocolDecl {
                    source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                    source_path: Some("/fonts/LegacyCoolFonts".to_string()),
                    target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
                }),
                fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                    source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                    source_path: Some("/data/assets".to_string()),
                    target_path: Some("/data/assets".to_string()),
                    rights: Some(fio2::Operations::ReadBytes),
                    subdir: None,
                }),
            ]),
            ..default_component_decl()
        };
        compile_test(input, output);
    }

    #[test]
    fn test_invalid_json() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_in_path = tmp_dir.path().join("test.cml");
        let tmp_out_path = tmp_dir.path().join("test.cm");

        let input = json!({
            "expose": [
                { "directory": "/volumes/blobfs", "from": "parent" }
            ]
        });
        File::create(&tmp_in_path).unwrap().write_all(format!("{}", input).as_bytes()).unwrap();
        {
            let result = compile(&tmp_in_path, &tmp_out_path.clone());
            assert_matches!(
                result,
                Err(Error::Parse { err, ..  }) if &err == "invalid value: string \"parent\", expected one or an array of \"framework\", \"self\", or \"#<child-name>\""
            );
        }
        // Compilation failed so output should not exist.
        {
            let result = fs::File::open(&tmp_out_path);
            assert_eq!(result.unwrap_err().kind(), io::ErrorKind::NotFound);
        }
    }
}
