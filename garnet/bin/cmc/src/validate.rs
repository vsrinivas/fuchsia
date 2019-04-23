// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml;
use cm_json::{self, Error, JsonSchema, CML_SCHEMA, CMX_SCHEMA, CM_SCHEMA};
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::Read;
use std::path::Path;

/// Read in and parse one or more manifest files. Returns an Err() if any file is not valid
/// or Ok(()) if all files are valid.
///
/// The primary JSON schemas are taken from cm_json, selected based on the file extension,
/// is used to determine the validity of each input file. Extra schemas to validate against can be
/// optionally provided.
pub fn validate<P: AsRef<Path>>(
    files: &[P],
    extra_schemas: &[(P, Option<String>)],
) -> Result<(), Error> {
    if files.is_empty() {
        return Err(Error::invalid_args("No files provided"));
    }

    for filename in files {
        validate_file(filename.as_ref(), extra_schemas)?;
    }
    Ok(())
}

/// Read in and parse .cml file. Returns a cml::Document if the file is valid, or an Error if not.
pub fn parse_cml(value: Value) -> Result<cml::Document, Error> {
    cm_json::validate_json(&value, CML_SCHEMA)?;
    let document: cml::Document = serde_json::from_value(value)
        .map_err(|e| Error::parse(format!("Couldn't read input as struct: {}", e)))?;
    let mut ctx = ValidationContext { document: &document, all_children: HashSet::new() };
    ctx.validate()?;
    Ok(document)
}

