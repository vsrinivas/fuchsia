// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{self, Error, JsonSchemaStr, CML_SCHEMA, CMX_SCHEMA, CM_SCHEMA};
use serde_json::Value;
use std::fs;
use std::io::Read;
use std::path::PathBuf;
use valico::json_schema;

/// Read in and parse a list of files, and return an Error if any of the given files are not valid
/// cmx. One of the JSON schemas located at ../*_schema.json, selected based on the file extension,
/// is used to determine the validity of each input file.
pub fn validate(files: Vec<PathBuf>) -> Result<(), Error> {
    const BAD_EXTENSION: &str = "Input file does not have a component manifest extension \
                                 (.cm, .cml, or .cmx)";
    if files.is_empty() {
        return Err(Error::invalid_args("No files provided"));
    }

    for filename in files {
        let mut buffer = String::new();
        fs::File::open(&filename)?.read_to_string(&mut buffer)?;
        match filename.extension().and_then(|e| e.to_str()) {
            Some("cm") => {
                let v = common::from_json_str(&buffer)?;
                validate_json(&v, CM_SCHEMA)
            }
            Some("cml") => {
                let v = common::from_json5_str(&buffer)?;
                validate_cml(&v)
            }
            Some("cmx") => {
                let v = common::from_json_str(&buffer)?;
                validate_json(&v, CMX_SCHEMA)
            }
            _ => Err(Error::invalid_args(BAD_EXTENSION)),
        }?;
    }
    Ok(())
}

/// Validate a JSON document according to the given schema.
pub fn validate_json(json: &Value, schema: JsonSchemaStr) -> Result<(), Error> {
    // Parse the schema
    let cmx_schema_json = serde_json::from_str(schema)
        .map_err(|e| Error::internal(format!("Couldn't read schema as JSON: {}", e)))?;
    let mut scope = json_schema::Scope::new();
    let schema = scope
        .compile_and_return(cmx_schema_json, false)
        .map_err(|e| Error::internal(format!("Couldn't parse schema: {:?}", e)))?;

    // Validate the json
    let res = schema.validate(json);
    if !res.is_strictly_valid() {
        let mut err_msgs = Vec::new();
        for e in &res.errors {
            err_msgs.push(format!("{} at {}", e.get_title(), e.get_path()).into_boxed_str());
        }
        // The ordering in which valico emits these errors is unstable.
        // Sort error messages so that the resulting message is predictable.
        err_msgs.sort_unstable();
        return Err(Error::parse(err_msgs.join(", ")));
    }
    Ok(())
}

