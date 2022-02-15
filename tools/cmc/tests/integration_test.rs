// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_decl::*;
use fidl_fuchsia_data as fdata;
use fidl_fuchsia_io as fio;
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;

fn main() {
    // example.cm has already been compiled by cmc as part of the build process
    // See: https://fuchsia.googlesource.com/fuchsia/+/c4b7ddf8128e782f957374c64f57aa2508ac3fe2/build/package.gni#304
    let mut cm_decl = read_cm("/pkg/meta/example.cm").expect("could not read cm file");

    // profile variant injects this protocol.
    if let Some(uses) = &mut cm_decl.uses {
        uses.retain(|u| match u {
            Use::Protocol(decl) => {
                if decl.source_name == Some("fuchsia.debugdata.DebugData".to_owned()) {
                    assert_eq!(
                        decl,
                        &UseProtocol {
                            dependency_type: Some(DependencyType::Strong),
                            source: Some(Ref::Debug(DebugRef {})),
                            source_name: Some("fuchsia.debugdata.DebugData".to_string()),
                            target_path: Some("/svc/fuchsia.debugdata.DebugData".to_string()),
                            ..UseProtocol::EMPTY
                        }
                    );
                    return false;
                }
                return true;
            }
            _ => true,
        })
    }

    let expected_decl = {
        let program = Program {
            runner: Some("elf".to_string()),
            info: Some(fdata::Dictionary {
                entries: Some(vec![
                    fdata::DictionaryEntry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(
                            "bin/example".to_string(),
                        ))),
                    },
                    fdata::DictionaryEntry {
                        key: "lifecycle.stop_event".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str("notify".to_string()))),
                    },
                ]),
                ..fdata::Dictionary::EMPTY
            }),
            ..Program::EMPTY
        };
        let uses = vec![
            Use::Service(UseService {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("fuchsia.fonts.Provider".to_string()),
                target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                ..UseService::EMPTY
            }),
            Use::Protocol(UseProtocol {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("fuchsia.fonts.LegacyProvider".to_string()),
                target_path: Some("/svc/fuchsia.fonts.OldProvider".to_string()),
                ..UseProtocol::EMPTY
            }),
            Use::Protocol(UseProtocol {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Debug(DebugRef {})),
                source_name: Some("fuchsia.log.LegacyLog".to_string()),
                target_path: Some("/svc/fuchsia.log.LegacyLog".to_string()),
                ..UseProtocol::EMPTY
            }),
            Use::Event(UseEvent {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Framework(FrameworkRef {})),
                source_name: Some("started".to_string()),
                target_name: Some("began".to_string()),
                filter: None,
                mode: Some(EventMode::Async),
                ..UseEvent::EMPTY
            }),
            Use::Event(UseEvent {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("destroyed".to_string()),
                target_name: Some("destroyed".to_string()),
                filter: None,
                mode: Some(EventMode::Async),
                ..UseEvent::EMPTY
            }),
            Use::Event(UseEvent {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("stopped".to_string()),
                target_name: Some("stopped".to_string()),
                filter: None,
                mode: Some(EventMode::Async),
                ..UseEvent::EMPTY
            }),
            Use::Event(UseEvent {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("directory_ready".to_string()),
                target_name: Some("diagnostics_ready".to_string()),
                filter: Some(fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: "path".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(
                            "diagnostics".to_string(),
                        ))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                }),
                mode: Some(EventMode::Async),
                ..UseEvent::EMPTY
            }),
            Use::EventStreamDeprecated(UseEventStreamDeprecated {
                name: Some("my_stream".to_string()),
                subscriptions: Some(vec![
                    EventSubscription {
                        event_name: Some("began".to_string()),
                        mode: Some(EventMode::Async),
                        ..EventSubscription::EMPTY
                    },
                    EventSubscription {
                        event_name: Some("destroyed".to_string()),
                        mode: Some(EventMode::Async),
                        ..EventSubscription::EMPTY
                    },
                    EventSubscription {
                        event_name: Some("diagnostics_ready".to_string()),
                        mode: Some(EventMode::Async),
                        ..EventSubscription::EMPTY
                    },
                ]),
                ..UseEventStreamDeprecated::EMPTY
            }),
            Use::EventStream(UseEventStream {
                source_names: Some(vec![
                    "events".to_string(),
                    "other".to_string(),
                    "some".to_string(),
                ]),
                source: Some(Ref::Parent(ParentRef {})),
                target_path: Some("/testdir/my_stream".to_string()),
                scope: Some(vec![Ref::Child(ChildRef {
                    collection: None,
                    name: "logger".to_string(),
                })]),
                ..UseEventStream::EMPTY
            }),
            Use::EventStream(UseEventStream {
                source_names: Some(vec!["filtered".to_string()]),
                source: Some(Ref::Parent(ParentRef {})),
                target_path: Some("/svc/fuchsia.component.EventStream".to_string()),
                ..UseEventStream::EMPTY
            }),
            Use::Protocol(UseProtocol {
                dependency_type: Some(DependencyType::Strong),
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("fuchsia.logger.LogSink".to_string()),
                target_path: Some("/svc/fuchsia.logger.LogSink".to_string()),
                ..UseProtocol::EMPTY
            }),
        ];
        let exposes = vec![
            Expose::Service(ExposeService {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.Log".to_string()),
                target_name: Some("fuchsia.logger.Log".to_string()),
                target: Some(Ref::Parent(ParentRef {})),
                ..ExposeService::EMPTY
            }),
            Expose::Protocol(ExposeProtocol {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                target_name: Some("fuchsia.logger.OldLog".to_string()),
                target: Some(Ref::Parent(ParentRef {})),
                ..ExposeProtocol::EMPTY
            }),
            Expose::Directory(ExposeDirectory {
                source: Some(Ref::Self_(SelfRef {})),
                source_name: Some("blobfs".to_string()),
                target_name: Some("blobfs".to_string()),
                target: Some(Ref::Parent(ParentRef {})),
                rights: None,
                subdir: Some("blob".to_string()),
                ..ExposeDirectory::EMPTY
            }),
        ];
        let offers = vec![
            Offer::Service(OfferService {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.Log".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_name: Some("fuchsia.logger.Log".to_string()),
                ..OfferService::EMPTY
            }),
            Offer::Protocol(OfferProtocol {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_name: Some("fuchsia.logger.OldLog".to_string()),
                dependency_type: Some(DependencyType::Strong),
                ..OfferProtocol::EMPTY
            }),
            Offer::Event(OfferEvent {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("stopped".to_string()),
                target: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                target_name: Some("stopped-logger".to_string()),
                filter: None,
                mode: Some(EventMode::Async),
                ..OfferEvent::EMPTY
            }),
        ];
        let capabilities = vec![
            Capability::Service(Service {
                name: Some("fuchsia.logger.Log".to_string()),
                source_path: Some("/svc/fuchsia.logger.Log".to_string()),
                ..Service::EMPTY
            }),
            Capability::Protocol(Protocol {
                name: Some("fuchsia.logger.Log2".to_string()),
                source_path: Some("/svc/fuchsia.logger.Log2".to_string()),
                ..Protocol::EMPTY
            }),
            Capability::Directory(Directory {
                name: Some("blobfs".to_string()),
                source_path: Some("/volumes/blobfs".to_string()),
                rights: Some(
                    fio::Operations::CONNECT
                        | fio::Operations::READ_BYTES
                        | fio::Operations::WRITE_BYTES
                        | fio::Operations::GET_ATTRIBUTES
                        | fio::Operations::UPDATE_ATTRIBUTES
                        | fio::Operations::ENUMERATE
                        | fio::Operations::TRAVERSE
                        | fio::Operations::MODIFY_DIRECTORY,
                ),
                ..Directory::EMPTY
            }),
            Capability::Storage(Storage {
                name: Some("minfs".to_string()),
                source: Some(Ref::Parent(ParentRef {})),
                backing_dir: Some("data".to_string()),
                subdir: None,
                storage_id: Some(StorageId::StaticInstanceIdOrMoniker),
                ..Storage::EMPTY
            }),
            Capability::Runner(Runner {
                name: Some("dart_runner".to_string()),
                source_path: Some("/svc/fuchsia.sys2.Runner".to_string()),
                ..Runner::EMPTY
            }),
            Capability::Resolver(Resolver {
                name: Some("pkg_resolver".to_string()),
                source_path: Some("/svc/fuchsia.pkg.Resolver".to_string()),
                ..Resolver::EMPTY
            }),
        ];
        let children = vec![Child {
            name: Some("logger".to_string()),
            url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
            startup: Some(StartupMode::Lazy),
            environment: Some("env_one".to_string()),
            ..Child::EMPTY
        }];
        let collections = vec![
            Collection {
                name: Some("modular".to_string()),
                durability: Some(Durability::Persistent),
                allowed_offers: None,
                environment: None,
                ..Collection::EMPTY
            },
            Collection {
                name: Some("explicit_static".to_string()),
                durability: Some(Durability::Persistent),
                allowed_offers: Some(AllowedOffers::StaticOnly),
                environment: None,
                ..Collection::EMPTY
            },
            Collection {
                name: Some("explicit_dynamic".to_string()),
                durability: Some(Durability::Persistent),
                allowed_offers: Some(AllowedOffers::StaticAndDynamic),
                environment: None,
                ..Collection::EMPTY
            },
        ];
        let facets = fdata::Dictionary {
            entries: Some(vec![
                fdata::DictionaryEntry {
                    key: "author".to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str("Fuchsia".to_string()))),
                },
                fdata::DictionaryEntry {
                    key: "metadata.publisher".to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str(
                        "The Books Publisher".to_string(),
                    ))),
                },
                fdata::DictionaryEntry {
                    key: "year".to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str("2018".to_string()))),
                },
            ]),
            ..fdata::Dictionary::EMPTY
        };
        let envs = vec![
            Environment {
                name: Some("env_one".to_string()),
                extends: Some(EnvironmentExtends::None),
                stop_timeout_ms: Some(1337),
                runners: None,
                resolvers: None,
                debug_capabilities: None,
                ..Environment::EMPTY
            },
            Environment {
                name: Some("env_two".to_string()),
                extends: Some(EnvironmentExtends::Realm),
                stop_timeout_ms: None,
                runners: None,
                resolvers: None,
                debug_capabilities: Some(vec![
                    DebugRegistration::Protocol(DebugProtocolRegistration {
                        source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        ..DebugProtocolRegistration::EMPTY
                    }),
                    DebugRegistration::Protocol(DebugProtocolRegistration {
                        source_name: Some("fuchsia.logger.OtherLog".to_string()),
                        source: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("fuchsia.logger.OtherLog".to_string()),
                        ..DebugProtocolRegistration::EMPTY
                    }),
                    DebugRegistration::Protocol(DebugProtocolRegistration {
                        source_name: Some("fuchsia.logger.Log2".to_string()),
                        source: Some(Ref::Self_(SelfRef {})),
                        target_name: Some("fuchsia.logger.Log2".to_string()),
                        ..DebugProtocolRegistration::EMPTY
                    }),
                ]),
                ..Environment::EMPTY
            },
        ];

        let config = ConfigSchema {
            fields: Some(vec![
                ConfigField {
                    key: Some("my_flag".to_string()),
                    type_: Some(ConfigType {
                        layout: ConfigTypeLayout::Bool,
                        parameters: Some(vec![]),
                        constraints: vec![],
                    }),
                    ..ConfigField::EMPTY
                },
                ConfigField {
                    key: Some("my_string".to_string()),
                    type_: Some(ConfigType {
                        layout: ConfigTypeLayout::String,
                        constraints: vec![LayoutConstraint::MaxSize(100)],
                        parameters: Some(vec![]),
                    }),
                    ..ConfigField::EMPTY
                },
                ConfigField {
                    key: Some("my_uint8".to_string()),
                    type_: Some(ConfigType {
                        layout: ConfigTypeLayout::Uint8,
                        parameters: Some(vec![]),
                        constraints: vec![],
                    }),
                    ..ConfigField::EMPTY
                },
                ConfigField {
                    key: Some("my_vector_of_string".to_string()),
                    type_: Some(ConfigType {
                        layout: ConfigTypeLayout::Vector,
                        constraints: vec![LayoutConstraint::MaxSize(100)],
                        parameters: Some(vec![LayoutParameter::NestedType(ConfigType {
                            layout: ConfigTypeLayout::String,
                            constraints: vec![LayoutConstraint::MaxSize(50)],
                            parameters: Some(vec![]),
                        })]),
                    }),
                    ..ConfigField::EMPTY
                },
            ]),
            checksum: Some(ConfigChecksum::Sha256([
                227, 126, 183, 151, 1, 15, 67, 110, 144, 49, 222, 176, 221, 111, 19, 165, 34, 204,
                240, 41, 165, 95, 117, 57, 203, 42, 186, 167, 84, 26, 25, 231,
            ])),
            value_source: Some(ConfigValueSource::PackagePath("meta/example.cvf".to_string())),
            ..ConfigSchema::EMPTY
        };

        Component {
            program: Some(program),
            uses: Some(uses),
            exposes: Some(exposes),
            offers: Some(offers),
            capabilities: Some(capabilities),
            children: Some(children),
            collections: Some(collections),
            facets: Some(facets),
            environments: Some(envs),
            config: Some(config),
            ..Component::EMPTY
        }
    };
    assert_eq!(cm_decl, expected_decl);
}

fn read_cm(file: &str) -> Result<Component, Error> {
    let mut buffer = Vec::new();
    let path = PathBuf::from(file);
    File::open(&path)?.read_to_end(&mut buffer)?;
    Ok(decode_persistent(&buffer)?)
}
