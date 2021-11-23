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
    experimental_sdk_output: bool,
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

    util::ensure_directory_exists(&output)?;
    let mut out_file =
        fs::OpenOptions::new().create(true).truncate(true).write(true).open(output)?;
    if experimental_sdk_output {
        let mut out_data = cml::fdecl::compile(&document)?;
        out_file.write(&encode_persistent(&mut out_data)?)?;
    } else {
        let mut out_data = cml::fsys::compile(&document)?;
        out_file.write(&encode_persistent(&mut out_data)?)?;
    }

    // Write includes to depfile
    if let Some(depfile_path) = depfile {
        util::write_depfile(&depfile_path, Some(&output.to_path_buf()), &includes)?;
    }

    Ok(())
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

macro_rules! test_suite {
    ($mod:ident, $namespace:ident) => {
        #[allow(unused)]
        #[cfg(test)]
        mod $mod {
            use super::*;
            use crate::features::Feature;
            use fidl::encoding::decode_persistent;
            use fidl_fuchsia_data as fdata;
            use fidl_fuchsia_sys2 as fsys;
            use cm_rust::convert as fdecl;
            use matches::assert_matches;
            use serde_json::json;
            use std::fs::File;
            use std::io::{self, Read, Write};
            use tempfile::TempDir;


            fn compile_test_with_forced_runner(
                in_path: PathBuf,
                out_path: PathBuf,
                includepath: Option<PathBuf>,
                input: serde_json::value::Value,
                expected_output: $namespace::ComponentDecl,
                features: &FeatureSet,
                experimental_force_runner: &Option<String>,
            ) -> Result<(), Error> {
                File::create(&in_path).unwrap().write_all(format!("{}", input).as_bytes()).unwrap();
                let includepath = includepath.unwrap_or(PathBuf::new());

                // First test using the default `fuchsia.sys2` namespace.
                // Then test using the `fuchsia.component.decl` namespace.
                // We'll only assert that the output matches the `fuchsia.sys2` type
                // because it 1) guarantees ABI compatibility and 2) is what is used
                // by consumers of the generated manifest at the moment.
                for use_sdk_output_type in vec![false, true].into_iter() {
                    compile(
                        &in_path.clone(),
                        &out_path.clone(),
                        None,
                        &vec![includepath.clone()],
                        &includepath.clone(),
                        features.clone(),
                        experimental_force_runner,
                        use_sdk_output_type,
                    )?;
                    let mut buffer = Vec::new();
                    fs::File::open(&out_path).unwrap().read_to_end(&mut buffer).unwrap();

                    let output: $namespace::ComponentDecl = decode_persistent(&buffer).unwrap();
                    assert_eq!(output, expected_output);
                }

                Ok(())
            }

            fn compile_test(
                in_path: PathBuf,
                out_path: PathBuf,
                includepath: Option<PathBuf>,
                input: serde_json::value::Value,
                expected_output: $namespace::ComponentDecl,
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

            fn default_component_decl() -> $namespace::ComponentDecl {
                $namespace::ComponentDecl::EMPTY
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
                    output = $namespace::ComponentDecl {
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Service (
                                $namespace::ServiceDecl {
                                    name: Some("myservice".to_string()),
                                    source_path: Some("/service".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Service (
                                $namespace::ServiceDecl {
                                    name: Some("myservice2".to_string()),
                                    source_path: Some("/svc/myservice2".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Service (
                                $namespace::ServiceDecl {
                                    name: Some("myservice3".to_string()),
                                    source_path: Some("/svc/myservice3".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                }
                            ),
                        ]),
                        ..$namespace::ComponentDecl::EMPTY
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
                    output = $namespace::ComponentDecl {
                        uses: Some(vec![
                            $namespace::UseDecl::Service (
                                $namespace::UseServiceDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("CoolFonts".to_string()),
                                    target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
                                    ..$namespace::UseServiceDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Service (
                                $namespace::UseServiceDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                    source_name: Some("fuchsia.component.Realm".to_string()),
                                    target_path: Some("/svc/fuchsia.component.Realm".to_string()),
                                    ..$namespace::UseServiceDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Service (
                                $namespace::UseServiceDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("myservice".to_string()),
                                    target_path: Some("/svc/myservice".to_string()),
                                    ..$namespace::UseServiceDecl::EMPTY
                                }
                            ),
                            $namespace::UseDecl::Service (
                                $namespace::UseServiceDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    source_name: Some("myservice2".to_string()),
                                    target_path: Some("/svc/myservice2".to_string()),
                                    ..$namespace::UseServiceDecl::EMPTY
                                }
                            ),
                        ]),
                        ..$namespace::ComponentDecl::EMPTY
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
                    output = $namespace::ComponentDecl {
                        offers: Some(vec![
                            $namespace::OfferDecl::Service (
                                $namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.Log".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("fuchsia.logger.Log".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Service (
                                $namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.Log".to_string()),
                                    target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                        name: "coll".to_string(),
                                    })),
                                    target_name: Some("fuchsia.logger.Log2".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Service (
                                $namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("my.service.Service".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("my.service.Service".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Service (
                                $namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("my.service.Service2".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("my.service.Service2".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Service (
                                $namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("my.service.Service".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("my.service.Service".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Service (
                                $namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("my.service.Service2".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("my.service.Service2".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }
                            ),
                            $namespace::OfferDecl::Service (
                                $namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "coll".to_string() })),
                                    source_name: Some("my.service.CollectionService".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("my.service.CollectionService".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }
                            ),
                        ]),
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Service (
                                $namespace::ServiceDecl {
                                    name: Some("my.service.Service".to_string()),
                                    source_path: Some("/svc/my.service.Service".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Service (
                                $namespace::ServiceDecl {
                                    name: Some("my.service.Service2".to_string()),
                                    source_path: Some("/svc/my.service.Service2".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                }
                            ),
                        ]),
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("logger".to_string()),
                                url: Some("fuchsia-pkg://logger.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                            $namespace::ChildDecl {
                                name: Some("netstack".to_string()),
                                url: Some("fuchsia-pkg://netstack.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            }
                        ]),
                        collections: Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("coll".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: None,
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]),
                        ..$namespace::ComponentDecl::EMPTY
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
                    output = $namespace::ComponentDecl {
                        exposes: Some(vec![
                            $namespace::ExposeDecl::Service (
                                $namespace::ExposeServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("fuchsia.logger.Log".to_string()),
                                    target_name: Some("fuchsia.logger.Log2".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    ..$namespace::ExposeServiceDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Service (
                                $namespace::ExposeServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("my.service.Service".to_string()),
                                    target_name: Some("my.service.Service".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    ..$namespace::ExposeServiceDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Service (
                                $namespace::ExposeServiceDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("my.service.Service".to_string()),
                                    target_name: Some("my.service.Service".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    ..$namespace::ExposeServiceDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Service (
                                $namespace::ExposeServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    })),
                                    source_name: Some("my.service.Service2".to_string()),
                                    target_name: Some("my.service.Service2".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    ..$namespace::ExposeServiceDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Service (
                                $namespace::ExposeServiceDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    source_name: Some("my.service.Service2".to_string()),
                                    target_name: Some("my.service.Service2".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    ..$namespace::ExposeServiceDecl::EMPTY
                                }
                            ),
                            $namespace::ExposeDecl::Service (
                                $namespace::ExposeServiceDecl {
                                    source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "coll".to_string() })),
                                    source_name: Some("my.service.CollectionService".to_string()),
                                    target_name: Some("my.service.CollectionService".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    ..$namespace::ExposeServiceDecl::EMPTY
                                }
                            ),
                        ]),
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Service (
                                $namespace::ServiceDecl {
                                    name: Some("my.service.Service".to_string()),
                                    source_path: Some("/svc/my.service.Service".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                }
                            ),
                            $namespace::CapabilityDecl::Service (
                                $namespace::ServiceDecl {
                                    name: Some("my.service.Service2".to_string()),
                                    source_path: Some("/svc/my.service.Service2".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                }
                            ),
                        ]),
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("logger".to_string()),
                                url: Some("fuchsia-pkg://logger.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                environment: None,
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            }
                        ]),
                        collections: Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("coll".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: None,
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]),
                        ..$namespace::ComponentDecl::EMPTY
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
                    output = $namespace::ComponentDecl {
                        collections: Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("modular".to_string()),
                                durability: Some($namespace::Durability::Persistent),
                                allowed_offers: None,
                                ..$namespace::CollectionDecl::EMPTY
                            },
                            $namespace::CollectionDecl {
                                name: Some("tests".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                                ..$namespace::CollectionDecl::EMPTY
                            },
                            $namespace::CollectionDecl {
                                name: Some("dynamic_offers".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: Some($namespace::AllowedOffers::StaticAndDynamic),
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]),
                        ..$namespace::ComponentDecl::EMPTY
                    },
                },
            }}

            test_compile_with_features! { FeatureSet::from(vec![Feature::StructuredConfig]), {
                test_compile_config => {
                    input = json!({
                        "config": {
                            "test8": {
                                "type": "vector",
                                "max_count": 100,
                                "element": {
                                    "type": "uint16"
                                }
                            },
                            "test7": { "type": "int64" },
                            "test6": { "type": "uint64" },
                            "test5": { "type": "int8" },
                            "test4": { "type": "uint8" },
                            "test3": { "type": "bool" },
                            "test2": {
                                "type": "vector",
                                "max_count": 100,
                                "element": {
                                    "type": "string",
                                    "max_size": 50
                                }
                            },
                            "test1": {
                                "type": "string",
                                "max_size": 50
                            }
                        }
                    }),
                    output = $namespace::ComponentDecl {
                        config: Some($namespace::ConfigDecl {
                            fields: Some(vec![
                                $namespace::ConfigField {
                                    key: Some("test1".to_string()),
                                    value_type: Some($namespace::ConfigValueType::String(
                                        $namespace::ConfigStringType {
                                            max_size: Some(50),
                                            ..$namespace::ConfigStringType::EMPTY
                                        }
                                    )),
                                    ..$namespace::ConfigField::EMPTY
                                },
                                $namespace::ConfigField {
                                    key: Some("test2".to_string()),
                                    value_type: Some($namespace::ConfigValueType::Vector(
                                        $namespace::ConfigVectorType {
                                            max_count: Some(100),
                                            element_type: Some($namespace::ConfigVectorElementType::String(
                                                $namespace::ConfigStringType {
                                                    max_size: Some(50),
                                                    ..$namespace::ConfigStringType::EMPTY
                                                }
                                            )),
                                            ..$namespace::ConfigVectorType::EMPTY
                                        }
                                    )),
                                    ..$namespace::ConfigField::EMPTY
                                },
                                $namespace::ConfigField {
                                    key: Some("test3".to_string()),
                                    value_type: Some($namespace::ConfigValueType::Bool($namespace::ConfigBooleanType::EMPTY)),
                                    ..$namespace::ConfigField::EMPTY
                                },
                                $namespace::ConfigField {
                                    key: Some("test4".to_string()),
                                    value_type: Some($namespace::ConfigValueType::Uint8($namespace::ConfigUnsigned8Type::EMPTY)),
                                    ..$namespace::ConfigField::EMPTY
                                },
                                $namespace::ConfigField {
                                    key: Some("test5".to_string()),
                                    value_type: Some($namespace::ConfigValueType::Int8($namespace::ConfigSigned8Type::EMPTY)),
                                    ..$namespace::ConfigField::EMPTY
                                },
                                $namespace::ConfigField {
                                    key: Some("test6".to_string()),
                                    value_type: Some($namespace::ConfigValueType::Uint64($namespace::ConfigUnsigned64Type::EMPTY)),
                                    ..$namespace::ConfigField::EMPTY
                                },
                                $namespace::ConfigField {
                                    key: Some("test7".to_string()),
                                    value_type: Some($namespace::ConfigValueType::Int64($namespace::ConfigSigned64Type::EMPTY)),
                                    ..$namespace::ConfigField::EMPTY
                                },

                                $namespace::ConfigField {
                                    key: Some("test8".to_string()),
                                    value_type: Some($namespace::ConfigValueType::Vector(
                                        $namespace::ConfigVectorType {
                                            max_count: Some(100),
                                            element_type: Some($namespace::ConfigVectorElementType::Uint16($namespace::ConfigUnsigned16Type::EMPTY)),
                                            ..$namespace::ConfigVectorType::EMPTY
                                        }
                                    )),
                                    ..$namespace::ConfigField::EMPTY
                                },
                            ]),
                            declaration_checksum: Some(vec![
                                29, 216, 58, 250, 74, 84, 151, 187, 165, 124, 211, 208, 215, 241, 78, 166,
                                54, 186, 142, 201, 10, 136, 180, 16, 171, 129, 154, 142, 44, 7, 46, 146
                            ]),
                            ..$namespace::ConfigDecl::EMPTY
                        }),
                        ..$namespace::ComponentDecl::EMPTY
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
                        false,
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
                    $namespace::ComponentDecl {
                        program: Some($namespace::ProgramDecl {
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
                            ..$namespace::ProgramDecl::EMPTY
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
                    $namespace::ComponentDecl {
                        program: Some($namespace::ProgramDecl {
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
                            ..$namespace::ProgramDecl::EMPTY
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
                    $namespace::ComponentDecl {
                        program: Some($namespace::ProgramDecl {
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
                            ..$namespace::ProgramDecl::EMPTY
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
                    $namespace::ComponentDecl {
                        program: Some($namespace::ProgramDecl {
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
                            ..$namespace::ProgramDecl::EMPTY
                        }),
                        uses: Some(vec![$namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                            dependency_type: Some($namespace::DependencyType::Strong),
                            source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                            source_name: Some("foo".to_string()),
                            target_path: Some("/svc/foo".to_string()),
                            ..$namespace::UseProtocolDecl::EMPTY
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
    }
}

test_suite!(fsys_test, fsys);
test_suite!(fdecl_test, fdecl);
