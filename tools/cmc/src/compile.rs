// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml::{self, CapabilityClause};
use crate::error::Error;
use crate::features::FeatureSet;
use crate::include;
use crate::one_or_many::OneOrMany;
use crate::translate;
use crate::util;
use crate::validate;
use cm_types as cm;
use cml::EventModesClause;
use cml::EventSubscriptionsClause;
use fidl::encoding::encode_persistent;
use fidl_fuchsia_data as fdata;
use fidl_fuchsia_io2 as fio2;
use fidl_fuchsia_sys2 as fsys;
use serde_json::{self, value::Value, Map};
use std::collections::HashSet;
use std::fs;
use std::io::Write;
use std::path::PathBuf;

/// Read in a CML file and produce the equivalent CM.
pub fn compile(
    file: &PathBuf,
    output: &PathBuf,
    depfile: Option<PathBuf>,
    includepath: &Vec<PathBuf>,
    includeroot: &PathBuf,
    features: &FeatureSet,
    experimental_force_runner: &Option<String>,
) -> Result<(), Error> {
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

    let mut document = util::read_cml(&file)?;
    let includes = include::transitive_includes(&file, &includepath, &includeroot)?;
    for include in &includes {
        let mut include_document = util::read_cml(&include)?;
        document.merge_from(&mut include_document, &include)?;
    }
    if let Some(ref force_runner) = experimental_force_runner.as_ref() {
        if let Some(mut program) = document.program.as_mut() {
            program.runner = Some(cm_types::Name::new(force_runner.to_string())?);
        } else {
            document.program = Some(cml::Program {
                runner: Some(cm_types::Name::new(force_runner.to_string())?),
                ..cml::Program::default()
            });
        }
    }
    validate::validate_cml(&document, &file, &features)?;

    let mut out_data = compile_cml(&document)?;
    util::ensure_directory_exists(&output)?;
    let mut out_file =
        fs::OpenOptions::new().create(true).truncate(true).write(true).open(output)?;
    out_file.write(&encode_persistent(&mut out_data)?)?;

    // Write includes to depfile
    if let Some(depfile_path) = depfile {
        util::write_depfile(&depfile_path, Some(&output.to_path_buf()), &includes)?;
    }

    Ok(())
}

fn compile_cml(document: &cml::Document) -> Result<fsys::ComponentDecl, Error> {
    let all_capability_names = document.all_capability_names();
    let all_children = document.all_children_names().into_iter().collect();
    let all_collections = document.all_collection_names().into_iter().collect();
    Ok(fsys::ComponentDecl {
        program: document.program.as_ref().map(|p| translate_program(p)).transpose()?,
        uses: document
            .r#use
            .as_ref()
            .map(|u| translate_use(u, &all_capability_names, &all_children))
            .transpose()?,
        exposes: document
            .expose
            .as_ref()
            .map(|e| translate_expose(e, &all_capability_names, &all_collections))
            .transpose()?,
        offers: document
            .offer
            .as_ref()
            .map(|offer| {
                translate_offer(offer, &all_capability_names, &all_children, &all_collections)
            })
            .transpose()?,
        capabilities: document
            .capabilities
            .as_ref()
            .map(translate::translate_capabilities)
            .transpose()?,
        children: document.children.as_ref().map(translate_children).transpose()?,
        collections: document.collections.as_ref().map(translate_collections).transpose()?,
        environments: document
            .environments
            .as_ref()
            .map(|env| translate_environments(env, &all_capability_names))
            .transpose()?,
        facets: document.facets.clone().map(fsys_object_from_map).transpose()?,
        ..fsys::ComponentDecl::EMPTY
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
    Ok(fdata::Dictionary { entries: Some(entries), ..fdata::Dictionary::EMPTY })
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

/// Converts a [`serde_json::Map<String, serde_json::Value>`] to a [`fuchsia.data.Dictionary`].
///
/// Values may be null, strings, arrays of strings, or objects.
/// Object value are flattened with keys separated by a period:
///
/// ```json
/// {
///   "binary": "bin/app",
///   "lifecycle": {
///     "stop_event": "notify",
///     "nested": {
///       "foo": "bar"
///     }
///   }
/// }
/// ```
///
/// is flattened to:
///
/// ```json
/// {
///   "binary": "bin/app",
///   "lifecycle.stop_event": "notify",
///   "lifecycle.nested.foo": "bar"
/// }
/// ```
fn dictionary_from_nested_map(map: Map<String, Value>) -> Result<fdata::Dictionary, Error> {
    fn key_value_to_entries(
        key: String,
        value: Value,
    ) -> Result<Vec<fdata::DictionaryEntry>, Error> {
        if let Value::Object(map) = value {
            let entries = map
                .into_iter()
                .map(|(k, v)| key_value_to_entries([key.clone(), ".".to_string(), k].concat(), v))
                .collect::<Result<Vec<_>, _>>()?
                .into_iter()
                .flatten()
                .collect();
            return Ok(entries);
        }

        let entry_value = match value {
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
            _ => Err(Error::validate("Value must be string or list of strings")),
        }?;
        Ok(vec![fdata::DictionaryEntry { key, value: entry_value }])
    }

    let entries = map
        .into_iter()
        .map(|(k, v)| key_value_to_entries(k, v))
        .collect::<Result<Vec<_>, _>>()?
        .into_iter()
        .flatten()
        .collect();
    Ok(fdata::Dictionary { entries: Some(entries), ..fdata::Dictionary::EMPTY })
}

/// Translates a [`cml::Program`] to a [`fuchsa.sys2.ProgramDecl`].
fn translate_program(program: &cml::Program) -> Result<fsys::ProgramDecl, Error> {
    Ok(fsys::ProgramDecl {
        runner: program.runner.as_ref().map(|r| r.to_string()),
        info: Some(dictionary_from_nested_map(program.info.clone())?),
        ..fsys::ProgramDecl::EMPTY
    })
}

/// `use` rules consume a single capability from one source (parent|framework).
fn translate_use(
    use_in: &Vec<cml::Use>,
    all_capability_names: &HashSet<cml::Name>,
    all_children: &HashSet<&cml::Name>,
) -> Result<Vec<fsys::UseDecl>, Error> {
    let mut out_uses = vec![];
    for use_ in use_in {
        if let Some(n) = &use_.service {
            let source = extract_use_source(use_, all_capability_names, all_children)?;
            let target_paths =
                all_target_use_paths(use_, use_).ok_or_else(|| Error::internal("no capability"))?;
            let source_names = n.to_vec();
            for (source_name, target_path) in source_names.into_iter().zip(target_paths.into_iter())
            {
                out_uses.push(fsys::UseDecl::Service(fsys::UseServiceDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.clone().into()),
                    target_path: Some(target_path.into()),
                    dependency_type: Some(
                        use_.dependency.clone().unwrap_or(cm::DependencyType::Strong).into(),
                    ),
                    ..fsys::UseServiceDecl::EMPTY
                }));
            }
        } else if let Some(n) = &use_.protocol {
            let source = extract_use_source(use_, all_capability_names, all_children)?;
            let target_paths =
                all_target_use_paths(use_, use_).ok_or_else(|| Error::internal("no capability"))?;
            let source_names = n.to_vec();
            for (source_name, target_path) in source_names.into_iter().zip(target_paths.into_iter())
            {
                out_uses.push(fsys::UseDecl::Protocol(fsys::UseProtocolDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.clone().into()),
                    target_path: Some(target_path.into()),
                    dependency_type: Some(
                        use_.dependency.clone().unwrap_or(cm::DependencyType::Strong).into(),
                    ),
                    ..fsys::UseProtocolDecl::EMPTY
                }));
            }
        } else if let Some(n) = &use_.directory {
            let source = extract_use_source(use_, all_capability_names, all_children)?;
            let target_path = one_target_use_path(use_, use_)?;
            let rights = translate::extract_required_rights(use_, "use")?;
            let subdir = extract_use_subdir(use_);
            out_uses.push(fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                source: Some(source),
                source_name: Some(n.clone().into()),
                target_path: Some(target_path.into()),
                rights: Some(rights),
                subdir: subdir.map(|s| s.into()),
                dependency_type: Some(
                    use_.dependency.clone().unwrap_or(cm::DependencyType::Strong).into(),
                ),
                ..fsys::UseDirectoryDecl::EMPTY
            }));
        } else if let Some(n) = &use_.storage {
            let target_path = one_target_use_path(use_, use_)?;
            out_uses.push(fsys::UseDecl::Storage(fsys::UseStorageDecl {
                source_name: Some(n.clone().into()),
                target_path: Some(target_path.into()),
                ..fsys::UseStorageDecl::EMPTY
            }));
        } else if let Some(n) = &use_.event {
            let source = extract_use_event_source(use_)?;
            let target_names = all_target_capability_names(use_, use_)
                .ok_or_else(|| Error::internal("no capability"))?;
            let source_names = n.to_vec();
            for target_name in target_names {
                // When multiple source names are provided, there is no way to alias each one, so
                // source_name == target_name,
                // When one source name is provided, source_name may be aliased to a different
                // target_name, so we use source_names[0] to derive the source_name.
                let source_name = if source_names.len() == 1 {
                    source_names[0].clone()
                } else {
                    target_name.clone()
                };
                out_uses.push(fsys::UseDecl::Event(fsys::UseEventDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.into()),
                    target_name: Some(target_name.into()),
                    mode: match use_.event_modes() {
                        Some(modes) => {
                            if modes.0.contains(&cml::EventMode::Sync) {
                                Some(fsys::EventMode::Sync)
                            } else {
                                Some(fsys::EventMode::Async)
                            }
                        }
                        None => Some(fsys::EventMode::Async),
                    },
                    // We have already validated that none will be present if we were using many
                    // events.
                    filter: match use_.filter.clone() {
                        Some(dict) => Some(dictionary_from_map(dict)?),
                        None => None,
                    },
                    dependency_type: Some(
                        use_.dependency.clone().unwrap_or(cm::DependencyType::Strong).into(),
                    ),
                    ..fsys::UseEventDecl::EMPTY
                }));
            }
        } else if let Some(name) = &use_.event_stream {
            let opt_subscriptions = use_.event_subscriptions();
            out_uses.push(fsys::UseDecl::EventStream(fsys::UseEventStreamDecl {
                name: Some(name.to_string()),
                subscriptions: opt_subscriptions.map(|subscriptions| {
                    subscriptions
                        .iter()
                        .flat_map(|subscription| {
                            let mode = subscription.mode.as_ref();
                            subscription.event.iter().map(move |event| fsys::EventSubscription {
                                event_name: Some(event.to_string()),
                                mode: Some(match mode {
                                    Some(cml::EventMode::Sync) => fsys::EventMode::Sync,
                                    _ => fsys::EventMode::Async,
                                }),
                                ..fsys::EventSubscription::EMPTY
                            })
                        })
                        .collect()
                }),
                ..fsys::UseEventStreamDecl::EMPTY
            }));
        } else {
            return Err(Error::internal(format!("no capability in use declaration")));
        };
    }
    Ok(out_uses)
}

