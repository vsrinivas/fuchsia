// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::{Error, Location},
    crate::{cml, one_or_many::OneOrMany},
    cm_json::{JsonSchema, CMX_SCHEMA},
    directed_graph::{self, DirectedGraph},
    serde_json::Value,
    serde_json5,
    std::{
        collections::{HashMap, HashSet},
        fmt,
        fs::File,
        hash::Hash,
        io::Read,
        iter,
        path::Path,
    },
    valico::json_schema,
};

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
pub fn parse_cml(buffer: &str, file: &Path) -> Result<cml::Document, Error> {
    let document: cml::Document = serde_json5::from_str(buffer).map_err(|e| {
        let serde_json5::Error::Message { location, msg } = e;
        let location = location.map(|l| Location { line: l.line, column: l.column });
        Error::parse(msg, location, Some(file))
    })?;
    let mut ctx = ValidationContext {
        document: &document,
        all_children: HashMap::new(),
        all_collections: HashSet::new(),
        all_storage_and_sources: HashMap::new(),
        all_services: HashSet::new(),
        all_protocols: HashSet::new(),
        all_directories: HashSet::new(),
        all_runners: HashSet::new(),
        all_resolvers: HashSet::new(),
        all_environment_names: HashSet::new(),
        all_event_names: HashSet::new(),
    };
    let mut res = ctx.validate();
    if let Err(Error::Validate { filename, .. }) = &mut res {
        *filename = Some(file.to_string_lossy().into_owned());
    }
    res?;
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
    match ext {
        Some("cmx") => {
            let v = serde_json::from_str(&buffer)?;
            validate_json(&v, CMX_SCHEMA)?;
            // Validate against any extra schemas provided.
            for extra_schema in extra_schemas {
                let schema = JsonSchema::new_from_file(&extra_schema.0.as_ref())?;
                validate_json(&v, &schema).map_err(|e| match (&e, &extra_schema.1) {
                    (Error::Validate { schema_name, err, filename }, Some(extra_msg)) => {
                        Error::Validate {
                            schema_name: schema_name.clone(),
                            err: format!("{}\n{}", err, extra_msg),
                            filename: filename.clone(),
                        }
                    }
                    _ => e,
                })?;
            }
        }
        Some("cml") => {
            parse_cml(&buffer, file)?;
        }
        _ => {
            return Err(Error::invalid_args(BAD_EXTENSION));
        }
    };
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
        return Err(Error::validate_schema(&schema.name.to_string(), err_msgs.join(", ")));
    }
    Ok(())
}