/// Validates CML JSON document according to the schema.
/// TODO: Perform extra validation beyond what the schema provides.
pub fn validate_cml(json: &Value) -> Result<(), Error> {
    validate_json(&json, CML_SCHEMA)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::fs::File;
    use std::io::Write;
    use tempfile::TempDir;

    macro_rules! test_validate_cm {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_test("test.cm", $input, $result);
                }
            )+
        }
    }

    macro_rules! test_validate_cml {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_test("test.cml", $input, $result);
                }
            )+
        }
    }

    macro_rules! test_validate_cmx {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_test("test.cmx", $input, $result);
                }
            )+
        }
    }

    fn validate_test(
        filename: &str, input: serde_json::value::Value, expected_result: Result<(), Error>,
    ) {
        let input_str = format!("{}", input);
        validate_json_str(filename, &input_str, expected_result);
    }

    fn validate_json_str(filename: &str, input: &str, expected_result: Result<(), Error>) {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join(filename);

        File::create(&tmp_file_path)
            .unwrap()
            .write_all(input.as_bytes())
            .unwrap();

        let result = validate(vec![tmp_file_path]);
        assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
    }

    // TODO: Consider converting these tests to a golden test

    test_validate_cm! {
        // program
        test_cm_empty_json => {
            input = json!({}),
            result = Ok(()),
        },
        test_cm_program => {
            input = json!({"program": { "foo": 55 }}),
            result = Ok(()),
        },

        // uses
        test_cm_uses => {
            input = json!({
                "uses": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.boot.Log",
                        "target_path": "/svc/fuchsia.logger.Log",
                    },
                    {
                        "type": "directory",
                        "source_path": "/data/assets",
                        "target_path": "/data/kitten_assets",
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_uses_missing_props => {
            input = json!({
                "uses": [ {} ]
            }),
            result = Err(Error::parse("This property is required at /uses/0/source_path, This property is required at /uses/0/target_path, This property is required at /uses/0/type")),
        },
        test_cm_uses_bad_type => {
            input = json!({
                "uses": [
                    {
                        "type": "bad",
                        "source_path": "/svc/fuchsia.logger.Log",
                        "target_path": "/svc/fuchsia.logger.Log",
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /uses/0/type")),
        },

        // exposes
        test_cm_exposes => {
            input = json!({
                "exposes": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "self"
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    },
                    {
                        "type": "directory",
                        "source_path": "/data/assets",
                        "source": {
                            "relation": "child",
                            "child_name": "cat_viewer"
                        },
                        "target_path": "/data/kitten_assets"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_exposes_all_valid_chars => {
            input = json!({
                "exposes": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "child",
                            "child_name": "ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    },
                ]
            }),
            result = Ok(()),
        },
        test_cm_exposes_missing_props => {
            input = json!({
                "exposes": [ {} ]
            }),
            result = Err(Error::parse("This property is required at /exposes/0/source, This property is required at /exposes/0/source_path, This property is required at /exposes/0/target_path, This property is required at /exposes/0/type")),
        },
        test_cm_exposes_bad_type => {
            input = json!({
                "exposes": [
                    {
                        "type": "bad",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "self"
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /exposes/0/type")),
        },
        test_cm_exposes_source_missing_props => {
            input = json!({
                "exposes": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {},
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::parse("This property is required at /exposes/0/source/relation")),
        },
        test_cm_exposes_source_extraneous_child => {
            input = json!({
                "exposes": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": { "relation": "self", "child_name": "scenic" },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::parse("OneOf conditions are not met at /exposes/0/source")),
        },
        test_cm_exposes_source_missing_child => {
            input = json!({
                "exposes": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": { "relation": "child" },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::parse("OneOf conditions are not met at /exposes/0/source")),
        },
        test_cm_exposes_source_bad_relation => {
            input = json!({
                "exposes": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "realm"
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /exposes/0/source/relation")),
        },
        test_cm_exposes_source_bad_child_name => {
            input = json!({
                "exposes": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "child",
                            "child_name": "bad^"
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /exposes/0/source/child_name")),
        },

        // offers
        test_cm_offers => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.logger.LogSink",
                        "source": {
                            "relation": "realm"
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.logger.SysLog",
                                "child_name": "viewer"
                            }
                        ]
                    },
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "self"
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "user_shell"
                            },
                            {
                                "target_path": "/services/fuchsia.ui.Scenic",
                                "child_name": "viewer"
                            }
                        ]
                    },
                    {
                        "type": "directory",
                        "source_path": "/data/assets",
                        "source": {
                            "relation": "child",
                            "child_name": "cat_provider"
                        },
                        "targets": [
                            {
                                "target_path": "/data/kitten_assets",
                                "child_name": "cat_viewer"
                            }
                        ]
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_offers_all_valid_chars => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.logger.LogSink",
                        "source": {
                            "relation": "child",
                            "child_name": "ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.logger.SysLog",
                                "child_name": "ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
                            }
                        ]
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_offers_missing_props => {
            input = json!({
                "offers": [ {} ]
            }),
            result = Err(Error::parse("This property is required at /offers/0/source, This property is required at /offers/0/source_path, This property is required at /offers/0/targets, This property is required at /offers/0/type")),
        },
        test_cm_offers_bad_type => {
            input = json!({
                "offers": [
                    {
                        "type": "bad",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "self"
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "user_shell"
                            }
                        ]
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /offers/0/type")),
        },
        test_cm_offers_source_missing_props => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {},
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "user_shell"
                            }
                        ]
                    }
                ]
            }),
            result = Err(Error::parse("This property is required at /offers/0/source/relation")),
        },
        test_cm_offers_source_extraneous_child => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": { "relation": "self", "child_name": "scenic" },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "user_shell"
                            }
                        ]
                    }
                ]
            }),
            result = Err(Error::parse("OneOf conditions are not met at /offers/0/source")),
        },
        test_cm_offers_source_missing_child => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": { "relation": "child" },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "user_shell"
                            }
                        ]
                    }
                ]
            }),
            result = Err(Error::parse("OneOf conditions are not met at /offers/0/source")),
        },
        test_cm_offers_source_bad_relation => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "bad"
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "user_shell"
                            }
                        ]
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /offers/0/source/relation")),
        },
        test_cm_offers_source_bad_child_name => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "child",
                            "child_name": "bad^"
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "user_shell"
                            }
                        ]
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /offers/0/source/child_name")),
        },
        test_cm_offers_target_missing_props => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "child",
                            "child_name": "cat_viewer"
                        },
                        "targets": [ {} ]
                    }
                ]
            }),
            result = Err(Error::parse("This property is required at /offers/0/targets/0/child_name, This property is required at /offers/0/targets/0/target_path")),
        },
        test_cm_offers_target_bad_child_name => {
            input = json!({
                "offers": [
                    {
                        "type": "service",
                        "source_path": "/svc/fuchsia.ui.Scenic",
                        "source": {
                            "relation": "self"
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.ui.Scenic",
                                "child_name": "bad^"
                            }
                        ]
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /offers/0/targets/0/child_name")),
        },

        // children
        test_cm_children => {
            input = json!({
                "children": [
                    {
                        "name": "System-logger2",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "ABCabc123_-",
                        "uri": "https://www.google.com/gmail"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_children_all_valid_chars => {
            input = json!({
                "children": [
                    {
                        "name": "ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-",
                        "uri": "https://www.google.com/gmail"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_children_missing_props => {
            input = json!({
                "children": [ {} ]
            }),
            result = Err(Error::parse("This property is required at /children/0/name, This property is required at /children/0/uri")),
        },
        test_cm_children_bad_name => {
            input = json!({
                "children": [
                    {
                        "name": "bad^",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /children/0/name")),
        },

        // facets
        test_cm_facets => {
            input = json!({
                "facets": {
                    "metadata": {
                        "title": "foo",
                        "authors": [ "me", "you" ],
                        "year": 2018
                    }
                }
            }),
            result = Ok(()),
        },
        test_cm_facets_wrong_type => {
            input = json!({
                "facets": 55
            }),
            result = Err(Error::parse("Type of the value is wrong at /facets")),
        },
    }

    #[test]
    fn test_cml_json5() {
        let input = r##"{
            "expose": [
                // Here are some services to expose.
                { "service": "/loggers/fuchsia.logger.Log", "from": "#logger", },
                { "directory": "/volumes/blobfs", "from": "self", },
            ],
            "children": [
                {
                    'name': 'logger',
                    'uri': 'fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm',
                },
            ],
        }"##;
        validate_json_str("test.cml", input, Ok(()));
    }

    test_validate_cml! {
        // program
        test_cml_empty_json => {
            input = json!({}),
            result = Ok(()),
        },
        test_cml_program => {
            input = json!({"program": { "binary": "bin/app" }}),
            result = Ok(()),
        },
        test_cml_program_no_binary => {
            input = json!({"program": {}}),
            result = Err(Error::parse("This property is required at /program/binary")),
        },

        // use
        test_cml_use => {
            input = json!({
                "use": [
                  { "service": "/fonts/CoolFonts", "as": "/svc/fuchsia.fonts.Provider" },
                  { "directory": "/data/assets" }
                ]
            }),
            result = Ok(()),
        },
        test_cml_use_missing_props => {
            input = json!({
                "use": [ { "as": "/svc/fuchsia.logger.Log" } ]
            }),
            result = Err(Error::parse("OneOf conditions are not met at /use/0")),
        },

        // expose
        test_cml_expose => {
            input = json!({
                "expose": [
                    { "service": "/loggers/fuchsia.logger.Log", "from": "#logger" },
                    { "directory": "/volumes/blobfs", "from": "self" }
                ],
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_expose_all_valid_chars => {
            input = json!({
                "expose": [
                    { "service": "/loggers/fuchsia.logger.Log", "from": "#ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-" }
                ],
                "children": [
                    {
                        "name": "ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-",
                        "uri": "https://www.google.com/gmail"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_expose_missing_props => {
            input = json!({
                "expose": [ {} ]
            }),
            result = Err(Error::parse("OneOf conditions are not met at /expose/0, This property is required at /expose/0/from")),
        },
        test_cml_expose_bad_from => {
            input = json!({
                "expose": [ {
                    "service": "/loggers/fuchsia.logger.Log", "from": "realm"
                } ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /expose/0/from")),
        },

        // offer
        test_cml_offer => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "targets": [
                            { "to": "#echo2_server" },
                            { "to": "#scenic", "as": "/svc/fuchsia.logger.SysLog" }
                        ]
                    },
                    {
                        "service": "/svc/fuchsia.fonts.Provider",
                        "from": "realm",
                        "targets": [
                            { "to": "#echo2_server" },
                        ]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "self",
                        "targets": [
                            { "to": "#echo2_server" },
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "scenic",
                        "uri": "fuchsia-pkg://fuchsia.com/scenic/stable#meta/scenic.cm"
                    },
                    {
                        "name": "echo2_server",
                        "uri": "fuchsia-pkg://fuchsia.com/echo2/stable#meta/echo2_server.cm"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_offer_all_valid_chars => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-",
                        "targets": [
                            {
                                "to": "#ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "ABCDEFGHIJILMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-",
                        "uri": "https://www.google.com/gmail"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_offer_missing_props => {
            input = json!({
                "offer": [ {} ]
            }),
            result = Err(Error::parse("OneOf conditions are not met at /offer/0, This property is required at /offer/0/from, This property is required at /offer/0/targets")),
        },
        test_cml_offer_bad_from => {
            input = json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#invalid@",
                        "targets": [
                            { "to": "#echo2_server" },
                        ]
                    } ]
                }),
            result = Err(Error::parse("Pattern condition is not met at /offer/0/from")),
        },
        test_cml_offer_empty_targets => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "targets": []
                } ]
            }),
            result = Err(Error::parse("MinItems condition is not met at /offer/0/targets")),
        },
        test_cml_offer_target_missing_props => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "targets": [
                        { "as": "/svc/fuchsia.logger.SysLog" }
                    ]
                } ]
            }),
            result = Err(Error::parse("This property is required at /offer/0/targets/0/to")),
        },
        test_cml_offer_target_bad_to => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "targets": [
                        { "to": "self", "as": "/svc/fuchsia.logger.SysLog" }
                    ]
                } ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /offer/0/targets/0/to")),
        },

        // children
        test_cml_children => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "gmail",
                        "uri": "https://www.google.com/gmail"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_children_missing_props => {
            input = json!({
                "children": [ {} ]
            }),
            result = Err(Error::parse("This property is required at /children/0/name, This property is required at /children/0/uri")),
        },
        test_cml_children_bad_name => {
            input = json!({
                "children": [
                    {
                        "name": "#bad",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /children/0/name")),
        },

        // facets
        test_cml_facets => {
            input = json!({
                "facets": {
                    "metadata": {
                        "title": "foo",
                        "authors": [ "me", "you" ],
                        "year": 2018
                    }
                }
            }),
            result = Ok(()),
        },
        test_cml_facets_wrong_type => {
            input = json!({
                "facets": 55
            }),
            result = Err(Error::parse("Type of the value is wrong at /facets")),
        },
    }

    test_validate_cmx! {
        test_cmx_err_empty_json => {
            input = json!({}),
            result = Err(Error::parse("This property is required at /program")),
        },
        test_cmx_program => {
            input = json!({"program": { "binary": "bin/app" }}),
            result = Ok(()),
        },
        test_cmx_program_no_binary => {
            input = json!({ "program": {}}),
            result = Err(Error::parse("OneOf conditions are not met at /program")),
        },
        test_cmx_bad_program => {
            input = json!({"prigram": { "binary": "bin/app" }}),
            result = Err(Error::parse("Property conditions are not met at , \
                                       This property is required at /program")),
        },
        test_cmx_sandbox => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": { "dev": [ "class/camera" ] }
            }),
            result = Ok(()),
        },
        test_cmx_facets => {
            input = json!({
                "program": { "binary": "bin/app" },
                "facets": {
                    "fuchsia.test": {
                         "system-services": [ "fuchsia.logger.LogSink" ]
                    }
                }
            }),
            result = Ok(()),
        },
    }
}