/// `expose` rules route a single capability from one or more sources (self|framework|#<child>) to
/// one or more targets (parent|framework).
fn translate_expose(
    expose_in: &Vec<cml::Expose>,
    all_capability_names: &HashSet<cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<fsys::ExposeDecl>, Error> {
    let mut out_exposes = vec![];
    for expose in expose_in.iter() {
        let target = extract_expose_target(expose)?;
        if let Some(source_names) = expose.service() {
            let sources = extract_all_expose_sources(expose, Some(all_collections))?;
            let target_names = all_target_capability_names(expose, expose)
                .ok_or_else(|| Error::internal("no capability"))?;
            for (source_name, target_name) in source_names.into_iter().zip(target_names.into_iter())
            {
                for source in &sources {
                    out_exposes.push(fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                        source: Some(clone_fsys_ref(source)?),
                        source_name: Some(source_name.clone().into()),
                        target_name: Some(target_name.clone().into()),
                        target: Some(clone_fsys_ref(&target)?),
                        ..fsys::ExposeServiceDecl::EMPTY
                    }))
                }
            }
        } else if let Some(n) = expose.protocol() {
            let source = extract_single_expose_source(expose, Some(all_capability_names))?;
            let source_names = n.to_vec();
            let target_names = all_target_capability_names(expose, expose)
                .ok_or_else(|| Error::internal("no capability"))?;
            for (source_name, target_name) in source_names.into_iter().zip(target_names.into_iter())
            {
                out_exposes.push(fsys::ExposeDecl::Protocol(fsys::ExposeProtocolDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.clone().into()),
                    target_name: Some(target_name.into()),
                    target: Some(clone_fsys_ref(&target)?),
                    ..fsys::ExposeProtocolDecl::EMPTY
                }))
            }
        } else if let Some(n) = expose.directory() {
            let source = extract_single_expose_source(expose, None)?;
            let source_names = n.to_vec();
            let target_names = all_target_capability_names(expose, expose)
                .ok_or_else(|| Error::internal("no capability"))?;
            let rights = extract_expose_rights(expose)?;
            let subdir = extract_expose_subdir(expose);
            for (source_name, target_name) in source_names.into_iter().zip(target_names.into_iter())
            {
                out_exposes.push(fsys::ExposeDecl::Directory(fsys::ExposeDirectoryDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.clone().into()),
                    target_name: Some(target_name.into()),
                    target: Some(clone_fsys_ref(&target)?),
                    rights,
                    subdir: subdir.as_ref().map(|s| s.clone().into()),
                    ..fsys::ExposeDirectoryDecl::EMPTY
                }))
            }
        } else if let Some(n) = expose.runner() {
            let source = extract_single_expose_source(expose, None)?;
            let source_names = n.to_vec();
            let target_names = all_target_capability_names(expose, expose)
                .ok_or_else(|| Error::internal("no capability"))?;
            for (source_name, target_name) in source_names.into_iter().zip(target_names.into_iter())
            {
                out_exposes.push(fsys::ExposeDecl::Runner(fsys::ExposeRunnerDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                    target_name: Some(target_name.into()),
                    ..fsys::ExposeRunnerDecl::EMPTY
                }))
            }
        } else if let Some(n) = expose.resolver() {
            let source = extract_single_expose_source(expose, None)?;
            let source_names = n.to_vec();
            let target_names = all_target_capability_names(expose, expose)
                .ok_or_else(|| Error::internal("no capability"))?;
            for (source_name, target_name) in source_names.into_iter().zip(target_names.into_iter())
            {
                out_exposes.push(fsys::ExposeDecl::Resolver(fsys::ExposeResolverDecl {
                    source: Some(clone_fsys_ref(&source)?),
                    source_name: Some(source_name.clone().into()),
                    target: Some(clone_fsys_ref(&target)?),
                    target_name: Some(target_name.into()),
                    ..fsys::ExposeResolverDecl::EMPTY
                }))
            }
        } else {
            return Err(Error::internal(format!("expose: must specify a known capability")));
        }
    }
    Ok(out_exposes)
}

/// `offer` rules route multiple capabilities from multiple sources to multiple targets.
fn translate_offer(
    offer_in: &Vec<cml::Offer>,
    all_capability_names: &HashSet<cml::Name>,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<fsys::OfferDecl>, Error> {
    let mut out_offers = vec![];
    for offer in offer_in.iter() {
        if let Some(n) = offer.service() {
            let entries = extract_offer_sources_and_targets(
                offer,
                n,
                all_capability_names,
                all_children,
                all_collections,
            )?;
            for (source, source_name, target, target_name) in entries {
                out_offers.push(fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                    source: Some(source),
                    source_name: Some(source_name.into()),
                    target: Some(target),
                    target_name: Some(target_name.into()),
                    ..fsys::OfferServiceDecl::EMPTY
                }));
            }
        } else if let Some(n) = offer.protocol() {
            let entries = extract_offer_sources_and_targets(
                offer,
                n,
                all_capability_names,
                all_children,
                all_collections,
            )?;
            for (source, source_name, target, target_name) in entries {
                out_offers.push(fsys::OfferDecl::Protocol(fsys::OfferProtocolDecl {
                    source: Some(source),
                    source_name: Some(source_name.into()),
                    target: Some(target),
                    target_name: Some(target_name.into()),
                    dependency_type: Some(
                        offer.dependency.clone().unwrap_or(cm::DependencyType::Strong).into(),
                    ),
                    ..fsys::OfferProtocolDecl::EMPTY
                }));
            }
        } else if let Some(n) = offer.directory() {
            let entries = extract_offer_sources_and_targets(
                offer,
                n,
                all_capability_names,
                all_children,
                all_collections,
            )?;
            for (source, source_name, target, target_name) in entries {
                out_offers.push(fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                    source: Some(source),
                    source_name: Some(source_name.into()),
                    target: Some(target),
                    target_name: Some(target_name.into()),
                    rights: extract_offer_rights(offer)?,
                    subdir: extract_offer_subdir(offer).map(|s| s.into()),
                    dependency_type: Some(
                        offer.dependency.clone().unwrap_or(cm::DependencyType::Strong).into(),
                    ),
                    ..fsys::OfferDirectoryDecl::EMPTY
                }));
            }
        } else if let Some(n) = offer.storage() {
            let entries = extract_offer_sources_and_targets(
                offer,
                n,
                all_capability_names,
                all_children,
                all_collections,
            )?;
            for (source, source_name, target, target_name) in entries {
                out_offers.push(fsys::OfferDecl::Storage(fsys::OfferStorageDecl {
                    source: Some(source),
                    source_name: Some(source_name.into()),
                    target: Some(target),
                    target_name: Some(target_name.into()),
                    ..fsys::OfferStorageDecl::EMPTY
                }));
            }
        } else if let Some(n) = offer.runner() {
            let entries = extract_offer_sources_and_targets(
                offer,
                n,
                all_capability_names,
                all_children,
                all_collections,
            )?;
            for (source, source_name, target, target_name) in entries {
                out_offers.push(fsys::OfferDecl::Runner(fsys::OfferRunnerDecl {
                    source: Some(source),
                    source_name: Some(source_name.into()),
                    target: Some(target),
                    target_name: Some(target_name.into()),
                    ..fsys::OfferRunnerDecl::EMPTY
                }));
            }
        } else if let Some(n) = offer.resolver() {
            let entries = extract_offer_sources_and_targets(
                offer,
                n,
                all_capability_names,
                all_children,
                all_collections,
            )?;
            for (source, source_name, target, target_name) in entries {
                out_offers.push(fsys::OfferDecl::Resolver(fsys::OfferResolverDecl {
                    source: Some(source),
                    source_name: Some(source_name.into()),
                    target: Some(target),
                    target_name: Some(target_name.into()),
                    ..fsys::OfferResolverDecl::EMPTY
                }));
            }
        } else if let Some(n) = offer.event() {
            let entries = extract_offer_sources_and_targets(
                offer,
                n,
                all_capability_names,
                all_children,
                all_collections,
            )?;
            for (source, source_name, target, target_name) in entries {
                out_offers.push(fsys::OfferDecl::Event(fsys::OfferEventDecl {
                    source: Some(source),
                    source_name: Some(source_name.into()),
                    target: Some(target),
                    target_name: Some(target_name.into()),
                    // We have already validated that none will be present if we were using many
                    // events.
                    filter: match offer.filter.clone() {
                        Some(dict) => Some(dictionary_from_map(dict)?),
                        None => None,
                    },
                    mode: match offer.event_modes() {
                        Some(modes) => {
                            if modes.0.contains(&cml::EventMode::Sync) {
                                Some(fsys::EventMode::Sync)
                            } else {
                                Some(fsys::EventMode::Async)
                            }
                        }
                        None => Some(fsys::EventMode::Async),
                    },
                    ..fsys::OfferEventDecl::EMPTY
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
            startup: Some(child.startup.clone().into()),
            environment: extract_environment_ref(child.environment.as_ref()).map(|e| e.into()),
            on_terminate: child.on_terminate.as_ref().map(|r| r.clone().into()),
            ..fsys::ChildDecl::EMPTY
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
            durability: Some(collection.durability.clone().into()),
            allowed_offers: collection.allowed_offers.clone().map(|a| a.into()),
            environment: extract_environment_ref(collection.environment.as_ref()).map(|e| e.into()),
            ..fsys::CollectionDecl::EMPTY
        });
    }
    Ok(out_collections)
}

fn translate_environments(
    envs_in: &Vec<cml::Environment>,
    all_capability_names: &HashSet<cml::Name>,
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
                debug_capabilities: env
                    .debug
                    .as_ref()
                    .map(|debug_capabiltities| {
                        translate_debug_capabilities(debug_capabiltities, all_capability_names)
                    })
                    .transpose()?,
                stop_timeout_ms: env.stop_timeout_ms.map(|s| s.0),
                ..fsys::EnvironmentDecl::EMPTY
            })
        })
        .collect()
}

