// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cml::{self, OneOrMany};
use cm_json::{self, Error, JsonSchema, CML_SCHEMA, CMX_SCHEMA};
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::Read;
use std::iter;
use std::path::Path;
use valico::json_schema;

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
    validate_json(&value, CML_SCHEMA)?;
    let document: cml::Document = serde_json::from_value(value)
        .map_err(|e| Error::parse(format!("Couldn't read input as struct: {}", e)))?;
    let mut ctx = ValidationContext {
        document: &document,
        all_children: HashSet::new(),
        all_collections: HashSet::new(),
        all_storage_and_sources: HashMap::new(),
    };
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
                                 (.cml or .cmx)";
    let mut buffer = String::new();
    File::open(&file)?.read_to_string(&mut buffer)?;

    // Validate based on file extension.
    let ext = file.extension().and_then(|e| e.to_str());
    let v = match ext {
        Some("cmx") => {
            let v = cm_json::from_json_str(&buffer)?;
            validate_json(&v, CMX_SCHEMA)?;
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
        validate_json(&v, &schema).map_err(|e| match (&e, &extra_schema.1) {
            (Error::Validate { schema_name, err }, Some(extra_msg)) => Error::Validate {
                schema_name: schema_name.clone(),
                err: format!("{}\n{}", err, extra_msg),
            },
            _ => e,
        })?;
    }
    Ok(())
}

/// Validates a JSON document according to the given schema.
pub fn validate_json(json: &Value, schema: &JsonSchema<'_>) -> Result<(), Error> {
    // Parse the schema
    let cmx_schema_json = serde_json::from_str(&schema.schema).map_err(|e| {
        Error::internal(format!("Couldn't read schema '{}' as JSON: {}", schema.name, e))
    })?;
    let mut scope = json_schema::Scope::new();
    let compiled_schema = scope.compile_and_return(cmx_schema_json, false).map_err(|e| {
        Error::internal(format!("Couldn't parse schema '{}': {:?}", schema.name, e))
    })?;

    // Validate the json
    let res = compiled_schema.validate(json);
    if !res.is_strictly_valid() {
        let mut err_msgs = Vec::new();
        for e in &res.errors {
            err_msgs.push(format!("{} at {}", e.get_title(), e.get_path()).into_boxed_str());
        }
        for u in &res.missing {
            err_msgs.push(
                format!("internal error: schema definition is missing URL {}", u).into_boxed_str(),
            );
        }
        // The ordering in which valico emits these errors is unstable.
        // Sort error messages so that the resulting message is predictable.
        err_msgs.sort_unstable();
        return Err(Error::validate_schema(&schema, err_msgs.join(", ")));
    }
    Ok(())
}

struct ValidationContext<'a> {
    document: &'a cml::Document,
    all_children: HashSet<&'a cml::Name>,
    all_collections: HashSet<&'a cml::Name>,
    all_storage_and_sources: HashMap<&'a cml::Name, &'a cml::Ref>,
}

/// A name/identity of a capability exposed/offered to another component.
///
/// Exposed or offered capabilities have an identifier whose format
/// depends on the capability type. For directories and services this is
/// a path, while for storage this is a storage name. Paths and storage
/// names, however, are in different conceptual namespaces, and can't
/// collide with each other.
///
/// This enum allows such names to be specified disambuating what
/// namespace they are in.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy)]
enum CapabilityId<'a> {
    Path(&'a str),
    Runner(&'a str),
    StorageType(&'a str),
}

impl<'a> CapabilityId<'a> {
    /// Return the string ID of this clause.
    pub fn as_str(&self) -> &'a str {
        match self {
            CapabilityId::Path(p) => p,
            CapabilityId::Runner(r) => r,
            CapabilityId::StorageType(s) => s,
        }
    }

    /// Human readable description of this capability type.
    pub fn type_str(&self) -> &'static str {
        match self {
            CapabilityId::Path(_) => "path",
            CapabilityId::Runner(_) => "runner",
            CapabilityId::StorageType(_) => "storage type",
        }
    }

    /// Given a CapabilityClause (Use, Offer or Expose), return the set of target identifiers.
    ///
    /// When only one capability identifier is specified, the target identifier name is derived
    /// using the "as" clause. If an "as" clause is not specified, the target identifier is the same
    /// name as the source.
    ///
    /// When multiple capability identifiers are specified, the target names are the same as the
    /// source names.
    pub fn from_clause<'b, T>(clause: &'b T) -> Result<Vec<CapabilityId<'b>>, Error>
    where
        T: cml::CapabilityClause + cml::AsClause,
    {
        // For directory/service/runner types, return the source name,
        // using the "as" clause to rename if neccessary.
        let alias = clause.r#as();
        if let Some(svc) = clause.service().as_ref() {
            return Ok(vec![CapabilityId::Path(alias.unwrap_or(svc))]);
        } else if let Some(OneOrMany::One(protocol)) = clause.protocol().as_ref() {
            return Ok(vec![CapabilityId::Path(alias.unwrap_or(protocol))]);
        } else if let Some(OneOrMany::Many(protocols)) = clause.protocol().as_ref() {
            return match (alias, protocols.len()) {
                (Some(valid_alias), 1) => Ok(vec![CapabilityId::Path(valid_alias)]),

                (Some(_), _) => Err(Error::validate(
                    "\"as\" field can only be specified when one `protocol` is supplied.",
                )),

                (None, _) => {
                    Ok(protocols.iter().map(|svc: &String| CapabilityId::Path(svc)).collect())
                }
            };
        } else if let Some(p) = clause.directory().as_ref() {
            return Ok(vec![CapabilityId::Path(alias.unwrap_or(p))]);
        } else if let Some(p) = clause.runner().as_ref() {
            return Ok(vec![CapabilityId::Runner(alias.unwrap_or(p))]);
        }

        // Offers rules prohibit using the "as" clause for storage; this is validated outside the
        // scope of this function.
        if let Some(p) = clause.storage().as_ref() {
            return Ok(vec![CapabilityId::StorageType(p)]);
        }

        // Unknown capability type.
        Err(Error::internal("unknown capability type"))
    }
}