/// Read in and parse a single manifest file, and return an Error if the given file is not valid.
/// If the file is a .cml file and is valid, will return Some(cml::Document), and for other valid
/// files returns None.
///
/// Internal single manifest file validation function, used to implement the two public validate
/// functions.
fn validate_file<P: AsRef<Path>>(
    file: &Path,
    extra_schemas: &[(P, Option<String>)],
) -> Result<(), Error> {
    const BAD_EXTENSION: &str = "Input file does not have a component manifest extension \
                                 (.cm, .cml, or .cmx)";
    let mut buffer = String::new();
    File::open(&file)?.read_to_string(&mut buffer)?;

    // Validate based on file extension.
    let ext = file.extension().and_then(|e| e.to_str());
    let v = match ext {
        Some("cmx") => {
            let v = cm_json::from_json_str(&buffer)?;
            cm_json::validate_json(&v, CMX_SCHEMA)?;
            v
        }
        Some("cm") => {
            let v = cm_json::from_json_str(&buffer)?;
            cm_json::validate_json(&v, CM_SCHEMA)?;
            v
        }
        Some("cml") => {
            let v = cm_json::from_json5_str(&buffer)?;
            parse_cml(v.clone())?;
            v
        }
        _ => {
            return Err(Error::invalid_args(BAD_EXTENSION));
        }
    };

    // Validate against any extra schemas provided.
    for extra_schema in extra_schemas {
        let schema = JsonSchema::new_from_file(&extra_schema.0.as_ref())?;
        cm_json::validate_json(&v, &schema).map_err(|e| match (&e, &extra_schema.1) {
            (Error::Validate { schema_name, err }, Some(extra_msg)) => Error::Validate {
                schema_name: schema_name.clone(),
                err: format!("{}\n{}", err, extra_msg),
            },
            _ => e,
        })?;
    }
    Ok(())
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
                    return Err(Error::validate(format!(
                        "Duplicate child name: \"{}\"",
                        &child.name
                    )));
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
        let from_caps = cml::CHILD_RE.captures(&offer.from);
        let from_child = match &from_caps {
            Some(caps) => Some(&caps[0]),
            None => None,
        };
        let mut prev_targets = HashSet::new();
        for to in offer.to.iter() {
            // Check that any referenced child in the target name is valid.
            if let Some(caps) = cml::CHILD_RE.captures(&to.dest) {
                if !self.all_children.contains(&caps[1]) {
                    return Err(Error::validate(format!(
                        "\"{}\" is an \"offer\" target but it does not appear in \"children\"",
                        &to.dest,
                    )));
                }
            }

            // Check that the capability is not being re-offered to a target that exposed it.
            if let Some(from_child) = &from_child {
                if from_child == &to.dest {
                    return Err(Error::validate(format!(
                        "Offer target \"{}\" is same as source",
                        &to.dest,
                    )));
                }
            }

            // Perform common target validation.
            self.validate_target("offer", offer, to, &mut prev_targets, prev_target_paths)?;
        }
        Ok(())
    }

    /// Validates that a source capability is valid, i.e. that any referenced child is valid.
    /// - `keyword` is the keyword for the clause ("offer" or "expose").
    fn validate_source<T>(&self, keyword: &str, source_obj: &'a T) -> Result<(), Error>
    where
        T: cml::FromClause + cml::CapabilityClause,
    {
        if let Some(caps) = cml::CHILD_RE.captures(source_obj.from()) {
            if !self.all_children.contains(&caps[1]) {
                return Err(Error::validate(format!(
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
    /// - `keyword` is the keyword for the clause ("offer" or "expose").
    /// - `source_obj` is the object containing the source capability info. This is needed for the
    ///   default path.
    /// - `target_obj` is the object containing the target capability info.
    /// - `prev_target` holds target names collected for this source capability so far.
    /// - `prev_target_paths` holds target paths collected so far.
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
        U: cml::DestClause + cml::AsClause,
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
        let target_name = target_obj.dest().unwrap_or("");
        let paths_for_target =
            prev_target_paths.entry(target_name.to_string()).or_insert(HashSet::new());
        if !paths_for_target.insert(target_path) {
            return match target_name {
                "" => Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"{}\" target path",
                    target_path, keyword
                ))),
                _ => Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"{}\" target path for \"{}\"",
                    target_path, keyword, target_name
                ))),
            };
        }

        // Check that the target is not a duplicate of a previous target (for this source).
        if let Some(target_name) = target_obj.dest() {
            if !prev_targets.insert(target_name) {
                return Err(Error::validate(format!(
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
    use lazy_static::lazy_static;
    use serde_json::json;
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

        let result = validate(&vec![tmp_file_path], &[]);
        assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
    }

    #[test]
    fn test_validate_invalid_json_fails() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join("test.cm");

        File::create(&tmp_file_path).unwrap().write_all(b"{,}").unwrap();

        let result = validate(&vec![tmp_file_path], &[]);
        let expected_result: Result<(), Error> = Err(Error::parse(
            "Couldn't read input as JSON: key must be a string at line 1 column 2",
        ));
        assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
    }

    #[test]
    fn test_json5_parse_number() {
        let json: Value = cm_json::from_json5_str("1").expect("couldn't parse");
        if let Value::Number(n) = json {
            assert!(n.is_i64());
        } else {
            panic!("{:?} is not a number", json);
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
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.boot.Log"
                            }
                        },
                        "target_path": "/svc/fuchsia.logger.Log",
                    },
                    {
                        "capability": {
                            "directory": {
                               "path": "/data/assets"
                            }
                        },
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
            result = Err(Error::validate_schema(CM_SCHEMA, "This property is required at /uses/0/capability, This property is required at /uses/0/target_path")),
        },
        test_cm_uses_bad_type => {
            input = json!({
                "uses": [
                    {
                        "capability": {
                            "bad": {
                               "path": "/svc/fuchsia.logger.Log"
                            }
                        },
                        "target_path": "/svc/fuchsia.logger.Log",
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "OneOf conditions are not met at /uses/0/capability")),
        },

        // exposes
        test_cm_exposes => {
            input = json!({
                "exposes": [
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "myself": {}
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    },
                    {
                        "capability": {
                            "directory": {
                               "path": "/data/assets"
                            }
                        },
                        "source": {
                            "child": {
                                "name": "cat_viewer"
                            }
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
            result = Err(Error::validate_schema(CM_SCHEMA, "This property is required at /exposes/0/capability, This property is required at /exposes/0/source, This property is required at /exposes/0/target_path")),
        },
        test_cm_exposes_bad_type => {
            input = json!({
                "exposes": [
                    {
                        "capability": {
                            "bad": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "myself": {}
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "OneOf conditions are not met at /exposes/0/capability")),
        },

        // offers
        test_cm_offers => {
            input = json!({
                "offers": [
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.logger.LogSink"
                            }
                        },
                        "source": {
                            "realm": {}
                        },
                        "targets": [
                            {
                                "target_path": "/svc/fuchsia.logger.SysLog",
                                "child_name": "viewer"
                            }
                        ]
                    },
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "myself": {}
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
                        "capability": {
                            "directory": {
                               "path": "/data/assets"
                            }
                        },
                        "source": {
                            "child": {
                                "name": "cat_provider"
                            }
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
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.logger.LogSink"
                            }
                        },
                        "source": {
                            "child": {
                                "name": "abcdefghijklmnopqrstuvwxyz0123456789_-."
                            }
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
            result = Err(Error::validate_schema(CM_SCHEMA, "This property is required at /offers/0/capability, This property is required at /offers/0/source, This property is required at /offers/0/targets")),
        },
        test_cm_offers_bad_type => {
            input = json!({
                "offers": [
                    {
                        "capability": {
                            "bad": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "myself": {}
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
            result = Err(Error::validate_schema(CM_SCHEMA, "OneOf conditions are not met at /offers/0/capability")),
        },
        test_cm_offers_target_missing_props => {
            input = json!({
                "offers": [
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "child": {
                                "name": "cat_viewer"
                            }
                        },
                        "targets": [ {} ]
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "This property is required at /offers/0/targets/0/child_name, This property is required at /offers/0/targets/0/target_path")),
        },
        test_cm_offers_target_bad_child_name => {
            input = json!({
                "offers": [
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "myself": {}
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
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /offers/0/targets/0/child_name")),
        },

        // children
        test_cm_children => {
            input = json!({
                "children": [
                    {
                        "name": "system-logger2",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
                    },
                    {
                        "name": "abc123_-",
                        "uri": "https://www.google.com/gmail",
                        "startup": "eager"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_children_missing_props => {
            input = json!({
                "children": [ {} ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "This property is required at /children/0/name, This property is required at /children/0/startup, This property is required at /children/0/uri")),
        },
        test_cm_children_bad_name => {
            input = json!({
                "children": [
                    {
                        "name": "bad^",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /children/0/name")),
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
            result = Err(Error::validate_schema(CM_SCHEMA, "Type of the value is wrong at /facets")),
        },

        // constraints
        test_cm_path => {
            input = json!({
                "uses": [
                    {
                        "capability": {
                            "directory": {
                               "path": "/foo/?!@#$%/Bar"
                            }
                        },
                        "target_path": "/bar/&*()/Baz"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cm_path_invalid_empty => {
            input = json!({
                "uses": [
                    {
                        "capability": {
                            "directory": {
                                "path": ""
                            }
                        },
                        "target_path": "/bar"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "MinLength condition is not met at /uses/0/capability/directory/path, Pattern condition is not met at /uses/0/capability/directory/path")),
        },
        test_cm_path_invalid_root => {
            input = json!({
                "uses": [
                    {
                        "capability": {
                            "directory": {
                               "path": "/"
                            }
                        },
                        "target_path": "/bar"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /uses/0/capability/directory/path")),
        },
        test_cm_path_invalid_relative => {
            input = json!({
                "uses": [
                    {
                        "capability": {
                            "directory": {
                               "path": "foo/bar"
                            }
                        },
                        "target_path": "/bar"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /uses/0/capability/directory/path")),
        },
        test_cm_path_invalid_trailing => {
            input = json!({
                "uses": [
                    {
                        "capability": {
                            "directory": {
                               "path": "/foo/bar/"
                            }
                        },
                        "target_path": "/bar"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /uses/0/capability/directory/path")),
        },
        test_cm_path_too_long => {
            input = json!({
                "uses": [
                    {
                        "capability": {
                            "directory": {
                                "path": format!("/{}", "a".repeat(1024))
                            }
                        },
                        "target_path": "/bar"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "MaxLength condition is not met at /uses/0/capability/directory/path")),
        },
        test_cm_name => {
            input = json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
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
                        "startup": "lazy"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /children/0/name")),
        },
        test_cm_name_too_long => {
            input = json!({
                "children": [
                    {
                        "name": "a".repeat(101),
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "lazy"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "MaxLength condition is not met at /children/0/name")),
        },
        test_cm_uri => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "my+awesome-scheme.2://abc123!@#$%.com",
                        "startup": "lazy"
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
                        "uri": "fuchsia-pkg://",
                        "startup": "lazy"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /children/0/uri")),
        },
        test_cm_uri_too_long => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": &format!("fuchsia-pkg://{}", "a".repeat(4083)),
                        "startup": "lazy"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "MaxLength condition is not met at /children/0/uri")),
        },
        test_cm_relative_id_missing_variant => {
            input = json!({
                "exposes": [
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {},
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "OneOf conditions are not met at /exposes/0/source")),
        },
        test_cm_relative_id_multiple_variants => {
            input = json!({
                "exposes": [
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "realm": {},
                            "myself": {}
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "OneOf conditions are not met at /exposes/0/source")),
        },
        test_cm_relative_id_bad_child_name => {
            input = json!({
                "exposes": [
                    {
                        "capability": {
                            "service": {
                               "path": "/svc/fuchsia.ui.Scenic"
                            }
                        },
                        "source": {
                            "child": {
                                "name": "bad^"
                            }
                        },
                        "target_path": "/svc/fuchsia.ui.Scenic"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CM_SCHEMA, "Pattern condition is not met at /exposes/0/source/child/name")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "This property is required at /program/binary")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "OneOf conditions are not met at /use/0")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "OneOf conditions are not met at /expose/0, This property is required at /expose/0/from")),
        },
        test_cml_expose_missing_from => {
            input = json!({
                "expose": [
                    { "service": "/loggers/fuchsia.logger.Log", "from": "#missing" }
                ]
            }),
            result = Err(Error::validate("\"#missing\" is an \"expose\" source but it does not appear in \"children\"")),
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
            result = Err(Error::validate("\"/thing\" is a duplicate \"expose\" target path")),
        },
        test_cml_expose_bad_from => {
            input = json!({
                "expose": [ {
                    "service": "/loggers/fuchsia.logger.Log", "from": "realm"
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /expose/0/from")),
        },

        // offer
        test_cml_offer => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [
                            { "dest": "#echo_server" },
                            { "dest": "#scenic", "as": "/svc/fuchsia.logger.SysLog" }
                        ]
                    },
                    {
                        "service": "/svc/fuchsia.fonts.Provider",
                        "from": "realm",
                        "to": [
                            { "dest": "#echo_server" },
                        ]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "self",
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
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "to": [
                            {
                                "dest": "#abcdefghijklmnopqrstuvwxyz0123456789_-to",
                            },
                        ],
                    }
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "uri": "https://www.google.com/gmail"
                    },
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-to",
                        "uri": "https://www.google.com/gmail"
                    },
                ]
            }),
            result = Ok(()),
        },
        test_cml_offer_missing_props => {
            input = json!({
                "offer": [ {} ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "OneOf conditions are not met at /offer/0, This property is required at /offer/0/from, This property is required at /offer/0/to")),
        },
        test_cml_offer_missing_from => {
            input = json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#missing",
                        "to": [
                            { "dest": "#echo_server" },
                        ]
                    } ]
                }),
            result = Err(Error::validate("\"#missing\" is an \"offer\" source but it does not appear in \"children\"")),
        },
        test_cml_offer_bad_from => {
            input = json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#invalid@",
                        "to": [
                            { "dest": "#echo_server" },
                        ]
                    } ]
                }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /offer/0/from")),
        },
        test_cml_offer_empty_targets => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": []
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MinItems condition is not met at /offer/0/to")),
        },
        test_cml_offer_target_missing_props => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [
                        { "as": "/svc/fuchsia.logger.SysLog" }
                    ]
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "This property is required at /offer/0/to/0/dest")),
        },
        test_cml_offer_target_missing_to => {
            input = json!({
                "offer": [ {
                    "service": "/snvc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [
                        { "dest": "#missing" }
                    ]
                } ],
                "children": [ {
                    "name": "logger",
                    "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                } ]
            }),
            result = Err(Error::validate("\"#missing\" is an \"offer\" target but it does not appear in \"children\"")),
        },
        test_cml_offer_target_bad_to => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [
                        { "dest": "self", "as": "/svc/fuchsia.logger.SysLog" }
                    ]
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /offer/0/to/0/dest")),
        },
        test_cml_offer_target_equals_from => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [
                        { "dest": "#logger", "as": "/svc/fuchsia.logger.SysLog", },
                    ],
                } ],
                "children": [ {
                    "name": "logger", "uri": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                } ],
            }),
            result = Err(Error::validate("Offer target \"#logger\" is same as source")),
        },
        test_cml_offer_duplicate_target_paths => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/logger",
                        "from": "self",
                        "to": [
                            { "dest": "#echo_server", "as": "/thing" },
                            { "dest": "#scenic" }
                        ]
                    },
                    {
                        "directory": "/thing",
                        "from": "realm",
                        "to": [
                            { "dest": "#echo_server" }
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
            result = Err(Error::validate("\"/thing\" is a duplicate \"offer\" target path for \"#echo_server\"")),
        },

        // children
        test_cml_children => {
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
            result = Ok(()),
        },
        test_cml_children_missing_props => {
            input = json!({
                "children": [ {} ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "This property is required at /children/0/name, This property is required at /children/0/uri")),
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
            result = Err(Error::validate("Duplicate child name: \"logger\"")),
        },
        test_cml_children_bad_startup => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "uri": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "zzz",
                    },
                ],
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /children/0/startup")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "Type of the value is wrong at /facets")),
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
        test_cml_path_invalid_empty => {
            input = json!({
                "use": [
                  { "service": "" },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MinLength condition is not met at /use/0/service, Pattern condition is not met at /use/0/service")),
        },
        test_cml_path_invalid_root => {
            input = json!({
                "use": [
                  { "service": "/" },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/service")),
        },
        test_cml_path_invalid_relative => {
            input = json!({
                "use": [
                  { "service": "foo/bar" },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/service")),
        },
        test_cml_path_invalid_trailing => {
            input = json!({
                "use": [
                  { "service": "/foo/bar/" },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/service")),
        },
        test_cml_path_too_long => {
            input = json!({
                "use": [
                  { "service": format!("/{}", "a".repeat(1024)) },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MaxLength condition is not met at /use/0/service")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "MaxLength condition is not met at /expose/0/from")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /children/0/name")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "MaxLength condition is not met at /children/0/name")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /children/0/uri")),
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
            result = Err(Error::validate_schema(CML_SCHEMA, "MaxLength condition is not met at /children/0/uri")),
        },
    }

    test_validate_cmx! {
        test_cmx_err_empty_json => {
            input = json!({}),
            result = Err(Error::validate_schema(CMX_SCHEMA, "This property is required at /program")),
        },
        test_cmx_program => {
            input = json!({"program": { "binary": "bin/app" }}),
            result = Ok(()),
        },
        test_cmx_program_no_binary => {
            input = json!({ "program": {}}),
            result = Err(Error::validate_schema(CMX_SCHEMA, "OneOf conditions are not met at /program")),
        },
        test_cmx_bad_program => {
            input = json!({"prigram": { "binary": "bin/app" }}),
            result = Err(Error::validate_schema(CMX_SCHEMA, "Property conditions are not met at , \
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

    // We can't simply using JsonSchema::new here and create a temp file with the schema content
    // to pass to validate() later because the path in the errors in the expected results below
    // need to include the whole path, since that's what you get in the Error::Validate.
    lazy_static! {
        static ref BLOCK_SHELL_FEATURE_SCHEMA: JsonSchema<'static> = str_to_json_schema(
            "block_shell_feature.json",
            include_str!("../test_block_shell_feature.json")
        );
    }
    lazy_static! {
        static ref BLOCK_DEV_SCHEMA: JsonSchema<'static> =
            str_to_json_schema("block_dev.json", include_str!("../test_block_dev.json"));
    }

    fn str_to_json_schema<'a, 'b>(name: &'a str, content: &'a str) -> JsonSchema<'b> {
        lazy_static! {
            static ref TEMPDIR: TempDir = TempDir::new().unwrap();
        }

        let tmp_path = TEMPDIR.path().join(name);
        File::create(&tmp_path).unwrap().write_all(content.as_bytes()).unwrap();
        JsonSchema::new_from_file(&tmp_path).unwrap()
    }

    macro_rules! test_validate_extra_schemas {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    extra_schemas = $extra_schemas:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() -> Result<(), Error> {
                    validate_extra_schemas_test($input, $extra_schemas, $result)
                }
            )+
        }
    }

    fn validate_extra_schemas_test(
        input: serde_json::value::Value,
        extra_schemas: &[(&JsonSchema, Option<String>)],
        expected_result: Result<(), Error>,
    ) -> Result<(), Error> {
        let input_str = format!("{}", input);
        let tmp_dir = TempDir::new()?;
        let tmp_cmx_path = tmp_dir.path().join("test.cmx");
        File::create(&tmp_cmx_path)?.write_all(input_str.as_bytes())?;

        let extra_schema_paths =
            extra_schemas.iter().map(|i| (Path::new(&*i.0.name), i.1.clone())).collect::<Vec<_>>();
        let result = validate(&[tmp_cmx_path.as_path()], &extra_schema_paths);
        assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
        Ok(())
    }

    test_validate_extra_schemas! {
        test_validate_extra_schemas_empty_json => {
            input = json!({"program": {"binary": "a"}}),
            extra_schemas = &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            result = Ok(()),
        },
        test_validate_extra_schemas_empty_features => {
            input = json!({"sandbox": {"features": []}, "program": {"binary": "a"}}),
            extra_schemas = &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            result = Ok(()),
        },
        test_validate_extra_schemas_feature_not_present => {
            input = json!({"sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            extra_schemas = &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            result = Ok(()),
        },
        test_validate_extra_schemas_feature_present => {
            input = json!({"sandbox": {"features" : ["shell"]}, "program": {"binary": "a"}}),
            extra_schemas = &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            result = Err(Error::validate_schema(&BLOCK_SHELL_FEATURE_SCHEMA, "Not condition is not met at /sandbox/features/0")),
        },
        test_validate_extra_schemas_block_dev => {
            input = json!({"dev": ["misc"], "program": {"binary": "a"}}),
            extra_schemas = &[(&BLOCK_DEV_SCHEMA, None)],
            result = Err(Error::validate_schema(&BLOCK_DEV_SCHEMA, "Not condition is not met at /dev")),
        },
        test_validate_multiple_extra_schemas_valid => {
            input = json!({"sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            extra_schemas = &[(&BLOCK_SHELL_FEATURE_SCHEMA, None), (&BLOCK_DEV_SCHEMA, None)],
            result = Ok(()),
        },
        test_validate_multiple_extra_schemas_invalid => {
            input = json!({"dev": ["misc"], "sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            extra_schemas = &[(&BLOCK_SHELL_FEATURE_SCHEMA, None), (&BLOCK_DEV_SCHEMA, None)],
            result = Err(Error::validate_schema(&BLOCK_DEV_SCHEMA, "Not condition is not met at /dev")),
        },
    }

    #[test]
    fn test_validate_extra_error() -> Result<(), Error> {
        validate_extra_schemas_test(
            json!({"dev": ["misc"], "program": {"binary": "a"}}),
            &[(&BLOCK_DEV_SCHEMA, Some("Extra error".to_string()))],
            Err(Error::validate_schema(
                &BLOCK_DEV_SCHEMA,
                "Not condition is not met at /dev\nExtra error",
            )),
        )
    }
}
