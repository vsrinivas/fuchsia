// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml::{self, CapabilityClause};
use crate::validate;
use cm_json::{self, cm, Error, CM_SCHEMA};
use serde::ser::Serialize;
use serde_json;
use serde_json::ser::{CompactFormatter, PrettyFormatter, Serializer};
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
    let json = serde_json::from_slice(&res)
        .map_err(|e| Error::parse(format!("Couldn't read output as JSON: {}", e)))?;
    cm_json::validate_json(&json, CM_SCHEMA)?;
    Ok(())
}

fn compile_cml(document: cml::Document) -> Result<cm::Document, Error> {
    let mut out = cm::Document::default();
    if let Some(program) = document.program {
        out.program = Some(program.clone());
    }
    if let Some(r#use) = document.r#use {
        out.uses = Some(translate_use(&r#use)?);
    }
    if let Some(expose) = document.expose {
        out.exposes = Some(translate_expose(&expose)?);
    }
    if let Some(offer) = document.offer {
        out.offers = Some(translate_offer(&offer)?);
    }
    if let Some(children) = document.children {
        out.children = Some(translate_children(&children)?);
    }
    if let Some(facets) = document.facets {
        out.facets = Some(facets.clone());
    }
    Ok(out)
}

fn translate_use(use_in: &Vec<cml::Use>) -> Result<Vec<cm::Use>, Error> {
    let mut out_uses = vec![];
    for use_ in use_in {
        let target_path = extract_target_path(use_, use_)?;
        let out = if let Some(p) = use_.service() {
            Ok(cm::Use::Service(cm::UseService { source_path: p.clone(), target_path }))
        } else if let Some(p) = use_.directory() {
            Ok(cm::Use::Directory(cm::UseDirectory { source_path: p.clone(), target_path }))
        } else {
            Err(Error::internal(format!("no capability")))
        }?;
        out_uses.push(out);
    }
    Ok(out_uses)
}

fn translate_expose(expose_in: &Vec<cml::Expose>) -> Result<Vec<cm::Expose>, Error> {
    let mut out_exposes = vec![];
    for expose in expose_in.iter() {
        let source = extract_expose_source(expose)?;
        let target_path = extract_target_path(expose, expose)?;
        let out = if let Some(p) = expose.service() {
            Ok(cm::Expose::Service(cm::ExposeService {
                source,
                source_path: p.clone(),
                target_path,
            }))
        } else if let Some(p) = expose.directory() {
            Ok(cm::Expose::Directory(cm::ExposeDirectory {
                source,
                source_path: p.clone(),
                target_path,
            }))
        } else {
            Err(Error::internal(format!("no capability")))
        }?;
        out_exposes.push(out);
    }
    Ok(out_exposes)
}

fn translate_offer(offer_in: &Vec<cml::Offer>) -> Result<Vec<cm::Offer>, Error> {
    let mut out_offers = vec![];
    for offer in offer_in.iter() {
        let source = extract_offer_source(offer)?;
        let targets = extract_targets(offer)?;
        let out = if let Some(p) = offer.service() {
            Ok(cm::Offer::Service(cm::OfferService { source_path: p.clone(), source, targets }))
        } else if let Some(p) = offer.directory() {
            Ok(cm::Offer::Directory(cm::OfferDirectory { source_path: p.clone(), source, targets }))
        } else {
            Err(Error::internal(format!("no capability")))
        }?;
        out_offers.push(out);
    }
    Ok(out_offers)
}

fn translate_children(children_in: &Vec<cml::Child>) -> Result<Vec<cm::Child>, Error> {
    let mut out_children = vec![];
    for child in children_in.iter() {
        let startup = match child.startup.as_ref().map(|s| s as &str) {
            Some(cml::LAZY) | None => cm::LAZY.to_string(),
            Some(cml::EAGER) => cm::EAGER.to_string(),
            Some(_) => {
                return Err(Error::internal(format!("invalid startup")));
            }
        };
        out_children.push(cm::Child { name: child.name.clone(), uri: child.uri.clone(), startup });
    }
    Ok(out_children)
}

fn extract_expose_source<T>(in_obj: &T) -> Result<cm::ExposeSource, Error>
where
    T: cml::FromClause,
{
    let from = in_obj.from().to_string();
    if !cml::FROM_RE.is_match(&from) {
        return Err(Error::internal(format!("invalid \"from\": {}", from)));
    }
    let ret = if from.starts_with("#") {
        let (_, child_name) = from.split_at(1);
        cm::ExposeSource::Child(cm::ChildId { name: child_name.to_string() })
    } else if from == "self" {
        cm::ExposeSource::Myself(cm::SelfId {})
    } else {
        return Err(Error::internal(format!("invalid \"from\" for \"expose\": {}", from)));
    };
    Ok(ret)
}

fn extract_offer_source<T>(in_obj: &T) -> Result<cm::OfferSource, Error>
where
    T: cml::FromClause,
{
    let from = in_obj.from().to_string();
    if !cml::FROM_RE.is_match(&from) {
        return Err(Error::internal(format!("invalid \"from\": {}", from)));
    }
    let ret = if from.starts_with("#") {
        let (_, child_name) = from.split_at(1);
        cm::OfferSource::Child(cm::ChildId { name: child_name.to_string() })
    } else if from == "realm" {
        cm::OfferSource::Realm(cm::RealmId {})
    } else if from == "self" {
        cm::OfferSource::Myself(cm::SelfId {})
    } else {
        return Err(Error::internal(format!("invalid \"from\" for \"offer\": {}", from)));
    };
    Ok(ret)
}

fn extract_targets(in_obj: &cml::Offer) -> Result<Vec<cm::Target>, Error> {
    let mut out_targets = vec![];
    for to in in_obj.to.iter() {
        let target_path = extract_target_path(in_obj, to)?;
        let caps = match cml::CHILD_RE.captures(&to.dest) {
            Some(c) => Ok(c),
            None => Err(Error::internal(format!("invalid \"dest\": {}", to.dest))),
        }?;
        let child_name = caps[1].to_string();
        out_targets.push(cm::Target { target_path, child_name });
    }
    Ok(out_targets)
}

fn extract_target_path<T, U>(in_obj: &T, to_obj: &U) -> Result<String, Error>
where
    T: cml::CapabilityClause,
    U: cml::AsClause,
{
    if let Some(as_) = to_obj.r#as() {
        Ok(as_.clone())
    } else {
        if let Some(p) = in_obj.service() {
            Ok(p.clone())
        } else if let Some(p) = in_obj.directory() {
            Ok(p.clone())
        } else {
            Err(Error::internal(format!("no capability")))
        }
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
                    { "directory": "/data/assets" }
                ]
            }),
            output = r#"{
    "uses": [
        {
            "service": {
                "source_path": "/fonts/CoolFonts",
                "target_path": "/svc/fuchsia.fonts.Provider"
            }
        },
        {
            "directory": {
                "source_path": "/data/assets",
                "target_path": "/data/assets"
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
                    { "directory": "/volumes/blobfs", "from": "self" }
                ],
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
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
                "target_path": "/svc/fuchsia.logger.Log"
            }
        },
        {
            "directory": {
                "source": {
                    "myself": {}
                },
                "source_path": "/volumes/blobfs",
                "target_path": "/volumes/blobfs"
            }
        }
    ],
    "children": [
        {
            "name": "logger",
            "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
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
                        "to": [
                            { "dest": "#netstack" },
                            { "dest": "#echo_server", "as": "/svc/fuchsia.logger.SysLog" }
                        ]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "realm",
                        "to": [
                            { "dest": "#echo_server" },
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "echo_server",
                        "uri": "fuchsia-pkg://fuchsia.com/echo_server/stable#meta/echo_server.cm"
                    },
                    {
                        "name": "netstack",
                        "uri": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm"
                    }
                ]
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
                "targets": [
                    {
                        "target_path": "/svc/fuchsia.logger.Log",
                        "child_name": "netstack"
                    },
                    {
                        "target_path": "/svc/fuchsia.logger.SysLog",
                        "child_name": "echo_server"
                    }
                ]
            }
        },
        {
            "directory": {
                "source": {
                    "realm": {}
                },
                "source_path": "/data/assets",
                "targets": [
                    {
                        "target_path": "/data/assets",
                        "child_name": "echo_server"
                    }
                ]
            }
        }
    ],
    "children": [
        {
            "name": "logger",
            "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
            "startup": "lazy"
        },
        {
            "name": "echo_server",
            "uri": "fuchsia-pkg://fuchsia.com/echo_server/stable#meta/echo_server.cm",
            "startup": "lazy"
        },
        {
            "name": "netstack",
            "uri": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm",
            "startup": "lazy"
        }
    ]
}"#,
        },
        test_compile_children => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                    {
                        "name": "gmail",
                        "uri": "https://www.google.com/gmail",
                        "startup": "eager",
                    },
                    {
                        "name": "echo",
                        "uri": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm",
                        "startup": "lazy",
                    },
                ]
            }),
            output = r#"{
    "children": [
        {
            "name": "logger",
            "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
            "startup": "lazy"
        },
        {
            "name": "gmail",
            "uri": "https://www.google.com/gmail",
            "startup": "eager"
        },
        {
            "name": "echo",
            "uri": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm",
            "startup": "lazy"
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
                    "binary": "bin/app"
                },
                "use": [
                    { "service": "/fonts/CoolFonts", "as": "/svc/fuchsia.fonts.Provider" },
                ],
                "expose": [
                    { "directory": "/volumes/blobfs", "from": "self" }
                ],
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [
                            { "dest": "#netstack" }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "netstack",
                        "uri": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm"
                    }
                ],
                "facets": {
                    "author": "Fuchsia",
                    "year": 2018
                }
            }),
            output = r#"{
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
                    "myself": {}
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
                        "child_name": "netstack"
                    }
                ]
            }
        }
    ],
    "children": [
        {
            "name": "logger",
            "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
            "startup": "lazy"
        },
        {
            "name": "netstack",
            "uri": "fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm",
            "startup": "lazy"
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
                { "directory": "/data/assets" }
            ]
        });
        let output = r#"{"uses":[{"service":{"source_path":"/fonts/CoolFonts","target_path":"/svc/fuchsia.fonts.Provider"}},{"directory":{"source_path":"/data/assets","target_path":"/data/assets"}}]}"#;
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