impl<'a> ValidationContext<'a> {
    fn validate(&mut self) -> Result<(), Error> {
        // Ensure child components, collections, and storage don't use the
        // same name.
        //
        // We currently have the ability to distinguish between storage and
        // children/collections based on context, but still enforce name
        // uniqueness to give us flexibility in future.
        let all_children_names =
            self.document.all_children_names().into_iter().zip(iter::repeat("children"));
        let all_collection_names =
            self.document.all_collection_names().into_iter().zip(iter::repeat("collections"));
        let all_storage_names =
            self.document.all_storage_names().into_iter().zip(iter::repeat("storage"));
        let all_runner_names =
            self.document.all_runner_names().into_iter().zip(iter::repeat("runners"));
        let all_environment_names =
            self.document.all_environment_names().into_iter().zip(iter::repeat("environments"));
        ensure_no_duplicates(
            all_children_names
                .chain(all_collection_names)
                .chain(all_storage_names)
                .chain(all_runner_names)
                .chain(all_environment_names),
        )?;

        // Populate the sets of children and collections.
        self.all_children = self.document.all_children_names().into_iter().collect();
        self.all_collections = self.document.all_collection_names().into_iter().collect();
        self.all_storage_and_sources = self.document.all_storage_and_sources();

        // Validate "use".
        if let Some(uses) = self.document.r#use.as_ref() {
            let mut used_ids = HashSet::new();
            for use_ in uses.iter() {
                self.validate_use(&use_, &mut used_ids)?;
            }
        }

        // Validate "expose".
        if let Some(exposes) = self.document.expose.as_ref() {
            let mut used_ids = HashSet::new();
            for expose in exposes.iter() {
                self.validate_expose(&expose, &mut used_ids)?;
            }
        }

        // Validate "offer".
        if let Some(offers) = self.document.offer.as_ref() {
            let mut used_ids = HashMap::new();
            for offer in offers.iter() {
                self.validate_offer(&offer, &mut used_ids)?;
            }
        }

        // Validate "storage".
        if let Some(storage) = self.document.storage.as_ref() {
            for s in storage.iter() {
                self.validate_component_ref("\"storage\" source", &s.from)?;
            }
        }

        // Validate "runners".
        if let Some(runners) = self.document.runners.as_ref() {
            for r in runners.iter() {
                self.validate_component_ref("\"runner\" source", &r.from)?;
            }
        }

