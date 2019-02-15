// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml;
use cm_json::{self, Error, CML_SCHEMA, CMX_SCHEMA, CM_SCHEMA};
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use std::fs;
use std::io::Read;
use std::path::PathBuf;

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
                let v: Value = serde_json::from_str(&buffer)
                    .map_err(|e| Error::parse(format!("Couldn't read input as JSON: {}", e)))?;
                cm_json::validate_json(&v, CM_SCHEMA)
            }
            Some("cml") => validate_cml(&buffer).map(|_d| ()),
            Some("cmx") => {
                let v: Value = serde_json::from_str(&buffer)
                    .map_err(|e| Error::parse(format!("Couldn't read input as JSON: {}", e)))?;
                cm_json::validate_json(&v, CMX_SCHEMA)
            }
            _ => Err(Error::invalid_args(BAD_EXTENSION)),
        }?;
    }
    Ok(())
}

/// Validates CML JSON document according to the schema and returns it as a cml::Document.
pub fn validate_cml(buffer: &str) -> Result<cml::Document, Error> {
    let json: Value = cm_json::from_json5_str(&buffer)?;
    cm_json::validate_json(&json, CML_SCHEMA)?;
    let document: cml::Document = serde_json::from_value(json)
        .map_err(|e| Error::parse(format!("Couldn't read input as struct: {}", e)))?;

    let mut ctx = ValidationContext { document: &document, all_children: HashSet::new() };
    ctx.validate()?;
    Ok(document)
}

struct ValidationContext<'a> {
    document: &'a cml::Document,
    all_children: HashSet<&'a str>,
}

type PathMap<'a> = HashMap<String, HashSet<&'a str>>;

impl<'a> ValidationContext<'a> {
    fn validate(&mut self) -> Result<(), Error> {
        // Get the set of all children.
        if let Some(children) = self.document.children.as_ref() {
            for child in children.iter() {
                if !self.all_children.insert(&child.name) {
                    return Err(Error::parse(format!("Duplicate child name: \"{}\"", &child.name)));
                }
            }
        }

        // Validate "expose".
        if let Some(exposes) = self.document.expose.as_ref() {
            let mut target_paths = HashMap::new();
            for expose in exposes.iter() {
                self.validate_expose(&expose, &mut target_paths)?;
            }
        }

        // Validate "offer".
        if let Some(offers) = self.document.offer.as_ref() {
            let mut target_paths = HashMap::new();
            for offer in offers.iter() {
                self.validate_offer(&offer, &mut target_paths)?;
            }
        }

        Ok(())
    }

    fn validate_expose(
        &self,
        expose: &'a cml::Expose,
        prev_target_paths: &mut PathMap<'a>,
    ) -> Result<(), Error> {
        self.validate_source("expose", expose)?;
        self.validate_target("expose", expose, expose, &mut HashSet::new(), prev_target_paths)
    }

    fn validate_offer(
        &self,
        offer: &'a cml::Offer,
        prev_target_paths: &mut PathMap<'a>,
    ) -> Result<(), Error> {
        self.validate_source("offer", offer)?;

        let mut prev_targets = HashSet::new();
        for target in offer.targets.iter() {
            // Check that any referenced child in the target name is valid.
            if let Some(caps) = cml::CHILD_RE.captures(&target.to) {
                if !self.all_children.contains(&caps[1]) {
                    return Err(Error::parse(format!(
                        "\"{}\" is an \"offer\" target but it does not appear in \"children\"",
                        &target.to,
                    )));
                }
            }
            self.validate_target("offer", offer, target, &mut prev_targets, prev_target_paths)?;
        }
        Ok(())
    }