struct ValidationContext<'a> {
    document: &'a cml::Document,
    all_children: HashMap<&'a cml::Name, &'a cml::Child>,
    all_collections: HashSet<&'a cml::Name>,
    all_storage_and_sources: HashMap<&'a cml::Name, &'a cml::CapabilityFromRef>,
    all_services: HashSet<&'a cml::Name>,
    all_protocols: HashSet<&'a cml::Name>,
    all_directories: HashSet<&'a cml::Name>,
    all_runners: HashSet<&'a cml::Name>,
    all_resolvers: HashSet<&'a cml::Name>,
    all_environment_names: HashSet<&'a cml::Name>,
    all_event_names: HashSet<cml::Name>,
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
        let all_resolver_names =
            self.document.all_resolver_names().into_iter().zip(iter::repeat("resolvers"));
        let all_environment_names =
            self.document.all_environment_names().into_iter().zip(iter::repeat("environments"));
        ensure_no_duplicate_names(
            all_children_names
                .chain(all_collection_names)
                .chain(all_storage_names)
                .chain(all_runner_names)
                .chain(all_resolver_names)
                .chain(all_environment_names),
        )?;

        // Populate the sets of children and collections.
        if let Some(children) = &self.document.children {
            self.all_children = children.iter().map(|c| (&c.name, c)).collect();
        }
        self.all_collections = self.document.all_collection_names().into_iter().collect();
        self.all_storage_and_sources = self.document.all_storage_and_sources();
        self.all_services = self.document.all_service_names().into_iter().collect();
        self.all_protocols = self.document.all_protocol_names().into_iter().collect();
        self.all_directories = self.document.all_directory_names().into_iter().collect();
        self.all_runners = self.document.all_runner_names().into_iter().collect();
        self.all_resolvers = self.document.all_resolver_names().into_iter().collect();
        self.all_environment_names = self.document.all_environment_names().into_iter().collect();
        self.all_event_names = self.document.all_event_names()?.into_iter().collect();

        // Validate "children".
        let mut strong_dependencies = DirectedGraph::new();
        if let Some(children) = &self.document.children {
            for child in children {
                self.validate_child(&child, &mut strong_dependencies)?;
            }
        }

        // Validate "collections".
        if let Some(collections) = &self.document.collections {
            for collection in collections {
                self.validate_collection(&collection)?;
            }
        }

        // Validate "capabilities".
        if let Some(capabilities) = self.document.capabilities.as_ref() {
            let mut used_ids = HashMap::new();
            for capability in capabilities {
                self.validate_capability(capability, &mut used_ids)?;
            }
        }

        // Validate "use".
        if let Some(uses) = self.document.r#use.as_ref() {
            let mut used_ids = HashMap::new();
            for use_ in uses.iter() {
                self.validate_use(&use_, &mut used_ids)?;
            }
        }

        // Validate "expose".
        if let Some(exposes) = self.document.expose.as_ref() {
            let mut used_ids = HashMap::new();
            for expose in exposes.iter() {
                self.validate_expose(&expose, &mut used_ids)?;
            }
        }

        // Validate "offer".
        if let Some(offers) = self.document.offer.as_ref() {
            let mut used_ids = HashMap::new();
            for offer in offers.iter() {
                self.validate_offer(&offer, &mut used_ids, &mut strong_dependencies)?;
            }
        }

        // Ensure we don't have a component with a "program" block which fails
        // to specify a runner.
        self.validate_runner_specified(
            self.document.program.as_ref(),
            self.document.r#use.as_ref(),
        )?;

        // Validate "environments".
        if let Some(environments) = &self.document.environments {
            for env in environments {
                self.validate_environment(&env, &mut strong_dependencies)?;
            }
        }

        // Check for dependency cycles
        match strong_dependencies.topological_sort() {
            Ok(_) => {}
            Err(e) => {
                return Err(Error::validate(format!(
                    "Strong dependency cycles were found. Break the cycle by removing a dependency or marking an offer as weak. Cycles: {}", e.format_cycle())));
            }
        }

        Ok(())
    }

    fn validate_child(
        &self,
        child: &'a cml::Child,
        strong_dependencies: &mut DirectedGraph<DependencyNode<'a>>,
    ) -> Result<(), Error> {
        if let Some(environment_ref) = &child.environment {
            match environment_ref {
                cml::EnvironmentRef::Named(environment_name) => {
                    if !self.all_environment_names.contains(&environment_name) {
                        return Err(Error::validate(format!(
                            "\"{}\" does not appear in \"environments\"",
                            &environment_name
                        )));
                    }
                    let source = DependencyNode::Environment(environment_name.as_str());
                    let target = DependencyNode::Child(child.name.as_str());
                    strong_dependencies.add_edge(source, target);
                }
            }
        }
        Ok(())
    }

    fn validate_collection(&self, collection: &'a cml::Collection) -> Result<(), Error> {
        if let Some(environment_ref) = &collection.environment {
            match environment_ref {
                cml::EnvironmentRef::Named(environment_name) => {
                    if !self.all_environment_names.contains(&environment_name) {
                        return Err(Error::validate(format!(
                            "\"{}\" does not appear in \"environments\"",
                            &environment_name
                        )));
                    }
                    // If there is an environment, we don't need to account for it in the dependency
                    // graph because a collection is always a sink node.
                }
            }
        }
        Ok(())
    }

    fn validate_capability(
        &self,
        capability: &'a cml::Capability,
        used_ids: &mut HashMap<String, cml::CapabilityId>,
    ) -> Result<(), Error> {
        if capability.directory.is_some() && capability.path.is_none() {
            return Err(Error::validate("\"path\" should be present with \"directory\""));
        }
        if capability.directory.is_some() && capability.rights.is_none() {
            return Err(Error::validate("\"rights\" should be present with \"directory\""));
        }
        if capability.storage.is_some() {
            if capability.from.is_none() {
                return Err(Error::validate("\"from\" should be present with \"storage\""));
            }
            if capability.path.is_none() && capability.backing_dir.is_none() {
                return Err(Error::validate(
                    "One of \"path\" or \"backing_dir\" must be present with \"storage\"",
                ));
            }
            if capability.path.is_some() && capability.backing_dir.is_some() {
                return Err(Error::validate(
                    "\"path\" and \"backing_dir\" may not both be present with \"storage\"",
                ));
            }
        }
        if capability.runner.is_some() && capability.from.is_none() {
            return Err(Error::validate("\"from\" should be present with \"runner\""));
        }
        if capability.runner.is_some() && capability.path.is_none() {
            return Err(Error::validate("\"path\" should be present with \"runner\""));
        }
        if capability.resolver.is_some() && capability.from.is_some() {
            return Err(Error::validate("\"from\" should not be present with \"resolver\""));
        }
        if capability.resolver.is_some() && capability.path.is_none() {
            return Err(Error::validate("\"path\" should be present with \"resolver\""));
        }
        if let Some(from) = capability.from.as_ref() {
            self.validate_component_ref("\"capabilities\" source", cml::AnyRef::from(from))?;
        }

        // Disallow multiple capability ids of the same name.
        let capability_ids =
            cml::CapabilityId::from_clause(capability, cml::RoutingClauseType::Capability)?;
        for capability_id in capability_ids {
            if used_ids.insert(capability_id.to_string(), capability_id.clone()).is_some() {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"capability\" name",
                    capability_id,
                )));
            }
        }

        Ok(())
    }

    fn validate_use(
        &self,
        use_: &'a cml::Use,
        used_ids: &mut HashMap<String, cml::CapabilityId>,
    ) -> Result<(), Error> {
        if use_.service.is_some() && use_.r#as.is_some() {
            return Err(Error::validate("\"as\" field cannot be used with \"service\""));
        }
        if let Some(protocol) = use_.protocol.as_ref() {
            for protocol in protocol.iter() {
                if protocol.is_name() && use_.r#as.is_some() {
                    return Err(Error::validate("\"as\" field cannot be used with \"protocol\""));
                }
            }
        }
        if use_.directory.is_some() && use_.r#as.is_some() {
            if use_.directory.as_ref().unwrap().is_name() {
                return Err(Error::validate("\"as\" field cannot be used with \"directory\""));
            }
        }
        if use_.runner.is_some() && use_.r#as.is_some() {
            return Err(Error::validate("\"as\" field cannot be used with \"runner\""));
        }
        if use_.event.is_some() && use_.from.is_none() {
            return Err(Error::validate("\"from\" should be present with \"event\""));
        }
        if use_.event.is_none() && use_.filter.is_some() {
            return Err(Error::validate("\"filter\" can only be used with \"event\""));
        }

        let storage = use_.storage.as_ref().map(|s| s.as_str());
        if storage.is_some() && use_.r#as.is_some() {
            if storage.unwrap() == "meta" {
                return Err(Error::validate(
                    "\"as\" field cannot be used with storage type \"meta\"",
                ));
            }
        }
        if storage.is_some() && use_.from.is_some() {
            return Err(Error::validate("\"from\" field cannot be used with \"storage\""));
        }

        if let Some(event_stream) = use_.event_stream.as_ref() {
            let events = event_stream.to_vec();
            for event in events {
                if !self.all_event_names.contains(event) {
                    return Err(Error::validate(format!(
                        "Event \"{}\" in event stream not found in any \"use\" declaration.",
                        event
                    )));
                }
            }
        }

        // Disallow multiple capability ids of the same name.
        let capability_ids = cml::CapabilityId::from_clause(use_, cml::RoutingClauseType::Use)?;
        for capability_id in capability_ids {
            if used_ids.insert(capability_id.to_string(), capability_id.clone()).is_some() {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"use\" target {}",
                    capability_id,
                    capability_id.type_str()
                )));
            }
            let dir = match capability_id.get_dir_path() {
                Some(d) => d,
                None => continue,
            };

            // Validate that paths-based capabilities (service, directory, protocol)
            // are not prefixes of each other.
            for (_, used_id) in used_ids.iter() {
                if capability_id == *used_id {
                    continue;
                }
                let used_dir = match used_id.get_dir_path() {
                    Some(d) => d,
                    None => continue,
                };

                if match (used_id, &capability_id) {
                    // Directories can't be the same or partially overlap.
                    (cml::CapabilityId::UsedDirectory(_), cml::CapabilityId::UsedDirectory(_)) => {
                        dir == used_dir || dir.starts_with(used_dir) || used_dir.starts_with(dir)
                    }

                    // Protocols and Services can't overlap with Directories.
                    (_, cml::CapabilityId::UsedDirectory(_))
                    | (cml::CapabilityId::UsedDirectory(_), _) => {
                        dir == used_dir || dir.starts_with(used_dir) || used_dir.starts_with(dir)
                    }

                    // Protocols and Services containing directories may be same, but
                    // partial overlap is disallowed.
                    (_, _) => {
                        dir != used_dir && (dir.starts_with(used_dir) || used_dir.starts_with(dir))
                    }
                } {
                    // TODO: This error message is in the wrong order
                    return Err(Error::validate(format!(
                        "{} \"{}\" is a prefix of \"use\" target {} \"{}\"",
                        capability_id.type_str(),
                        capability_id,
                        used_id.type_str(),
                        used_id,
                    )));
                }
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
        used_ids: &mut HashMap<String, cml::CapabilityId>,
    ) -> Result<(), Error> {
        // TODO: Many of these checks are similar, see if we can unify them

        // Ensure that if the expose target is framework, the source target is self always.
        if expose.to == Some(cml::ExposeToRef::Framework) {
            match &expose.from {
                OneOrMany::One(cml::ExposeFromRef::Self_) => {}
                OneOrMany::Many(vec)
                    if vec.iter().all(|from| *from == cml::ExposeFromRef::Self_) => {}
                _ => {
                    return Err(Error::validate("Expose to framework can only be done from self."))
                }
            }
        }

        // Ensure that services exposed from self are defined in `capabilities`.
        if let Some(service_name) = &expose.service {
            if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_) {
                if !self.all_services.contains(service_name) {
                    return Err(Error::validate(format!(
                       "Service \"{}\" is exposed from self, so it must be declared as a \"service\" in \"capabilities\"",
                       service_name
                   )));
                }
            }
        }

        // Ensure that protocols exposed from self are defined in `capabilities`.
        if let Some(protocol) = expose.protocol.as_ref() {
            for protocol in protocol {
                if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_) {
                    if let cml::NameOrPath::Name(protocol) = protocol {
                        if !self.all_protocols.contains(protocol) {
                            return Err(Error::validate(format!(
                           "Protocol \"{}\" is exposed from self, so it must be declared as a \"protocol\" in \"capabilities\"",
                           protocol
                       )));
                        }
                    }
                }
            }
        }

        // Ensure that directories exposed from self are defined in `capabilities`.
        if let Some(directory) = &expose.directory {
            if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_) {
                if let cml::NameOrPath::Name(directory) = directory {
                    if !self.all_directories.contains(directory) {
                        return Err(Error::validate(format!(
                           "Directory \"{}\" is exposed from self, so it must be declared as a \"directory\" in \"capabilities\"",
                           directory
                       )));
                    }
                }
            }
        }

        // Ensure directory rights are specified if exposing from self.
        if let Some(directory) = expose.directory.as_ref() {
            if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_)
                || expose.rights.is_some()
            {
                match &expose.rights {
                    Some(rights) => self.validate_directory_rights(&rights)?,
                    None => {
                        if directory.is_path() {
                            return Err(Error::validate(
                            "Rights required for this expose statement as it is exposing from self.",
                        ));
                        }
                    }
                };
            }

            // Exposing a subdirectory makes sense for routing but when exposing to framework,
            // the subdir should be exposed directly.
            if expose.to == Some(cml::ExposeToRef::Framework) {
                if expose.subdir.is_some() {
                    return Err(Error::validate(
                        "`subdir` is not supported for expose to framework. Directly expose the subdirectory instead."
                    ));
                }
            }
        }

        // Ensure that runners exposed from self are defined in `capabilities`.
        if let Some(runner_name) = &expose.runner {
            if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_) {
                if !self.all_runners.contains(runner_name) {
                    return Err(Error::validate(format!(
                        "Runner \"{}\" is exposed from self, so it must be declared as a \"runner\" in \"capabilities\"",
                        runner_name
                    )));
                }
            }
        }

        // Ensure that resolvers exposed from self are defined in `capabilities`.
        if let Some(resolver_name) = &expose.resolver {
            if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_) {
                if !self.all_resolvers.contains(resolver_name) {
                    return Err(Error::validate(format!(
                       "Resolver \"{}\" is exposed from self, so it must be declared as a \"resolver\" in \"capabilities\"", resolver_name
                   )));
                }
            }
        }

        // Ensure we haven't already exposed an entity of the same name.
        let capability_ids =
            cml::CapabilityId::from_clause(expose, cml::RoutingClauseType::Expose)?;
        for capability_id in capability_ids {
            if used_ids.insert(capability_id.to_string(), capability_id.clone()).is_some() {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"expose\" target capability for \"{}\"",
                    capability_id,
                    expose.to.as_ref().unwrap_or(&cml::ExposeToRef::Parent)
                )));
            }
        }

        // Validate `from` (done last because this validation depends on the capability type, which
        // must be validated first)
        self.validate_from_clause("expose", expose)?;

        Ok(())
    }

    fn validate_offer(
        &self,
        offer: &'a cml::Offer,
        used_ids: &mut HashMap<&'a cml::Name, HashMap<String, cml::CapabilityId>>,
        strong_dependencies: &mut DirectedGraph<DependencyNode<'a>>,
    ) -> Result<(), Error> {
        // TODO: Many of these checks are repititious, see if we can unify them

        // Ensure that services offered from self are defined in `services`.
        if let Some(service_name) = &offer.service {
            if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) {
                if !self.all_services.contains(service_name) {
                    return Err(Error::validate(format!(
                        "Service \"{}\" is offered from self, so it must be declared as a \
                       \"service\" in \"capabilities\"",
                        service_name
                    )));
                }
            }
        }

        // Ensure that protocols offered from self are defined in `capabilities`.
        if let Some(protocol) = offer.protocol.as_ref() {
            for protocol in protocol.iter() {
                if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) {
                    if let cml::NameOrPath::Name(protocol) = protocol {
                        if !self.all_protocols.contains(protocol) {
                            return Err(Error::validate(format!(
                           "Protocol \"{}\" is offered from self, so it must be declared as a \"protocol\" in \"capabilities\"",
                           protocol
                       )));
                        }
                    }
                }
            }
        }

        // Ensure that directories offered from self are defined in `capabilities`.
        if let Some(directory) = &offer.directory {
            if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) {
                if let cml::NameOrPath::Name(directory) = directory {
                    if !self.all_directories.contains(directory) {
                        return Err(Error::validate(format!(
                           "Directory \"{}\" is offered from self, so it must be declared as a \"directory\" in \"capabilities\"",
                           directory
                       )));
                    }
                }
            }
        }

        // Ensure directory rights are specified if offering from self.
        if let Some(directory) = offer.directory.as_ref() {
            if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) || offer.rights.is_some() {
                match &offer.rights {
                    Some(rights) => self.validate_directory_rights(&rights)?,
                    None => {
                        if directory.is_path() {
                            return Err(Error::validate(
                                "Rights required for this offer as it is offering from self.",
                            ));
                        }
                    }
                };
            }
        }

        // Ensure that runners offered from self are defined in `runners`.
        if let Some(runner_name) = &offer.runner {
            if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) {
                if !self.all_runners.contains(runner_name) {
                    return Err(Error::validate(format!(
                        "Runner \"{}\" is offered from self, so it must be declared as a \
                       \"runner\" in \"capabilities\"",
                        runner_name
                    )));
                }
            }
        }

        // Ensure that resolvers offered from self are defined in `resolvers`.
        if let Some(resolver_name) = &offer.resolver {
            if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) {
                if !self.all_resolvers.contains(resolver_name) {
                    return Err(Error::validate(format!(
                        "Resolver \"{}\" is offered from self, so it must be declared as a \
                       \"resolver\" in \"capabilities\"",
                        resolver_name
                    )));
                }
            }
        }

        // Ensure that dependency can only be provided for directories and protocols
        if offer.dependency.is_some() && offer.directory.is_none() && offer.protocol.is_none() {
            return Err(Error::validate(
                "Dependency can only be provided for protocol and directory capabilities",
            ));
        }

        // Ensure that only events can have filter.
        match (&offer.event, &offer.filter) {
            (None, Some(_)) => Err(Error::validate("\"filter\" can only be used with \"event\"")),
            _ => Ok(()),
        }?;

        // Validate every target of this offer.
        let target_cap_ids = cml::CapabilityId::from_clause(offer, cml::RoutingClauseType::Offer)?;
        for to in &offer.to.0 {
            // Ensure the "to" value is a child.
            let to_target = match to {
                cml::OfferToRef::Named(ref name) => name,
            };

            // Check that any referenced child actually exists.
            if !self.all_children.contains_key(to_target)
                && !self.all_collections.contains(to_target)
            {
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
            let ids_for_entity = used_ids.entry(to_target).or_insert(HashMap::new());
            for target_cap_id in &target_cap_ids {
                if ids_for_entity.insert(target_cap_id.to_string(), target_cap_id.clone()).is_some()
                {
                    return Err(Error::validate(format!(
                        "\"{}\" is a duplicate \"offer\" target capability for \"{}\"",
                        target_cap_id, to
                    )));
                }
            }

            // Ensure we are not offering a capability back to its source.
            if offer.storage.is_some() {
                // Storage can only have a single `from` clause and this has been
                // verified.
                if let OneOrMany::One(cml::OfferFromRef::Named(name)) = &offer.from {
                    if let Some(cml::CapabilityFromRef::Named(source)) =
                        self.all_storage_and_sources.get(&name)
                    {
                        if to_target == source {
                            return Err(Error::validate(format!(
                                "Storage offer target \"{}\" is same as source",
                                to
                            )));
                        }
                    }
                }
            } else {
                for reference in offer.from.to_vec() {
                    match reference {
                        cml::OfferFromRef::Named(name) if name == to_target => {
                            return Err(Error::validate(format!(
                                "Offer target \"{}\" is same as source",
                                to
                            )));
                        }
                        _ => {}
                    }
                }
            }

            // Collect strong dependencies. We'll check for dependency cycles after all offer
            // declarations are validated.
            for from in offer.from.to_vec().iter() {
                let is_strong = if offer.directory.is_some() || offer.protocol.is_some() {
                    offer.dependency.as_ref().unwrap_or(&cml::DependencyType::Strong)
                        == &cml::DependencyType::Strong
                } else {
                    true
                };
                if is_strong {
                    if let cml::OfferFromRef::Named(from) = from {
                        match to {
                            cml::OfferToRef::Named(to) => {
                                let source = DependencyNode::Child(from.as_str());
                                let target = DependencyNode::Child(to.as_str());
                                strong_dependencies.add_edge(source, target);
                            }
                        }
                    }
                }
            }
        }

        // Validate `from` (done last because this validation depends on the capability type, which
        // must be validated first)
        self.validate_from_clause("offer", offer)?;

        Ok(())
    }

    /// Validates that the from clause:
    ///
    /// - is applicable to the capability type,
    /// - does not contain duplicates,
    /// - references names that exist.
    ///
    /// `verb` is used in any error messages and is expected to be "offer", "expose", etc.
    fn validate_from_clause<T>(&self, verb: &str, cap: &T) -> Result<(), Error>
    where
        T: cml::CapabilityClause + cml::FromClause,
    {
        let from = cap.from_();
        if cap.service().is_none() && from.is_many() {
            return Err(Error::validate(format!(
                "\"{}\" capabilities cannot have multiple \"from\" clauses",
                cap.capability_name()
            )));
        }

        if from.is_many() {
            ensure_no_duplicate_values(&cap.from_())?;
        }

        // If offered cap is a storage type, then "from" should be interpreted
        // as a storage name. Otherwise, it should be interpreted as a child
        // or collection.
        let reference_description = format!("\"{}\" source", verb);
        if cap.storage_type().is_some() {
            for from_clause in from {
                self.validate_storage_ref(&reference_description, from_clause)?;
            }
        } else {
            for from_clause in from {
                self.validate_component_ref(&reference_description, from_clause)?;
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
        component_ref: cml::AnyRef,
    ) -> Result<(), Error> {
        match component_ref {
            cml::AnyRef::Named(name) => {
                // Ensure we have a child defined by that name.
                if !self.all_children.contains_key(name) {
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
        storage_ref: cml::AnyRef,
    ) -> Result<(), Error> {
        if let cml::AnyRef::Named(name) = storage_ref {
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
    /// contain duplicate rights.
    fn validate_directory_rights(&self, rights_clause: &cml::Rights) -> Result<(), Error> {
        let mut rights = HashSet::new();
        for right_token in rights_clause.0.iter() {
            for right in right_token.expand() {
                if !rights.insert(right) {
                    return Err(Error::validate(format!(
                        "\"{}\" is duplicated in the rights clause.",
                        right_token
                    )));
                }
            }
        }
        Ok(())
    }

    /// Ensure we don't have a component with a "program" block which fails
    /// to specify a runner.
    fn validate_runner_specified(
        &self,
        program: Option<&serde_json::map::Map<String, serde_json::value::Value>>,
        use_: Option<&Vec<cml::Use>>,
    ) -> Result<(), Error> {
        // Components that have no "program" don't need a runner.
        if program.is_none() {
            return Ok(());
        }

        // Otherwise, ensure a runner is being used.
        let mut found_runner = false;
        if let Some(use_) = use_ {
            found_runner = use_.iter().any(|u| u.runner.is_some())
        }
        if !found_runner {
            return Err(Error::validate(concat!(
                "Component has a 'program' block defined, but doesn't 'use' ",
                "a runner capability. Components need to 'use' a runner ",
                "to actually execute code."
            )));
        }

        Ok(())
    }

    fn validate_environment(
        &self,
        environment: &'a cml::Environment,
        strong_dependencies: &mut DirectedGraph<DependencyNode<'a>>,
    ) -> Result<(), Error> {
        match &environment.extends {
            Some(cml::EnvironmentExtends::None) => {
                if environment.stop_timeout_ms.is_none() {
                    return Err(Error::validate(
                        "'__stop_timeout_ms' must be provided if the environment does not extend \
                        another environment",
                    ));
                }
            }
            Some(cml::EnvironmentExtends::Realm) | None => {}
        }

        if let Some(runners) = &environment.runners {
            let mut used_names = HashMap::new();
            for registration in runners {
                // Validate that this name is not already used.
                let name = registration.r#as.as_ref().unwrap_or(&registration.runner);
                if let Some(previous_runner) = used_names.insert(name, &registration.runner) {
                    return Err(Error::validate(format!(
                        "Duplicate runners registered under name \"{}\": \"{}\" and \"{}\".",
                        name, &registration.runner, previous_runner
                    )));
                }

                // Ensure that the environment is defined in `runners` if it comes from `self`.
                if registration.from == cml::RegistrationRef::Self_
                    && !self.all_runners.contains(&registration.runner)
                {
                    return Err(Error::validate(format!(
                        "Runner \"{}\" registered in environment is not in \"runners\"",
                        &registration.runner,
                    )));
                }

                self.validate_component_ref(
                    &format!("\"{}\" runner source", &registration.runner),
                    cml::AnyRef::from(&registration.from),
                )?;

                // Ensure there are no cycles, such as a resolver in an environment being assigned
                // to a child which the resolver depends on.
                if let cml::RegistrationRef::Named(child_name) = &registration.from {
                    let source = DependencyNode::Child(child_name.as_str());
                    let target = DependencyNode::Environment(environment.name.as_str());
                    strong_dependencies.add_edge(source, target);
                }
            }
        }

        if let Some(resolvers) = &environment.resolvers {
            let mut used_schemes = HashMap::new();
            for registration in resolvers {
                // Validate that the scheme is not already used.
                if let Some(previous_resolver) =
                    used_schemes.insert(&registration.scheme, &registration.resolver)
                {
                    return Err(Error::validate(format!(
                        "scheme \"{}\" for resolver \"{}\" is already registered; \
                        previously registered to resolver \"{}\".",
                        &registration.scheme, &registration.resolver, previous_resolver
                    )));
                }

                self.validate_component_ref(
                    &format!("\"{}\" resolver source", &registration.resolver),
                    cml::AnyRef::from(&registration.from),
                )?;
                // Ensure there are no cycles, such as a resolver in an environment being assigned
                // to a child which the resolver depends on.
                if let cml::RegistrationRef::Named(child_name) = &registration.from {
                    let source = DependencyNode::Child(child_name.as_str());
                    let target = DependencyNode::Environment(environment.name.as_str());
                    strong_dependencies.add_edge(source, target);
                }
            }
        }
        Ok(())
    }
}

/// Given an iterator with `(key, name)` tuples, ensure that `key` doesn't
/// appear twice. `name` is used in generated error messages.
fn ensure_no_duplicate_names<'a, I>(values: I) -> Result<(), Error>
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

/// Returns an error if the iterator contains duplicate values.
fn ensure_no_duplicate_values<'a, I, V>(values: I) -> Result<(), Error>
where
    I: IntoIterator<Item = &'a V>,
    V: 'a + Hash + Eq + fmt::Display,
{
    let mut seen = HashSet::new();
    for value in values {
        if !seen.insert(value) {
            return Err(Error::validate(format!("Found duplicate value \"{}\" in array.", value)));
        }
    }
    Ok(())
}

/// A node in the DependencyGraph. This enum is used to differentiate between node types.
#[derive(Copy, Clone, Hash, Ord, Debug, PartialOrd, PartialEq, Eq)]
enum DependencyNode<'a> {
    Child(&'a str),
    Environment(&'a str),
}

impl<'a> fmt::Display for DependencyNode<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DependencyNode::Child(name) => write!(f, "child {}", name),
            DependencyNode::Environment(name) => write!(f, "environment {}", name),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use lazy_static::lazy_static;
    use matches::assert_matches;
    use serde_json::json;
    use std::io::Write;
    use tempfile::TempDir;

    macro_rules! test_validate_cml {
        (
            $(
                $test_name:ident($input:expr, $($pattern:tt)+),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let input = format!("{}", $input);
                    let result = write_and_validate("test.cml", input.as_bytes());
                    assert_matches!(result, $($pattern)+);
                }
            )+
        }
    }

    macro_rules! test_validate_cmx {
        (
            $(
                $test_name:ident($input:expr, $($pattern:tt)+),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let input = format!("{}", $input);
                    let result = write_and_validate("test.cmx", input.as_bytes());
                    assert_matches!(result, $($pattern)+);
                }
            )+
        }
    }

    fn write_and_validate(filename: &str, input: &[u8]) -> Result<(), Error> {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join(filename);
        File::create(&tmp_file_path).unwrap().write_all(input).unwrap();
        validate(&vec![tmp_file_path], &[])
    }

    #[test]
    fn test_validate_invalid_json_fails() {
        let result = write_and_validate("test.cml", b"{");
        let expected_err = r#" --> 1:2
  |
1 | {
  |  ^---
  |
  = expected identifier or string"#;
        assert_matches!(result, Err(Error::Parse { err, .. }) if &err == expected_err);
    }

    #[test]
    fn test_cml_json5() {
        let input = r##"{
            "expose": [
                // Here are some services to expose.
                { "service": "fuchsia.logger.Log", "from": "#logger", },
                { "directory": "/volumes/blobfs", "from": "self", "rights": ["rw*"]},
            ],
            "children": [
                {
                    'name': 'logger',
                    'url': 'fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm',
                },
            ],
        }"##;
        let result = write_and_validate("test.cml", input.as_bytes());
        assert_matches!(result, Ok(()));
    }

    #[test]
    fn test_cml_error_location() {
        let input = r##"{
    "use": [
        {
            "protocol": "/svc/fuchsia.logger.Log",
            "from": "self",
        },
    ],
}"##;
        let result = write_and_validate("test.cml", input.as_bytes());
        assert_matches!(
            result,
            Err(Error::Parse { err, location: Some(l), filename: Some(f) })
                if &err == "invalid value: string \"self\", expected \"parent\", \"framework\", or none" &&
                l == Location { line: 5, column: 21 } &&
                f.ends_with("/test.cml")
        );

        let input = r##"{
    "use": [
        { "event": "started" },
    ],
}"##;
        let result = write_and_validate("test.cml", input.as_bytes());
        assert_matches!(
            result,
            Err(Error::Validate { schema_name: None, err, filename: Some(f) })
                if &err == "\"from\" should be present with \"event\"" &&
                f.ends_with("/test.cml")
        );
    }

    test_validate_cml! {
        // program
        test_cml_empty_json(
            json!({}),
            Ok(())
        ),
        test_cml_program(
            json!(
                {
                    "program": { "binary": "bin/app" },
                    "use": [ { "runner": "elf" } ],
                }
            ),
            Ok(())
        ),

        // use
        test_cml_use(
            json!({
                "use": [
                  { "service": "CoolFonts", "path": "/svc/fuchsia.fonts.Provider" },
                  { "service": "fuchsia.sys2.Realm", "from": "framework" },
                  { "protocol": "CoolFonts", "path": "/svc/MyFonts" },
                  { "protocol": "/fonts/CoolFonts", "as": "/svc/MyFonts2" },
                  { "protocol": "fuchsia.test.hub.HubReport", "from": "framework" },
                  { "protocol": "/svc/fuchsia.test.hub.HubReport2", "from": "framework" },
                  { "protocol": ["fuchsia.ui.scenic.Scenic", "fuchsia.logger.LogSink"] },
                  { "protocol": ["/svc/fuchsia.ui.scenic.Scenic2", "/svc/fuchsia.logger.LogSink2"] },
                  {
                    "directory": "assets",
                    "path": "/data/assets",
                    "rights": ["rw*"],
                  },
                  {
                    "directory": "config",
                    "from": "parent",
                    "path": "/data/config",
                    "rights": ["rx*"],
                    "subdir": "fonts/all",
                  },
                  {
                    "directory": "/data/assets2",
                    "rights": ["rw*"],
                  },
                  {
                    "directory": "/data/config2",
                    "from": "parent",
                    "rights": ["rx*"],
                    "subdir": "fonts/all",
                  },
                  { "storage": "data", "as": "/example" },
                  { "storage": "cache", "as": "/tmp" },
                  { "storage": "meta" },
                  { "runner": "elf" },
                  { "event": [ "started", "stopped"], "from": "parent" },
                  { "event": [ "launched"], "from": "framework" },
                  { "event": "destroyed", "from": "framework", "as": "destroyed_x" },
                  {
                    "event": "capability_ready_diagnostics",
                    "as": "capability_ready",
                    "from": "parent",
                    "filter": {
                        "path": "/diagnositcs"
                    }
                  },
                  {
                    "event_stream": [ "started", "stopped", "launched" ]
                  },
                  {
                    "event_stream": [ "started", "stopped", "launched" ],
                    "as": "/svc/my_stream"
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_use_event_missing_from(
            json!({
                "use": [
                    { "event": "started" },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"from\" should be present with \"event\""
        ),
        test_cml_use_missing_props(
            json!({
                "use": [ { "as": "/svc/fuchsia.logger.Log" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "`use` declaration is missing a capability keyword, one of: \"service\", \"protocol\", \"directory\", \"storage\", \"runner\", \"event\", \"event_stream\""
        ),
        test_cml_use_as_with_meta_storage(
            json!({
                "use": [ { "storage": "meta", "as": "/meta" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used with storage type \"meta\""
        ),
        test_cml_use_as_with_service(
            json!({
                "use": [ { "service": "foo", "as": "xxx" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used with \"service\""
        ),
        test_cml_use_as_with_protocol(
            json!({
                "use": [ { "protocol": "foo", "as": "xxx" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used with \"protocol\""
        ),
        test_cml_use_as_with_directory(
            json!({
                "use": [ { "directory": "foo", "as": "xxx" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used with \"directory\""
        ),
        test_cml_use_as_with_runner(
            json!({
                "use": [ { "runner": "elf", "as": "xxx" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used with \"runner\""
        ),
        test_cml_use_from_with_meta_storage(
            json!({
                "use": [ { "storage": "cache", "from": "parent" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"from\" field cannot be used with \"storage\""
        ),
        test_cml_use_invalid_from(
            json!({
                "use": [
                  { "service": "/fonts/CoolFonts", "from": "self" }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"self\", expected \"parent\", \"framework\", or none"
        ),
        test_cml_use_bad_as(
            json!({
                "use": [
                    {
                        "protocol": ["/fonts/CoolFonts", "/fonts/FunkyFonts"],
                        "as": "/fonts/MyFonts"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `protocol` is supplied."
        ),
        test_cml_use_bad_duplicate_target_paths(
            json!({
                "use": [
                  { "protocol": "fuchsia.sys2.Realm" },
                  { "service": "fuchsia.sys2.Realm" },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"/svc/fuchsia.sys2.Realm\" is a duplicate \"use\" target service"
        ),
        test_cml_use_empty_protocols(
            json!({
                "use": [
                    {
                        "protocol": [],
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a name or path or nonempty array of names or paths, with unique elements"
        ),
        test_cml_use_bad_subdir(
            json!({
                "use": [
                  {
                    "directory": "/data/config",
                    "from": "parent",
                    "rights": [ "r*" ],
                    "subdir": "/",
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_use_resolver_fails(
            json!({
                "use": [
                    {
                        "resolver": "pkg_resolver",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown field `resolver`, expected one of `service`, `protocol`, `directory`, `storage`, `runner`, `from`, `path`, `as`, `rights`, `subdir`, `event`, `event_stream`, `filter`"
        ),

        test_cml_use_disallows_nested_dirs(
            json!({
                "use": [
                    { "directory": "/foo/bar", "rights": [ "r*" ] },
                    { "directory": "/foo/bar/baz", "rights": [ "r*" ] },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "directory \"/foo/bar/baz\" is a prefix of \"use\" target directory \"/foo/bar\""
        ),
        test_cml_use_disallows_common_prefixes_service(
            json!({
                "use": [
                    { "directory": "foobar", "path": "/foo/bar", "rights": [ "r*" ] },
                    { "service": "fuchsia", "path": "/foo/bar/fuchsia" },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "service \"/foo/bar/fuchsia\" is a prefix of \"use\" target directory \"/foo/bar\""
        ),
        test_cml_use_disallows_common_prefixes_protocol(
            json!({
                "use": [
                    { "directory": "foobar", "path": "/foo/bar", "rights": [ "r*" ] },
                    { "protocol": "/foo/bar/fuchsia.2" },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "protocol \"/foo/bar/fuchsia.2\" is a prefix of \"use\" target directory \"/foo/bar\""
        ),
        test_cml_use_disallows_filter_on_non_events(
            json!({
                "use": [
                    { "directory": "/foo/bar", "rights": [ "r*" ], "filter": {"path": "/diagnostics"} },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"filter\" can only be used with \"event\""
        ),
        test_cml_use_bad_as_in_event(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "stopped"],
                        "from": "parent",
                        "as": "gone"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `event` is supplied"
        ),
        test_cml_use_duplicate_events(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "started"],
                        "from": "parent",
                    },
                    {
                        "event": ["destroyed"],
                        "from": "parent",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"destroyed\" is a duplicate \"use\" target event"
        ),
        test_cml_use_event_stream_missing_events(
            json!({
                "use": [
                    {
                        "event_stream": ["destroyed"],
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Event \"destroyed\" in event stream not found in any \"use\" declaration."
        ),
        test_cml_use_bad_filter_in_event(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "stopped"],
                        "filter": {"path": "/diagnostics"},
                        "from": "parent"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"filter\" field can only be specified when one `event` is supplied"
        ),
        test_cml_use_bad_filter_and_as_in_event(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "stopped"],
                        "from": "framework",
                        "as": "gone",
                        "filter": {"path": "/diagnostics"}
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\",\"filter\" fields can only be specified when one `event` is supplied"
        ),

        // expose
        test_cml_expose(
            json!({
                "expose": [
                    {
                        "service": "fuchsia.fonts.Provider",
                        "from": "self",
                    },
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#logger",
                        "as": "logger"
                    },
                    {
                        "protocol": "A",
                        "from": "self",
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
                    { "runner": "elf", "from": "#logger",  },
                    { "resolver": "pkg_resolver", "from": "#logger" },
                ],
                "capabilities": [
                    { "service": "fuchsia.fonts.Provider" },
                    { "protocol": "A" },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_expose_service_multiple_from(
            json!({
                "expose": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": [ "#logger", "self" ],
                    },
                ],
                "capabilities": [
                    { "service": "fuchsia.logger.Log" },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_expose_all_valid_chars(
            json!({
                "expose": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-.",
                    },
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
                        "url": "https://www.google.com/gmail"
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_expose_missing_props(
            json!({
                "expose": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `from`"
        ),
        test_cml_expose_missing_from(
            json!({
                "expose": [
                    { "service": "fuchsia.logger.Log", "from": "#missing" },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"expose\" source \"#missing\" does not appear in \"children\""
        ),
        test_cml_expose_duplicate_target_names(
            json!({
                "expose": [
                    { "service": "logger", "from": "#logger", "as": "thing" },
                    { "protocol": "thing", "from": "self" },
                ],
                "capabilities": [
                    { "protocol": "thing" },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"thing\" is a duplicate \"expose\" target capability for \"parent\""
        ),
        test_cml_expose_duplicate_target_paths(
            json!({
                "expose": [
                    { "protocol": "/svc/logger", "from": "#logger", "as": "/thing" },
                    { "directory": "/thing", "from": "self" , "rights": ["rx*"] },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"/thing\" is a duplicate \"expose\" target capability for \"parent\""
        ),
        test_cml_expose_invalid_multiple_from(
            json!({
                    "expose": [ {
                        "protocol": "/svc/fuchsia.logger.Log",
                        "from": [ "self", "#logger" ],
                    } ],
                    "children": [
                        {
                            "name": "logger",
                            "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                        },
                    ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"protocol\" capabilities cannot have multiple \"from\" clauses"
        ),
        test_cml_expose_bad_from(
            json!({
                "expose": [ {
                    "service": "/loggers/fuchsia.logger.Log", "from": "parent"
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"parent\", expected one or an array of \"framework\", \"self\", or \"#<child-name>\""
        ),
        // if "as" is specified, only 1 "protocol" array item is allowed.
        test_cml_expose_bad_as(
            json!({
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
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `protocol` is supplied."
        ),
        test_cml_expose_empty_protocols(
            json!({
                "expose": [
                    {
                        "protocol": [],
                        "from": "self",
                        "as": "/thing"
                    }
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a name or path or nonempty array of names or paths, with unique elements"
        ),
        test_cml_expose_bad_subdir(
            json!({
                "expose": [
                    {
                        "directory": "/volumes/blobfs",
                        "from": "self",
                        "rights": ["r*"],
                        "subdir": "/",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_expose_invalid_subdir_to_framework(
            json!({
                "expose": [
                    {
                        "directory": "/volumes/blobfs",
                        "from": "self",
                        "to": "framework",
                        "rights": ["r*"],
                        "subdir": "blob",
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "`subdir` is not supported for expose to framework. Directly expose the subdirectory instead."
        ),
        test_cml_expose_from_self(
            json!({
                "expose": [
                    {
                        "service": "foo_service",
                        "from": "self",
                    },
                    {
                        "protocol": "foo_protocol",
                        "from": "self",
                    },
                    {
                        "protocol": [ "bar_protocol", "baz_protocol" ],
                        "from": "self",
                    },
                    {
                        "directory": "foo_directory",
                        "from": "self",
                    },
                    {
                        "runner": "foo_runner",
                        "from": "self",
                    },
                    {
                        "resolver": "foo_resolver",
                        "from": "self",
                    },
                ],
                "capabilities": [
                    {
                        "service": "foo_service",
                    },
                    {
                        "protocol": "foo_protocol",
                    },
                    {
                        "protocol": "bar_protocol",
                    },
                    {
                        "protocol": "baz_protocol",
                    },
                    {
                        "directory": "foo_directory",
                        "path": "/dir",
                        "rights": [ "r*" ],
                    },
                    {
                        "runner": "foo_runner",
                        "path": "/svc/runner",
                        "from": "self",
                    },
                    {
                        "resolver": "foo_resolver",
                        "path": "/svc/resolver",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_expose_service_from_self_missing(
            json!({
                "expose": [
                    {
                        "service": "pkg_service",
                        "from": "self",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Service \"pkg_service\" is exposed from self, so it must be declared as a \"service\" in \"capabilities\""
        ),
        test_cml_expose_protocol_from_self_missing(
            json!({
                "expose": [
                    {
                        "protocol": "pkg_protocol",
                        "from": "self",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Protocol \"pkg_protocol\" is exposed from self, so it must be declared as a \"protocol\" in \"capabilities\""
        ),
        test_cml_expose_protocol_from_self_missing_multiple(
            json!({
                "expose": [
                    {
                        "protocol": [ "foo_protocol", "bar_protocol" ],
                        "from": "self",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Protocol \"foo_protocol\" is exposed from self, so it must be declared as a \"protocol\" in \"capabilities\""
        ),
        test_cml_expose_directory_from_self_missing(
            json!({
                "expose": [
                    {
                        "directory": "pkg_directory",
                        "from": "self",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Directory \"pkg_directory\" is exposed from self, so it must be declared as a \"directory\" in \"capabilities\""
        ),
        test_cml_expose_runner_from_self_missing(
            json!({
                "expose": [
                    {
                        "runner": "dart",
                        "from": "self",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Runner \"dart\" is exposed from self, so it must be declared as a \"runner\" in \"capabilities\""
        ),
        test_cml_expose_resolver_from_self_missing(
            json!({
                "expose": [
                    {
                        "resolver": "pkg_resolver",
                        "from": "self",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Resolver \"pkg_resolver\" is exposed from self, so it must be declared as a \"resolver\" in \"capabilities\""
        ),
        test_cml_expose_to_framework_ok(
            json!({
                "expose": [
                    {
                        "directory": "/foo",
                        "from": "self",
                        "rights": ["r*"],
                        "to": "framework"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_expose_to_framework_invalid(
            json!({
                "expose": [
                    {
                        "directory": "/foo",
                        "from": "#logger",
                        "to": "framework"
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Expose to framework can only be done from self."
        ),

        // offer
        test_cml_offer(
            json!({
                "offer": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#echo_server", "#modular" ],
                        "as": "fuchsia.logger.SysLog"
                    },
                    {
                        "service": "fuchsia.fonts.Provider",
                        "from": "parent",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "service": "fuchsia.net.Netstack",
                        "from": "self",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "protocol": "fuchsia.fonts.LegacyProvider",
                        "from": "parent",
                        "to": [ "#echo_server" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "protocol": "/svc/fuchsia.fonts.LegacyProvider",
                        "from": "parent",
                        "to": [ "#echo_server" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "protocol": [
                            "fuchsia.settings.Accessibility",
                            "fuchsia.ui.scenic.Scenic"
                        ],
                        "from": "parent",
                        "to": [ "#echo_server" ],
                        "dependency": "strong"
                    },
                    {
                        "protocol": [
                            "/svc/fuchsia.settings.Accessibility",
                            "/svc/fuchsia.ui.scenic.Scenic"
                        ],
                        "from": "parent",
                        "to": [ "#echo_server" ],
                        "dependency": "strong"
                    },
                    {
                        "directory": "assets",
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "rights": ["r*"]
                    },
                    {
                        "directory": "/data/assets",
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "rights": ["rw*"]
                    },
                    {
                        "directory": "index",
                        "subdir": "files",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "directory": "/data/index",
                        "subdir": "files",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "directory": "/hub",
                        "from": "framework",
                        "to": [ "#modular" ],
                        "as": "/hub",
                        "dependency": "strong"
                    },
                    {
                        "storage": "data",
                        "from": "#minfs",
                        "to": [ "#modular", "#logger" ]
                    },
                    {
                        "runner": "elf",
                        "from": "parent",
                        "to": [ "#modular", "#logger" ]
                    },
                    {
                        "resolver": "pkg_resolver",
                        "from": "parent",
                        "to": [ "#modular" ],
                    },
                    {
                        "event": "capability_ready",
                        "from": "parent",
                        "to": [ "#modular" ],
                        "as": "capability-ready-for-modular",
                        "filter": {
                            "path": "/modular"
                        }
                    },
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
                "capabilities": [
                    { "service": "fuchsia.net.Netstack" },
                    {
                        "directory": "assets",
                        "path": "/data/assets",
                        "rights": [ "rw*" ],
                    },
                    {
                        "storage": "minfs",
                        "from": "parent",
                        "path": "/minfs",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_offer_service_multiple_from(
            json!({
                "offer": [
                    {
                        "service": "fuchsia.logger.Log",
                        "from": [ "#logger", "parent" ],
                        "to": [ "#echo_server" ],
                    },
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
            }),
            Ok(())
        ),
        test_cml_offer_all_valid_chars(
            json!({
                "offer": [
                    {
                        "service": "fuchsia.logger.Log",
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
                "capabilities": [
                    {
                        "storage": "abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_offer_missing_props(
            json!({
                "offer": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `from`"
        ),
        test_cml_offer_missing_from(
            json!({
                    "offer": [
                        {
                            "service": "fuchsia.logger.Log",
                            "from": "#missing",
                            "to": [ "#echo_server" ],
                        },
                    ],
                    "children": [
                        {
                            "name": "echo_server",
                            "url": "fuchsia-pkg://fuchsia.com/echo_server#meta/echo_server.cm",
                        },
                    ],
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"offer\" source \"#missing\" does not appear in \"children\""
        ),
        test_cml_storage_offer_missing_from(
            json!({
                    "offer": [
                        {
                            "storage": "cache",
                            "from": "#missing",
                            "to": [ "#echo_server" ],
                        },
                    ],
                    "children": [
                        {
                            "name": "echo_server",
                            "url": "fuchsia-pkg://fuchsia.com/echo_server#meta/echo_server.cm",
                        },
                    ],
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"offer\" source \"#missing\" does not appear in \"storage\""
        ),
        test_cml_offer_bad_from(
            json!({
                    "offer": [ {
                        "service": "fuchsia.logger.Log",
                        "from": "#invalid@",
                        "to": [ "#echo_server" ],
                    } ]
                }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"#invalid@\", expected one or an array of \"parent\", \"framework\", \"self\", or \"#<child-name>\""
        ),
        test_cml_offer_invalid_multiple_from(
            json!({
                    "offer": [ {
                        "protocol": "/svc/fuchsia.logger.Log",
                        "from": [ "self", "#logger" ],
                        "to": [ "#echo_server" ],
                    } ],
                    "children": [
                        {
                            "name": "logger",
                            "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                        },
                        {
                            "name": "echo_server",
                            "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm",
                        },
                    ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"protocol\" capabilities cannot have multiple \"from\" clauses"
        ),
        test_cml_storage_offer_bad_to(
            json!({
                    "offer": [ {
                        "storage": "cache",
                        "from": "parent",
                        "to": [ "#logger" ],
                        "as": "/invalid",
                    } ],
                    "children": [ {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm"
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used for storage offer targets"
        ),
        test_cml_offer_empty_targets(
            json!({
                "offer": [ {
                    "service": "fuchsia.logger.Log",
                    "from": "#logger",
                    "to": []
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a nonempty array of offer targets, with unique elements"
        ),
        test_cml_offer_duplicate_targets(
            json!({
                "offer": [ {
                    "service": "fuchsia.logger.Log",
                    "from": "#logger",
                    "to": ["#a", "#a"]
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: array with duplicate element, expected a nonempty array of offer targets, with unique elements"
        ),
        test_cml_offer_target_missing_props(
            json!({
                "offer": [ {
                    "service": "fuchsia.logger.Log",
                    "from": "#logger",
                    "as": "fuchsia.logger.SysLog",
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `to`"
        ),
        test_cml_offer_target_missing_to(
            json!({
                "offer": [ {
                    "service": "fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "#missing" ],
                } ],
                "children": [ {
                    "name": "logger",
                    "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"#missing\" is an \"offer\" target but it does not appear in \"children\" or \"collections\""
        ),
        test_cml_offer_target_bad_to(
            json!({
                "offer": [ {
                    "service": "fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "self" ],
                    "as": "fuchsia.logger.SysLog",
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"self\", expected \"parent\", \"framework\", \"self\", \"#<child-name>\", or \"#<collection-name>\""
        ),
        test_cml_offer_empty_protocols(
            json!({
                "offer": [
                    {
                        "protocol": [],
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a name or path or nonempty array of names or paths, with unique elements"
        ),
        test_cml_offer_target_equals_from(
            json!({
                "offer": [ {
                    "service": "fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "#logger" ],
                    "as": "fuchsia.logger.SysLog",
                } ],
                "children": [ {
                    "name": "logger", "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                } ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Offer target \"#logger\" is same as source"
        ),
        test_cml_storage_offer_target_equals_from(
            json!({
                "offer": [ {
                    "storage": "data",
                    "from": "#minfs",
                    "to": [ "#logger" ],
                } ],
                "children": [ {
                    "name": "logger",
                    "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                } ],
                "capabilities": [ {
                    "storage": "minfs",
                    "from": "#logger",
                    "path": "/minfs",
                } ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Storage offer target \"#logger\" is same as source"
        ),
        test_cml_offer_duplicate_target_names(
            json!({
                "offer": [
                    {
                        "service": "logger",
                        "from": "#logger",
                        "to": [ "#echo_server" ],
                        "as": "thing"
                    },
                    {
                        "directory": "thing",
                        "from": "self",
                        "to": [ "#echo_server" ],
                    },
                ],
                "capabilities": [
                    {
                        "directory": "thing",
                        "path": "/thing",
                        "rights": [ "r*" ],
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"thing\" is a duplicate \"offer\" target capability for \"#echo_server\""
        ),
        test_cml_offer_duplicate_target_paths(
            json!({
                "offer": [
                    {
                        "protocol": "/svc/logger",
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                    {
                        "protocol": "/svc/logger",
                        "from": "self",
                        "to": [ "#scenic" ],
                    },
                    {
                        "directory": "/thing",
                        "from": "parent",
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
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"/thing\" is a duplicate \"offer\" target capability for \"#echo_server\""
        ),
        test_cml_offer_duplicate_storage_types(
            json!({
                "offer": [
                    {
                        "storage": "cache",
                        "from": "parent",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "storage": "cache",
                        "from": "#minfs",
                        "to": [ "#echo_server" ]
                    }
                ],
                "capabilities": [ {
                    "storage": "minfs",
                    "from": "self",
                    "path": "/minfs"
                } ],
                "children": [ {
                    "name": "echo_server",
                    "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"cache\" is a duplicate \"offer\" target capability for \"#echo_server\""
        ),
        // if "as" is specified, only 1 "protocol" array item is allowed.
        test_cml_offer_bad_as(
            json!({
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
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `protocol` is supplied."
        ),
        test_cml_offer_bad_subdir(
            json!({
                "offer": [
                    {
                        "directory": "/data/index",
                        "subdir": "/",
                        "from": "parent",
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
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_offer_from_self(
            json!({
                "offer": [
                    {
                        "service": "foo_service",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                    {
                        "protocol": "foo_protocol",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                    {
                        "protocol": [ "bar_protocol", "baz_protocol" ],
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                    {
                        "directory": "foo_directory",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                    {
                        "runner": "foo_runner",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                    {
                        "resolver": "foo_resolver",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
                "capabilities": [
                    {
                        "service": "foo_service",
                    },
                    {
                        "protocol": "foo_protocol",
                    },
                    {
                        "protocol": "bar_protocol",
                    },
                    {
                        "protocol": "baz_protocol",
                    },
                    {
                        "directory": "foo_directory",
                        "path": "/dir",
                        "rights": [ "r*" ],
                    },
                    {
                        "runner": "foo_runner",
                        "path": "/svc/fuchsia.sys2.ComponentRunner",
                        "from": "self",
                    },
                    {
                        "resolver": "foo_resolver",
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_offer_service_from_self_missing(
            json!({
                "offer": [
                    {
                        "service": "pkg_service",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Service \"pkg_service\" is offered from self, so it must be declared as a \"service\" in \"capabilities\""
        ),
        test_cml_offer_protocol_from_self_missing(
            json!({
                "offer": [
                    {
                        "protocol": "pkg_protocol",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Protocol \"pkg_protocol\" is offered from self, so it must be declared as a \"protocol\" in \"capabilities\""
        ),
        test_cml_offer_protocol_from_self_missing_multiple(
            json!({
                "offer": [
                    {
                        "protocol": [ "foo_protocol", "bar_protocol" ],
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Protocol \"foo_protocol\" is offered from self, so it must be declared as a \"protocol\" in \"capabilities\""
        ),
        test_cml_offer_directory_from_self_missing(
            json!({
                "offer": [
                    {
                        "directory": "pkg_directory",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Directory \"pkg_directory\" is offered from self, so it must be declared as a \"directory\" in \"capabilities\""
        ),
        test_cml_offer_runner_from_self_missing(
            json!({
                "offer": [
                    {
                        "runner": "dart",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Runner \"dart\" is offered from self, so it must be declared as a \"runner\" in \"capabilities\""
        ),
        test_cml_offer_resolver_from_self_missing(
            json!({
                "offer": [
                    {
                        "resolver": "pkg_resolver",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Resolver \"pkg_resolver\" is offered from self, so it must be declared as a \"resolver\" in \"capabilities\""
        ),
        test_cml_offer_dependency_on_wrong_type(
            json!({
                    "offer": [ {
                        "service": "fuchsia.logger.Log",
                        "from": "parent",
                        "to": [ "#echo_server" ],
                        "dependency": "strong",
                    } ],
                    "children": [ {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm",
                    } ],
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Dependency can only be provided for protocol and directory capabilities"
        ),
        test_cml_offer_dependency_cycle(
            json!({
                    "offer": [
                        {
                            "protocol": "/svc/fuchsia.logger.Log",
                            "from": "#a",
                            "to": [ "#b" ],
                            "dependency": "strong"
                        },
                        {
                            "directory": "/data",
                            "from": "#b",
                            "to": [ "#c" ],
                        },
                        {
                            "service": "ethernet",
                            "from": "#c",
                            "to": [ "#a" ],
                        },
                        {
                            "runner": "elf",
                            "from": "#b",
                            "to": [ "#d" ],
                        },
                        {
                            "resolver": "http",
                            "from": "#d",
                            "to": [ "#b" ],
                        },
                    ],
                    "children": [
                        {
                            "name": "a",
                            "url": "fuchsia-pkg://fuchsia.com/a#meta/a.cm"
                        },
                        {
                            "name": "b",
                            "url": "fuchsia-pkg://fuchsia.com/b#meta/b.cm"
                        },
                        {
                            "name": "c",
                            "url": "fuchsia-pkg://fuchsia.com/b#meta/c.cm"
                        },
                        {
                            "name": "d",
                            "url": "fuchsia-pkg://fuchsia.com/b#meta/d.cm"
                        },
                    ]
                }),
            Err(Error::Validate {
                schema_name: None,
                err,
                ..
            }) if &err ==
                "Strong dependency cycles were found. Break the cycle by removing a \
                dependency or marking an offer as weak. Cycles: \
                {{child a -> child b -> child c -> child a}, {child b -> child d -> child b}}"
        ),
        test_cml_offer_weak_dependency_cycle(
            json!({
                    "offer": [
                        {
                            "protocol": "/svc/fuchsia.logger.Log",
                            "from": "#child_a",
                            "to": [ "#child_b" ],
                            "dependency": "weak_for_migration"
                        },
                        {
                            "directory": "/data",
                            "from": "#child_b",
                            "to": [ "#child_a" ],
                        },
                    ],
                    "children": [
                        {
                            "name": "child_a",
                            "url": "fuchsia-pkg://fuchsia.com/child_a#meta/child_a.cm"
                        },
                        {
                            "name": "child_b",
                            "url": "fuchsia-pkg://fuchsia.com/child_b#meta/child_b.cm"
                        },
                    ]
                }),
            Ok(())
        ),
        test_cml_offer_disallows_filter_on_non_events(
            json!({
                "offer": [
                    {
                        "directory": "/foo/bar",
                        "rights": [ "r*" ],
                        "from": "self",
                        "to": [ "#logger" ],
                        "filter": {"path": "/diagnostics"}
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"filter\" can only be used with \"event\""
        ),

        // children
        test_cml_children(
            json!({
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
            Ok(())
        ),
        test_cml_children_missing_props(
            json!({
                "children": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),
        test_cml_children_duplicate_names(
           json!({
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
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"children\" and once in \"children\""
        ),
        test_cml_children_bad_startup(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "zzz",
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `zzz`, expected `lazy` or `eager`"
        ),
        test_cml_children_bad_environment(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "environment": "parent",
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"parent\", expected \"#<environment-name>\""
        ),
        test_cml_children_unknown_environment(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "environment": "#foo_env",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"foo_env\" does not appear in \"environments\""
        ),
        test_cml_children_environment(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "environment": "#foo_env",
                    }
                ],
                "environments": [
                    {
                        "name": "foo_env",
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_collections_bad_environment(
            json!({
                "collections": [
                    {
                        "name": "tests",
                        "durability": "transient",
                        "environment": "parent",
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"parent\", expected \"#<environment-name>\""
        ),
        test_cml_collections_unknown_environment(
            json!({
                "collections": [
                    {
                        "name": "tests",
                        "durability": "transient",
                        "environment": "#foo_env",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"foo_env\" does not appear in \"environments\""
        ),
        test_cml_collections_environment(
            json!({
                "collections": [
                    {
                        "name": "tests",
                        "durability": "transient",
                        "environment": "#foo_env",
                    }
                ],
                "environments": [
                    {
                        "name": "foo_env",
                    }
                ]
            }),
            Ok(())
        ),


        test_cml_environment_timeout(
            json!({
                "environments": [
                    {
                        "name": "foo_env",
                        "__stop_timeout_ms": 10000,
                    }
                ]
            }),
            Ok(())
        ),

        test_cml_environment_bad_timeout(
            json!({
                "environments": [
                    {
                        "name": "foo_env",
                        "__stop_timeout_ms": -3,
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: integer `-3`, expected an unsigned 32-bit integer"
        ),

        // collections
        test_cml_collections(
            json!({
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
            Ok(())
        ),
        test_cml_collections_missing_props(
            json!({
                "collections": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),
        test_cml_collections_duplicate_names(
           json!({
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
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"modular\" is defined twice, once in \"collections\" and once in \"collections\""
        ),
        test_cml_collections_bad_durability(
            json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "zzz",
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `zzz`, expected `persistent` or `transient`"
        ),

        // capabilities
        test_cml_service(
            json!({
                "capabilities": [
                    {
                        "service": "a",
                        "path": "/minfs",
                    },
                    {
                        "service": "b",
                        "path": "/data",
                    },
                    {
                        "service": "c",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_service_all_valid_chars(
            json!({
                "capabilities": [
                    {
                        "service": "abcdefghijklmnopqrstuvwxyz0123456789_-service",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_protocol(
            json!({
                "capabilities": [
                    {
                        "protocol": "a",
                        "path": "/minfs",
                    },
                    {
                        "protocol": "b",
                        "path": "/data",
                    },
                    {
                        "protocol": "c",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_protocol_multi(
            json!({
                "capabilities": [
                    {
                        "protocol": ["a", "b", "c"],
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_protocol_multi_invalid_path(
            json!({
                "capabilities": [
                    {
                        "protocol": ["a", "b", "c"],
                        "path": "/minfs",
                    },
                ],
            }),
            Err(Error::Validate { err, .. }) if &err == "\"path\" field can only be specified when one `protocol` is supplied."
        ),
        test_cml_protocol_all_valid_chars(
            json!({
                "capabilities": [
                    {
                        "protocol": "abcdefghijklmnopqrstuvwxyz0123456789_-service",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_directory(
            json!({
                "capabilities": [
                    {
                        "directory": "a",
                        "path": "/minfs",
                        "rights": ["connect"],
                    },
                    {
                        "directory": "b",
                        "path": "/data",
                        "rights": ["connect"],
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_directory_all_valid_chars(
            json!({
                "capabilities": [
                    {
                        "directory": "abcdefghijklmnopqrstuvwxyz0123456789_-service",
                        "path": "/data",
                        "rights": ["connect"],
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_directory_missing_path(
            json!({
                "capabilities": [
                    {
                        "directory": "dir",
                        "rights": ["connect"],
                    },
                ]
            }),
            Err(Error::Validate { err, .. }) if &err == "\"path\" should be present with \"directory\""
        ),
        test_cml_directory_missing_rights(
            json!({
                "capabilities": [
                    {
                        "directory": "dir",
                        "path": "/dir",
                    },
                ]
            }),
            Err(Error::Validate { err, .. }) if &err == "\"rights\" should be present with \"directory\""
        ),
        test_cml_storage(
            json!({
                "capabilities": [
                    {
                        "storage": "a",
                        "from": "#minfs",
                        "path": "/minfs",
                    },
                    {
                        "storage": "b",
                        "from": "parent",
                        "path": "/data",
                    },
                    {
                        "storage": "c",
                        "from": "self",
                        "backing_dir": "storage",
                    },
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_storage_all_valid_chars(
            json!({
                "capabilities": [
                    {
                        "storage": "abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example",
                    },
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_storage_invalid_from(
            json!({
                    "capabilities": [ {
                        "storage": "minfs",
                        "from": "#missing",
                        "path": "/minfs"
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"capabilities\" source \"#missing\" does not appear in \"children\""
        ),
        test_cml_storage_missing_path_or_backing_dir(
            json!({
                    "capabilities": [ {
                        "storage": "minfs",
                        "from": "self",
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "One of \"path\" or \"backing_dir\" must be present with \"storage\""

        ),
        test_cml_storage_both_path_and_backing_dir(
            json!({
                    "capabilities": [ {
                        "storage": "minfs",
                        "from": "self",
                        "path": "/minfs",
                        "backing_dir": "minfs",
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"path\" and \"backing_dir\" may not both be present with \"storage\""
        ),
        test_cml_runner(
            json!({
                "capabilities": [
                    {
                        "runner": "a",
                        "from": "#minfs",
                        "path": "/minfs",
                    },
                    {
                        "runner": "b",
                        "from": "parent",
                        "path": "/data",
                    },
                    {
                        "runner": "c",
                        "from": "self",
                        "path": "/runner",
                    },
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm",
                    },
                ],
            }),
            Ok(())
        ),
        test_cml_runner_all_valid_chars(
            json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail"
                    },
                ],
                "capabilities": [
                    {
                        "runner": "abcdefghijklmnopqrstuvwxyz0123456789_-runner",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_runner_invalid_from(
            json!({
                    "capabilities": [ {
                        "runner": "minfs",
                        "from": "#missing",
                        "path": "/minfs"
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"capabilities\" source \"#missing\" does not appear in \"children\""
        ),
        test_cml_capability_missing_name(
            json!({
                "capabilities": [
                    {
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                    },
                ]
            }),
            Err(Error::Validate { err, .. }) if &err == "`capability` declaration is missing a capability keyword, one of: \"service\", \"protocol\", \"directory\", \"storage\", \"runner\", \"resolver\""
        ),
        test_cml_resolver_missing_path(
            json!({
                "capabilities": [
                    {
                        "resolver": "pkg_resolver",
                    },
                ]
            }),
            Err(Error::Validate { err, .. }) if &err == "\"path\" should be present with \"resolver\""
        ),
        test_cml_capabilities_extraneous_from(
            json!({
                "capabilities": [
                    {
                        "resolver": "pkg_resolver",
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                        "from": "self",
                    },
                ]
            }),
            Err(Error::Validate { err, .. }) if &err == "\"from\" should not be present with \"resolver\""
        ),
        test_cml_capabilities_duplicates(
            json!({
                "capabilities": [
                    {
                        "runner": "pkg_resolver",
                        "from": "self",
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                    },
                    {
                        "resolver": "pkg_resolver",
                        "path": "/svc/my-resolver",
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"pkg_resolver\" is defined twice, once in \"resolvers\" and once in \"runners\""
        ),

        // environments
        test_cml_environments(
            json!({
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
                        "__stop_timeout_ms": 8000,
                    },
                ],
            }),
            Ok(())
        ),

        test_invalid_cml_environment_no_stop_timeout(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "none",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err ==
                "'__stop_timeout_ms' must be provided if the environment does not extend \
                another environment"
        ),

        test_cml_environment_invalid_extends(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "some_made_up_string",
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `some_made_up_string`, expected `realm` or `none`"
        ),
        test_cml_environment_missing_props(
            json!({
                "environments": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),

        test_cml_environment_with_runners(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "parent",
                            }
                        ]
                    }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_runners_alias(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "parent",
                                "as": "my-dart",
                            }
                        ]
                    }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_runners_missing(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "self",
                            }
                        ]
                    }
                ],
                "capabilities": [
                     {
                         "runner": "dart",
                         "path": "/svc/fuchsia.component.Runner",
                         "from": "parent"
                     }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_runners_bad_name(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "elf",
                                "from": "parent",
                                "as": "#elf",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"#elf\", expected a name containing only alpha-numeric characters or [_-.]"
        ),
        test_cml_environment_with_runners_duplicate_name(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "parent",
                            },
                            {
                                "runner": "other-dart",
                                "from": "parent",
                                "as": "dart",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Duplicate runners registered under name \"dart\": \"other-dart\" and \"dart\"."
        ),
        test_cml_environment_with_runner_from_missing_child(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "elf",
                                "from": "#missing_child",
                            }
                        ]
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"elf\" runner source \"#missing_child\" does not appear in \"children\""
        ),
        test_cml_environment_with_runner_cycle(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "elf",
                                "from": "#child",
                                "as": "my-elf",
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "child",
                        "url": "fuchsia-pkg://child",
                        "environment": "#my_env",
                    }
                ]
            }),
            Err(Error::Validate { err, schema_name: None, .. }) if &err ==
                    "Strong dependency cycles were found. Break the cycle by removing a \
                    dependency or marking an offer as weak. Cycles: \
                    {{child child -> environment my_env -> child child}}"
        ),
        test_cml_environment_with_resolvers(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "parent",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_resolvers_bad_scheme(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "parent",
                                "scheme": "9scheme",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"9scheme\", expected a valid URL scheme"
        ),
        test_cml_environment_with_resolvers_duplicate_scheme(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "parent",
                                "scheme": "fuchsia-pkg",
                            },
                            {
                                "resolver": "base_resolver",
                                "from": "parent",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "scheme \"fuchsia-pkg\" for resolver \"base_resolver\" is already registered; previously registered to resolver \"pkg_resolver\"."
        ),
        test_cml_environment_with_resolver_from_missing_child(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "#missing_child",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"pkg_resolver\" resolver source \"#missing_child\" does not appear in \"children\""
        ),
        test_cml_environment_with_resolver_cycle(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "#child",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "child",
                        "url": "fuchsia-pkg://child",
                        "environment": "#my_env",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err ==
                    "Strong dependency cycles were found. Break the cycle by removing a \
                    dependency or marking an offer as weak. \
                    Cycles: {{child child -> environment my_env -> child child}}"
        ),
        test_cml_environment_with_cycle_multiple_components(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "#b",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "a",
                        "url": "fuchsia-pkg://a",
                        "environment": "#my_env",
                    },
                    {
                        "name": "b",
                        "url": "fuchsia-pkg://b",
                    }
                ],
                "offer": [
                    {
                        "protocol": "/svc/fuchsia.logger.Log",
                        "from": "#a",
                        "to": [ "#b" ],
                        "dependency": "strong"
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err ==
                "Strong dependency cycles were found. Break the cycle by removing a dependency \
                or marking an offer as weak. \
                Cycles: {{child a -> child b -> environment my_env -> child a}}"
        ),

        // facets
        test_cml_facets(
            json!({
                "facets": {
                    "metadata": {
                        "title": "foo",
                        "authors": [ "me", "you" ],
                        "year": 2018
                    }
                }
            }),
            Ok(())
        ),
        test_cml_facets_wrong_type(
            json!({
                "facets": 55
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid type: integer `55`, expected a map"
        ),

        // constraints
        test_cml_rights_all(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["connect", "enumerate", "read_bytes", "write_bytes",
                               "execute", "update_attributes", "get_attributes", "traverse",
                               "modify_directory", "admin"],
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_rights_invalid(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["cAnnect", "enumerate"],
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `cAnnect`, expected one of `connect`, `enumerate`, `execute`, `get_attributes`, `modify_directory`, `read_bytes`, `traverse`, `update_attributes`, `write_bytes`, `admin`, `r*`, `w*`, `x*`, `rw*`, `rx*`"
        ),
        test_cml_rights_duplicate(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["connect", "connect"],
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: array with duplicate element, expected a nonempty array of rights, with unique elements"
        ),
        test_cml_rights_empty(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": [],
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a nonempty array of rights, with unique elements"
        ),
        test_cml_rights_alias_star_expansion(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["r*"],
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_rights_alias_star_expansion_with_longform(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["w*", "read_bytes"],
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_rights_alias_star_expansion_with_longform_collision(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["r*", "read_bytes"],
                  },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"read_bytes\" is duplicated in the rights clause."
        ),
        test_cml_rights_alias_star_expansion_collision(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["w*", "x*"],
                  },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"x*\" is duplicated in the rights clause."
        ),
        test_cml_rights_use_invalid(
            json!({
                "use": [
                  { "directory": "/foo", },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Rights required for this use statement."
        ),
        test_cml_rights_offer_dir_invalid(
            json!({
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
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Rights required for this offer as it is offering from self."
        ),
        test_cml_rights_expose_dir_invalid(
            json!({
                "expose": [
                  {
                    "directory": "/foo/bar",
                    "from": "self",
                  },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Rights required for this expose statement as it is exposing from self."
        ),
        test_cml_path(
            json!({
                "capabilities": [
                    {
                        "protocol": "foo",
                        "path": "/foo/?!@#$%/Bar",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_path_invalid_empty(
            json!({
                "capabilities": [
                    { "protocol": "foo", "path": "" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_path_invalid_root(
            json!({
                "capabilities": [
                    { "protocol": "foo", "path": "/" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with leading `/` and non-empty segments"
        ),
        test_cml_path_invalid_absolute_is_relative(
            json!({
                "capabilities": [
                    { "protocol": "foo", "path": "foo/bar" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"foo/bar\", expected a path with leading `/` and non-empty segments"
        ),
        test_cml_path_invalid_trailing(
            json!({
                "capabilities": [
                    { "protocol": "foo", "path":"/foo/bar/" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/foo/bar/\", expected a path with leading `/` and non-empty segments"
        ),
        test_cml_path_too_long(
            json!({
                "capabilities": [
                    { "protocol": "foo", "path": format!("/{}", "a".repeat(1024)) },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 1025, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_relative_path(
            json!({
                "use": [
                    {
                        "directory": "foo",
                        "path": "/foo",
                        "rights": ["r*"],
                        "subdir": "?!@#$%/Bar",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_relative_path_invalid_empty(
            json!({
                "use": [
                    {
                        "directory": "foo",
                        "path": "/foo",
                        "rights": ["r*"],
                        "subdir": "",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_relative_path_invalid_root(
            json!({
                "use": [
                    {
                        "directory": "foo",
                        "path": "/foo",
                        "rights": ["r*"],
                        "subdir": "/",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_relative_path_invalid_absolute(
            json!({
                "use": [
                    {
                        "directory": "foo",
                        "path": "/foo",
                        "rights": ["r*"],
                        "subdir": "/bar",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/bar\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_relative_path_invalid_trailing(
            json!({
                "use": [
                    {
                        "directory": "foo",
                        "path": "/foo",
                        "rights": ["r*"],
                        "subdir": "bar/",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"bar/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_relative_path_too_long(
            json!({
                "use": [
                    {
                        "directory": "foo",
                        "path": "/foo",
                        "rights": ["r*"],
                        "subdir": format!("{}", "a".repeat(1025)),
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 1025, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_relative_ref_too_long(
            json!({
                "expose": [
                    {
                        "service": "fuchsia.logger.Log",
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
            Err(Error::Parse { err, .. }) if &err == "invalid length 102, expected one or an array of \"framework\", \"self\", or \"#<child-name>\""
        ),
        test_cml_child_name(
            json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_child_name_invalid(
            json!({
                "children": [
                    {
                        "name": "#bad",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"#bad\", expected a name containing only alpha-numeric characters or [_-.]"
        ),
        test_cml_child_name_too_long(
            json!({
                "children": [
                    {
                        "name": "a".repeat(101),
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 101, expected a non-empty name no more than 100 characters in length"
        ),
        test_cml_url(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "my+awesome-scheme.2://abc123!@#$%.com",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_url_invalid(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"fuchsia-pkg\", expected a valid URL"
        ),
        test_cml_url_too_long(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": &format!("fuchsia-pkg://{}", "a".repeat(4083)),
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 4097, expected a non-empty URL no more than 4096 characters in length"
        ),
        test_cml_duplicate_identifiers_children_collection(
           json!({
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
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"collections\" and once in \"children\""
        ),
        test_cml_duplicate_identifiers_children_storage(
           json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "capabilities": [
                    {
                        "storage": "logger",
                        "path": "/logs",
                        "from": "parent"
                    }
                ]
           }),
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"storage\" and once in \"children\""
        ),
        test_cml_duplicate_identifiers_collection_storage(
           json!({
               "collections": [
                    {
                        "name": "logger",
                        "durability": "transient"
                    }
                ],
                "capabilities": [
                    {
                        "storage": "logger",
                        "path": "/logs",
                        "from": "parent"
                    }
                ]
           }),
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"storage\" and once in \"collections\""
        ),
        test_cml_duplicate_identifiers_children_runners(
           json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "capabilities": [
                    {
                        "runner": "logger",
                        "path": "/logs",
                        "from": "parent"
                    }
                ]
           }),
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"runners\" and once in \"children\""
        ),
        test_cml_duplicate_identifiers_environments(
            json!({
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
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"environments\" and once in \"children\""
        ),
        test_cml_program_no_runner(
            json!({"program": { "binary": "bin/app" }}),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Component has a \'program\' block defined, but doesn\'t \'use\' a runner capability. Components need to \'use\' a runner to actually execute code."
        ),

        // deny unknown fields
        test_deny_unknown_fields(
            json!(
                {
                    "program": { "binary": "bin/app" },
                    "use": [ { "runner": "elf" } ],
                    "unknown_field": {},
                }
            ),
            Err(Error::Parse { err, .. }) if err.starts_with("unknown field `unknown_field`, expected one of ")
        ),
    }

    test_validate_cmx! {
        test_cmx_err_empty_json(
            json!({}),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "This property is required at /program"
        ),
        test_cmx_program(
            json!({"program": { "binary": "bin/app" }}),
            Ok(())
        ),
        test_cmx_program_no_binary(
            json!({ "program": {}}),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "OneOf conditions are not met at /program"
        ),
        test_cmx_bad_program(
            json!({"prigram": { "binary": "bin/app" }}),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Property conditions are not met at , This property is required at /program"
        ),
        test_cmx_sandbox(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": { "dev": [ "class/camera" ] }
            }),
            Ok(())
        ),
        test_cmx_facets(
            json!({
                "program": { "binary": "bin/app" },
                "facets": {
                    "fuchsia.test": {
                         "system-services": [ "fuchsia.logger.LogSink" ]
                    }
                }
            }),
            Ok(())
        ),
        test_cmx_block_system_data(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_data_stem(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data-should-pass" ]
                }
            }),
            Ok(())
        ),
        test_cmx_block_system_data_leading_slash(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "/data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_data_subdir(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data/should-fail" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_deprecated_data(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_deprecated_data_stem(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data-should-pass" ]
                }
            }),
            Ok(())
        ),
        test_cmx_block_system_deprecated_data_leading_slash(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "/deprecated-data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_deprecated_data_subdir(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data/should-fail" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
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
                $test_name:ident($input:expr, $extra_schemas:expr, $($pattern:tt)+),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() -> Result<(), Error> {
                    let tmp_dir = TempDir::new()?;
                    let tmp_cmx_path = tmp_dir.path().join("test.cmx");
                    let input = format!("{}", $input);
                    File::create(&tmp_cmx_path)?.write_all(input.as_bytes())?;
                    let extra_schemas: &[(&JsonSchema<'_>, Option<String>)] = $extra_schemas;
                    let extra_schema_paths: Vec<_> = extra_schemas
                        .iter()
                        .map(|i| (Path::new(&*i.0.name), i.1.clone()))
                        .collect();
                    let result = validate(&[tmp_cmx_path.as_path()], &extra_schema_paths);
                    assert_matches!(result, $($pattern)+);
                    Ok(())
                }
            )+
        }
    }

    test_validate_extra_schemas! {
        test_validate_extra_schemas_empty_json(
            json!({"program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Ok(())
        ),
        test_validate_extra_schemas_empty_features(
            json!({"sandbox": {"features": []}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Ok(())
        ),
        test_validate_extra_schemas_feature_not_present(
            json!({"sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Ok(())
        ),
        test_validate_extra_schemas_feature_present(
            json!({"sandbox": {"features" : ["deprecated-shell"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_SHELL_FEATURE_SCHEMA.name && &err == "Not condition is not met at /sandbox/features/0"
        ),
        test_validate_extra_schemas_block_dev(
            json!({"dev": ["misc"], "program": {"binary": "a"}}),
            &[(&BLOCK_DEV_SCHEMA, None)],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_DEV_SCHEMA.name && &err == "Not condition is not met at /dev"
        ),
        test_validate_multiple_extra_schemas_valid(
            json!({"sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None), (&BLOCK_DEV_SCHEMA, None)],
            Ok(())
        ),
        test_validate_multiple_extra_schemas_invalid(
            json!({"dev": ["misc"], "sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None), (&BLOCK_DEV_SCHEMA, None)],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_DEV_SCHEMA.name && &err == "Not condition is not met at /dev"
        ),
        test_validate_extra_error(
            json!({"dev": ["misc"], "program": {"binary": "a"}}),
            &[(&BLOCK_DEV_SCHEMA, Some("Extra error".to_string()))],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_DEV_SCHEMA.name && &err == "Not condition is not met at /dev\nExtra error"
        ),
    }
}