fn translate_runner_registration(
    reg: &cml::RunnerRegistration,
) -> Result<fsys::RunnerRegistration, Error> {
    Ok(fsys::RunnerRegistration {
        source_name: Some(reg.runner.clone().into()),
        source: Some(extract_single_offer_source(reg, None)?),
        target_name: Some(reg.r#as.as_ref().unwrap_or(&reg.runner).clone().into()),
        ..fsys::RunnerRegistration::EMPTY
    })
}

fn translate_resolver_registration(
    reg: &cml::ResolverRegistration,
) -> Result<fsys::ResolverRegistration, Error> {
    Ok(fsys::ResolverRegistration {
        resolver: Some(reg.resolver.clone().into()),
        source: Some(extract_single_offer_source(reg, None)?),
        scheme: Some(
            reg.scheme
                .as_str()
                .parse::<cm_types::UrlScheme>()
                .map_err(|e| Error::internal(format!("invalid URL scheme: {}", e)))?
                .into(),
        ),
        ..fsys::ResolverRegistration::EMPTY
    })
}

fn translate_debug_capabilities(
    capabilities: &Vec<cml::DebugRegistration>,
    all_capability_names: &HashSet<cml::Name>,
) -> Result<Vec<fsys::DebugRegistration>, Error> {
    let mut out_capabilities = vec![];
    for capability in capabilities {
        if let Some(n) = capability.protocol() {
            let source = extract_single_offer_source(capability, Some(all_capability_names))?;
            let targets = all_target_capability_names(capability, capability)
                .ok_or_else(|| Error::internal("no capability"))?;
            let source_names = n.to_vec();
            for target_name in targets {
                // When multiple source names are provided, there is no way to alias each one, so
                // source_name == target_name.
                // When one source name is provided, source_name may be aliased to a different
                // target_name, so we source_names[0] to derive the source_name.
                //
                // TODO: This logic could be simplified to use iter::zip() if
                // extract_all_targets_for_each_child returned separate vectors for targets and
                // target_names instead of the cross product of them.
                let source_name = if source_names.len() == 1 {
                    source_names[0].clone()
                } else {
                    target_name.clone()
                };
                out_capabilities.push(fsys::DebugRegistration::Protocol(
                    fsys::DebugProtocolRegistration {
                        source: Some(clone_fsys_ref(&source)?),
                        source_name: Some(source_name.into()),
                        target_name: Some(target_name.into()),
                        ..fsys::DebugProtocolRegistration::EMPTY
                    },
                ));
            }
        }
    }
    Ok(out_capabilities)
}

fn extract_use_source(
    in_obj: &cml::Use,
    all_capability_names: &HashSet<cml::Name>,
    all_children_names: &HashSet<&cml::Name>,
) -> Result<fsys::Ref, Error> {
    match in_obj.from.as_ref() {
        Some(cml::UseFromRef::Parent) => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
        Some(cml::UseFromRef::Framework) => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        Some(cml::UseFromRef::Debug) => Ok(fsys::Ref::Debug(fsys::DebugRef {})),
        Some(cml::UseFromRef::Self_) => Ok(fsys::Ref::Self_(fsys::SelfRef {})),
        Some(cml::UseFromRef::Named(name)) => {
            if all_capability_names.contains(&name) {
                Ok(fsys::Ref::Capability(fsys::CapabilityRef { name: name.clone().into() }))
            } else if all_children_names.contains(&name) {
                Ok(fsys::Ref::Child(fsys::ChildRef { name: name.clone().into(), collection: None }))
            } else {
                Err(Error::internal(format!(
                    "use source \"{:?}\" not supported for \"use from\"",
                    name
                )))
            }
        }
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
        fsys::Ref::Framework(framework_ref) => Ok(fsys::Ref::Framework(framework_ref.clone())),
        fsys::Ref::Capability(capability_ref) => Ok(fsys::Ref::Capability(capability_ref.clone())),
        fsys::Ref::Debug(debug_ref) => Ok(fsys::Ref::Debug(debug_ref.clone())),
        _ => Err(Error::internal("Unknown fsys::Ref found.")),
    }
}

fn extract_use_event_source(in_obj: &cml::Use) -> Result<fsys::Ref, Error> {
    match in_obj.from.as_ref() {
        Some(cml::UseFromRef::Parent) => Ok(fsys::Ref::Parent(fsys::ParentRef {})),
        Some(cml::UseFromRef::Framework) => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        Some(cml::UseFromRef::Named(name)) => {
            Ok(fsys::Ref::Capability(fsys::CapabilityRef { name: name.clone().into() }))
        }
        Some(cml::UseFromRef::Debug) => {
            Err(Error::internal(format!("Debug source provided for \"use event\"")))
        }
        Some(cml::UseFromRef::Self_) => {
            Err(Error::internal(format!("Self source not supported for \"use event\"")))
        }
        None => Err(Error::internal(format!("No source \"from\" provided for \"use\""))),
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

fn expose_source_from_ref(
    reference: &cml::ExposeFromRef,
    all_capability_names: Option<&HashSet<cml::Name>>,
    all_collections: Option<&HashSet<&cml::Name>>,
) -> Result<fsys::Ref, Error> {
    match reference {
        cml::ExposeFromRef::Named(name) => {
            if all_capability_names.is_some() && all_capability_names.unwrap().contains(&name) {
                Ok(fsys::Ref::Capability(fsys::CapabilityRef { name: name.clone().into() }))
            } else if all_collections.is_some() && all_collections.unwrap().contains(&name) {
                Ok(fsys::Ref::Collection(fsys::CollectionRef { name: name.clone().into() }))
            } else {
                Ok(fsys::Ref::Child(fsys::ChildRef { name: name.clone().into(), collection: None }))
            }
        }
        cml::ExposeFromRef::Framework => Ok(fsys::Ref::Framework(fsys::FrameworkRef {})),
        cml::ExposeFromRef::Self_ => Ok(fsys::Ref::Self_(fsys::SelfRef {})),
    }
}

fn extract_single_expose_source(
    in_obj: &cml::Expose,
    all_capability_names: Option<&HashSet<cml::Name>>,
) -> Result<fsys::Ref, Error> {
    match &in_obj.from {
        OneOrMany::One(reference) => expose_source_from_ref(&reference, all_capability_names, None),
        OneOrMany::Many(many) => {
            return Err(Error::internal(format!(
                "multiple unexpected \"from\" clauses for \"expose\": {:?}",
                many
            )))
        }
    }
}

fn extract_all_expose_sources(
    in_obj: &cml::Expose,
    all_collections: Option<&HashSet<&cml::Name>>,
) -> Result<Vec<fsys::Ref>, Error> {
    in_obj
        .from
        .to_vec()
        .into_iter()
        .map(|e| expose_source_from_ref(e, None, all_collections))
        .collect()
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

fn extract_single_offer_source<T>(
    in_obj: &T,
    all_capability_names: Option<&HashSet<cml::Name>>,
) -> Result<fsys::Ref, Error>
where
    T: cml::FromClause,
{
    match in_obj.from_() {
        OneOrMany::One(reference) => {
            translate::offer_source_from_ref(reference, all_capability_names, None)
        }
        many => {
            return Err(Error::internal(format!(
                "multiple unexpected \"from\" clauses for \"offer\": {}",
                many
            )))
        }
    }
}

fn extract_all_offer_sources<T: cml::FromClause>(
    in_obj: &T,
    all_capability_names: &HashSet<cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<fsys::Ref>, Error> {
    in_obj
        .from_()
        .to_vec()
        .into_iter()
        .map(|r| {
            translate::offer_source_from_ref(
                r.clone(),
                Some(all_capability_names),
                Some(all_collections),
            )
        })
        .collect()
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

// Return a list of (source, source capability id, target, target capability id) expressed in the
// `offer`.
fn extract_offer_sources_and_targets(
    offer: &cml::Offer,
    source_names: OneOrMany<cml::Name>,
    all_capability_names: &HashSet<cml::Name>,
    all_children: &HashSet<&cml::Name>,
    all_collections: &HashSet<&cml::Name>,
) -> Result<Vec<(fsys::Ref, cml::Name, fsys::Ref, cml::Name)>, Error> {
    let mut out = vec![];

    let source_names = source_names.to_vec();
    let sources = extract_all_offer_sources(offer, all_capability_names, all_collections)?;
    let target_names = all_target_capability_names(offer, offer)
        .ok_or_else(|| Error::internal("no capability".to_string()))?;

    for source in &sources {
        for to in &offer.to {
            for target_name in &target_names {
                // When multiple source names are provided, there is no way to alias each one,
                // so we can assume source_name == target_name.  When one source name is provided,
                // source_name may be aliased to a different target_name, so we use
                // source_names[0] to obtain the source_name.
                let source_name = if source_names.len() == 1 {
                    source_names[0].clone()
                } else {
                    target_name.clone()
                };
                let target =
                    translate_child_or_collection_ref(to.into(), all_children, all_collections)?;
                out.push((source.clone(), source_name, target, target_name.clone()))
            }
        }
    }
    Ok(out)
}

/// Return the target paths specified in the given use declaration.
fn all_target_use_paths<T, U>(in_obj: &T, to_obj: &U) -> Option<OneOrMany<cml::Path>>
where
    T: cml::CapabilityClause,
    U: cml::AsClause + cml::PathClause,
{
    if let Some(n) = in_obj.service() {
        Some(svc_paths_from_names(n, to_obj))
    } else if let Some(n) = in_obj.protocol() {
        Some(svc_paths_from_names(n, to_obj))
    } else if let Some(_) = in_obj.directory() {
        let path = to_obj.path().expect("no path on use directory");
        Some(OneOrMany::One(path.clone()))
    } else if let Some(_) = in_obj.storage() {
        let path = to_obj.path().expect("no path on use storage");
        Some(OneOrMany::One(path.clone()))
    } else if let Some(_) = in_obj.event_stream() {
        let path = to_obj.path().expect("no path on event stream");
        Some(OneOrMany::One(path.clone()))
    } else {
        None
    }
}

/// Returns the list of paths derived from a `use` declaration with `names` and `to_obj`. `to_obj`
/// must be a declaration that has a `path` clause.
fn svc_paths_from_names<T>(names: OneOrMany<cml::Name>, to_obj: &T) -> OneOrMany<cml::Path>
where
    T: cml::PathClause,
{
    match names {
        OneOrMany::One(n) => {
            if let Some(path) = to_obj.path() {
                OneOrMany::One(path.clone())
            } else {
                OneOrMany::One(format!("/svc/{}", n).parse().unwrap())
            }
        }
        OneOrMany::Many(v) => {
            let many = v.iter().map(|n| format!("/svc/{}", n).parse().unwrap()).collect();
            OneOrMany::Many(many)
        }
    }
}

/// Return the single target path specified in the given use declaration.
fn one_target_use_path<T, U>(in_obj: &T, to_obj: &U) -> Result<cml::Path, Error>
where
    T: cml::CapabilityClause,
    U: cml::AsClause + cml::PathClause,
{
    match all_target_use_paths(in_obj, to_obj) {
        Some(OneOrMany::One(target_name)) => Ok(target_name),
        Some(OneOrMany::Many(_)) => {
            Err(Error::internal("expecting one capability, but multiple provided"))
        }
        _ => Err(Error::internal("expecting one capability, but none provided")),
    }
}

/// Return the target names or paths specified in the given capability.
fn all_target_capability_names<T, U>(in_obj: &T, to_obj: &U) -> Option<OneOrMany<cml::Name>>
where
    T: cml::CapabilityClause,
    U: cml::AsClause + cml::PathClause,
{
    if let Some(as_) = to_obj.r#as() {
        // We've already validated that when `as` is specified, only 1 source id exists.
        Some(OneOrMany::One(as_.clone()))
    } else {
        if let Some(n) = in_obj.service() {
            Some(n.clone())
        } else if let Some(n) = in_obj.protocol() {
            Some(n.clone())
        } else if let Some(n) = in_obj.directory() {
            Some(n.clone())
        } else if let Some(n) = in_obj.storage() {
            Some(n.clone())
        } else if let Some(n) = in_obj.runner() {
            Some(n.clone())
        } else if let Some(n) = in_obj.resolver() {
            Some(n.clone())
        } else if let Some(n) = in_obj.event() {
            Some(n.clone())
        } else {
            None
        }
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
    use crate::features::Feature;
    use fidl::encoding::decode_persistent;
    use matches::assert_matches;
    use serde_json::json;
    use std::fs::File;
    use std::io::{self, Read, Write};
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
                    let tmp_dir = TempDir::new().unwrap();
                    let tmp_in_path = tmp_dir.path().join("test.cml");
                    let tmp_out_path = tmp_dir.path().join("test.cm");
                    compile_test(tmp_in_path, tmp_out_path, None, $input, $result, &FeatureSet::empty()).expect("compilation failed");
                }
            )+
        }
    }

    macro_rules! test_compile_with_features {
        (
            $features:expr,
            {
                $(
                    $(#[$m:meta])*
                    $test_name:ident => {
                        input = $input:expr,
                        output = $result:expr,
                    },
                )+
            }
        ) => {
            $(
                $(#[$m])*
                #[test]
                fn $test_name() {
                    let tmp_dir = TempDir::new().unwrap();
                    let tmp_in_path = tmp_dir.path().join("test.cml");
                    let tmp_out_path = tmp_dir.path().join("test.cm");
                    let features = $features;
                    compile_test(tmp_in_path, tmp_out_path, None, $input, $result, &features).expect("compilation failed");
                }
            )+
        }
    }

    fn compile_test_with_forced_runner(
        in_path: PathBuf,
        out_path: PathBuf,
        includepath: Option<PathBuf>,
        input: serde_json::value::Value,
        expected_output: fsys::ComponentDecl,
        features: &FeatureSet,
        experimental_force_runner: &Option<String>,
    ) -> Result<(), Error> {
        File::create(&in_path).unwrap().write_all(format!("{}", input).as_bytes()).unwrap();
        let includepath = includepath.unwrap_or(PathBuf::new());
        compile(
            &in_path,
            &out_path.clone(),
            None,
            &vec![includepath.clone()],
            &includepath,
            features,
            experimental_force_runner,
        )?;
        let mut buffer = Vec::new();
        fs::File::open(&out_path).unwrap().read_to_end(&mut buffer).unwrap();
        let output: fsys::ComponentDecl = decode_persistent(&buffer).unwrap();
        assert_eq!(output, expected_output);
        Ok(())
    }

    fn compile_test(
        in_path: PathBuf,
        out_path: PathBuf,
        includepath: Option<PathBuf>,
        input: serde_json::value::Value,
        expected_output: fsys::ComponentDecl,
        features: &FeatureSet,
    ) -> Result<(), Error> {
        compile_test_with_forced_runner(
            in_path,
            out_path,
            includepath,
            input,
            expected_output,
            features,
            &None,
        )
    }

    fn default_component_decl() -> fsys::ComponentDecl {
        fsys::ComponentDecl::EMPTY
    }

    test_compile_with_features! { FeatureSet::from(vec![Feature::Services]), {
        test_compile_service_capabilities => {
            input = json!({
                "capabilities": [
                    {
                        "service": "myservice",
                        "path": "/service",
                    },
                    {
                        "service": [ "myservice2", "myservice3" ],
                    },
                ]
            }),
            output = fsys::ComponentDecl {
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("myservice".to_string()),
                            source_path: Some("/service".to_string()),
                            ..fsys::ServiceDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("myservice2".to_string()),
                            source_path: Some("/svc/myservice2".to_string()),
                            ..fsys::ServiceDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("myservice3".to_string()),
                            source_path: Some("/svc/myservice3".to_string()),
                            ..fsys::ServiceDecl::EMPTY
                        }
                    ),
                ]),
                ..fsys::ComponentDecl::EMPTY
            },
        },
        test_compile_use_service => {
            input = json!({
                "use": [
                    { "service": "CoolFonts", "path": "/svc/fuchsia.fonts.Provider" },
                    { "service": "fuchsia.sys2.Realm", "from": "framework" },
                    { "service": [ "myservice", "myservice2" ] },
                ]
            }),
            output = fsys::ComponentDecl {
                uses: Some(vec![
                    fsys::UseDecl::Service (
                        fsys::UseServiceDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("CoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                            ..fsys::UseServiceDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Service (
                        fsys::UseServiceDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("fuchsia.sys2.Realm".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.Realm".to_string()),
                            ..fsys::UseServiceDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Service (
                        fsys::UseServiceDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("myservice".to_string()),
                            target_path: Some("/svc/myservice".to_string()),
                            ..fsys::UseServiceDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Service (
                        fsys::UseServiceDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("myservice2".to_string()),
                            target_path: Some("/svc/myservice2".to_string()),
                            ..fsys::UseServiceDecl::EMPTY
                        }
                    ),
                ]),
                ..fsys::ComponentDecl::EMPTY
            },
        },
        test_compile_offer_service => {
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
                        "to": [ "#coll" ],
                        "as": "fuchsia.logger.Log2",
                    },
                    {
                        "service": [
                            "my.service.Service",
                            "my.service.Service2",
                        ],
                        "from": ["#logger", "self"],
                        "to": [ "#netstack" ]
                    },
                    {
                        "service": "my.service.CollectionService",
                        "from": ["#coll"],
                        "to": [ "#netstack" ],
                    },
                ],
                "capabilities": [
                    {
                        "service": [
                            "my.service.Service",
                            "my.service.Service2",
                        ],
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://logger.cm"
                    },
                    {
                        "name": "netstack",
                        "url": "fuchsia-pkg://netstack.cm"
                    },
                ],
                "collections": [
                    {
                        "name": "coll",
                        "durability": "transient",
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
                            ..fsys::OfferServiceDecl::EMPTY
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
                                name: "coll".to_string(),
                            })),
                            target_name: Some("fuchsia.logger.Log2".to_string()),
                            ..fsys::OfferServiceDecl::EMPTY
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
                            ..fsys::OfferServiceDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("my.service.Service2".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("my.service.Service2".to_string()),
                            ..fsys::OfferServiceDecl::EMPTY
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
                            ..fsys::OfferServiceDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("my.service.Service2".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("my.service.Service2".to_string()),
                            ..fsys::OfferServiceDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Service (
                        fsys::OfferServiceDecl {
                            source: Some(fsys::Ref::Collection(fsys::CollectionRef { name: "coll".to_string() })),
                            source_name: Some("my.service.CollectionService".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("my.service.CollectionService".to_string()),
                            ..fsys::OfferServiceDecl::EMPTY
                        }
                    ),
                ]),
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("my.service.Service".to_string()),
                            source_path: Some("/svc/my.service.Service".to_string()),
                            ..fsys::ServiceDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("my.service.Service2".to_string()),
                            source_path: Some("/svc/my.service.Service2".to_string()),
                            ..fsys::ServiceDecl::EMPTY
                        }
                    ),
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    },
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://netstack.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    }
                ]),
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("coll".to_string()),
                        durability: Some(fsys::Durability::Transient),
                        allowed_offers: None,
                        environment: None,
                        ..fsys::CollectionDecl::EMPTY
                    }
                ]),
                ..fsys::ComponentDecl::EMPTY
            },
        },
        test_compile_expose_service => {
            input = json!({
                "expose": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#logger",
                        "as": "fuchsia.logger.Log2",
                    },
                    {
                        "service": [
                            "my.service.Service",
                            "my.service.Service2",
                        ],
                        "from": ["#logger", "self"],
                    },
                    {
                        "service": "my.service.CollectionService",
                        "from": ["#coll"],
                    },
                ],
                "capabilities": [
                    {
                        "service": [
                            "my.service.Service",
                            "my.service.Service2",
                        ],
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://logger.cm"
                    },
                ],
                "collections": [
                    {
                        "name": "coll",
                        "durability": "transient",
                    },
                ],
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
                            ..fsys::ExposeServiceDecl::EMPTY
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
                            ..fsys::ExposeServiceDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Service (
                        fsys::ExposeServiceDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("my.service.Service".to_string()),
                            target_name: Some("my.service.Service".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            ..fsys::ExposeServiceDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Service (
                        fsys::ExposeServiceDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("my.service.Service2".to_string()),
                            target_name: Some("my.service.Service2".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            ..fsys::ExposeServiceDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Service (
                        fsys::ExposeServiceDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("my.service.Service2".to_string()),
                            target_name: Some("my.service.Service2".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            ..fsys::ExposeServiceDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Service (
                        fsys::ExposeServiceDecl {
                            source: Some(fsys::Ref::Collection(fsys::CollectionRef { name: "coll".to_string() })),
                            source_name: Some("my.service.CollectionService".to_string()),
                            target_name: Some("my.service.CollectionService".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            ..fsys::ExposeServiceDecl::EMPTY
                        }
                    ),
                ]),
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("my.service.Service".to_string()),
                            source_path: Some("/svc/my.service.Service".to_string()),
                            ..fsys::ServiceDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Service (
                        fsys::ServiceDecl {
                            name: Some("my.service.Service2".to_string()),
                            source_path: Some("/svc/my.service.Service2".to_string()),
                            ..fsys::ServiceDecl::EMPTY
                        }
                    ),
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    }
                ]),
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("coll".to_string()),
                        durability: Some(fsys::Durability::Transient),
                        allowed_offers: None,
                        environment: None,
                        ..fsys::CollectionDecl::EMPTY
                    }
                ]),
                ..fsys::ComponentDecl::EMPTY
            },
        },
    }}

    test_compile_with_features! { FeatureSet::from(vec![Feature::DynamicOffers]), {
        test_compile_dynamic_offers => {
            input = json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent",
                    },
                    {
                        "name": "tests",
                        "durability": "transient",
                        "allowed_offers": "static_only",
                    },
                    {
                        "name": "dynamic_offers",
                        "durability": "transient",
                        "allowed_offers": "static_and_dynamic",
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                        allowed_offers: None,
                        ..fsys::CollectionDecl::EMPTY
                    },
                    fsys::CollectionDecl {
                        name: Some("tests".to_string()),
                        durability: Some(fsys::Durability::Transient),
                        allowed_offers: Some(fsys::AllowedOffers::StaticOnly),
                        ..fsys::CollectionDecl::EMPTY
                    },
                    fsys::CollectionDecl {
                        name: Some("dynamic_offers".to_string()),
                        durability: Some(fsys::Durability::Transient),
                        allowed_offers: Some(fsys::AllowedOffers::StaticAndDynamic),
                        ..fsys::CollectionDecl::EMPTY
                    }
                ]),
                ..fsys::ComponentDecl::EMPTY
            },
        },
    }}

    test_compile! {
        test_compile_empty => {
            input = json!({}),
            output = default_component_decl(),
        },

        test_compile_empty_includes => {
            input = json!({ "include": [] }),
            output = default_component_decl(),
        },

        test_compile_program => {
            input = json!({
                "program": {
                    "runner": "elf",
                    "binary": "bin/app",
                },
            }),
            output = fsys::ComponentDecl {
                program: Some(fsys::ProgramDecl {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fsys::ProgramDecl::EMPTY
                }),
                ..default_component_decl()
            },
        },

        test_compile_program_with_nested_objects => {
            input = json!({
                "program": {
                    "runner": "elf",
                    "binary": "bin/app",
                    "one": {
                        "two": {
                            "three.four": {
                                "five": "six"
                            }
                        },
                    }
                },
            }),
            output = fsys::ComponentDecl {
                program: Some(fsys::ProgramDecl {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![
                            fdata::DictionaryEntry {
                                key: "binary".to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                            },
                            fdata::DictionaryEntry {
                                key: "one.two.three.four.five".to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str("six".to_string()))),
                            },
                        ]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fsys::ProgramDecl::EMPTY
                }),
                ..default_component_decl()
            },
        },

        test_compile_use => {
            input = json!({
                "use": [
                    { "protocol": "LegacyCoolFonts", "path": "/svc/fuchsia.fonts.LegacyProvider" },
                    { "protocol": "fuchsia.sys2.LegacyRealm", "from": "framework" },
                    { "protocol": "fuchsia.sys2.StorageAdmin", "from": "#data-storage" },
                    { "protocol": "fuchsia.sys2.DebugProto", "from": "debug" },
                    { "protocol": "fuchsia.sys2.Echo", "from": "self"},
                    { "directory": "assets", "rights" : ["read_bytes"], "path": "/data/assets" },
                    {
                        "directory": "config",
                        "path": "/data/config",
                        "from": "parent",
                        "rights": ["read_bytes"],
                        "subdir": "fonts",
                    },
                    { "storage": "hippos", "path": "/hippos" },
                    { "storage": "cache", "path": "/tmp" },
                    { "event": "destroyed", "from": "parent" },
                    { "event": ["started", "stopped"], "from": "framework" },
                    {
                        "event": "directory_ready",
                        "as": "diagnostics",
                        "from": "parent",
                        "filter": { "name": "diagnostics" }
                    },
                    {
                        "event_stream": "foo_stream",
                        "subscriptions": [
                            {
                                "event": [ "started", "diagnostics" ],
                                "mode": "async",
                            },
                            {
                                "event": [ "destroyed" ],
                                "mode": "sync"
                            }
                        ]
                    }
                ],
                "capabilities": [
                    {
                        "storage": "data-storage",
                        "from": "parent",
                        "backing_dir": "minfs",
                        "storage_id": "static_instance_id_or_moniker",
                    }
                ]
            }),
            output = fsys::ComponentDecl {
                uses: Some(vec![
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("LegacyCoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("fuchsia.sys2.LegacyRealm".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.LegacyRealm".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Capability(fsys::CapabilityRef { name: "data-storage".to_string() })),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Debug(fsys::DebugRef {})),
                            source_name: Some("fuchsia.sys2.DebugProto".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.DebugProto".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("fuchsia.sys2.Echo".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.Echo".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Directory (
                        fsys::UseDirectoryDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("assets".to_string()),
                            target_path: Some("/data/assets".to_string()),
                            rights: Some(fio2::Operations::ReadBytes),
                            subdir: None,
                            ..fsys::UseDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Directory (
                        fsys::UseDirectoryDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("config".to_string()),
                            target_path: Some("/data/config".to_string()),
                            rights: Some(fio2::Operations::ReadBytes),
                            subdir: Some("fonts".to_string()),
                            ..fsys::UseDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Storage (
                        fsys::UseStorageDecl {
                            source_name: Some("hippos".to_string()),
                            target_path: Some("/hippos".to_string()),
                            ..fsys::UseStorageDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Storage (
                        fsys::UseStorageDecl {
                            source_name: Some("cache".to_string()),
                            target_path: Some("/tmp".to_string()),
                            ..fsys::UseStorageDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("destroyed".to_string()),
                            target_name: Some("destroyed".to_string()),
                            filter: None,
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::UseEventDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("started".to_string()),
                            target_name: Some("started".to_string()),
                            filter: None,
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::UseEventDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("stopped".to_string()),
                            target_name: Some("stopped".to_string()),
                            filter: None,
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::UseEventDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Event (
                        fsys::UseEventDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("directory_ready".to_string()),
                            target_name: Some("diagnostics".to_string()),
                            filter: Some(fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: "name".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str("diagnostics".to_string()))),
                                    },
                                ]),
                                ..fdata::Dictionary::EMPTY
                            }),
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::UseEventDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::EventStream(fsys::UseEventStreamDecl {
                        name: Some("foo_stream".to_string()),
                        subscriptions: Some(vec![
                            fsys::EventSubscription {
                                event_name: Some("started".to_string()),
                                mode: Some(fsys::EventMode::Async),
                                ..fsys::EventSubscription::EMPTY
                            },
                            fsys::EventSubscription {
                                event_name: Some("diagnostics".to_string()),
                                mode: Some(fsys::EventMode::Async),
                                ..fsys::EventSubscription::EMPTY
                            },
                            fsys::EventSubscription {
                                event_name: Some("destroyed".to_string()),
                                mode: Some(fsys::EventMode::Sync),
                                ..fsys::EventSubscription::EMPTY
                            },
                        ]),
                        ..fsys::UseEventStreamDecl::EMPTY
                    })
                ]),
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Storage(fsys::StorageDecl {
                        name: Some("data-storage".to_string()),
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        backing_dir: Some("minfs".to_string()),
                        subdir: None,
                        storage_id: Some(fsys::StorageId::StaticInstanceIdOrMoniker),
                        ..fsys::StorageDecl::EMPTY
                    }),
                ]),
                ..default_component_decl()
            },
        },

        test_compile_expose => {
            input = json!({
                "expose": [
                    {
                        "protocol": "fuchsia.logger.Log",
                        "from": "#logger",
                        "as": "fuchsia.logger.LegacyLog",
                        "to": "parent"
                    },
                    {
                        "protocol": [ "A", "B" ],
                        "from": "self",
                        "to": "parent"
                    },
                    {
                        "protocol": "C",
                        "from": "#data-storage",
                    },
                    {
                        "directory": "blob",
                        "from": "self",
                        "to": "framework",
                        "rights": ["r*"],
                    },
                    {
                        "directory": [ "blob2", "blob3" ],
                        "from": "#logger",
                        "to": "parent",
                    },
                    { "directory": "hub", "from": "framework" },
                    { "runner": "web", "from": "#logger", "to": "parent", "as": "web-rename" },
                    { "runner": [ "runner_a", "runner_b" ], "from": "#logger" },
                    { "resolver": "my_resolver", "from": "#logger", "to": "parent", "as": "pkg_resolver" },
                    { "resolver": [ "resolver_a", "resolver_b" ], "from": "#logger" },
                ],
                "capabilities": [
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
                    },
                    {
                        "storage": "data-storage",
                        "from": "parent",
                        "backing_dir": "minfs",
                        "storage_id": "static_instance_id_or_moniker",
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                exposes: Some(vec![
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.Log".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            ..fsys::ExposeProtocolDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("A".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("A".to_string()),
                            ..fsys::ExposeProtocolDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("B".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("B".to_string()),
                            ..fsys::ExposeProtocolDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Protocol (
                        fsys::ExposeProtocolDecl {
                            source: Some(fsys::Ref::Capability(fsys::CapabilityRef {
                                name: "data-storage".to_string(),
                            })),
                            source_name: Some("C".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("C".to_string()),
                            ..fsys::ExposeProtocolDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("blob".to_string()),
                            target: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            target_name: Some("blob".to_string()),
                            rights: Some(
                                fio2::Operations::Connect | fio2::Operations::Enumerate |
                                fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                fio2::Operations::GetAttributes
                            ),
                            subdir: None,
                            ..fsys::ExposeDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("blob2".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("blob2".to_string()),
                            rights: None,
                            subdir: None,
                            ..fsys::ExposeDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("blob3".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("blob3".to_string()),
                            rights: None,
                            subdir: None,
                            ..fsys::ExposeDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("hub".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("hub".to_string()),
                            rights: None,
                            subdir: None,
                            ..fsys::ExposeDirectoryDecl::EMPTY
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
                            ..fsys::ExposeRunnerDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Runner (
                        fsys::ExposeRunnerDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("runner_a".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("runner_a".to_string()),
                            ..fsys::ExposeRunnerDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Runner (
                        fsys::ExposeRunnerDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("runner_b".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("runner_b".to_string()),
                            ..fsys::ExposeRunnerDecl::EMPTY
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
                            ..fsys::ExposeResolverDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Resolver (
                        fsys::ExposeResolverDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("resolver_a".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("resolver_a".to_string()),
                            ..fsys::ExposeResolverDecl::EMPTY
                        }
                    ),
                    fsys::ExposeDecl::Resolver (
                        fsys::ExposeResolverDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("resolver_b".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("resolver_b".to_string()),
                            ..fsys::ExposeResolverDecl::EMPTY
                        }
                    ),
                ]),
                offers: None,
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("A".to_string()),
                            source_path: Some("/svc/A".to_string()),
                            ..fsys::ProtocolDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("B".to_string()),
                            source_path: Some("/svc/B".to_string()),
                            ..fsys::ProtocolDecl::EMPTY
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
                            ..fsys::DirectoryDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Runner (
                        fsys::RunnerDecl {
                            name: Some("web".to_string()),
                            source_path: Some("/svc/fuchsia.component.ComponentRunner".to_string()),
                            ..fsys::RunnerDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Storage(fsys::StorageDecl {
                        name: Some("data-storage".to_string()),
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        backing_dir: Some("minfs".to_string()),
                        subdir: None,
                        storage_id: Some(fsys::StorageId::StaticInstanceIdOrMoniker),
                        ..fsys::StorageDecl::EMPTY
                    }),
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    }
                ]),
                ..default_component_decl()
            },
        },

        test_compile_offer => {
            input = json!({
                "offer": [
                    {
                        "protocol": "fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": "#netstack", // Verifies compilation of singleton "to:".
                        "dependency": "weak"
                    },
                    {
                        "protocol": "fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#modular" ], // Verifies compilation of "to:" as array of one element.
                        "as": "fuchsia.logger.LegacySysLog",
                        "dependency": "strong"
                    },
                    {
                        "protocol": [
                            "fuchsia.setui.SetUiService",
                            "fuchsia.test.service.Name"
                        ],
                        "from": "parent",
                        "to": [ "#modular" ]
                    },
                    {
                        "protocol": "fuchsia.sys2.StorageAdmin",
                        "from": "#data",
                        "to": [ "#modular" ],
                    },
                    {
                        "directory": "assets",
                        "from": "parent",
                        "to": [ "#netstack" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "directory": [ "assets2", "assets3" ],
                        "from": "parent",
                        "to": [ "#modular", "#netstack" ],
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
                        "directory": "hub",
                        "from": "framework",
                        "to": [ "#modular" ],
                        "as": "hub",
                    },
                    {
                        "storage": "data",
                        "from": "self",
                        "to": [
                            "#netstack",
                            "#modular"
                        ],
                    },
                    {
                        "storage": [ "storage_a", "storage_b" ],
                        "from": "parent",
                        "to": "#netstack",
                    },
                    {
                        "runner": "elf",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "as": "elf-renamed",
                    },
                    {
                        "runner": [ "runner_a", "runner_b" ],
                        "from": "parent",
                        "to": "#netstack",
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
                        "event": "directory_ready",
                        "from": "parent",
                        "to": [ "#netstack" ],
                        "as": "net-ready",
                        "filter": {
                            "name": [
                                "diagnostics",
                                "foo"
                            ],
                        }
                    },
                    {
                        "resolver": "my_resolver",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "as": "pkg_resolver",
                    },
                    {
                        "resolver": [ "resolver_a", "resolver_b" ],
                        "from": "parent",
                        "to": "#netstack",
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
                    {
                        "storage": "data",
                        "backing_dir": "minfs",
                        "from": "#logger",
                        "storage_id": "static_instance_id_or_moniker",
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                offers: Some(vec![
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::Weak),
                            ..fsys::OfferProtocolDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("fuchsia.logger.LegacySysLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferProtocolDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("fuchsia.setui.SetUiService".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("fuchsia.setui.SetUiService".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferProtocolDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("fuchsia.test.service.Name".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("fuchsia.test.service.Name".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferProtocolDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Capability(fsys::CapabilityRef {
                                name: "data".to_string(),
                            })),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferProtocolDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("assets".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("assets".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::WeakForMigration),
                            ..fsys::OfferDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("assets2".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("assets2".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("assets3".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("assets3".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("assets2".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("assets2".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("assets3".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("assets3".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("data".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("assets".to_string()),
                            rights: None,
                            subdir: Some("index/file".to_string()),
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Directory (
                        fsys::OfferDirectoryDecl {
                            source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                            source_name: Some("hub".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("hub".to_string()),
                            rights: None,
                            subdir: None,
                            dependency_type: Some(fsys::DependencyType::Strong),
                            ..fsys::OfferDirectoryDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Storage (
                        fsys::OfferStorageDecl {
                            source_name: Some("data".to_string()),
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("data".to_string()),
                            ..fsys::OfferStorageDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Storage (
                        fsys::OfferStorageDecl {
                            source_name: Some("data".to_string()),
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("data".to_string()),
                            ..fsys::OfferStorageDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Storage (
                        fsys::OfferStorageDecl {
                            source_name: Some("storage_a".to_string()),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("storage_a".to_string()),
                            ..fsys::OfferStorageDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Storage (
                        fsys::OfferStorageDecl {
                            source_name: Some("storage_b".to_string()),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("storage_b".to_string()),
                            ..fsys::OfferStorageDecl::EMPTY
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
                            ..fsys::OfferRunnerDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Runner (
                        fsys::OfferRunnerDecl {
                            source_name: Some("runner_a".to_string()),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("runner_a".to_string()),
                            ..fsys::OfferRunnerDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Runner (
                        fsys::OfferRunnerDecl {
                            source_name: Some("runner_b".to_string()),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("runner_b".to_string()),
                            ..fsys::OfferRunnerDecl::EMPTY
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
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::OfferEventDecl::EMPTY
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
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::OfferEventDecl::EMPTY
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
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::OfferEventDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Event (
                        fsys::OfferEventDecl {
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("directory_ready".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("net-ready".to_string()),
                            filter: Some(fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: "name".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::StrVec(
                                            vec!["diagnostics".to_string(), "foo".to_string()]
                                        ))),
                                    },
                                ]),
                                ..fdata::Dictionary::EMPTY
                            }),
                            mode: Some(fsys::EventMode::Async),
                            ..fsys::OfferEventDecl::EMPTY
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
                            ..fsys::OfferResolverDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Resolver (
                        fsys::OfferResolverDecl {
                            source_name: Some("resolver_a".to_string()),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("resolver_a".to_string()),
                            ..fsys::OfferResolverDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Resolver (
                        fsys::OfferResolverDecl {
                            source_name: Some("resolver_b".to_string()),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("resolver_b".to_string()),
                            ..fsys::OfferResolverDecl::EMPTY
                        }
                    ),
                ]),
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Storage (
                        fsys::StorageDecl {
                            name: Some("data".to_string()),
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            backing_dir: Some("minfs".to_string()),
                            subdir: None,
                            storage_id: Some(fsys::StorageId::StaticInstanceIdOrMoniker),
                            ..fsys::StorageDecl::EMPTY
                        }
                    )
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    },
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    },
                ]),
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                        allowed_offers: None,
                        environment: None,
                        ..fsys::CollectionDecl::EMPTY
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
                        "on_terminate": "reboot",
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
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    },
                    fsys::ChildDecl {
                        name: Some("gmail".to_string()),
                        url: Some("https://www.google.com/gmail".to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    },
                    fsys::ChildDecl {
                        name: Some("echo".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: Some("myenv".to_string()),
                        on_terminate: Some(fsys::OnTerminate::Reboot),
                        ..fsys::ChildDecl::EMPTY
                    }
                ]),
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                        ..fsys::EnvironmentDecl::EMPTY
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
                        allowed_offers: None,
                        environment: None,
                        ..fsys::CollectionDecl::EMPTY
                    },
                    fsys::CollectionDecl {
                        name: Some("tests".to_string()),
                        durability: Some(fsys::Durability::Transient),
                        allowed_offers: None,
                        environment: Some("myenv".to_string()),
                        ..fsys::CollectionDecl::EMPTY
                    }
                ]),
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                        ..fsys::EnvironmentDecl::EMPTY
                    }
                ]),
                ..default_component_decl()
            },
        },

        test_compile_capabilities => {
            input = json!({
                "capabilities": [
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
                        "storage_id": "static_instance_id_or_moniker",
                    },
                    {
                        "storage": "mystorage2",
                        "backing_dir": "storage2",
                        "from": "#minfs",
                        "storage_id": "static_instance_id",
                    },
                    {
                        "runner": "myrunner",
                        "path": "/runner",
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
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol".to_string()),
                            source_path: Some("/protocol".to_string()),
                            ..fsys::ProtocolDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol2".to_string()),
                            source_path: Some("/svc/myprotocol2".to_string()),
                            ..fsys::ProtocolDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol3".to_string()),
                            source_path: Some("/svc/myprotocol3".to_string()),
                            ..fsys::ProtocolDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Protocol (
                        fsys::ProtocolDecl {
                            name: Some("myprotocol4".to_string()),
                            source_path: Some("/svc/myprotocol4".to_string()),
                            ..fsys::ProtocolDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Directory (
                        fsys::DirectoryDecl {
                            name: Some("mydirectory".to_string()),
                            source_path: Some("/directory".to_string()),
                            rights: Some(fio2::Operations::Connect),
                            ..fsys::DirectoryDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Storage (
                        fsys::StorageDecl {
                            name: Some("mystorage".to_string()),
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "minfs".to_string(),
                                collection: None,
                            })),
                            backing_dir: Some("storage".to_string()),
                            subdir: None,
                            storage_id: Some(fsys::StorageId::StaticInstanceIdOrMoniker),
                            ..fsys::StorageDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Storage (
                        fsys::StorageDecl {
                            name: Some("mystorage2".to_string()),
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "minfs".to_string(),
                                collection: None,
                            })),
                            backing_dir: Some("storage2".to_string()),
                            subdir: None,
                            storage_id: Some(fsys::StorageId::StaticInstanceId),
                            ..fsys::StorageDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Runner (
                        fsys::RunnerDecl {
                            name: Some("myrunner".to_string()),
                            source_path: Some("/runner".to_string()),
                            ..fsys::RunnerDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Resolver (
                        fsys::ResolverDecl {
                            name: Some("myresolver".to_string()),
                            source_path: Some("/resolver".to_string()),
                            ..fsys::ResolverDecl::EMPTY
                        }
                    )
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("minfs".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
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
                        ..fsys::EnvironmentDecl::EMPTY
                    },
                    fsys::EnvironmentDecl {
                        name: Some("myenv2".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                        ..fsys::EnvironmentDecl::EMPTY
                    },
                    fsys::EnvironmentDecl {
                        name: Some("myenv3".to_string()),
                        extends: Some(fsys::EnvironmentExtends::None),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: Some(8000),
                        ..fsys::EnvironmentDecl::EMPTY
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
                                ..fsys::RunnerRegistration::EMPTY
                            }
                        ]),
                        resolvers: Some(vec![
                            fsys::ResolverRegistration {
                                resolver: Some("pkg_resolver".to_string()),
                                source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                                scheme: Some("fuchsia-pkg".to_string()),
                                ..fsys::ResolverRegistration::EMPTY
                            }
                        ]),
                        stop_timeout_ms: None,
                        ..fsys::EnvironmentDecl::EMPTY
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
                                ..fsys::RunnerRegistration::EMPTY
                            }
                        ]),
                        resolvers: None,
                        stop_timeout_ms: None,
                        ..fsys::EnvironmentDecl::EMPTY
                    },
                ]),
                ..default_component_decl()
            },
        },

        test_compile_environment_with_debug => {
            input = json!({
                "capabilities": [
                    {
                        "protocol": "fuchsia.serve.service",
                    },
                ],
                "environments": [
                    {
                        "name": "myenv",
                        "debug": [
                            {
                                "protocol": "fuchsia.serve.service",
                                "from": "self",
                                "as": "my-service",
                            }
                        ],
                    },
                ],
            }),
            output = fsys::ComponentDecl {
                capabilities: Some(vec![
                    fsys::CapabilityDecl::Protocol(
                        fsys::ProtocolDecl {
                            name : Some("fuchsia.serve.service".to_owned()),
                            source_path: Some("/svc/fuchsia.serve.service".to_owned()),
                            ..fsys::ProtocolDecl::EMPTY
                        }
                    )
                ]),
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::None),
                        debug_capabilities: Some(vec![
                            fsys::DebugRegistration::Protocol( fsys::DebugProtocolRegistration {
                                source_name: Some("fuchsia.serve.service".to_string()),
                                source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                                target_name: Some("my-service".to_string()),
                                ..fsys::DebugProtocolRegistration::EMPTY
                            }),
                        ]),
                        resolvers: None,
                        runners: None,
                        stop_timeout_ms: None,
                        ..fsys::EnvironmentDecl::EMPTY
                    },
                ]),
                ..default_component_decl()
            },
        },

        test_compile_all_sections => {
            input = json!({
                "program": {
                    "runner": "elf",
                    "binary": "bin/app",
                },
                "use": [
                    { "protocol": "LegacyCoolFonts", "path": "/svc/fuchsia.fonts.LegacyProvider" },
                    { "protocol": [ "ReallyGoodFonts", "IWouldNeverUseTheseFonts"]},
                    { "protocol":  "DebugProtocol", "from": "debug"},
                ],
                "expose": [
                    { "directory": "blobfs", "from": "self", "rights": ["r*"]},
                ],
                "offer": [
                    {
                        "protocol": "fuchsia.logger.LegacyLog",
                        "from": "#logger",
                        "to": [ "#netstack", "#modular" ],
                        "dependency": "weak"
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
                    },
                    {
                        "protocol": "fuchsia.serve.service",
                    }
                ],
                "facets": {
                    "author": "Fuchsia",
                    "year": 2018,
                },
                "environments": [
                    {
                        "name": "myenv",
                        "extends": "realm",
                        "debug": [
                            {
                                "protocol": "fuchsia.serve.service",
                                "from": "self",
                                "as": "my-service",
                            },
                            {
                                "protocol": "fuchsia.logger.LegacyLog",
                                "from": "#logger",
                            }
                        ]
                    }
                ],
            }),
            output = fsys::ComponentDecl {
                program: Some(fsys::ProgramDecl {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fsys::ProgramDecl::EMPTY
                }),
                uses: Some(vec![
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("LegacyCoolFonts".to_string()),
                            target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("ReallyGoodFonts".to_string()),
                            target_path: Some("/svc/ReallyGoodFonts".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            source_name: Some("IWouldNeverUseTheseFonts".to_string()),
                            target_path: Some("/svc/IWouldNeverUseTheseFonts".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                    fsys::UseDecl::Protocol (
                        fsys::UseProtocolDecl {
                            dependency_type: Some(fsys::DependencyType::Strong),
                            source: Some(fsys::Ref::Debug(fsys::DebugRef {})),
                            source_name: Some("DebugProtocol".to_string()),
                            target_path: Some("/svc/DebugProtocol".to_string()),
                            ..fsys::UseProtocolDecl::EMPTY
                        }
                    ),
                ]),
                exposes: Some(vec![
                    fsys::ExposeDecl::Directory (
                        fsys::ExposeDirectoryDecl {
                            source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                            source_name: Some("blobfs".to_string()),
                            target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                            target_name: Some("blobfs".to_string()),
                            rights: Some(
                                fio2::Operations::Connect | fio2::Operations::Enumerate |
                                fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                fio2::Operations::GetAttributes
                            ),
                            subdir: None,
                            ..fsys::ExposeDirectoryDecl::EMPTY
                        }
                    ),
                ]),
                offers: Some(vec![
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::Weak),
                            ..fsys::OfferProtocolDecl::EMPTY
                        }
                    ),
                    fsys::OfferDecl::Protocol (
                        fsys::OfferProtocolDecl {
                            source: Some(fsys::Ref::Child(fsys::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            })),
                            source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            target: Some(fsys::Ref::Collection(fsys::CollectionRef {
                                name: "modular".to_string(),
                            })),
                            target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                            dependency_type: Some(fsys::DependencyType::Weak),
                            ..fsys::OfferProtocolDecl::EMPTY
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
                            ..fsys::DirectoryDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Runner (
                        fsys::RunnerDecl {
                            name: Some("myrunner".to_string()),
                            source_path: Some("/runner".to_string()),
                            ..fsys::RunnerDecl::EMPTY
                        }
                    ),
                    fsys::CapabilityDecl::Protocol(
                        fsys::ProtocolDecl {
                            name : Some("fuchsia.serve.service".to_owned()),
                            source_path: Some("/svc/fuchsia.serve.service".to_owned()),
                            ..fsys::ProtocolDecl::EMPTY
                        }
                    )
                ]),
                children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    },
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fsys::ChildDecl::EMPTY
                    },
                ]),
                collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                        allowed_offers: None,
                        environment: None,
                        ..fsys::CollectionDecl::EMPTY
                    }
                ]),
                environments: Some(vec![
                    fsys::EnvironmentDecl {
                        name: Some("myenv".to_string()),
                        extends: Some(fsys::EnvironmentExtends::Realm),
                        runners: None,
                        resolvers: None,
                        stop_timeout_ms: None,
                        debug_capabilities: Some(vec![
                            fsys::DebugRegistration::Protocol( fsys::DebugProtocolRegistration {
                                source_name: Some("fuchsia.serve.service".to_string()),
                                source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                                target_name: Some("my-service".to_string()),
                                ..fsys::DebugProtocolRegistration::EMPTY
                            }),
                            fsys::DebugRegistration::Protocol( fsys::DebugProtocolRegistration {
                                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                source: Some(fsys::Ref::Child(fsys::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                ..fsys::DebugProtocolRegistration::EMPTY
                            }),
                        ]),
                        ..fsys::EnvironmentDecl::EMPTY
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
                ..fsys::ComponentDecl::EMPTY
            },
        },
    }

    #[test]
    fn test_invalid_json() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_in_path = tmp_dir.path().join("test.cml");
        let tmp_out_path = tmp_dir.path().join("test.cm");

        let input = json!({
            "expose": [
                { "directory": "blobfs", "from": "parent" }
            ]
        });
        File::create(&tmp_in_path).unwrap().write_all(format!("{}", input).as_bytes()).unwrap();
        {
            let result = compile(
                &tmp_in_path,
                &tmp_out_path.clone(),
                None,
                &vec![],
                &PathBuf::new(),
                &FeatureSet::empty(),
                &None,
            );
            assert_matches!(
                result,
                Err(Error::Parse { err, .. }) if &err == "invalid value: string \"parent\", expected one or an array of \"framework\", \"self\", or \"#<child-name>\""
            );
        }
        // Compilation failed so output should not exist.
        {
            let result = fs::File::open(&tmp_out_path);
            assert_eq!(result.unwrap_err().kind(), io::ErrorKind::NotFound);
        }
    }

    #[test]
    fn test_missing_include() {
        let tmp_dir = TempDir::new().unwrap();
        let in_path = tmp_dir.path().join("test.cml");
        let out_path = tmp_dir.path().join("test.cm");
        let result = compile_test(
            in_path,
            out_path,
            Some(tmp_dir.into_path()),
            json!({ "include": [ "doesnt_exist.cml" ] }),
            default_component_decl(),
            &FeatureSet::empty(),
        );
        assert_matches!(
            result,
            Err(Error::Parse { err, .. }) if err.starts_with("Couldn't read include ") && err.contains("doesnt_exist.cml")
        );
    }

    #[test]
    fn test_good_include() {
        let tmp_dir = TempDir::new().unwrap();
        let foo_path = tmp_dir.path().join("foo.cml");
        fs::File::create(&foo_path)
            .unwrap()
            .write_all(format!("{}", json!({ "program": { "runner": "elf" } })).as_bytes())
            .unwrap();

        let in_path = tmp_dir.path().join("test.cml");
        let out_path = tmp_dir.path().join("test.cm");
        compile_test(
            in_path,
            out_path,
            Some(tmp_dir.into_path()),
            json!({
                "include": [ "foo.cml" ],
                "program": { "binary": "bin/test" },
            }),
            fsys::ComponentDecl {
                program: Some(fsys::ProgramDecl {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str(
                                "bin/test".to_string(),
                            ))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fsys::ProgramDecl::EMPTY
                }),
                ..default_component_decl()
            },
            &FeatureSet::empty(),
        )
        .unwrap();
    }

    #[test]
    fn test_good_include_with_force_runner() {
        let tmp_dir = TempDir::new().unwrap();
        let foo_path = tmp_dir.path().join("foo.cml");
        fs::File::create(&foo_path)
            .unwrap()
            .write_all(format!("{}", json!({ "program": { "runner": "elf" } })).as_bytes())
            .unwrap();

        let in_path = tmp_dir.path().join("test.cml");
        let out_path = tmp_dir.path().join("test.cm");
        compile_test_with_forced_runner(
            in_path,
            out_path,
            Some(tmp_dir.into_path()),
            json!({
                "include": [ "foo.cml" ],
                "program": { "binary": "bin/test" },
            }),
            fsys::ComponentDecl {
                program: Some(fsys::ProgramDecl {
                    runner: Some("elf_test_runner".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str(
                                "bin/test".to_string(),
                            ))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fsys::ProgramDecl::EMPTY
                }),
                ..default_component_decl()
            },
            &FeatureSet::empty(),
            &Some("elf_test_runner".to_string()),
        )
        .unwrap();
    }

    #[test]
    fn test_recursive_include() {
        let tmp_dir = TempDir::new().unwrap();
        let foo_path = tmp_dir.path().join("foo.cml");
        fs::File::create(&foo_path)
            .unwrap()
            .write_all(format!("{}", json!({ "include": [ "bar.cml" ] })).as_bytes())
            .unwrap();

        let bar_path = tmp_dir.path().join("bar.cml");
        fs::File::create(&bar_path)
            .unwrap()
            .write_all(format!("{}", json!({ "program": { "runner": "elf" } })).as_bytes())
            .unwrap();

        let in_path = tmp_dir.path().join("test.cml");
        let out_path = tmp_dir.path().join("test.cm");
        compile_test(
            in_path,
            out_path,
            Some(tmp_dir.into_path()),
            json!({
                "include": [ "foo.cml" ],
                "program": { "binary": "bin/test" },
            }),
            fsys::ComponentDecl {
                program: Some(fsys::ProgramDecl {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str(
                                "bin/test".to_string(),
                            ))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fsys::ProgramDecl::EMPTY
                }),
                ..default_component_decl()
            },
            &FeatureSet::empty(),
        )
        .unwrap();
    }

    #[test]
    fn test_cyclic_include() {
        let tmp_dir = TempDir::new().unwrap();
        let foo_path = tmp_dir.path().join("foo.cml");
        fs::File::create(&foo_path)
            .unwrap()
            .write_all(format!("{}", json!({ "include": [ "bar.cml" ] })).as_bytes())
            .unwrap();

        let bar_path = tmp_dir.path().join("bar.cml");
        fs::File::create(&bar_path)
            .unwrap()
            .write_all(format!("{}", json!({ "include": [ "foo.cml" ] })).as_bytes())
            .unwrap();

        let in_path = tmp_dir.path().join("test.cml");
        let out_path = tmp_dir.path().join("test.cm");
        let result = compile_test(
            in_path,
            out_path,
            Some(tmp_dir.into_path()),
            json!({
                "include": [ "foo.cml" ],
                "program": {
                    "runner": "elf",
                    "binary": "bin/test",
                },
            }),
            default_component_decl(),
            &FeatureSet::empty(),
        );
        assert_matches!(result, Err(Error::Parse { err, .. }) if err.contains("Includes cycle"));
    }

    #[test]
    fn test_conflicting_includes() {
        let tmp_dir = TempDir::new().unwrap();
        let foo_path = tmp_dir.path().join("foo.cml");
        fs::File::create(&foo_path)
            .unwrap()
            .write_all(
                format!("{}", json!({ "use": [ { "protocol": "foo", "path": "/svc/foo" } ] }))
                    .as_bytes(),
            )
            .unwrap();
        let bar_path = tmp_dir.path().join("bar.cml");

        // Try to mount protocol "bar" under the same path "/svc/foo".
        fs::File::create(&bar_path)
            .unwrap()
            .write_all(
                format!("{}", json!({ "use": [ { "protocol": "bar", "path": "/svc/foo" } ] }))
                    .as_bytes(),
            )
            .unwrap();

        let in_path = tmp_dir.path().join("test.cml");
        let out_path = tmp_dir.path().join("test.cm");
        let result = compile_test(
            in_path,
            out_path,
            Some(tmp_dir.into_path()),
            json!({
                "include": [ "foo.cml", "bar.cml" ],
                "program": {
                    "runner": "elf",
                    "binary": "bin/test",
                },
            }),
            default_component_decl(),
            &FeatureSet::empty(),
        );
        // Including both foo.cml and bar.cml should fail to validate because of an incoming
        // namespace collision.
        assert_matches!(result, Err(Error::Validate { err, .. }) if err.contains("is a duplicate \"use\""));
    }

    #[test]
    fn test_overlapping_includes() {
        let tmp_dir = TempDir::new().unwrap();
        let foo1_path = tmp_dir.path().join("foo1.cml");
        fs::File::create(&foo1_path)
            .unwrap()
            .write_all(format!("{}", json!({ "use": [ { "protocol": "foo" } ] })).as_bytes())
            .unwrap();

        let foo2_path = tmp_dir.path().join("foo2.cml");
        // Include protocol "foo" again
        fs::File::create(&foo2_path)
            .unwrap()
            // Use different but equivalent syntax to further stress any overlap affordances
            .write_all(format!("{}", json!({ "use": [ { "protocol": [ "foo" ] } ] })).as_bytes())
            .unwrap();

        let in_path = tmp_dir.path().join("test.cml");
        let out_path = tmp_dir.path().join("test.cm");
        let result = compile_test(
            in_path,
            out_path,
            Some(tmp_dir.into_path()),
            json!({
                "include": [ "foo1.cml", "foo2.cml" ],
                "program": {
                    "runner": "elf",
                    "binary": "bin/test",
                },
            }),
            fsys::ComponentDecl {
                program: Some(fsys::ProgramDecl {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str(
                                "bin/test".to_string(),
                            ))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fsys::ProgramDecl::EMPTY
                }),
                uses: Some(vec![fsys::UseDecl::Protocol(fsys::UseProtocolDecl {
                    dependency_type: Some(fsys::DependencyType::Strong),
                    source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                    source_name: Some("foo".to_string()),
                    target_path: Some("/svc/foo".to_string()),
                    ..fsys::UseProtocolDecl::EMPTY
                })]),
                ..default_component_decl()
            },
            &FeatureSet::empty(),
        );
        // Including both foo1.cml and foo2.cml is fine because they overlap,
        // so merging foo2.cml after having merged foo1.cml is a no-op.
        assert_matches!(result, Ok(()));
    }
}
