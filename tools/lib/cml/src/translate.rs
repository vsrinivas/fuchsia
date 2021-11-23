// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// In order to support compiling Component decl types from both the
// `fuchsia.sys2` namespace and the `fuchsia.component.decl` one, we
// use a macro to generate the library code for each decl type.
// This is a temporary workaround while all clients of this lib
// are migrated off the `fuchsia.sys2` namespace.
// Each namespace will get its own Rust module so that the end result would
// be:
// cml::translate::fdecl::compile (for `fuchsia.component.decl`)
// cml::translate::fsys::compile (for `fuchsia.sys2`)
macro_rules! compile {
    ($namespace:ident) => {
        pub mod $namespace {

            // `fidl_fuchsia_sys2` is unused when generating `fdecl` code;
            // `fdecl` is unused when generating `$namespace` code.
            #[allow(unused_imports)]
            use {
                crate::{
                    error::Error, AnyRef, AsClause, Capability, CapabilityClause, Child,
                    Collection, ConfigKey, ConfigValueType, ConfigVectorElementType,
                    DebugRegistration, Document, Environment, EnvironmentExtends, EnvironmentRef,
                    EventMode, EventModesClause, EventSubscriptionsClause, Expose, ExposeFromRef,
                    ExposeToRef, FromClause, Offer, OneOrMany, Path, PathClause, Program,
                    ResolverRegistration, RightsClause, RunnerRegistration, Use, UseFromRef,
                },
                cm_rust::convert as fdecl,
                cm_types::{self as cm, Name},
                fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
                serde_json::{Map, Value},
                sha2::{Digest, Sha256},
                std::collections::{BTreeMap, HashSet},
                std::convert::Into,
            };

            /// Compiles the Document into a FIDL `ComponentDecl`.
            /// Note: This function ignores the `include` section of the document. It is
            /// assumed that those entries were already processed.
            pub fn compile(document: &Document) -> Result<$namespace::ComponentDecl, Error> {
                let all_capability_names = document.all_capability_names();
                let all_children = document.all_children_names().into_iter().collect();
                let all_collections = document.all_collection_names().into_iter().collect();
                Ok($namespace::ComponentDecl {
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
                            translate_offer(
                                offer,
                                &all_capability_names,
                                &all_children,
                                &all_collections,
                            )
                        })
                        .transpose()?,
                    capabilities: document
                        .capabilities
                        .as_ref()
                        .map(|c| translate_capabilities(c, false))
                        .transpose()?,
                    children: document.children.as_ref().map(translate_children).transpose()?,
                    collections: document
                        .collections
                        .as_ref()
                        .map(translate_collections)
                        .transpose()?,
                    environments: document
                        .environments
                        .as_ref()
                        .map(|env| translate_environments(env, &all_capability_names))
                        .transpose()?,
                    facets: document.facets.clone().map(dictionary_from_nested_map).transpose()?,
                    config: document.config.as_ref().map(|c| translate_config(c)),
                    ..$namespace::ComponentDecl::EMPTY
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
            fn value_to_dictionary_value(
                value: Value,
            ) -> Result<Option<Box<fdata::DictionaryValue>>, Error> {
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
            fn dictionary_from_nested_map(
                map: Map<String, Value>,
            ) -> Result<fdata::Dictionary, Error> {
                fn key_value_to_entries(
                    key: String,
                    value: Value,
                ) -> Result<Vec<fdata::DictionaryEntry>, Error> {
                    if let Value::Object(map) = value {
                        let entries = map
                            .into_iter()
                            .map(|(k, v)| {
                                key_value_to_entries([key.clone(), ".".to_string(), k].concat(), v)
                            })
                            .collect::<Result<Vec<_>, _>>()?
                            .into_iter()
                            .flatten()
                            .collect();
                        return Ok(entries);
                    }

                    let entry_value = match value {
                        Value::Null => Ok(None),
                        Value::String(s) => {
                            Ok(Some(Box::new(fdata::DictionaryValue::Str(s.clone()))))
                        }
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

            /// Translates a [`Program`] to a [`fuchsa.sys2.ProgramDecl`].
            fn translate_program(program: &Program) -> Result<$namespace::ProgramDecl, Error> {
                Ok($namespace::ProgramDecl {
                    runner: program.runner.as_ref().map(|r| r.to_string()),
                    info: Some(dictionary_from_nested_map(program.info.clone())?),
                    ..$namespace::ProgramDecl::EMPTY
                })
            }

            /// `use` rules consume a single capability from one source (parent|framework).
            fn translate_use(
                use_in: &Vec<Use>,
                all_capability_names: &HashSet<Name>,
                all_children: &HashSet<&Name>,
            ) -> Result<Vec<$namespace::UseDecl>, Error> {
                let mut out_uses = vec![];
                for use_ in use_in {
                    if let Some(n) = &use_.service {
                        let source = extract_use_source(use_, all_capability_names, all_children)?;
                        let target_paths = all_target_use_paths(use_, use_)
                            .ok_or_else(|| Error::internal("no capability"))?;
                        let source_names = n.to_vec();
                        for (source_name, target_path) in
                            source_names.into_iter().zip(target_paths.into_iter())
                        {
                            out_uses.push($namespace::UseDecl::Service(
                                $namespace::UseServiceDecl {
                                    source: Some(clone_ref(&source)?),
                                    source_name: Some(source_name.clone().into()),
                                    target_path: Some(target_path.into()),
                                    dependency_type: Some(
                                        use_.dependency
                                            .clone()
                                            .unwrap_or(cm::DependencyType::Strong)
                                            .into(),
                                    ),
                                    ..$namespace::UseServiceDecl::EMPTY
                                },
                            ));
                        }
                    } else if let Some(n) = &use_.protocol {
                        let source = extract_use_source(use_, all_capability_names, all_children)?;
                        let target_paths = all_target_use_paths(use_, use_)
                            .ok_or_else(|| Error::internal("no capability"))?;
                        let source_names = n.to_vec();
                        for (source_name, target_path) in
                            source_names.into_iter().zip(target_paths.into_iter())
                        {
                            out_uses.push($namespace::UseDecl::Protocol(
                                $namespace::UseProtocolDecl {
                                    source: Some(clone_ref(&source)?),
                                    source_name: Some(source_name.clone().into()),
                                    target_path: Some(target_path.into()),
                                    dependency_type: Some(
                                        use_.dependency
                                            .clone()
                                            .unwrap_or(cm::DependencyType::Strong)
                                            .into(),
                                    ),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                },
                            ));
                        }
                    } else if let Some(n) = &use_.directory {
                        let source = extract_use_source(use_, all_capability_names, all_children)?;
                        let target_path = one_target_use_path(use_, use_)?;
                        let rights = extract_required_rights(use_, "use")?;
                        let subdir = extract_use_subdir(use_);
                        out_uses.push($namespace::UseDecl::Directory(
                            $namespace::UseDirectoryDecl {
                                source: Some(source),
                                source_name: Some(n.clone().into()),
                                target_path: Some(target_path.into()),
                                rights: Some(rights),
                                subdir: subdir.map(|s| s.into()),
                                dependency_type: Some(
                                    use_.dependency
                                        .clone()
                                        .unwrap_or(cm::DependencyType::Strong)
                                        .into(),
                                ),
                                ..$namespace::UseDirectoryDecl::EMPTY
                            },
                        ));
                    } else if let Some(n) = &use_.storage {
                        let target_path = one_target_use_path(use_, use_)?;
                        out_uses.push($namespace::UseDecl::Storage($namespace::UseStorageDecl {
                            source_name: Some(n.clone().into()),
                            target_path: Some(target_path.into()),
                            ..$namespace::UseStorageDecl::EMPTY
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
                            out_uses.push($namespace::UseDecl::Event($namespace::UseEventDecl {
                                source: Some(clone_ref(&source)?),
                                source_name: Some(source_name.into()),
                                target_name: Some(target_name.into()),
                                mode: match use_.event_modes() {
                                    Some(modes) => {
                                        if modes.0.contains(&EventMode::Sync) {
                                            Some($namespace::EventMode::Sync)
                                        } else {
                                            Some($namespace::EventMode::Async)
                                        }
                                    }
                                    None => Some($namespace::EventMode::Async),
                                },
                                // We have already validated that none will be present if we were using many
                                // events.
                                filter: match use_.filter.clone() {
                                    Some(dict) => Some(dictionary_from_map(dict)?),
                                    None => None,
                                },
                                dependency_type: Some(
                                    use_.dependency
                                        .clone()
                                        .unwrap_or(cm::DependencyType::Strong)
                                        .into(),
                                ),
                                ..$namespace::UseEventDecl::EMPTY
                            }));
                        }
                    } else if let Some(name) = &use_.event_stream {
                        let opt_subscriptions = use_.event_subscriptions();
                        out_uses.push($namespace::UseDecl::EventStream(
                            $namespace::UseEventStreamDecl {
                                name: Some(name.to_string()),
                                subscriptions: opt_subscriptions.map(|subscriptions| {
                                    subscriptions
                                        .iter()
                                        .flat_map(|subscription| {
                                            let mode = subscription.mode.as_ref();
                                            subscription.event.iter().map(move |event| {
                                                $namespace::EventSubscription {
                                                    event_name: Some(event.to_string()),
                                                    mode: Some(match mode {
                                                        Some(EventMode::Sync) => {
                                                            $namespace::EventMode::Sync
                                                        }
                                                        _ => $namespace::EventMode::Async,
                                                    }),
                                                    ..$namespace::EventSubscription::EMPTY
                                                }
                                            })
                                        })
                                        .collect()
                                }),
                                ..$namespace::UseEventStreamDecl::EMPTY
                            },
                        ));
                    } else {
                        return Err(Error::internal(format!("no capability in use declaration")));
                    };
                }
                Ok(out_uses)
            }

            /// `expose` rules route a single capability from one or more sources (self|framework|#<child>) to
            /// one or more targets (parent|framework).
            fn translate_expose(
                expose_in: &Vec<Expose>,
                all_capability_names: &HashSet<Name>,
                all_collections: &HashSet<&Name>,
            ) -> Result<Vec<$namespace::ExposeDecl>, Error> {
                let mut out_exposes = vec![];
                for expose in expose_in.iter() {
                    let target = extract_expose_target(expose)?;
                    if let Some(source_names) = expose.service() {
                        let sources = extract_all_expose_sources(expose, Some(all_collections))?;
                        let target_names = all_target_capability_names(expose, expose)
                            .ok_or_else(|| Error::internal("no capability"))?;
                        for (source_name, target_name) in
                            source_names.into_iter().zip(target_names.into_iter())
                        {
                            for source in &sources {
                                out_exposes.push($namespace::ExposeDecl::Service(
                                    $namespace::ExposeServiceDecl {
                                        source: Some(clone_ref(source)?),
                                        source_name: Some(source_name.clone().into()),
                                        target_name: Some(target_name.clone().into()),
                                        target: Some(clone_ref(&target)?),
                                        ..$namespace::ExposeServiceDecl::EMPTY
                                    },
                                ))
                            }
                        }
                    } else if let Some(n) = expose.protocol() {
                        let source =
                            extract_single_expose_source(expose, Some(all_capability_names))?;
                        let source_names = n.to_vec();
                        let target_names = all_target_capability_names(expose, expose)
                            .ok_or_else(|| Error::internal("no capability"))?;
                        for (source_name, target_name) in
                            source_names.into_iter().zip(target_names.into_iter())
                        {
                            out_exposes.push($namespace::ExposeDecl::Protocol(
                                $namespace::ExposeProtocolDecl {
                                    source: Some(clone_ref(&source)?),
                                    source_name: Some(source_name.clone().into()),
                                    target_name: Some(target_name.into()),
                                    target: Some(clone_ref(&target)?),
                                    ..$namespace::ExposeProtocolDecl::EMPTY
                                },
                            ))
                        }
                    } else if let Some(n) = expose.directory() {
                        let source = extract_single_expose_source(expose, None)?;
                        let source_names = n.to_vec();
                        let target_names = all_target_capability_names(expose, expose)
                            .ok_or_else(|| Error::internal("no capability"))?;
                        let rights = extract_expose_rights(expose)?;
                        let subdir = extract_expose_subdir(expose);
                        for (source_name, target_name) in
                            source_names.into_iter().zip(target_names.into_iter())
                        {
                            out_exposes.push($namespace::ExposeDecl::Directory(
                                $namespace::ExposeDirectoryDecl {
                                    source: Some(clone_ref(&source)?),
                                    source_name: Some(source_name.clone().into()),
                                    target_name: Some(target_name.into()),
                                    target: Some(clone_ref(&target)?),
                                    rights,
                                    subdir: subdir.as_ref().map(|s| s.clone().into()),
                                    ..$namespace::ExposeDirectoryDecl::EMPTY
                                },
                            ))
                        }
                    } else if let Some(n) = expose.runner() {
                        let source = extract_single_expose_source(expose, None)?;
                        let source_names = n.to_vec();
                        let target_names = all_target_capability_names(expose, expose)
                            .ok_or_else(|| Error::internal("no capability"))?;
                        for (source_name, target_name) in
                            source_names.into_iter().zip(target_names.into_iter())
                        {
                            out_exposes.push($namespace::ExposeDecl::Runner(
                                $namespace::ExposeRunnerDecl {
                                    source: Some(clone_ref(&source)?),
                                    source_name: Some(source_name.clone().into()),
                                    target: Some(clone_ref(&target)?),
                                    target_name: Some(target_name.into()),
                                    ..$namespace::ExposeRunnerDecl::EMPTY
                                },
                            ))
                        }
                    } else if let Some(n) = expose.resolver() {
                        let source = extract_single_expose_source(expose, None)?;
                        let source_names = n.to_vec();
                        let target_names = all_target_capability_names(expose, expose)
                            .ok_or_else(|| Error::internal("no capability"))?;
                        for (source_name, target_name) in
                            source_names.into_iter().zip(target_names.into_iter())
                        {
                            out_exposes.push($namespace::ExposeDecl::Resolver(
                                $namespace::ExposeResolverDecl {
                                    source: Some(clone_ref(&source)?),
                                    source_name: Some(source_name.clone().into()),
                                    target: Some(clone_ref(&target)?),
                                    target_name: Some(target_name.into()),
                                    ..$namespace::ExposeResolverDecl::EMPTY
                                },
                            ))
                        }
                    } else {
                        return Err(Error::internal(format!(
                            "expose: must specify a known capability"
                        )));
                    }
                }
                Ok(out_exposes)
            }

            /// `offer` rules route multiple capabilities from multiple sources to multiple targets.
            fn translate_offer(
                offer_in: &Vec<Offer>,
                all_capability_names: &HashSet<Name>,
                all_children: &HashSet<&Name>,
                all_collections: &HashSet<&Name>,
            ) -> Result<Vec<$namespace::OfferDecl>, Error> {
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
                            out_offers.push($namespace::OfferDecl::Service(
                                $namespace::OfferServiceDecl {
                                    source: Some(source),
                                    source_name: Some(source_name.into()),
                                    target: Some(target),
                                    target_name: Some(target_name.into()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                },
                            ));
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
                            out_offers.push($namespace::OfferDecl::Protocol(
                                $namespace::OfferProtocolDecl {
                                    source: Some(source),
                                    source_name: Some(source_name.into()),
                                    target: Some(target),
                                    target_name: Some(target_name.into()),
                                    dependency_type: Some(
                                        offer
                                            .dependency
                                            .clone()
                                            .unwrap_or(cm::DependencyType::Strong)
                                            .into(),
                                    ),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                },
                            ));
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
                            out_offers.push($namespace::OfferDecl::Directory(
                                $namespace::OfferDirectoryDecl {
                                    source: Some(source),
                                    source_name: Some(source_name.into()),
                                    target: Some(target),
                                    target_name: Some(target_name.into()),
                                    rights: extract_offer_rights(offer)?,
                                    subdir: extract_offer_subdir(offer).map(|s| s.into()),
                                    dependency_type: Some(
                                        offer
                                            .dependency
                                            .clone()
                                            .unwrap_or(cm::DependencyType::Strong)
                                            .into(),
                                    ),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                },
                            ));
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
                            out_offers.push($namespace::OfferDecl::Storage(
                                $namespace::OfferStorageDecl {
                                    source: Some(source),
                                    source_name: Some(source_name.into()),
                                    target: Some(target),
                                    target_name: Some(target_name.into()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                },
                            ));
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
                            out_offers.push($namespace::OfferDecl::Runner(
                                $namespace::OfferRunnerDecl {
                                    source: Some(source),
                                    source_name: Some(source_name.into()),
                                    target: Some(target),
                                    target_name: Some(target_name.into()),
                                    ..$namespace::OfferRunnerDecl::EMPTY
                                },
                            ));
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
                            out_offers.push($namespace::OfferDecl::Resolver(
                                $namespace::OfferResolverDecl {
                                    source: Some(source),
                                    source_name: Some(source_name.into()),
                                    target: Some(target),
                                    target_name: Some(target_name.into()),
                                    ..$namespace::OfferResolverDecl::EMPTY
                                },
                            ));
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
                            out_offers.push($namespace::OfferDecl::Event(
                                $namespace::OfferEventDecl {
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
                                            if modes.0.contains(&EventMode::Sync) {
                                                Some($namespace::EventMode::Sync)
                                            } else {
                                                Some($namespace::EventMode::Async)
                                            }
                                        }
                                        None => Some($namespace::EventMode::Async),
                                    },
                                    ..$namespace::OfferEventDecl::EMPTY
                                },
                            ));
                        }
                    } else {
                        return Err(Error::internal(format!("no capability")));
                    }
                }
                Ok(out_offers)
            }

            fn translate_children(
                children_in: &Vec<Child>,
            ) -> Result<Vec<$namespace::ChildDecl>, Error> {
                let mut out_children = vec![];
                for child in children_in.iter() {
                    out_children.push($namespace::ChildDecl {
                        name: Some(child.name.clone().into()),
                        url: Some(child.url.clone().into()),
                        startup: Some(child.startup.clone().into()),
                        environment: extract_environment_ref(child.environment.as_ref())
                            .map(|e| e.into()),
                        on_terminate: child.on_terminate.as_ref().map(|r| r.clone().into()),
                        ..$namespace::ChildDecl::EMPTY
                    });
                }
                Ok(out_children)
            }

            fn translate_collections(
                collections_in: &Vec<Collection>,
            ) -> Result<Vec<$namespace::CollectionDecl>, Error> {
                let mut out_collections = vec![];
                for collection in collections_in.iter() {
                    out_collections.push($namespace::CollectionDecl {
                        name: Some(collection.name.clone().into()),
                        durability: Some(collection.durability.clone().into()),
                        allowed_offers: collection.allowed_offers.clone().map(|a| a.into()),
                        environment: extract_environment_ref(collection.environment.as_ref())
                            .map(|e| e.into()),
                        ..$namespace::CollectionDecl::EMPTY
                    });
                }
                Ok(out_collections)
            }

            /// Translates a vector element type string to a [`fuchsia.sys2.ConfigVectorElementType`]
            fn translate_vector_element_type(
                element_type: &ConfigVectorElementType,
            ) -> $namespace::ConfigVectorElementType {
                match element_type {
                    ConfigVectorElementType::Bool => $namespace::ConfigVectorElementType::Bool(
                        $namespace::ConfigBooleanType::EMPTY,
                    ),
                    ConfigVectorElementType::Uint8 => $namespace::ConfigVectorElementType::Uint8(
                        $namespace::ConfigUnsigned8Type::EMPTY,
                    ),
                    ConfigVectorElementType::Int8 => $namespace::ConfigVectorElementType::Int8(
                        $namespace::ConfigSigned8Type::EMPTY,
                    ),
                    ConfigVectorElementType::Uint16 => $namespace::ConfigVectorElementType::Uint16(
                        $namespace::ConfigUnsigned16Type::EMPTY,
                    ),
                    ConfigVectorElementType::Int16 => $namespace::ConfigVectorElementType::Int16(
                        $namespace::ConfigSigned16Type::EMPTY,
                    ),
                    ConfigVectorElementType::Uint32 => $namespace::ConfigVectorElementType::Uint32(
                        $namespace::ConfigUnsigned32Type::EMPTY,
                    ),
                    ConfigVectorElementType::Int32 => $namespace::ConfigVectorElementType::Int32(
                        $namespace::ConfigSigned32Type::EMPTY,
                    ),
                    ConfigVectorElementType::Uint64 => $namespace::ConfigVectorElementType::Uint64(
                        $namespace::ConfigUnsigned64Type::EMPTY,
                    ),
                    ConfigVectorElementType::Int64 => $namespace::ConfigVectorElementType::Int64(
                        $namespace::ConfigSigned64Type::EMPTY,
                    ),
                    ConfigVectorElementType::String { max_size } => {
                        $namespace::ConfigVectorElementType::String($namespace::ConfigStringType {
                            max_size: Some(max_size.get()),
                            ..$namespace::ConfigStringType::EMPTY
                        })
                    }
                }
            }

            /// Translates a value type string to a [`fuchsia.sys2.ConfigValueType`]
            fn translate_value_type(value_type: &ConfigValueType) -> $namespace::ConfigValueType {
                match value_type {
                    ConfigValueType::Bool => {
                        $namespace::ConfigValueType::Bool($namespace::ConfigBooleanType::EMPTY)
                    }
                    ConfigValueType::Uint8 => {
                        $namespace::ConfigValueType::Uint8($namespace::ConfigUnsigned8Type::EMPTY)
                    }
                    ConfigValueType::Int8 => {
                        $namespace::ConfigValueType::Int8($namespace::ConfigSigned8Type::EMPTY)
                    }
                    ConfigValueType::Uint16 => {
                        $namespace::ConfigValueType::Uint16($namespace::ConfigUnsigned16Type::EMPTY)
                    }
                    ConfigValueType::Int16 => {
                        $namespace::ConfigValueType::Int16($namespace::ConfigSigned16Type::EMPTY)
                    }
                    ConfigValueType::Uint32 => {
                        $namespace::ConfigValueType::Uint32($namespace::ConfigUnsigned32Type::EMPTY)
                    }
                    ConfigValueType::Int32 => {
                        $namespace::ConfigValueType::Int32($namespace::ConfigSigned32Type::EMPTY)
                    }
                    ConfigValueType::Uint64 => {
                        $namespace::ConfigValueType::Uint64($namespace::ConfigUnsigned64Type::EMPTY)
                    }
                    ConfigValueType::Int64 => {
                        $namespace::ConfigValueType::Int64($namespace::ConfigSigned64Type::EMPTY)
                    }
                    ConfigValueType::String { max_size } => {
                        $namespace::ConfigValueType::String($namespace::ConfigStringType {
                            max_size: Some(max_size.get()),
                            ..$namespace::ConfigStringType::EMPTY
                        })
                    }
                    ConfigValueType::Vector { max_count, element } => {
                        let element_type = translate_vector_element_type(element);
                        $namespace::ConfigValueType::Vector($namespace::ConfigVectorType {
                            max_count: Some(max_count.get()),
                            element_type: Some(element_type),
                            ..$namespace::ConfigVectorType::EMPTY
                        })
                    }
                }
            }

            /// Translates a map of [`String`] -> [`ConfigValueType`] to a [`fuchsia.sys2.ConfigDecl`]
            fn translate_config(
                fields: &BTreeMap<ConfigKey, ConfigValueType>,
            ) -> $namespace::ConfigDecl {
                let mut fidl_fields = vec![];

                // Compute a SHA-256 hash from each field
                let mut hasher = Sha256::new();

                for (key, value) in fields {
                    fidl_fields.push($namespace::ConfigField {
                        key: Some(key.to_string()),
                        value_type: Some(translate_value_type(value)),
                        ..$namespace::ConfigField::EMPTY
                    });

                    hasher.update(key.as_str());

                    // TODO(fxbug.dev/88499): Compute checksum using Hash trait. Currently,
                    // the Debug trait does not provide a strong uniqueness guarantee.
                    hasher.update(format!("{:?}", value));
                }

                // The SHA-256 hash must be 32 bytes in size
                let hash: Vec<u8> = hasher.finalize().to_vec();
                assert_eq!(hash.len(), 32);

                $namespace::ConfigDecl {
                    fields: Some(fidl_fields),
                    declaration_checksum: Some(hash),
                    ..$namespace::ConfigDecl::EMPTY
                }
            }

            fn translate_environments(
                envs_in: &Vec<Environment>,
                all_capability_names: &HashSet<Name>,
            ) -> Result<Vec<$namespace::EnvironmentDecl>, Error> {
                envs_in
                    .iter()
                    .map(|env| {
                        Ok($namespace::EnvironmentDecl {
                            name: Some(env.name.clone().into()),
                            extends: match env.extends {
                                Some(EnvironmentExtends::Realm) => {
                                    Some($namespace::EnvironmentExtends::Realm)
                                }
                                Some(EnvironmentExtends::None) => {
                                    Some($namespace::EnvironmentExtends::None)
                                }
                                None => Some($namespace::EnvironmentExtends::None),
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
                                    translate_debug_capabilities(
                                        debug_capabiltities,
                                        all_capability_names,
                                    )
                                })
                                .transpose()?,
                            stop_timeout_ms: env.stop_timeout_ms.map(|s| s.0),
                            ..$namespace::EnvironmentDecl::EMPTY
                        })
                    })
                    .collect()
            }

            fn translate_runner_registration(
                reg: &RunnerRegistration,
            ) -> Result<$namespace::RunnerRegistration, Error> {
                Ok($namespace::RunnerRegistration {
                    source_name: Some(reg.runner.clone().into()),
                    source: Some(extract_single_offer_source(reg, None)?),
                    target_name: Some(reg.r#as.as_ref().unwrap_or(&reg.runner).clone().into()),
                    ..$namespace::RunnerRegistration::EMPTY
                })
            }

            fn translate_resolver_registration(
                reg: &ResolverRegistration,
            ) -> Result<$namespace::ResolverRegistration, Error> {
                Ok($namespace::ResolverRegistration {
                    resolver: Some(reg.resolver.clone().into()),
                    source: Some(extract_single_offer_source(reg, None)?),
                    scheme: Some(
                        reg.scheme
                            .as_str()
                            .parse::<cm_types::UrlScheme>()
                            .map_err(|e| Error::internal(format!("invalid URL scheme: {}", e)))?
                            .into(),
                    ),
                    ..$namespace::ResolverRegistration::EMPTY
                })
            }

            fn translate_debug_capabilities(
                capabilities: &Vec<DebugRegistration>,
                all_capability_names: &HashSet<Name>,
            ) -> Result<Vec<$namespace::DebugRegistration>, Error> {
                let mut out_capabilities = vec![];
                for capability in capabilities {
                    if let Some(n) = capability.protocol() {
                        let source =
                            extract_single_offer_source(capability, Some(all_capability_names))?;
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
                            out_capabilities.push($namespace::DebugRegistration::Protocol(
                                $namespace::DebugProtocolRegistration {
                                    source: Some(clone_ref(&source)?),
                                    source_name: Some(source_name.into()),
                                    target_name: Some(target_name.into()),
                                    ..$namespace::DebugProtocolRegistration::EMPTY
                                },
                            ));
                        }
                    }
                }
                Ok(out_capabilities)
            }

            fn extract_use_source(
                in_obj: &Use,
                all_capability_names: &HashSet<Name>,
                all_children_names: &HashSet<&Name>,
            ) -> Result<$namespace::Ref, Error> {
                match in_obj.from.as_ref() {
                    Some(UseFromRef::Parent) => {
                        Ok($namespace::Ref::Parent($namespace::ParentRef {}))
                    }
                    Some(UseFromRef::Framework) => {
                        Ok($namespace::Ref::Framework($namespace::FrameworkRef {}))
                    }
                    Some(UseFromRef::Debug) => Ok($namespace::Ref::Debug($namespace::DebugRef {})),
                    Some(UseFromRef::Self_) => Ok($namespace::Ref::Self_($namespace::SelfRef {})),
                    Some(UseFromRef::Named(name)) => {
                        if all_capability_names.contains(&name) {
                            Ok($namespace::Ref::Capability($namespace::CapabilityRef {
                                name: name.clone().into(),
                            }))
                        } else if all_children_names.contains(&name) {
                            Ok($namespace::Ref::Child($namespace::ChildRef {
                                name: name.clone().into(),
                                collection: None,
                            }))
                        } else {
                            Err(Error::internal(format!(
                                "use source \"{:?}\" not supported for \"use from\"",
                                name
                            )))
                        }
                    }
                    None => Ok($namespace::Ref::Parent($namespace::ParentRef {})), // Default value.
                }
            }

            // Since $namespace::Ref is not cloneable, write our own clone function.
            fn clone_ref(ref_: &$namespace::Ref) -> Result<$namespace::Ref, Error> {
                match ref_ {
                    $namespace::Ref::Parent(parent_ref) => {
                        Ok($namespace::Ref::Parent(parent_ref.clone()))
                    }
                    $namespace::Ref::Self_(self_ref) => {
                        Ok($namespace::Ref::Self_(self_ref.clone()))
                    }
                    $namespace::Ref::Child(child_ref) => {
                        Ok($namespace::Ref::Child(child_ref.clone()))
                    }
                    $namespace::Ref::Collection(collection_ref) => {
                        Ok($namespace::Ref::Collection(collection_ref.clone()))
                    }
                    $namespace::Ref::Framework(framework_ref) => {
                        Ok($namespace::Ref::Framework(framework_ref.clone()))
                    }
                    $namespace::Ref::Capability(capability_ref) => {
                        Ok($namespace::Ref::Capability(capability_ref.clone()))
                    }
                    $namespace::Ref::Debug(debug_ref) => {
                        Ok($namespace::Ref::Debug(debug_ref.clone()))
                    }
                    _ => Err(Error::internal("Unknown $namespace::Ref found.")),
                }
            }

            fn extract_use_event_source(in_obj: &Use) -> Result<$namespace::Ref, Error> {
                match in_obj.from.as_ref() {
                    Some(UseFromRef::Parent) => {
                        Ok($namespace::Ref::Parent($namespace::ParentRef {}))
                    }
                    Some(UseFromRef::Framework) => {
                        Ok($namespace::Ref::Framework($namespace::FrameworkRef {}))
                    }
                    Some(UseFromRef::Named(name)) => {
                        Ok($namespace::Ref::Capability($namespace::CapabilityRef {
                            name: name.clone().into(),
                        }))
                    }
                    Some(UseFromRef::Debug) => {
                        Err(Error::internal(format!("Debug source provided for \"use event\"")))
                    }
                    Some(UseFromRef::Self_) => {
                        Err(Error::internal(format!("Self source not supported for \"use event\"")))
                    }
                    None => {
                        Err(Error::internal(format!("No source \"from\" provided for \"use\"")))
                    }
                }
            }

            fn extract_use_subdir(in_obj: &Use) -> Option<cm::RelativePath> {
                in_obj.subdir.clone()
            }

            fn extract_expose_subdir(in_obj: &Expose) -> Option<cm::RelativePath> {
                in_obj.subdir.clone()
            }

            fn extract_offer_subdir(in_obj: &Offer) -> Option<cm::RelativePath> {
                in_obj.subdir.clone()
            }

            fn extract_expose_rights(in_obj: &Expose) -> Result<Option<fio2::Operations>, Error> {
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
                reference: &ExposeFromRef,
                all_capability_names: Option<&HashSet<Name>>,
                all_collections: Option<&HashSet<&Name>>,
            ) -> Result<$namespace::Ref, Error> {
                match reference {
                    ExposeFromRef::Named(name) => {
                        if all_capability_names.is_some()
                            && all_capability_names.unwrap().contains(&name)
                        {
                            Ok($namespace::Ref::Capability($namespace::CapabilityRef {
                                name: name.clone().into(),
                            }))
                        } else if all_collections.is_some()
                            && all_collections.unwrap().contains(&name)
                        {
                            Ok($namespace::Ref::Collection($namespace::CollectionRef {
                                name: name.clone().into(),
                            }))
                        } else {
                            Ok($namespace::Ref::Child($namespace::ChildRef {
                                name: name.clone().into(),
                                collection: None,
                            }))
                        }
                    }
                    ExposeFromRef::Framework => {
                        Ok($namespace::Ref::Framework($namespace::FrameworkRef {}))
                    }
                    ExposeFromRef::Self_ => Ok($namespace::Ref::Self_($namespace::SelfRef {})),
                }
            }

            fn extract_single_expose_source(
                in_obj: &Expose,
                all_capability_names: Option<&HashSet<Name>>,
            ) -> Result<$namespace::Ref, Error> {
                match &in_obj.from {
                    OneOrMany::One(reference) => {
                        expose_source_from_ref(&reference, all_capability_names, None)
                    }
                    OneOrMany::Many(many) => {
                        return Err(Error::internal(format!(
                            "multiple unexpected \"from\" clauses for \"expose\": {:?}",
                            many
                        )))
                    }
                }
            }

            fn extract_all_expose_sources(
                in_obj: &Expose,
                all_collections: Option<&HashSet<&Name>>,
            ) -> Result<Vec<$namespace::Ref>, Error> {
                in_obj
                    .from
                    .to_vec()
                    .into_iter()
                    .map(|e| expose_source_from_ref(e, None, all_collections))
                    .collect()
            }

            fn extract_offer_rights(in_obj: &Offer) -> Result<Option<fio2::Operations>, Error> {
                match in_obj.rights.as_ref() {
                    Some(rights_tokens) => {
                        let mut rights = Vec::new();
                        for token in rights_tokens.0.iter() {
                            rights.append(&mut token.expand())
                        }
                        if rights.is_empty() {
                            return Err(Error::missing_rights(
                                "Rights provided to offer are not well formed.",
                            ));
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
                all_capability_names: Option<&HashSet<Name>>,
            ) -> Result<$namespace::Ref, Error>
            where
                T: FromClause,
            {
                match in_obj.from_() {
                    OneOrMany::One(reference) => {
                        offer_source_from_ref(reference, all_capability_names, None)
                    }
                    many => {
                        return Err(Error::internal(format!(
                            "multiple unexpected \"from\" clauses for \"offer\": {}",
                            many
                        )))
                    }
                }
            }

            fn extract_all_offer_sources<T: FromClause>(
                in_obj: &T,
                all_capability_names: &HashSet<Name>,
                all_collections: &HashSet<&Name>,
            ) -> Result<Vec<$namespace::Ref>, Error> {
                in_obj
                    .from_()
                    .to_vec()
                    .into_iter()
                    .map(|r| {
                        offer_source_from_ref(
                            r.clone(),
                            Some(all_capability_names),
                            Some(all_collections),
                        )
                    })
                    .collect()
            }

            fn translate_child_or_collection_ref(
                reference: AnyRef,
                all_children: &HashSet<&Name>,
                all_collections: &HashSet<&Name>,
            ) -> Result<$namespace::Ref, Error> {
                match reference {
                    AnyRef::Named(name) if all_children.contains(name) => {
                        Ok($namespace::Ref::Child($namespace::ChildRef {
                            name: name.clone().into(),
                            collection: None,
                        }))
                    }
                    AnyRef::Named(name) if all_collections.contains(name) => {
                        Ok($namespace::Ref::Collection($namespace::CollectionRef {
                            name: name.clone().into(),
                        }))
                    }
                    AnyRef::Named(_) => {
                        Err(Error::internal(format!("dangling reference: \"{}\"", reference)))
                    }
                    _ => {
                        Err(Error::internal(format!("invalid child reference: \"{}\"", reference)))
                    }
                }
            }

            // Return a list of (source, source capability id, target, target capability id) expressed in the
            // `offer`.
            fn extract_offer_sources_and_targets(
                offer: &Offer,
                source_names: OneOrMany<Name>,
                all_capability_names: &HashSet<Name>,
                all_children: &HashSet<&Name>,
                all_collections: &HashSet<&Name>,
            ) -> Result<Vec<($namespace::Ref, Name, $namespace::Ref, Name)>, Error> {
                let mut out = vec![];

                let source_names = source_names.to_vec();
                let sources =
                    extract_all_offer_sources(offer, all_capability_names, all_collections)?;
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
                            let target = translate_child_or_collection_ref(
                                to.into(),
                                all_children,
                                all_collections,
                            )?;
                            out.push((source.clone(), source_name, target, target_name.clone()))
                        }
                    }
                }
                Ok(out)
            }

            /// Return the target paths specified in the given use declaration.
            fn all_target_use_paths<T, U>(in_obj: &T, to_obj: &U) -> Option<OneOrMany<Path>>
            where
                T: CapabilityClause,
                U: AsClause + PathClause,
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
            fn svc_paths_from_names<T>(names: OneOrMany<Name>, to_obj: &T) -> OneOrMany<Path>
            where
                T: PathClause,
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
                        let many =
                            v.iter().map(|n| format!("/svc/{}", n).parse().unwrap()).collect();
                        OneOrMany::Many(many)
                    }
                }
            }

            /// Return the single target path specified in the given use declaration.
            fn one_target_use_path<T, U>(in_obj: &T, to_obj: &U) -> Result<Path, Error>
            where
                T: CapabilityClause,
                U: AsClause + PathClause,
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
            fn all_target_capability_names<T, U>(in_obj: &T, to_obj: &U) -> Option<OneOrMany<Name>>
            where
                T: CapabilityClause,
                U: AsClause + PathClause,
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

            fn extract_expose_target(in_obj: &Expose) -> Result<$namespace::Ref, Error> {
                match &in_obj.to {
                    Some(ExposeToRef::Parent) => {
                        Ok($namespace::Ref::Parent($namespace::ParentRef {}))
                    }
                    Some(ExposeToRef::Framework) => {
                        Ok($namespace::Ref::Framework($namespace::FrameworkRef {}))
                    }
                    None => Ok($namespace::Ref::Parent($namespace::ParentRef {})),
                }
            }

            fn extract_environment_ref(r: Option<&EnvironmentRef>) -> Option<cm::Name> {
                r.map(|r| {
                    let EnvironmentRef::Named(name) = r;
                    name.clone()
                })
            }

            pub fn translate_capabilities(
                capabilities_in: &Vec<Capability>,
                as_builtin: bool,
            ) -> Result<Vec<$namespace::CapabilityDecl>, Error> {
                let mut out_capabilities = vec![];
                for capability in capabilities_in {
                    if let Some(service) = &capability.service {
                        for n in service.to_vec() {
                            let source_path = match as_builtin {
                                true => None,
                                false => Some(
                                    capability
                                        .path
                                        .clone()
                                        .unwrap_or_else(|| format!("/svc/{}", n).parse().unwrap())
                                        .into(),
                                ),
                            };
                            out_capabilities.push($namespace::CapabilityDecl::Service(
                                $namespace::ServiceDecl {
                                    name: Some(n.clone().into()),
                                    source_path: source_path,
                                    ..$namespace::ServiceDecl::EMPTY
                                },
                            ));
                        }
                    } else if let Some(protocol) = &capability.protocol {
                        for n in protocol.to_vec() {
                            let source_path = match as_builtin {
                                true => None,
                                false => Some(
                                    capability
                                        .path
                                        .clone()
                                        .unwrap_or_else(|| format!("/svc/{}", n).parse().unwrap())
                                        .into(),
                                ),
                            };
                            out_capabilities.push($namespace::CapabilityDecl::Protocol(
                                $namespace::ProtocolDecl {
                                    name: Some(n.clone().into()),
                                    source_path: source_path,
                                    ..$namespace::ProtocolDecl::EMPTY
                                },
                            ));
                        }
                    } else if let Some(n) = &capability.directory {
                        let source_path = match as_builtin {
                            true => None,
                            false => Some(
                                capability
                                    .path
                                    .as_ref()
                                    .expect("missing source path")
                                    .clone()
                                    .into(),
                            ),
                        };
                        let rights = extract_required_rights(capability, "capability")?;
                        out_capabilities.push($namespace::CapabilityDecl::Directory(
                            $namespace::DirectoryDecl {
                                name: Some(n.clone().into()),
                                source_path: source_path,
                                rights: Some(rights),
                                ..$namespace::DirectoryDecl::EMPTY
                            },
                        ));
                    } else if let Some(n) = &capability.storage {
                        if as_builtin {
                            return Err(Error::internal(format!(
                                "built-in storage capabilities are not supported"
                            )));
                        }
                        let backing_dir = capability
                            .backing_dir
                            .as_ref()
                            .expect("storage has no path or backing_dir")
                            .clone()
                            .into();
                        out_capabilities.push($namespace::CapabilityDecl::Storage(
                            $namespace::StorageDecl {
                                name: Some(n.clone().into()),
                                backing_dir: Some(backing_dir),
                                source: Some(offer_source_from_ref(
                                    capability.from.as_ref().unwrap().into(),
                                    None,
                                    None,
                                )?),
                                subdir: capability.subdir.clone().map(Into::into),
                                storage_id: Some(
                                    capability
                                        .storage_id
                                        .clone()
                                        .expect("storage is missing storage_id")
                                        .into(),
                                ),
                                ..$namespace::StorageDecl::EMPTY
                            },
                        ));
                    } else if let Some(n) = &capability.runner {
                        let source_path = match as_builtin {
                            true => None,
                            false => Some(
                                capability
                                    .path
                                    .as_ref()
                                    .expect("missing source path")
                                    .clone()
                                    .into(),
                            ),
                        };
                        out_capabilities.push($namespace::CapabilityDecl::Runner(
                            $namespace::RunnerDecl {
                                name: Some(n.clone().into()),
                                source_path: source_path,
                                ..$namespace::RunnerDecl::EMPTY
                            },
                        ));
                    } else if let Some(n) = &capability.resolver {
                        let source_path = match as_builtin {
                            true => None,
                            false => Some(
                                capability
                                    .path
                                    .as_ref()
                                    .expect("missing source path")
                                    .clone()
                                    .into(),
                            ),
                        };
                        out_capabilities.push($namespace::CapabilityDecl::Resolver(
                            $namespace::ResolverDecl {
                                name: Some(n.clone().into()),
                                source_path: source_path,
                                ..$namespace::ResolverDecl::EMPTY
                            },
                        ));
                    } else if let Some(n) = &capability.event {
                        if !as_builtin {
                            return Err(Error::internal(format!(
                                "event capabilities may only be declared as built-in capabilities"
                            )));
                        }
                        out_capabilities.push($namespace::CapabilityDecl::Event(
                            $namespace::EventDecl {
                                name: Some(n.clone().into()),
                                ..$namespace::EventDecl::EMPTY
                            },
                        ));
                    } else {
                        return Err(Error::internal(format!(
                            "no capability declaration recognized"
                        )));
                    }
                }
                Ok(out_capabilities)
            }

            pub fn extract_required_rights<T>(
                in_obj: &T,
                keyword: &str,
            ) -> Result<fio2::Operations, Error>
            where
                T: RightsClause,
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

            pub fn offer_source_from_ref(
                reference: AnyRef<'_>,
                all_capability_names: Option<&HashSet<Name>>,
                all_collection_names: Option<&HashSet<&Name>>,
            ) -> Result<$namespace::Ref, Error> {
                match reference {
                    AnyRef::Named(name) => {
                        if all_capability_names.is_some()
                            && all_capability_names.unwrap().contains(&name)
                        {
                            Ok($namespace::Ref::Capability($namespace::CapabilityRef {
                                name: name.clone().into(),
                            }))
                        } else if all_collection_names.is_some()
                            && all_collection_names.unwrap().contains(&name)
                        {
                            Ok($namespace::Ref::Collection($namespace::CollectionRef {
                                name: name.clone().into(),
                            }))
                        } else {
                            Ok($namespace::Ref::Child($namespace::ChildRef {
                                name: name.clone().into(),
                                collection: None,
                            }))
                        }
                    }
                    AnyRef::Framework => {
                        Ok($namespace::Ref::Framework($namespace::FrameworkRef {}))
                    }
                    AnyRef::Debug => Ok($namespace::Ref::Debug($namespace::DebugRef {})),
                    AnyRef::Parent => Ok($namespace::Ref::Parent($namespace::ParentRef {})),
                    AnyRef::Self_ => Ok($namespace::Ref::Self_($namespace::SelfRef {})),
                }
            }
        }
    };
}

