// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use cm_fidl_translator;
use fidl_fuchsia_data as fdata;
use fidl_fuchsia_io2 as fio2;
use fidl_fuchsia_sys2::{
    ChildDecl, ChildRef, CollectionDecl, CollectionRef, ComponentDecl, DependencyType, Durability,
    Entry, ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl, ExposeServiceDecl, FrameworkRef,
    Object, OfferDecl, OfferEventDecl, OfferProtocolDecl, OfferRunnerDecl, OfferServiceDecl,
    RealmRef, Ref, RunnerDecl, SelfRef, StartupMode, UseDecl, UseEventDecl, UseProtocolDecl,
    UseRunnerDecl, UseServiceDecl, Value,
};
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;

fn main() {
    let cm_content = read_cm("/pkg/meta/example.cm").expect("could not open example.cm");
    let golden_cm = read_cm("/pkg/data/golden.cm").expect("could not open golden.cm");
    assert_eq!(&cm_content, &golden_cm);

    let cm_decl = cm_fidl_translator::translate(&cm_content).expect("could not translate cm");
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
        };
        let uses = vec![
            UseDecl::Runner(UseRunnerDecl { source_name: Some("elf".to_string()) }),
            UseDecl::Service(UseServiceDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_path: Some("/fonts/CoolFonts".to_string()),
                target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
            }),
            UseDecl::Protocol(UseProtocolDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_path: Some("/fonts/LegacyCoolFonts".to_string()),
                target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
            }),
            UseDecl::Event(UseEventDecl {
                source: Some(Ref::Framework(FrameworkRef {})),
                source_name: Some("started".to_string()),
                target_name: Some("started".to_string()),
                filter: None,
            }),
            UseDecl::Event(UseEventDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_name: Some("capability_ready".to_string()),
                target_name: Some("diagnostics_ready".to_string()),
                filter: Some(fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: "path".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(
                            "/diagnostics".to_string(),
                        ))),
                    }]),
                }),
            }),
        ];
        let runners = vec![RunnerDecl {
            name: Some("dart_runner".to_string()),
            source: Some(Ref::Self_(SelfRef {})),
            source_path: Some("/svc/fuchsia.sys2.Runner".to_string()),
        }];
        let exposes = vec![
            ExposeDecl::Service(ExposeServiceDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                target: Some(Ref::Realm(RealmRef {})),
            }),
            ExposeDecl::Protocol(ExposeProtocolDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                target: Some(Ref::Realm(RealmRef {})),
            }),
            ExposeDecl::Directory(ExposeDirectoryDecl {
                source: Some(Ref::Self_(SelfRef {})),
                source_path: Some("/volumes/blobfs".to_string()),
                target_path: Some("/volumes/blobfs".to_string()),
                target: Some(Ref::Framework(FrameworkRef {})),
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
                subdir: Some("blob".to_string()),
            }),
        ];
        let offers = vec![
            OfferDecl::Service(OfferServiceDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/svc/fuchsia.logger.Log".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
            }),
            OfferDecl::Protocol(OfferProtocolDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                dependency_type: Some(DependencyType::Strong),
            }),
            OfferDecl::Runner(OfferRunnerDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_name: Some("elf".to_string()),
                target: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                target_name: Some("elf".to_string()),
            }),
            OfferDecl::Runner(OfferRunnerDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_name: Some("elf".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_name: Some("elf".to_string()),
            }),
            OfferDecl::Event(OfferEventDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_name: Some("stopped".to_string()),
                target: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                target_name: Some("stopped-logger".to_string()),
                filter: None,
            }),
        ];
        let children = vec![ChildDecl {
            name: Some("logger".to_string()),
            url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
            startup: Some(StartupMode::Lazy),
            environment: None,
        }];
        let collections = vec![CollectionDecl {
            name: Some("modular".to_string()),
            durability: Some(Durability::Persistent),
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
        ComponentDecl {
            program: Some(program),
            uses: Some(uses),
            exposes: Some(exposes),
            offers: Some(offers),
            children: Some(children),
            collections: Some(collections),
            facets: Some(facets),
            runners: Some(runners),
            // TODO: test storage
            storage: None,
            environments: None,
            resolvers: None,
        }
    };
    assert_eq!(cm_decl, expected_decl);
}

fn read_cm(file: &str) -> Result<String, Error> {
    let mut buffer = String::new();
    let path = PathBuf::from(file);
    File::open(&path)?.read_to_string(&mut buffer)?;
    Ok(buffer)
}