    /// Validates that a source capability is valid, i.e. that any referenced child is valid.
    /// - |keyword| is the keyword for the clause ("offer" or "expose").
    fn validate_source<T>(&self, keyword: &str, source_obj: &'a T) -> Result<(), Error>
    where
        T: cml::FromClause + cml::CapabilityClause,
    {
        if let Some(caps) = cml::CHILD_RE.captures(source_obj.from()) {
            if !self.all_children.contains(&caps[1]) {
                return Err(Error::parse(format!(
                    "\"{}\" is an \"{}\" source but it does not appear in \"children\"",
                    source_obj.from(),
                    keyword,
                )));
            }
        }
        Ok(())
    }

    /// Validates that a target is valid, i.e. that it does not duplicate the path of any capability
    /// and any referenced child is valid.
    /// - |keyword| is the keyword for the clause ("offer" or "expose").
    /// - |source_obj| is the object containing the source capability info. This is needed for the
    ///   default path.
    /// - |target_obj| is the object containing the target capability info.
    /// - |prev_target| holds target names collected for this source capability so far.
    /// - |prev_target_paths| holds target paths collected so far.
    fn validate_target<T, U>(
        &self,
        keyword: &str,
        source_obj: &'a T,
        target_obj: &'a U,
        prev_targets: &mut HashSet<&'a str>,
        prev_target_paths: &mut PathMap<'a>,
    ) -> Result<(), Error>
    where
        T: cml::CapabilityClause,
        U: cml::ToClause + cml::AsClause,
    {
        // Get the source capability's path.
        let source_path = if let Some(p) = source_obj.service().as_ref() {
            p
        } else if let Some(p) = source_obj.directory().as_ref() {
            p
        } else {
            return Err(Error::internal(format!("no capability path")));
        };

        // Get the target capability's path (defaults to the source path).
        let ref target_path = match &target_obj.r#as() {
            Some(a) => a,
            None => source_path,
        };

        // Check that target path is not a duplicate of another capability.
        let target_name = target_obj.to().unwrap_or("");
        let paths_for_target =
            prev_target_paths.entry(target_name.to_string()).or_insert(HashSet::new());
        if !paths_for_target.insert(target_path) {
            return match target_name {
                "" => Err(Error::parse(format!(
                    "\"{}\" is a duplicate \"{}\" target path",
                    target_path, keyword
                ))),
                _ => Err(Error::parse(format!(
                    "\"{}\" is a duplicate \"{}\" target path for \"{}\"",
                    target_path, keyword, target_name
                ))),
            };
        }

        // Check that the target is not a duplicate of a previous target (for this source).
        if let Some(target_name) = target_obj.to() {
            if !prev_targets.insert(target_name) {
                return Err(Error::parse(format!(
                    "\"{}\" is a duplicate \"{}\" target for \"{}\"",
                    target_name, keyword, source_path
                )));
            }
        }

        Ok(())
    }
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
        filename: &str,
        input: serde_json::value::Value,
        expected_result: Result<(), Error>,
    ) {
        let input_str = format!("{}", input);
        validate_json_str(filename, &input_str, expected_result);
    }

    fn validate_json_str(filename: &str, input: &str, expected_result: Result<(), Error>) {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join(filename);

        File::create(&tmp_file_path).unwrap().write_all(input.as_bytes()).unwrap();

        let result = validate(vec![tmp_file_path]);
        assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
    }

    #[test]
    fn test_validate_invalid_json_fails() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join("test.cm");

        File::create(&tmp_file_path).unwrap().write_all(b"{,}").unwrap();

        let result = validate(vec![tmp_file_path]);
        let expected_result: Result<(), Error> = Err(Error::parse(
            "Couldn't read input as JSON: key must be a string at line 1 column 2",
        ));
        assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
    }

    // TODO(CF-167): fix JSON5 int->float parse bug
    // TODO(viktard): re-enable after json5 0.2.4 merged
    #[ignore]
    #[test]
    fn test_json5_parse_number() {
        let json: Value = cm_json::from_json5_str("1").expect("couldn't parse");
        if let Value::Number(n) = json {
            // This should be assert!(n.is_i64()), but the json5 parser has a bug that parses all
            // numbers as floats.
            assert!(!n.is_i64() && !n.is_u64() && n.is_f64());
        } else {
            panic!("not a number");
        }
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
                            "child_name": "abcdefghijklmnopqrstuvwxyz0123456789_-."
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.logger.SysLog",
                                "child_name": "abcdefghijklmnopqrstuvwxyz0123456789_-."
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
                        "name": "system-logger2",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "abc123_-",
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

        // constraints
        test_cm_path => {
            input = json!({
                "uses": [
                    {
                        "type": "directory",
                        "source_path": "/foo/?!@#$%/Bar",
                        "target_path": "/bar/&*()/Baz"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_path_invalid => {
            input = json!({
                "uses": [
                    {
                        "type": "directory",
                        "source_path": "foo/",
                        "target_path": "/bar"
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /uses/0/source_path")),
        },
        test_cm_path_too_long => {
            input = json!({
                "uses": [
                    {
                        "type": "directory",
                        "source_path": "/".repeat(1025),
                        "target_path": "/bar"
                    }
                ]
            }),
            result = Err(Error::parse("MaxLength condition is not met at /uses/0/source_path")),
        },
        test_cm_name => {
            input = json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_name_invalid => {
            input = json!({
                "children": [
                    {
                        "name": "#bad",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /children/0/name")),
        },
        test_cm_name_too_long => {
            input = json!({
                "children": [
                    {
                        "name": "a".repeat(101),
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            result = Err(Error::parse("MaxLength condition is not met at /children/0/name")),
        },
        test_cm_uri => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "my+awesome-scheme.2://abc123!@#$%.com"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_uri_invalid => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://"
                    }
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /children/0/uri")),
        },
        test_cm_uri_too_long => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": &format!("fuchsia-pkg://{}", "a".repeat(4083))
                    }
                ]
            }),
            result = Err(Error::parse("MaxLength condition is not met at /children/0/uri")),
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
                    {
                        "service": "/loggers/fuchsia.logger.Log",
                        "from": "#logger",
                        "as": "/svc/logger"
                    },
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
                    { "service": "/loggers/fuchsia.logger.Log", "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-." }
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
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
        test_cml_expose_missing_from => {
            input = json!({
                "expose": [
                    { "service": "/loggers/fuchsia.logger.Log", "from": "#missing" }
                ]
            }),
            result = Err(Error::parse("\"#missing\" is an \"expose\" source but it does not appear in \"children\"")),
        },
        test_cml_expose_duplicate_target_paths => {
            input = json!({
                "expose": [
                    { "service": "/fonts/CoolFonts", "from": "self" },
                    { "service": "/svc/logger", "from": "#logger", "as": "/thing" },
                    { "directory": "/thing", "from": "self" }
                ],
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            result = Err(Error::parse("\"/thing\" is a duplicate \"expose\" target path")),
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
                            { "to": "#echo_server" },
                            { "to": "#scenic", "as": "/svc/fuchsia.logger.SysLog" }
                        ]
                    },
                    {
                        "service": "/svc/fuchsia.fonts.Provider",
                        "from": "realm",
                        "targets": [
                            { "to": "#echo_server" },
                        ]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "self",
                        "targets": [
                            { "to": "#echo_server" },
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
                        "name": "echo_server",
                        "uri": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
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
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-",
                        "targets": [
                            {
                                "to": "#abcdefghijklmnopqrstuvwxyz0123456789_-"
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-",
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
        test_cml_offer_missing_from => {
            input = json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#missing",
                        "targets": [
                            { "to": "#echo_server" },
                        ]
                    } ]
                }),
            result = Err(Error::parse("\"#missing\" is an \"offer\" source but it does not appear in \"children\"")),
        },
        test_cml_offer_bad_from => {
            input = json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#invalid@",
                        "targets": [
                            { "to": "#echo_server" },
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
        test_cml_offer_target_missing_to => {
            input = json!({
                "offer": [ {
                    "service": "/snvc/fuchsia.logger.Log",
                    "from": "#logger",
                    "targets": [
                        { "to": "#missing" }
                    ]
                } ],
                "children": [ {
                    "name": "logger",
                    "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                } ]
            }),
            result = Err(Error::parse("\"#missing\" is an \"offer\" target but it does not appear in \"children\"")),
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
        test_cml_offer_duplicate_target_paths => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/logger",
                        "from": "self",
                        "targets": [
                            { "to": "#echo_server", "as": "/thing" },
                            { "to": "#scenic" }
                        ]
                    },
                    {
                        "directory": "/thing",
                        "from": "realm",
                        "targets": [
                            { "to": "#echo_server" }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "scenic",
                        "uri": "fuchsia-pkg://fuchsia.com/scenic/stable#meta/scenic.cm"
                    },
                    {
                        "name": "echo_server",
                        "uri": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            result = Err(Error::parse("\"/thing\" is a duplicate \"offer\" target path for \"#echo_server\"")),
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
        test_cml_children_duplicate_names => {
           input = json!({
               "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/beta#meta/logger.cm"
                    }
                ]
            }),
            result = Err(Error::parse("Duplicate child name: \"logger\"")),
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

        // constraints
        test_cml_path => {
            input = json!({
                "use": [
                  { "directory": "/foo/?!@#$%/Bar" },
                ]
            }),
            result = Ok(()),
        },
        test_cml_path_invalid => {
            input = json!({
                "use": [
                  { "service": "foo/" },
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /use/0/service")),
        },
        test_cml_path_too_long => {
            input = json!({
                "use": [
                  { "service": "/".repeat(1025) },
                ]
            }),
            result = Err(Error::parse("MaxLength condition is not met at /use/0/service")),
        },
        test_cml_relative_id_too_long => {
            input = json!({
                "expose": [
                    {
                        "service": "/loggers/fuchsia.logger.Log",
                        "from": &format!("#{}", "a".repeat(101)),
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            result = Err(Error::parse("MaxLength condition is not met at /expose/0/from")),
        },
        test_cml_child_name => {
            input = json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            result = Ok(()),
        },
        test_cml_child_name_invalid => {
            input = json!({
                "children": [
                    {
                        "name": "#bad",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /children/0/name")),
        },
        test_cml_child_name_too_long => {
            input = json!({
                "children": [
                    {
                        "name": "a".repeat(101),
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    }
                ]
            }),
            result = Err(Error::parse("MaxLength condition is not met at /children/0/name")),
        },
        test_cml_uri => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "my+awesome-scheme.2://abc123!@#$%.com",
                    },
                ]
            }),
            result = Ok(()),
        },
        test_cml_uri_invalid => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://",
                    },
                ]
            }),
            result = Err(Error::parse("Pattern condition is not met at /children/0/uri")),
        },
        test_cml_uri_too_long => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": &format!("fuchsia-pkg://{}", "a".repeat(4083)),
                    },
                ]
            }),
            result = Err(Error::parse("MaxLength condition is not met at /children/0/uri")),
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