        Ok(())
    }

    fn validate_use(
        &self,
        use_: &'a cml::Use,
        used_ids: &mut HashSet<CapabilityId<'a>>,
    ) -> Result<(), Error> {
        let storage = use_.storage.as_ref().map(|s| s.as_str());
        match (storage, &use_.r#as) {
            (Some("meta"), Some(_)) => {
                Err(Error::validate("\"as\" field cannot be used with storage type \"meta\""))
            }
            _ => Ok(()),
        }?;
        match (storage, &use_.from) {
            (Some(_), Some(_)) => {
                Err(Error::validate("\"from\" field cannot be used with \"storage\""))
            }
            _ => Ok(()),
        }?;

        // Disallow multiple capability ids of the same name.
        let capability_ids = CapabilityId::from_clause(use_)?;
        for capability_id in capability_ids {
            if !used_ids.insert(capability_id) {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"use\" target {}",
                    capability_id.as_str(),
                    capability_id.type_str()
                )));
            }
        }

        // All directory "use" expressions must have directory rights.
        if use_.directory.is_some() {
            match &use_.rights {
                Some(rights) => self.validate_directory_rights(&rights)?,
                None => return Err(Error::validate("Rights required for this use statement.")),
            };
        }

        Ok(())
    }

    fn validate_expose(
        &self,
        expose: &'a cml::Expose,
        used_ids: &mut HashSet<CapabilityId<'a>>,
    ) -> Result<(), Error> {
        self.validate_component_ref("\"expose\" source", &expose.from)?;

        // Ensure directory rights are specified if exposing from self.
        if expose.directory.is_some() {
            if expose.from == cml::Ref::Self_ || expose.rights.is_some() {
                match &expose.rights {
                    Some(rights) => self.validate_directory_rights(&rights)?,
                    None => return Err(Error::validate(
                        "Rights required for this expose statement as it is exposing from self.",
                    )),
                };
            }
        }

        // Ensure we haven't already exposed an entity of the same name.
        let capability_ids = CapabilityId::from_clause(expose)?;
        for capability_id in capability_ids {
            if !used_ids.insert(capability_id) {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"expose\" target {} for \"{}\"",
                    capability_id.as_str(),
                    capability_id.type_str(),
                    expose.to.as_ref().unwrap_or(&cml::Ref::Realm)
                )));
            }
        }

        Ok(())
    }

    fn validate_offer(
        &self,
        offer: &'a cml::Offer,
        used_ids: &mut HashMap<&'a cml::Name, HashSet<CapabilityId<'a>>>,
    ) -> Result<(), Error> {
        // If offered cap is a storage type, then "from" should be interpreted
        // as a storage name. Otherwise, it should be interpreted as a child
        // or collection.
        if offer.storage.is_some() {
            self.validate_storage_ref("\"offer\" source", &offer.from)?;
        } else {
            self.validate_component_ref("\"offer\" source", &offer.from)?;
        }

        // Ensure directory rights are specified if offering from self.
        if offer.directory.is_some() {
            if offer.from == cml::Ref::Self_ || offer.rights.is_some() {
                match &offer.rights {
                    Some(rights) => self.validate_directory_rights(&rights)?,
                    None => {
                        return Err(Error::validate(
                            "Rights required for this offer as it is offering from self.",
                        ))
                    }
                };
            }
        }

        // Validate every target of this offer.
        for to in offer.to.iter() {
            // Ensure the "to" value is a child.
            let to_target = if let cml::Ref::Named(name) = to {
                name
            } else {
                return Err(Error::validate(format!("invalid \"offer\" target: \"{}\"", to)));
            };

            // Check that any referenced child actually exists.
            if !self.all_children.contains(to_target) && !self.all_collections.contains(to_target) {
                return Err(Error::validate(format!(
                    "\"{}\" is an \"offer\" target but it does not appear in \"children\" \
                     or \"collections\"",
                    to
                )));
            }

            // Storage cannot be aliased when offered. Return an error if it is used.
            if offer.storage.is_some() && offer.r#as.is_some() {
                return Err(Error::validate(
                    "\"as\" field cannot be used for storage offer targets",
                ));
            }

            // Ensure that a target is not offered more than once.
            let target_cap_ids = CapabilityId::from_clause(offer)?;
            let ids_for_entity = used_ids.entry(to_target).or_insert(HashSet::new());
            for target_cap_id in target_cap_ids {
                if !ids_for_entity.insert(target_cap_id) {
                    return Err(Error::validate(format!(
                        "\"{}\" is a duplicate \"offer\" target {} for \"{}\"",
                        target_cap_id.as_str(),
                        target_cap_id.type_str(),
                        to
                    )));
                }
            }

            // Ensure we are not offering a capability back to its source.
            if let cml::Ref::Named(name) = &offer.from {
                match offer.storage {
                    None => {
                        if name == to_target {
                            return Err(Error::validate(format!(
                                "Offer target \"{}\" is same as source",
                                to
                            )));
                        }
                    }
                    Some(_) => {
                        if let Some(cml::Ref::Named(source)) =
                            self.all_storage_and_sources.get(name)
                        {
                            if to_target == source {
                                return Err(Error::validate(format!(
                                    "Storage offer target \"{}\" is same as source",
                                    to
                                )));
                            }
                        }
                    }
                }
            }
        }

        Ok(())
    }

    /// Validates that the given component exists.
    ///
    /// - `reference_description` is a human-readable description of
    ///   the reference used in error message, such as `"offer" source`.
    /// - `component_ref` is a reference to a component. If the reference
    ///   is a named child, we ensure that the child component exists.
    fn validate_component_ref(
        &self,
        reference_description: &str,
        component_ref: &cml::Ref,
    ) -> Result<(), Error> {
        match component_ref {
            cml::Ref::Named(name) => {
                // Ensure we have a child defined by that name.
                if !self.all_children.contains(name) {
                    return Err(Error::validate(format!(
                        "{} \"{}\" does not appear in \"children\"",
                        reference_description, component_ref
                    )));
                }
                Ok(())
            }
            // We don't attempt to validate other reference types.
            _ => Ok(()),
        }
    }

    /// Validates that the given storage reference exists.
    ///
    /// - `reference_description` is a human-readable description of
    ///   the reference used in error message, such as `"storage" source`.
    /// - `storage_ref` is a reference to a storage source.
    fn validate_storage_ref(
        &self,
        reference_description: &str,
        storage_ref: &cml::Ref,
    ) -> Result<(), Error> {
        if let cml::Ref::Named(name) = storage_ref {
            if !self.all_storage_and_sources.contains_key(name) {
                return Err(Error::validate(format!(
                    "{} \"{}\" does not appear in \"storage\"",
                    reference_description, storage_ref,
                )));
            }
        }

        Ok(())
    }

    /// Validates that directory rights for all route types are valid, i.e that it does not
    /// contain duplicate rights and exists if the FromClause originates at "self".
    /// - `keyword` is the keyword for the clause ("offer", "expose", or "use").
    /// - `source_obj` is the object containing the directory.
    fn validate_directory_rights(&self, rights_clause: &Vec<String>) -> Result<(), Error> {
        // Verify all right tokens are valid.
        let mut rights = HashSet::new();
        for right_token in rights_clause.iter() {
            match cml::parse_right_token(right_token) {
                Some(rights_expanded) => {
                    for right in rights_expanded.into_iter() {
                        if !rights.insert(right) {
                            return Err(Error::validate(format!(
                                "\"{}\" is duplicated in the rights clause.",
                                right_token
                            )));
                        }
                    }
                }
                None => {
                    return Err(Error::validate(format!(
                        "\"{}\" is not a valid right token.",
                        right_token
                    )))
                }
            }
        }
        Ok(())
    }
}

