// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::encoding::decode_persistent;
use fidl_fuchsia_data as fdata;
use fidl_fuchsia_io2 as fio2;
use fidl_fuchsia_sys2::*;
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;

fn main() {
    // example.cm has already been compiled by cmc as part of the build process
    // See: https://fuchsia.googlesource.com/fuchsia/+/c4b7ddf8128e782f957374c64f57aa2508ac3fe2/build/package.gni#304
    let cm_decl = read_cm("/pkg/meta/example.cm").expect("could not read cm file");

    let expected_decl = {
        let program = fdata::Dictionary {
            entries: Some(vec![
                fdata::DictionaryEntry {
                    key: "binary".to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str("bin/example".to_string()))),
                },
                fdata::DictionaryEntry {
                    key: "lifecycle.stop_event".to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str("notify".to_string()))),
                },
            ]),
            ..fdata::Dictionary::EMPTY
        };
        let uses = vec![
            UseDecl::Runner(UseRunnerDecl {
                source_name: Some("elf".to_string()),
                ..UseRunnerDecl::EMPTY
            }),
            UseDecl::Service(UseServiceDecl {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("fuchsia.fonts.Provider".to_string()),
                target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                ..UseServiceDecl::EMPTY
            }),
            UseDecl::Protocol(UseProtocolDecl {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("fuchsia.fonts.LegacyProvider".to_string()),
                target_path: Some("/svc/fuchsia.fonts.OldProvider".to_string()),
                ..UseProtocolDecl::EMPTY
            }),
            UseDecl::Event(UseEventDecl {
                source: Some(Ref::Framework(FrameworkRef {})),
                source_name: Some("started".to_string()),
                target_name: Some("began".to_string()),
                filter: None,
                ..UseEventDecl::EMPTY
            }),
            UseDecl::Event(UseEventDecl {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("destroyed".to_string()),
                target_name: Some("destroyed".to_string()),
                filter: None,
                ..UseEventDecl::EMPTY
            }),
            UseDecl::Event(UseEventDecl {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("stopped".to_string()),
                target_name: Some("stopped".to_string()),
                filter: None,
                ..UseEventDecl::EMPTY
            }),
            UseDecl::Event(UseEventDecl {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("capability_ready".to_string()),
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
                ..UseEventDecl::EMPTY
            }),
            UseDecl::EventStream(UseEventStreamDecl {
                target_path: Some("/svc/my_stream".to_string()),
                events: Some(vec![
                    "began".to_string(),
                    "destroyed".to_string(),
                    "diagnostics_ready".to_string(),
                ]),
                ..UseEventStreamDecl::EMPTY
            }),
            UseDecl::Protocol(UseProtocolDecl {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("fuchsia.logger.LogSink".to_string()),
                target_path: Some("/svc/fuchsia.logger.LogSink".to_string()),
                ..UseProtocolDecl::EMPTY
            }),
        ];
        let exposes = vec![
            ExposeDecl::Service(ExposeServiceDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.Log".to_string()),
                target_name: Some("fuchsia.logger.Log".to_string()),
                target: Some(Ref::Parent(ParentRef {})),
                ..ExposeServiceDecl::EMPTY
            }),
            ExposeDecl::Protocol(ExposeProtocolDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                target_name: Some("fuchsia.logger.OldLog".to_string()),
                target: Some(Ref::Parent(ParentRef {})),
                ..ExposeProtocolDecl::EMPTY
            }),
            ExposeDecl::Directory(ExposeDirectoryDecl {
                source: Some(Ref::Self_(SelfRef {})),
                source_name: Some("blobfs".to_string()),
                target_name: Some("blobfs".to_string()),
                target: Some(Ref::Parent(ParentRef {})),
                rights: None,
                subdir: Some("blob".to_string()),
                ..ExposeDirectoryDecl::EMPTY
            }),
        ];
        let offers = vec![
            OfferDecl::Service(OfferServiceDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.Log".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_name: Some("fuchsia.logger.Log".to_string()),
                ..OfferServiceDecl::EMPTY
            }),
            OfferDecl::Protocol(OfferProtocolDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_name: Some("fuchsia.logger.OldLog".to_string()),
                dependency_type: Some(DependencyType::Strong),
                ..OfferProtocolDecl::EMPTY
            }),
            OfferDecl::Event(OfferEventDecl {
                source: Some(Ref::Parent(ParentRef {})),
                source_name: Some("stopped".to_string()),
                target: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                target_name: Some("stopped-logger".to_string()),
                filter: None,
                ..OfferEventDecl::EMPTY
            }),
        ];
        let capabilities = vec![
            CapabilityDecl::Service(ServiceDecl {
                name: Some("fuchsia.logger.Log".to_string()),
                source_path: Some("/svc/fuchsia.logger.Log".to_string()),
                ..ServiceDecl::EMPTY
            }),
            CapabilityDecl::Protocol(ProtocolDecl {
                name: Some("fuchsia.logger.Log2".to_string()),
                source_path: Some("/svc/fuchsia.logger.Log2".to_string()),
                ..ProtocolDecl::EMPTY
            }),
            CapabilityDecl::Directory(DirectoryDecl {
                name: Some("blobfs".to_string()),
                source_path: Some("/volumes/blobfs".to_string()),
                rights: Some(
                    fio2::Operations::Connect
                        | fio2::Operations::ReadBytes
                        | fio2::Operations::WriteBytes
                        | fio2::Operations::GetAttributes
                        | fio2::Operations::UpdateAttributes
                        | fio2::Operations::Enumerate
                        | fio2::Operations::Traverse
                        | fio2::Operations::ModifyDirectory,
                ),
                ..DirectoryDecl::EMPTY
            }),
            CapabilityDecl::Storage(StorageDecl {
                name: Some("minfs".to_string()),
                source: Some(Ref::Parent(ParentRef {})),
                backing_dir: Some("data".to_string()),
                subdir: None,
                ..StorageDecl::EMPTY
            }),
            CapabilityDecl::Runner(RunnerDecl {
                name: Some("dart_runner".to_string()),
                source: Some(Ref::Self_(SelfRef {})),
                source_path: Some("/svc/fuchsia.sys2.Runner".to_string()),
                ..RunnerDecl::EMPTY
            }),
            CapabilityDecl::Resolver(ResolverDecl {
                name: Some("pkg_resolver".to_string()),
                source_path: Some("/svc/fuchsia.pkg.Resolver".to_string()),
                ..ResolverDecl::EMPTY
            }),
        ];
        let children = vec![ChildDecl {
            name: Some("logger".to_string()),
            url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
            startup: Some(StartupMode::Lazy),
            environment: Some("env_one".to_string()),
            ..ChildDecl::EMPTY
        }];
        let collections = vec![CollectionDecl {
            name: Some("modular".to_string()),
            durability: Some(Durability::Persistent),
            environment: None,
            ..CollectionDecl::EMPTY
        }];
        let facets = Object {
            entries: vec![
                Entry {
                    key: "author".to_string(),
                    value: Some(Box::new(Value::Str("Fuchsia".to_string()))),
                },
                Entry { key: "year".to_string(), value: Some(Box::new(Value::Inum(2018))) },
            ],
        };
        let envs = vec![
            EnvironmentDecl {
                name: Some("env_one".to_string()),
                extends: Some(EnvironmentExtends::None),
                stop_timeout_ms: Some(1337),
                runners: None,
                resolvers: None,
                ..EnvironmentDecl::EMPTY
            },
            EnvironmentDecl {
                name: Some("env_two".to_string()),
                extends: Some(EnvironmentExtends::Realm),
                stop_timeout_ms: None,
                runners: None,
                resolvers: None,
                ..EnvironmentDecl::EMPTY
            },
        ];
        ComponentDecl {
            program: Some(program),
            uses: Some(uses),
            exposes: Some(exposes),
            offers: Some(offers),
            capabilities: Some(capabilities),
            children: Some(children),
            collections: Some(collections),
            facets: Some(facets),
            environments: Some(envs),
            ..ComponentDecl::EMPTY
        }
    };
    assert_eq!(cm_decl, expected_decl);
}

fn read_cm(file: &str) -> Result<ComponentDecl, Error> {
    let mut buffer = Vec::new();
    let path = PathBuf::from(file);
    File::open(&path)?.read_to_end(&mut buffer)?;
    Ok(decode_persistent(&buffer)?)
}