compile!(fsys);
compile!(fdecl);

// Tests
macro_rules! test_compile {
    (
        $(
            $(#[$m:meta])*
            $test_name:ident => {
                namespace = $namespace:ident,
                input = $input:expr,
                output = $expected:expr,
            },
        )+
    ) => {
        $(
            $(#[$m])*
            #[test]
            fn $test_name() {
                let input = serde_json::from_str(&$input.to_string()).expect("deserialization failed");
                let actual = super::$namespace::compile(&input).expect("compilation failed");
                assert_eq!(actual, $expected);
            }
        )+
    }
}

macro_rules! compile_tests {
     ($mod:ident, $namespace:ident) => {
        #[cfg(test)]
        mod $mod {
             // `fidl_fuchsia_sys2` is unused when generating `fdecl` code;
             // `fdecl` is unused when generating `$namespace` code.
             #[allow(unused_imports)]
            use {
                crate::{
                    error::Error, AnyRef, AsClause, Capability, CapabilityClause,
                    Child, Collection, DebugRegistration, Document, Environment,
                    EnvironmentExtends, EnvironmentRef, EventMode, EventModesClause,
                    EventSubscriptionsClause, Expose, ExposeFromRef, ExposeToRef, FromClause,
                    Offer, OneOrMany, Path, PathClause, Program, ResolverRegistration,
                    RightsClause, RunnerRegistration, Use, UseFromRef,
                },
                cm_types::{self as cm, Name},
                cm_rust::convert as fdecl,
                fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
                serde_json::{json, Map, Value},
                std::collections::HashSet,
                std::convert::Into,
            };

            fn default_component_decl() -> $namespace::ComponentDecl {
                $namespace::ComponentDecl::EMPTY
            }

            test_compile! {
                test_compile_empty => {
                    namespace = $namespace,
                    input = json!({}),
                    output = default_component_decl(),
                },

                test_compile_empty_includes => {
                    namespace = $namespace,
                    input = json!({ "include": [] }),
                    output = default_component_decl(),
                },

                test_compile_program => {
                    namespace = $namespace,
                    input = json!({
                        "program": {
                            "runner": "elf",
                            "binary": "bin/app",
                        },
                    }),
                    output = $namespace::ComponentDecl {
                        program: Some($namespace::ProgramDecl {
                            runner: Some("elf".to_string()),
                            info: Some(fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: "binary".to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            }),
                            ..$namespace::ProgramDecl::EMPTY
                        }),
                        ..default_component_decl()
                    },
                },

                test_compile_program_with_nested_objects => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        program: Some($namespace::ProgramDecl {
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
                            ..$namespace::ProgramDecl::EMPTY
                        }),
                        ..default_component_decl()
                    },
                },

                test_compile_use => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        uses: Some(vec![
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("LegacyCoolFonts".to_string()),
                                    target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    source_name: Some("fuchsia.sys2.LegacyRealm".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.LegacyRealm".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Capability($namespace::CapabilityRef { name: "data-storage".to_string() })),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Debug($namespace::DebugRef {})),
                                    source_name: Some("fuchsia.sys2.DebugProto".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.DebugProto".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("fuchsia.sys2.Echo".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.Echo".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Directory (
                                $namespace::UseDirectoryDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("assets".to_string()),
                                    target_path: Some("/data/assets".to_string()),
                                    rights: Some(fio2::Operations::ReadBytes),
                                    subdir: None,
                                    ..$namespace::UseDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Directory (
                                $namespace::UseDirectoryDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("config".to_string()),
                                    target_path: Some("/data/config".to_string()),
                                    rights: Some(fio2::Operations::ReadBytes),
                                    subdir: Some("fonts".to_string()),
                                    ..$namespace::UseDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Storage (
                                $namespace::UseStorageDecl {
                                    source_name: Some("hippos".to_string()),
                                    target_path: Some("/hippos".to_string()),
                                    ..$namespace::UseStorageDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Storage (
                                $namespace::UseStorageDecl {
                                    source_name: Some("cache".to_string()),
                                    target_path: Some("/tmp".to_string()),
                                    ..$namespace::UseStorageDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Event (
                                $namespace::UseEventDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("destroyed".to_string()),
                                    target_name: Some("destroyed".to_string()),
                                    filter: None,
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::UseEventDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Event (
                                $namespace::UseEventDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    source_name: Some("started".to_string()),
                                    target_name: Some("started".to_string()),
                                    filter: None,
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::UseEventDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Event (
                                $namespace::UseEventDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    source_name: Some("stopped".to_string()),
                                    target_name: Some("stopped".to_string()),
                                    filter: None,
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::UseEventDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Event (
                                $namespace::UseEventDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
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
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::UseEventDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::EventStream($namespace::UseEventStreamDecl {
                                name: Some("foo_stream".to_string()),
                                subscriptions: Some(vec![
                                    $namespace::EventSubscription {
                                        event_name: Some("started".to_string()),
                                        mode: Some($namespace::EventMode::Async),
                                        ..$namespace::EventSubscription::EMPTY
                                    },
                                    $namespace::EventSubscription {
                                        event_name: Some("diagnostics".to_string()),
                                        mode: Some($namespace::EventMode::Async),
                                        ..$namespace::EventSubscription::EMPTY
                                    },
                                    $namespace::EventSubscription {
                                        event_name: Some("destroyed".to_string()),
                                        mode: Some($namespace::EventMode::Sync),
                                        ..$namespace::EventSubscription::EMPTY
                                    },
                                ]),
                                ..$namespace::UseEventStreamDecl::EMPTY
                            })
                        ]),
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("data-storage".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                backing_dir: Some("minfs".to_string()),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_expose => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        exposes: Some(vec![
                            $namespace::ExposeDecl::Protocol (
                                $namespace::ExposeProtocolDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.Log".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    ..$namespace::ExposeProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Protocol (
                                $namespace::ExposeProtocolDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("A".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("A".to_string()),
                                    ..$namespace::ExposeProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Protocol (
                                $namespace::ExposeProtocolDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("B".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("B".to_string()),
                                    ..$namespace::ExposeProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Protocol (
                                $namespace::ExposeProtocolDecl {
                                    source: Some($namespace::Ref::Capability($namespace::CapabilityRef {
                                        name: "data-storage".to_string(),
                                    })),
                                    source_name: Some("C".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("C".to_string()),
                                    ..$namespace::ExposeProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Directory (
                                $namespace::ExposeDirectoryDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("blob".to_string()),
                                    target: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    target_name: Some("blob".to_string()),
                                    rights: Some(
                                        fio2::Operations::Connect | fio2::Operations::Enumerate |
                                        fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                        fio2::Operations::GetAttributes
                                    ),
                                    subdir: None,
                                    ..$namespace::ExposeDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Directory (
                                $namespace::ExposeDirectoryDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("blob2".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("blob2".to_string()),
                                    rights: None,
                                    subdir: None,
                                    ..$namespace::ExposeDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Directory (
                                $namespace::ExposeDirectoryDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("blob3".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("blob3".to_string()),
                                    rights: None,
                                    subdir: None,
                                    ..$namespace::ExposeDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Directory (
                                $namespace::ExposeDirectoryDecl {
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    source_name: Some("hub".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("hub".to_string()),
                                    rights: None,
                                    subdir: None,
                                    ..$namespace::ExposeDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Runner (
                                $namespace::ExposeRunnerDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("web".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("web-rename".to_string()),
                                    ..$namespace::ExposeRunnerDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Runner (
                                $namespace::ExposeRunnerDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("runner_a".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("runner_a".to_string()),
                                    ..$namespace::ExposeRunnerDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Runner (
                                $namespace::ExposeRunnerDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("runner_b".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("runner_b".to_string()),
                                    ..$namespace::ExposeRunnerDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Resolver (
                                $namespace::ExposeResolverDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("my_resolver".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("pkg_resolver".to_string()),
                                    ..$namespace::ExposeResolverDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Resolver (
                                $namespace::ExposeResolverDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("resolver_a".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("resolver_a".to_string()),
                                    ..$namespace::ExposeResolverDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Resolver (
                                $namespace::ExposeResolverDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("resolver_b".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("resolver_b".to_string()),
                                    ..$namespace::ExposeResolverDecl::EMPTY
                                }
                            ),
                        ]),
                        offers: None,
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Protocol (
                                $namespace::ProtocolDecl {
                                    name: Some("A".to_string()),
                                    source_path: Some("/svc/A".to_string()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Protocol (
                                $namespace::ProtocolDecl {
                                    name: Some("B".to_string()),
                                    source_path: Some("/svc/B".to_string()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Directory (
                                $namespace::DirectoryDecl {
                                    name: Some("blob".to_string()),
                                    source_path: Some("/volumes/blobfs/blob".to_string()),
                                    rights: Some(fio2::Operations::Connect | fio2::Operations::Enumerate |
                                        fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                        fio2::Operations::GetAttributes
                                    ),
                                    ..$namespace::DirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Runner (
                                $namespace::RunnerDecl {
                                    name: Some("web".to_string()),
                                    source_path: Some("/svc/fuchsia.component.ComponentRunner".to_string()),
                                    ..$namespace::RunnerDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("data-storage".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                backing_dir: Some("minfs".to_string()),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                        ]),
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("logger".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            }
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_offer => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        offers: Some(vec![
                            $namespace::OfferDecl::Protocol (
                                $namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Weak),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Protocol (
                                $namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("fuchsia.logger.LegacySysLog".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Protocol (
                                $namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("fuchsia.setui.SetUiService".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("fuchsia.setui.SetUiService".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Protocol (
                                $namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("fuchsia.test.service.Name".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("fuchsia.test.service.Name".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Protocol (
                                $namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Capability($namespace::CapabilityRef {
                                        name: "data".to_string(),
                                    })),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Directory (
                                $namespace::OfferDirectoryDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("assets".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("assets".to_string()),
                                    rights: None,
                                    subdir: None,
                                    dependency_type: Some($namespace::DependencyType::WeakForMigration),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Directory (
                                $namespace::OfferDirectoryDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("assets2".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("assets2".to_string()),
                                    rights: None,
                                    subdir: None,
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Directory (
                                $namespace::OfferDirectoryDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("assets3".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("assets3".to_string()),
                                    rights: None,
                                    subdir: None,
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Directory (
                                $namespace::OfferDirectoryDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("assets2".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("assets2".to_string()),
                                    rights: None,
                                    subdir: None,
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Directory (
                                $namespace::OfferDirectoryDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("assets3".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("assets3".to_string()),
                                    rights: None,
                                    subdir: None,
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Directory (
                                $namespace::OfferDirectoryDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("data".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("assets".to_string()),
                                    rights: None,
                                    subdir: Some("index/file".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Directory (
                                $namespace::OfferDirectoryDecl {
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    source_name: Some("hub".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("hub".to_string()),
                                    rights: None,
                                    subdir: None,
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferDirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Storage (
                                $namespace::OfferStorageDecl {
                                    source_name: Some("data".to_string()),
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("data".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Storage (
                                $namespace::OfferStorageDecl {
                                    source_name: Some("data".to_string()),
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("data".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Storage (
                                $namespace::OfferStorageDecl {
                                    source_name: Some("storage_a".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("storage_a".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Storage (
                                $namespace::OfferStorageDecl {
                                    source_name: Some("storage_b".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("storage_b".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Runner (
                                $namespace::OfferRunnerDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("elf".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("elf-renamed".to_string()),
                                    ..$namespace::OfferRunnerDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Runner (
                                $namespace::OfferRunnerDecl {
                                    source_name: Some("runner_a".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("runner_a".to_string()),
                                    ..$namespace::OfferRunnerDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Runner (
                                $namespace::OfferRunnerDecl {
                                    source_name: Some("runner_b".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("runner_b".to_string()),
                                    ..$namespace::OfferRunnerDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Event (
                                $namespace::OfferEventDecl {
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    source_name: Some("destroyed".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("destroyed_net".to_string()),
                                    filter: None,
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::OfferEventDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Event (
                                $namespace::OfferEventDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("stopped".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("stopped".to_string()),
                                    filter: None,
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::OfferEventDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Event (
                                $namespace::OfferEventDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("started".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("started".to_string()),
                                    filter: None,
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::OfferEventDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Event (
                                $namespace::OfferEventDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("directory_ready".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
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
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::OfferEventDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Resolver (
                                $namespace::OfferResolverDecl {
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("my_resolver".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("pkg_resolver".to_string()),
                                    ..$namespace::OfferResolverDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Resolver (
                                $namespace::OfferResolverDecl {
                                    source_name: Some("resolver_a".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("resolver_a".to_string()),
                                    ..$namespace::OfferResolverDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Resolver (
                                $namespace::OfferResolverDecl {
                                    source_name: Some("resolver_b".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("resolver_b".to_string()),
                                    ..$namespace::OfferResolverDecl::EMPTY
                                }
                            ),
                        ]),
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Storage (
                                $namespace::StorageDecl {
                                    name: Some("data".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    backing_dir: Some("minfs".to_string()),
                                    subdir: None,
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                }
                            )
                        ]),
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("logger".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                            $namespace::ChildDecl {
                                name: Some("netstack".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]),
                        collections: Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("modular".to_string()),
                                durability: Some($namespace::Durability::Persistent),
                                allowed_offers: None,
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_children => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("logger".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                            $namespace::ChildDecl {
                                name: Some("gmail".to_string()),
                                url: Some("https://www.google.com/gmail".to_string()),
                                startup: Some($namespace::StartupMode::Eager),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                            $namespace::ChildDecl {
                                name: Some("echo".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: Some("myenv".to_string()),
                                on_terminate: Some($namespace::OnTerminate::Reboot),
                                ..$namespace::ChildDecl::EMPTY
                            }
                        ]),
                        environments: Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("myenv".to_string()),
                                extends: Some($namespace::EnvironmentExtends::Realm),
                                runners: None,
                                resolvers: None,
                                stop_timeout_ms: None,
                                ..$namespace::EnvironmentDecl::EMPTY
                            }
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_collections => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        collections: Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("modular".to_string()),
                                durability: Some($namespace::Durability::Persistent),
                                allowed_offers: None,
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            },
                            $namespace::CollectionDecl {
                                name: Some("tests".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: None,
                                environment: Some("myenv".to_string()),
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]),
                        environments: Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("myenv".to_string()),
                                extends: Some($namespace::EnvironmentExtends::Realm),
                                runners: None,
                                resolvers: None,
                                stop_timeout_ms: None,
                                ..$namespace::EnvironmentDecl::EMPTY
                            }
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_capabilities => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Protocol (
                                $namespace::ProtocolDecl {
                                    name: Some("myprotocol".to_string()),
                                    source_path: Some("/protocol".to_string()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Protocol (
                                $namespace::ProtocolDecl {
                                    name: Some("myprotocol2".to_string()),
                                    source_path: Some("/svc/myprotocol2".to_string()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Protocol (
                                $namespace::ProtocolDecl {
                                    name: Some("myprotocol3".to_string()),
                                    source_path: Some("/svc/myprotocol3".to_string()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Protocol (
                                $namespace::ProtocolDecl {
                                    name: Some("myprotocol4".to_string()),
                                    source_path: Some("/svc/myprotocol4".to_string()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Directory (
                                $namespace::DirectoryDecl {
                                    name: Some("mydirectory".to_string()),
                                    source_path: Some("/directory".to_string()),
                                    rights: Some(fio2::Operations::Connect),
                                    ..$namespace::DirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Storage (
                                $namespace::StorageDecl {
                                    name: Some("mystorage".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "minfs".to_string(),
                                        collection: None,
                                    })),
                                    backing_dir: Some("storage".to_string()),
                                    subdir: None,
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Storage (
                                $namespace::StorageDecl {
                                    name: Some("mystorage2".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "minfs".to_string(),
                                        collection: None,
                                    })),
                                    backing_dir: Some("storage2".to_string()),
                                    subdir: None,
                                    storage_id: Some($namespace::StorageId::StaticInstanceId),
                                    ..$namespace::StorageDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Runner (
                                $namespace::RunnerDecl {
                                    name: Some("myrunner".to_string()),
                                    source_path: Some("/runner".to_string()),
                                    ..$namespace::RunnerDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Resolver (
                                $namespace::ResolverDecl {
                                    name: Some("myresolver".to_string()),
                                    source_path: Some("/resolver".to_string()),
                                    ..$namespace::ResolverDecl::EMPTY
                                }
                            )
                        ]),
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("minfs".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            }
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_facets => {
                    namespace = $namespace,
                    input = json!({
                        "facets": {
                            "title": "foo",
                            "authors": [ "me", "you" ],
                            "year": "2018",
                            "metadata": {
                                "publisher": "The Books Publisher",
                            }
                        }
                    }),
                    output = $namespace::ComponentDecl {
                        facets: Some(fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: "authors".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::StrVec(vec!["me".to_owned(), "you".to_owned()]))),
                                    },
                                    fdata::DictionaryEntry {
                                        key: "metadata.publisher".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str("The Books Publisher".to_string()))),
                                    },
                                    fdata::DictionaryEntry {
                                        key: "title".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str("foo".to_string()))),
                                    },
                                    fdata::DictionaryEntry {
                                        key: "year".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str("2018".to_string()))),
                                    },
                                ]),
                                ..fdata::Dictionary::EMPTY
                            }
                    ),
                    ..default_component_decl()
                    },
                },

                test_compile_environment => {
                    namespace = $namespace,
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
                                "__stop_timeout_ms": 8000u32,
                            }
                        ],
                    }),
                    output = $namespace::ComponentDecl {
                        environments: Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("myenv".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                runners: None,
                                resolvers: None,
                                stop_timeout_ms: None,
                                ..$namespace::EnvironmentDecl::EMPTY
                            },
                            $namespace::EnvironmentDecl {
                                name: Some("myenv2".to_string()),
                                extends: Some($namespace::EnvironmentExtends::Realm),
                                runners: None,
                                resolvers: None,
                                stop_timeout_ms: None,
                                ..$namespace::EnvironmentDecl::EMPTY
                            },
                            $namespace::EnvironmentDecl {
                                name: Some("myenv3".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                runners: None,
                                resolvers: None,
                                stop_timeout_ms: Some(8000),
                                ..$namespace::EnvironmentDecl::EMPTY
                            },
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_environment_with_runner_and_resolver => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        environments: Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("myenv".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                runners: Some(vec![
                                    $namespace::RunnerRegistration {
                                        source_name: Some("dart".to_string()),
                                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                        target_name: Some("dart".to_string()),
                                        ..$namespace::RunnerRegistration::EMPTY
                                    }
                                ]),
                                resolvers: Some(vec![
                                    $namespace::ResolverRegistration {
                                        resolver: Some("pkg_resolver".to_string()),
                                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                        scheme: Some("fuchsia-pkg".to_string()),
                                        ..$namespace::ResolverRegistration::EMPTY
                                    }
                                ]),
                                stop_timeout_ms: None,
                                ..$namespace::EnvironmentDecl::EMPTY
                            },
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_environment_with_runner_alias => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        environments: Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("myenv".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                runners: Some(vec![
                                    $namespace::RunnerRegistration {
                                        source_name: Some("dart".to_string()),
                                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                        target_name: Some("my-dart".to_string()),
                                        ..$namespace::RunnerRegistration::EMPTY
                                    }
                                ]),
                                resolvers: None,
                                stop_timeout_ms: None,
                                ..$namespace::EnvironmentDecl::EMPTY
                            },
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_environment_with_debug => {
                    namespace = $namespace,
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
                    output = $namespace::ComponentDecl {
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Protocol(
                                $namespace::ProtocolDecl {
                                    name : Some("fuchsia.serve.service".to_owned()),
                                    source_path: Some("/svc/fuchsia.serve.service".to_owned()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            )
                        ]),
                        environments: Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("myenv".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                debug_capabilities: Some(vec![
                                    $namespace::DebugRegistration::Protocol( $namespace::DebugProtocolRegistration {
                                        source_name: Some("fuchsia.serve.service".to_string()),
                                        source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                        target_name: Some("my-service".to_string()),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                                ]),
                                resolvers: None,
                                runners: None,
                                stop_timeout_ms: None,
                                ..$namespace::EnvironmentDecl::EMPTY
                            },
                        ]),
                        ..default_component_decl()
                    },
                },

                test_compile_all_sections => {
                    namespace = $namespace,
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
                            "year": "2018",
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
                    output = $namespace::ComponentDecl {
                        program: Some($namespace::ProgramDecl {
                            runner: Some("elf".to_string()),
                            info: Some(fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: "binary".to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            }),
                            ..$namespace::ProgramDecl::EMPTY
                        }),
                        uses: Some(vec![
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("LegacyCoolFonts".to_string()),
                                    target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("ReallyGoodFonts".to_string()),
                                    target_path: Some("/svc/ReallyGoodFonts".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("IWouldNeverUseTheseFonts".to_string()),
                                    target_path: Some("/svc/IWouldNeverUseTheseFonts".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Protocol (
                                $namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Debug($namespace::DebugRef {})),
                                    source_name: Some("DebugProtocol".to_string()),
                                    target_path: Some("/svc/DebugProtocol".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }
                            ),
                        ]),
                        exposes: Some(vec![
                            $namespace::ExposeDecl::Directory (
                                $namespace::ExposeDirectoryDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("blobfs".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    target_name: Some("blobfs".to_string()),
                                    rights: Some(
                                        fio2::Operations::Connect | fio2::Operations::Enumerate |
                                        fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                        fio2::Operations::GetAttributes
                                    ),
                                    subdir: None,
                                    ..$namespace::ExposeDirectoryDecl::EMPTY
                                }
                            ),
                        ]),
                        offers: Some(vec![
                            $namespace::OfferDecl::Protocol (
                                $namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Weak),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Protocol (
                                $namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "modular".to_string(),
                                    })),
                                    target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Weak),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }
                            ),
                        ]),
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Directory (
                                $namespace::DirectoryDecl {
                                    name: Some("blobfs".to_string()),
                                    source_path: Some("/volumes/blobfs".to_string()),
                                    rights: Some(fio2::Operations::Connect | fio2::Operations::Enumerate |
                                        fio2::Operations::Traverse | fio2::Operations::ReadBytes |
                                        fio2::Operations::GetAttributes
                                    ),
                                    ..$namespace::DirectoryDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Runner (
                                $namespace::RunnerDecl {
                                    name: Some("myrunner".to_string()),
                                    source_path: Some("/runner".to_string()),
                                    ..$namespace::RunnerDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Protocol(
                                $namespace::ProtocolDecl {
                                    name : Some("fuchsia.serve.service".to_owned()),
                                    source_path: Some("/svc/fuchsia.serve.service".to_owned()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                }
                            )
                        ]),
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("logger".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                            $namespace::ChildDecl {
                                name: Some("netstack".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]),
                        collections: Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("modular".to_string()),
                                durability: Some($namespace::Durability::Persistent),
                                allowed_offers: None,
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]),
                        environments: Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("myenv".to_string()),
                                extends: Some($namespace::EnvironmentExtends::Realm),
                                runners: None,
                                resolvers: None,
                                stop_timeout_ms: None,
                                debug_capabilities: Some(vec![
                                    $namespace::DebugRegistration::Protocol( $namespace::DebugProtocolRegistration {
                                        source_name: Some("fuchsia.serve.service".to_string()),
                                        source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                        target_name: Some("my-service".to_string()),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                                    $namespace::DebugRegistration::Protocol( $namespace::DebugProtocolRegistration {
                                        source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                                            name: "logger".to_string(),
                                            collection: None,
                                        })),
                                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                                ]),
                                ..$namespace::EnvironmentDecl::EMPTY
                            }
                        ]),
                        facets: Some(fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: "author".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str("Fuchsia".to_string()))),
                                    },
                                    fdata::DictionaryEntry {
                                        key: "year".to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str("2018".to_string()))),
                                    },
                                ]),
                                ..fdata::Dictionary::EMPTY
                            }),
                        ..$namespace::ComponentDecl::EMPTY
                    },
                },
            }
}
}
}

compile_tests!(fsys_translate_tests, fsys);
compile_tests!(fdecl_translate_tests, fdecl);