/// Given an iterator with `(key, name)` tuples, ensure that `key` doesn't
/// appear twice. `name` is used in generated error messages.
fn ensure_no_duplicates<'a, I>(values: I) -> Result<(), Error>
where
    I: Iterator<Item = (&'a cml::Name, &'a str)>,
{
    let mut seen_keys = HashMap::new();
    for (key, name) in values {
        if let Some(preexisting_name) = seen_keys.insert(key, name) {
            return Err(Error::validate(format!(
                "identifier \"{}\" is defined twice, once in \"{}\" and once in \"{}\"",
                key, name, preexisting_name
            )));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use lazy_static::lazy_static;
    use serde_json::json;
    use std::io::Write;
    use tempfile::TempDir;
    use test_util::assert_matches;

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
        let tmp_file_path = tmp_dir.path().join("test.cml");

        File::create(&tmp_file_path).unwrap().write_all(b"{").unwrap();

        let err = validate(&vec![tmp_file_path], &[]).expect_err("not an err");
        let err = format!("{}", err);
        assert!(err.contains("Couldn't read input as JSON5"), "{}", err);
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

    #[test]
    fn test_cml_json5() {
        let input = r##"{
            "expose": [
                // Here are some services to expose.
                { "service": "/loggers/fuchsia.logger.Log", "from": "#logger", },
                { "directory": "/volumes/blobfs", "from": "self", "rights": ["rw*"]},
            ],
            "children": [
                {
                    'name': 'logger',
                    'url': 'fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm',
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
                  { "service": "/svc/fuchsia.sys2.Realm", "from": "framework" },
                  { "protocol": "/fonts/CoolFonts", "as": "/svc/MyFonts" },
                  { "protocol": "/svc/fuchsia.test.hub.HubReport", "from": "framework" },
                  { "protocol": ["/svc/fuchsia.ui.scenic.Scenic", "/svc/fuchsia.net.Connectivity"] },
                  {
                    "directory": "/data/assets",
                    "rights": ["rw*"],
                  },
                  {
                    "directory": "/data/config",
                    "from": "realm",
                    "rights": ["rx*"],
                    "subdir": "fonts/all",
                  },
                  { "storage": "data", "as": "/example" },
                  { "storage": "cache", "as": "/tmp" },
                  { "storage": "meta" },
                  { "runner": "elf" }
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
        test_cml_use_as_with_meta_storage => {
            input = json!({
                "use": [ { "storage": "meta", "as": "/meta" } ]
            }),
            result = Err(Error::validate("\"as\" field cannot be used with storage type \"meta\"")),
        },
        test_cml_use_as_with_runner => {
            input = json!({
                "use": [ { "runner": "elf", "as": "xxx" } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/as")),
        },
        test_cml_use_from_with_meta_storage => {
            input = json!({
                "use": [ { "storage": "cache", "from": "realm" } ]
            }),
            result = Err(Error::validate("\"from\" field cannot be used with \"storage\"")),
        },
        test_cml_use_invalid_from => {
            input = json!({
                "use": [
                  { "service": "/fonts/CoolFonts", "from": "self" }
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/from")),
        },
        test_cml_use_bad_as => {
            input = json!({
                "use": [
                    {
                        "protocol": ["/fonts/CoolFonts", "/fonts/FunkyFonts"],
                        "as": "/fonts/MyFonts"
                    }
                ]
            }),
            result = Err(Error::validate("\"as\" field can only be specified when one `protocol` is supplied.")),
        },
        test_cml_use_bad_duplicate_targets => {
            input = json!({
                "use": [
                  { "service": "/svc/fuchsia.sys2.Realm", "from": "framework" },
                  { "protocol": "/svc/fuchsia.sys2.Realm", "from": "framework" },
                ],
            }),
            result = Err(Error::validate("\"/svc/fuchsia.sys2.Realm\" is a duplicate \"use\" target path")),
        },
        test_cml_use_bad_duplicate_protocol => {
            input = json!({
                "use": [
                  { "protocol": ["/svc/fuchsia.sys2.Realm", "/svc/fuchsia.sys2.Realm"] },
                ],
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "OneOf conditions are not met at /use/0/protocol")),
        },
        test_cml_use_empty_protocols => {
            input = json!({
                "use": [
                    {
                        "protocol": [],
                    },
                ],
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "OneOf conditions are not met at /use/0/protocol")),
        },
        test_cml_use_bad_subdir => {
            input = json!({
                "use": [
                  {
                    "directory": "/data/config",
                    "from": "realm",
                    "rights": [ "r*" ],
                    "subdir": "/",
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/subdir")),
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
                    {
                        "protocol": "/svc/A",
                        "from": "self",
                    },
                    {
                        "protocol": ["/svc/B", "/svc/C"],
                        "from": "self",
                    },
                    {
                        "directory": "/volumes/blobfs",
                        "from": "self",
                        "rights": ["r*"],
                        "subdir": "blob",
                    },
                    { "directory": "/hub", "from": "framework" },
                    { "runner": "elf", "from": "#logger",  }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
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
                        "url": "https://www.google.com/gmail"
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
            result = Err(Error::validate("\"expose\" source \"#missing\" does not appear in \"children\"")),
        },
        test_cml_expose_duplicate_target_paths => {
            input = json!({
                "expose": [
                    { "service": "/fonts/CoolFonts", "from": "self" },
                    { "service": "/svc/logger", "from": "#logger", "as": "/thing" },
                    { "directory": "/thing", "from": "self" , "rights": ["rx*"] }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            result = Err(Error::validate(
                    "\"/thing\" is a duplicate \"expose\" target path for \"realm\""
            )),
        },
        test_cml_expose_bad_from => {
            input = json!({
                "expose": [ {
                    "service": "/loggers/fuchsia.logger.Log", "from": "realm"
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /expose/0/from")),
        },
        // if "as" is specified, only 1 "protocol" array item is allowed.
        test_cml_expose_bad_as => {
            input = json!({
                "expose": [
                    {
                        "protocol": ["/svc/A", "/svc/B"],
                        "from": "self",
                        "as": "/thing"
                    },
                ],
                "children": [
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            result = Err(Error::validate("\"as\" field can only be specified when one `protocol` is supplied.")),
        },
        test_cml_expose_bad_duplicate_targets => {
            input = json!({
                "expose": [
                    {
                        "protocol": ["/svc/A", "/svc/B"],
                        "from": "self"
                    },
                    {
                        "protocol": "/svc/A",
                        "from": "self"
                    },
                ],
            }),
            result = Err(Error::validate("\"/svc/A\" is a duplicate \"expose\" target path for \"realm\"")),
        },
        test_cml_expose_empty_protocols => {
            input = json!({
                "expose": [
                    {
                        "protocol": [],
                        "from": "self",
                        "as": "/thing"
                    }
                ],
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "OneOf conditions are not met at /expose/0/protocol")),
        },
        test_cml_expose_bad_subdir => {
            input = json!({
                "expose": [
                    {
                        "directory": "/volumes/blobfs",
                        "from": "self",
                        "rights": ["r*"],
                        "subdir": "/",
                    },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /expose/0/subdir")),
        },

        // offer
        test_cml_offer => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#echo_server", "#modular" ],
                        "as": "/svc/fuchsia.logger.SysLog"
                    },
                    {
                        "service": "/svc/fuchsia.fonts.Provider",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "protocol": "/svc/fuchsia.fonts.LegacyProvider",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "protocol": [
                            "/svc/fuchsia.settings.Accessibility",
                            "/svc/fuchsia.ui.scenic.Scenic"
                        ],
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "rights": ["rw*"]
                    },
                    {
                        "directory": "/data/index",
                        "subdir": "files",
                        "from": "realm",
                        "to": [ "#modular" ]
                    },
                    {
                        "directory": "/hub",
                        "from": "framework",
                        "to": [ "#modular" ],
                        "as": "/hub"
                    },
                    {
                        "storage": "data",
                        "from": "#minfs",
                        "to": [ "#modular", "#logger" ]
                    },
                    {
                        "runner": "elf",
                        "from": "realm",
                        "to": [ "#modular", "#logger" ]
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    },
                ],
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent",
                    },
                ],
                "storage": [
                    {
                        "name": "minfs",
                        "from": "realm",
                        "path": "/minfs",
                    },
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
                        "to": [ "#abcdefghijklmnopqrstuvwxyz0123456789_-to" ],
                    },
                    {
                        "storage": "data",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "to": [ "#abcdefghijklmnopqrstuvwxyz0123456789_-to" ],
                    }
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail"
                    },
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-to",
                        "url": "https://www.google.com/gmail"
                    },
                ],
                "storage": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example"
                    }
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
                        "to": [ "#echo_server" ],
                    } ]
                }),
            result = Err(Error::validate("\"offer\" source \"#missing\" does not appear in \"children\"")),
        },
        test_cml_storage_offer_missing_from => {
            input = json!({
                    "offer": [ {
                        "storage": "cache",
                        "from": "#missing",
                        "to": [ "#echo_server" ],
                    } ]
                }),
            result = Err(Error::validate("\"offer\" source \"#missing\" does not appear in \"storage\"")),
        },
        test_cml_offer_bad_from => {
            input = json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#invalid@",
                        "to": [ "#echo_server" ],
                    } ]
                }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /offer/0/from")),
        },
        test_cml_storage_offer_bad_to => {
            input = json!({
                    "offer": [ {
                        "storage": "cache",
                        "from": "realm",
                        "to": [ "#logger" ],
                        "as": "/invalid",
                    } ],
                    "children": [ {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm"
                    } ]
                }),
            result = Err(Error::validate("\"as\" field cannot be used for storage offer targets")),
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
        test_cml_offer_duplicate_targets => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": ["#a", "#a"]
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "UniqueItems condition is not met at /offer/0/to")),
        },
        test_cml_offer_target_missing_props => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "as": "/svc/fuchsia.logger.SysLog",
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "This property is required at /offer/0/to")),
        },
        test_cml_offer_target_missing_to => {
            input = json!({
                "offer": [ {
                    "service": "/snvc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "#missing" ],
                } ],
                "children": [ {
                    "name": "logger",
                    "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                } ]
            }),
            result = Err(Error::validate("\"#missing\" is an \"offer\" target but it does not appear in \"children\" or \"collections\"")),
        },
        test_cml_offer_target_bad_to => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "self" ],
                    "as": "/svc/fuchsia.logger.SysLog",
                } ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /offer/0/to/0")),
        },
        test_cml_offer_empty_protocols => {
            input = json!({
                "offer": [
                    {
                        "protocol": [],
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                ],
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "OneOf conditions are not met at /offer/0/protocol")),
        },
        test_cml_offer_target_equals_from => {
            input = json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "#logger" ],
                    "as": "/svc/fuchsia.logger.SysLog",
                } ],
                "children": [ {
                    "name": "logger", "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                } ],
            }),
            result = Err(Error::validate("Offer target \"#logger\" is same as source")),
        },
        test_cml_storage_offer_target_equals_from => {
            input = json!({
                "offer": [ {
                    "storage": "data",
                    "from": "#minfs",
                    "to": [ "#logger" ],
                } ],
                "children": [ {
                    "name": "logger",
                    "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                } ],
                "storage": [ {
                    "name": "minfs",
                    "from": "#logger",
                    "path": "/minfs",
                } ],
            }),
            result = Err(Error::validate("Storage offer target \"#logger\" is same as source")),
        },
        test_cml_offer_duplicate_target_paths => {
            input = json!({
                "offer": [
                    {
                        "service": "/svc/logger",
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                    {
                        "service": "/svc/logger",
                        "from": "self",
                        "to": [ "#scenic" ],
                    },
                    {
                        "directory": "/thing",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    }
                ],
                "children": [
                    {
                        "name": "scenic",
                        "url": "fuchsia-pkg://fuchsia.com/scenic/stable#meta/scenic.cm"
                    },
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            result = Err(Error::validate("\"/thing\" is a duplicate \"offer\" target path for \"#echo_server\"")),
        },
        test_cml_offer_duplicate_storage_types => {
            input = json!({
                "offer": [
                    {
                        "storage": "cache",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "storage": "cache",
                        "from": "#minfs",
                        "to": [ "#echo_server" ]
                    }
                ],
                "storage": [ {
                    "name": "minfs",
                    "from": "self",
                    "path": "/minfs"
                } ],
                "children": [ {
                    "name": "echo_server",
                    "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                } ]
            }),
            result = Err(Error::validate("\"cache\" is a duplicate \"offer\" target storage type for \"#echo_server\"")),
        },
        test_cml_offer_duplicate_runner_name => {
            input = json!({
                "offer": [
                    {
                        "runner": "elf",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "runner": "elf",
                        "from": "framework",
                        "to": [ "#echo_server" ]
                    }
                ],
                "children": [ {
                    "name": "echo_server",
                    "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                } ]
            }),
            result = Err(Error::validate("\"elf\" is a duplicate \"offer\" target runner for \"#echo_server\"")),
        },
        // if "as" is specified, only 1 "protocol" array item is allowed.
        test_cml_offer_bad_as => {
            input = json!({
                "offer": [
                    {
                        "protocol": ["/svc/A", "/svc/B"],
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                ],
                "children": [
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            result = Err(Error::validate("\"as\" field can only be specified when one `protocol` is supplied.")),
        },
        test_cml_offer_bad_subdir => {
            input = json!({
                "offer": [
                    {
                        "directory": "/data/index",
                        "subdir": "/",
                        "from": "realm",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    }
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /offer/0/subdir")),
        },

        // children
        test_cml_children => {
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
                    },
                ]
            }),
            result = Ok(()),
        },
        test_cml_children_missing_props => {
            input = json!({
                "children": [ {} ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "This property is required at /children/0/name, This property is required at /children/0/url")),
        },
        test_cml_children_duplicate_names => {
           input = json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/beta#meta/logger.cm"
                    }
                ]
            }),
            result = Err(Error::validate("identifier \"logger\" is defined twice, once in \"children\" and once in \"children\"")),
        },
        test_cml_children_bad_startup => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "zzz",
                    },
                ],
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /children/0/startup")),
        },

        // collections
        test_cml_collections => {
            input = json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent"
                    },
                    {
                        "name": "tests",
                        "durability": "transient"
                    },
                ]
            }),
            result = Ok(()),
        },
        test_cml_collections_missing_props => {
            input = json!({
                "collections": [ {} ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "This property is required at /collections/0/durability, This property is required at /collections/0/name")),
        },
        test_cml_collections_duplicate_names => {
           input = json!({
               "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent"
                    },
                    {
                        "name": "modular",
                        "durability": "transient"
                    }
                ]
            }),
            result = Err(Error::validate("identifier \"modular\" is defined twice, once in \"collections\" and once in \"collections\"")),
        },
        test_cml_collections_bad_durability => {
            input = json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "zzz",
                    },
                ],
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /collections/0/durability")),
        },

        // storage
        test_cml_storage => {
            input = json!({
                "storage": [
                    {
                        "name": "a",
                        "from": "#minfs",
                        "path": "/minfs"
                    },
                    {
                        "name": "b",
                        "from": "realm",
                        "path": "/data"
                    },
                    {
                        "name": "c",
                        "from": "self",
                        "path": "/storage"
                    }
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_storage_all_valid_chars => {
            input = json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail"
                    },
                ],
                "storage": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_storage_missing_props => {
            input = json!({
                "storage": [ {} ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "This property is required at /storage/0/from, This property is required at /storage/0/name, This property is required at /storage/0/path")),
        },
        test_cml_storage_missing_from => {
            input = json!({
                    "storage": [ {
                        "name": "minfs",
                        "from": "#missing",
                        "path": "/minfs"
                    } ]
                }),
            result = Err(Error::validate("\"storage\" source \"#missing\" does not appear in \"children\"")),
        },

        // runner
        test_cml_runner => {
            input = json!({
                "runner": [
                    {
                        "name": "a",
                        "from": "#minfs",
                        "path": "/minfs"
                    },
                    {
                        "name": "b",
                        "from": "realm",
                        "path": "/data"
                    },
                    {
                        "name": "c",
                        "from": "self",
                        "path": "/runner"
                    }
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_runner_all_valid_chars => {
            input = json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail"
                    },
                ],
                "runner": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-runner",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example"
                    }
                ]
            }),
            result = Ok(()),
        },
        test_cml_runner_missing_props => {
            input = json!({
                "runners": [ {} ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, concat!(
                   "This property is required at /runners/0/from, ",
                   "This property is required at /runners/0/name, ",
                   "This property is required at /runners/0/path",
            ))),
        },
        test_cml_runner_missing_from => {
            input = json!({
                    "runners": [ {
                        "name": "minfs",
                        "from": "#missing",
                        "path": "/minfs"
                    } ]
                }),
            result = Err(Error::validate("\"runner\" source \"#missing\" does not appear in \"children\"")),
        },

        // environments
        test_cml_environments => {
            input = json!({
                "environments": [
                    {
                        "name": "my_env_a",
                    },
                    {
                        "name": "my_env_b",
                        "extends": "realm",
                    },
                    {
                        "name": "my_env_c",
                        "extends": "none",
                    },
                ],
            }),
            result = Ok(()),
        },
        test_cml_environment_invalid_extends => {
            input = json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "some_made_up_string",
                    },
                ],
            }),
            result = Err(Error::Parse("Couldn't read input as struct: unknown variant `some_made_up_string`, expected `realm` or `none`".to_string())),
        },
        test_cml_environment_missing_props => {
            input = json!({
                "environments": [ {} ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, concat!(
                "This property is required at /environments/0/name",
            ))),
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
        test_cml_rights_all => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["connect", "enumerate", "read_bytes", "write_bytes",
                               "execute", "update_attributes", "get_attributes", "traverse",
                               "modify_directory", "admin"],
                  },
                ]
            }),
            result = Ok(()),
        },
        test_cml_rights_invalid => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["cAnnect", "enumerate"],
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Enum conditions are not met at /use/0/rights/0")),
        },
        test_cml_rights_duplicate => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["connect", "connect"],
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "UniqueItems condition is not met at /use/0/rights")),
        },
        test_cml_rights_empty => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": [],
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MinItems condition is not met at /use/0/rights")),
        },
        test_cml_rights_alias_star_expansion => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["r*"],
                  },
                ]
            }),
            result = Ok(()),
        },
        test_cml_rights_alias_star_expansion_with_longform => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["w*", "read_bytes"],
                  },
                ]
            }),
            result = Ok(()),
        },
        test_cml_rights_alias_star_expansion_with_longform_collision => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["r*", "read_bytes"],
                  },
                ]
            }),
            result = Err(Error::validate("\"read_bytes\" is duplicated in the rights clause.")),
        },
        test_cml_rights_alias_star_expansion_collision => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["w*", "x*"],
                  },
                ]
            }),
            result = Err(Error::validate("\"x*\" is duplicated in the rights clause.")),
        },
        test_cml_rights_use_invalid => {
            input = json!({
                "use": [
                  { "directory": "/foo", },
                ]
            }),
            result = Err(Error::validate("Rights required for this use statement.")),
        },
        test_cml_rights_offer_dir_invalid => {
            input = json!({
                "offer": [
                  {
                    "directory": "/foo",
                    "from": "self",
                    "to": [ "#echo_server" ],
                  },
                ],
                "children": [
                  {
                    "name": "echo_server",
                    "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                  }
                ],
            }),
            result = Err(Error::validate("Rights required for this offer as it is offering from self.")),
        },
        test_cml_rights_expose_dir_invalid => {
            input = json!({
                "expose": [
                  {
                    "directory": "/foo/bar",
                    "from": "self",
                  },
                ]
            }),
            result = Err(Error::validate("Rights required for this expose statement as it is exposing from self.")),
        },
        test_cml_path => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo/?!@#$%/Bar",
                    "rights": ["read_bytes"],
                  },
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
        test_cml_relative_path => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "?!@#$%/Bar",
                  },
                ]
            }),
            result = Ok(()),
        },
        test_cml_relative_path_invalid_empty => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "",
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MinLength condition is not met at /use/0/subdir, Pattern condition is not met at /use/0/subdir")),
        },
        test_cml_relative_path_invalid_root => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "/",
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/subdir")),
        },
        test_cml_relative_path_invalid_absolute => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "/bar",
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/subdir")),
        },
        test_cml_relative_path_invalid_trailing => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "bar/",
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /use/0/subdir")),
        },
        test_cml_relative_path_too_long => {
            input = json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": format!("{}", "a".repeat(1025)),
                  },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MaxLength condition is not met at /use/0/subdir")),
        },
        test_cml_relative_ref_too_long => {
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
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
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
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
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
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
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
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    }
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MaxLength condition is not met at /children/0/name")),
        },
        test_cml_url => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "my+awesome-scheme.2://abc123!@#$%.com",
                    },
                ]
            }),
            result = Ok(()),
        },
        test_cml_url_invalid => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://",
                    },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "Pattern condition is not met at /children/0/url")),
        },
        test_cml_url_too_long => {
            input = json!({
                "children": [
                    {
                        "name": "logger",
                        "url": &format!("fuchsia-pkg://{}", "a".repeat(4083)),
                    },
                ]
            }),
            result = Err(Error::validate_schema(CML_SCHEMA, "MaxLength condition is not met at /children/0/url")),
        },
        test_cml_duplicate_identifiers_children_collection => {
           input = json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "collections": [
                   {
                       "name": "logger",
                       "durability": "transient"
                   }
               ]
           }),
           result = Err(Error::validate("identifier \"logger\" is defined twice, once in \"collections\" and once in \"children\"")),
        },
        test_cml_duplicate_identifiers_children_storage => {
           input = json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "storage": [
                    {
                        "name": "logger",
                        "path": "/logs",
                        "from": "realm"
                    }
                ]
           }),
           result = Err(Error::validate("identifier \"logger\" is defined twice, once in \"storage\" and once in \"children\"")),
        },
        test_cml_duplicate_identifiers_collection_storage => {
           input = json!({
               "collections": [
                    {
                        "name": "logger",
                        "durability": "transient"
                    }
                ],
                "storage": [
                    {
                        "name": "logger",
                        "path": "/logs",
                        "from": "realm"
                    }
                ]
           }),
           result = Err(Error::validate("identifier \"logger\" is defined twice, once in \"storage\" and once in \"collections\"")),
        },
        test_cml_duplicate_identifiers_children_runners => {
           input = json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "runners": [
                    {
                        "name": "logger",
                        "path": "/logs",
                        "from": "realm"
                    }
                ]
           }),
           result = Err(Error::validate("identifier \"logger\" is defined twice, once in \"runners\" and once in \"children\"")),
        },
        test_cml_duplicate_identifiers_environments => {
            input = json!({
                "children": [
                     {
                         "name": "logger",
                         "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                     }
                ],
                "environments": [
                     {
                         "name": "logger",
                     }
                 ]
            }),
            result = Err(Error::validate("identifier \"logger\" is defined twice, once in \"environments\" and once in \"children\"")),
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
        test_cmx_block_system_data => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data" ]
                }
            }),
            result = Err(Error::validate_schema(CMX_SCHEMA, "Not condition is not met at /sandbox/system/0")),
        },
        test_cmx_block_system_data_stem => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data-should-pass" ]
                }
            }),
            result = Ok(()),
        },
        test_cmx_block_system_data_leading_slash => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "/data" ]
                }
            }),
            result = Err(Error::validate_schema(CMX_SCHEMA, "Not condition is not met at /sandbox/system/0")),
        },
        test_cmx_block_system_data_subdir => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data/should-fail" ]
                }
            }),
            result = Err(Error::validate_schema(CMX_SCHEMA, "Not condition is not met at /sandbox/system/0")),
        },
        test_cmx_block_system_deprecated_data => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data" ]
                }
            }),
            result = Err(Error::validate_schema(CMX_SCHEMA, "Not condition is not met at /sandbox/system/0")),
        },
        test_cmx_block_system_deprecated_data_stem => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data-should-pass" ]
                }
            }),
            result = Ok(()),
        },
        test_cmx_block_system_deprecated_data_leading_slash => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "/deprecated-data" ]
                }
            }),
            result = Err(Error::validate_schema(CMX_SCHEMA, "Not condition is not met at /sandbox/system/0")),
        },
        test_cmx_block_system_deprecated_data_subdir => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data/should-fail" ]
                }
            }),
            result = Err(Error::validate_schema(CMX_SCHEMA, "Not condition is not met at /sandbox/system/0")),
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
        extra_schemas: &[(&JsonSchema<'_>, Option<String>)],
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
            input = json!({"sandbox": {"features" : ["deprecated-shell"]}, "program": {"binary": "a"}}),
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

    fn empty_offer() -> cml::Offer {
        cml::Offer {
            service: None,
            protocol: None,
            directory: None,
            storage: None,
            runner: None,
            from: cml::Ref::Self_,
            to: vec![],
            r#as: None,
            rights: None,
            subdir: None,
        }
    }

    #[test]
    fn test_capability_id() -> Result<(), Error> {
        // Simple tests.
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                service: Some("/a".to_string()),
                ..empty_offer()
            })?,
            vec![CapabilityId::Path("/a")]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                protocol: Some(OneOrMany::One("/a".to_string())),
                ..empty_offer()
            })?,
            vec![CapabilityId::Path("/a")]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                protocol: Some(OneOrMany::Many(vec!["/a".to_string(), "/b".to_string()])),
                ..empty_offer()
            })?,
            vec![CapabilityId::Path("/a"), CapabilityId::Path("/b")]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                directory: Some("/a".to_string()),
                ..empty_offer()
            })?,
            vec![CapabilityId::Path("/a")]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                storage: Some("a".to_string()),
                ..empty_offer()
            })?,
            vec![CapabilityId::StorageType("a")]
        );

        // "as" aliasing.
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                service: Some("/a".to_string()),
                r#as: Some("/b".to_string()),
                ..empty_offer()
            })?,
            vec![CapabilityId::Path("/b")]
        );

        // Error case.
        assert_matches!(CapabilityId::from_clause(&empty_offer()), Err(_));

        Ok(())
    }
}
