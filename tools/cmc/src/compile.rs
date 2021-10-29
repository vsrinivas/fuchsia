// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml;
use crate::error::Error;
use crate::features::FeatureSet;
use crate::include;
use crate::util;
use crate::validate;
use fidl::encoding::encode_persistent;
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

    let mut out_data = cml::compile(&document)?;
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::features::Feature;
    use fidl::encoding::decode_persistent;
    use fidl_fuchsia_data as fdata;
    use fidl_fuchsia_sys2 as fsys;
    use matches::assert_matches;
    use serde_json::json;
    use std::fs::File;
    use std::io::{self, Read, Write};
    use tempfile::TempDir;

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
                    { "service": "fuchsia.component.Realm", "from": "framework" },
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
                            source_name: Some("fuchsia.component.Realm".to_string()),
                            target_path: Some("/svc/fuchsia.component.Realm".to_string()),
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
