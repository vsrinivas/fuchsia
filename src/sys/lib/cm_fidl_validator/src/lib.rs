// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) mod util;

pub mod error;

use {
    crate::{error::*, util::*},
    directed_graph::DirectedGraph,
    fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
    itertools::Itertools,
    std::{
        collections::{HashMap, HashSet},
        fmt,
        path::Path,
    },
};

/// Validates Configuration Value Spec.
///
/// For now, this simply verifies that all semantically required fields are present.
pub fn validate_value_spec(spec: &fconfig::ValueSpec) -> Result<(), ErrorList> {
    let mut errors = vec![];
    if let Some(value) = &spec.value {
        match value {
            fconfig::Value::Single(s) => match s {
                fconfig::SingleValue::Bool(_)
                | fconfig::SingleValue::Uint8(_)
                | fconfig::SingleValue::Uint16(_)
                | fconfig::SingleValue::Uint32(_)
                | fconfig::SingleValue::Uint64(_)
                | fconfig::SingleValue::Int8(_)
                | fconfig::SingleValue::Int16(_)
                | fconfig::SingleValue::Int32(_)
                | fconfig::SingleValue::Int64(_)
                | fconfig::SingleValue::String(_) => {}
                fconfig::SingleValueUnknown!() => {
                    errors.push(Error::invalid_field("ValueSpec", "value"));
                }
            },
            fconfig::Value::Vector(l) => match l {
                fconfig::VectorValue::BoolVector(_)
                | fconfig::VectorValue::Uint8Vector(_)
                | fconfig::VectorValue::Uint16Vector(_)
                | fconfig::VectorValue::Uint32Vector(_)
                | fconfig::VectorValue::Uint64Vector(_)
                | fconfig::VectorValue::Int8Vector(_)
                | fconfig::VectorValue::Int16Vector(_)
                | fconfig::VectorValue::Int32Vector(_)
                | fconfig::VectorValue::Int64Vector(_)
                | fconfig::VectorValue::StringVector(_) => {}
                fconfig::VectorValueUnknown!() => {
                    errors.push(Error::invalid_field("ValueSpec", "value"));
                }
            },
            fconfig::ValueUnknown!() => {
                errors.push(Error::invalid_field("ValueSpec", "value"));
            }
        }
    } else {
        errors.push(Error::missing_field("ValueSpec", "value"));
    }

    if errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList::new(errors))
    }
}

/// Validates Configuration Values Data.
///
/// The Value Data may ultimately originate from a CVF file, or be directly constructed by the
/// caller. Either way, Value Data should always be validated before it's used. For now, this
/// simply verifies that all semantically required fields are present.
///
/// This method does not validate value data against a configuration schema.
pub fn validate_values_data(data: &fconfig::ValuesData) -> Result<(), ErrorList> {
    let mut errors = vec![];
    if let Some(values) = &data.values {
        for spec in values {
            if let Err(mut e) = validate_value_spec(spec) {
                errors.append(&mut e.errs);
            }
        }
    } else {
        errors.push(Error::missing_field("ValuesData", "values"));
    }

    if let Some(checksum) = &data.checksum {
        match checksum {
            fdecl::ConfigChecksum::Sha256(_) => {}
            fdecl::ConfigChecksumUnknown!() => {
                errors.push(Error::invalid_field("ValuesData", "checksum"));
            }
        }
    } else {
        errors.push(Error::missing_field("ValuesData", "checksum"));
    }

    if errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList::new(errors))
    }
}

/// Validates a Component.
///
/// The Component may ultimately originate from a CM file, or be directly constructed by the
/// caller. Either way, a Component should always be validated before it's used. Examples
/// of what is validated (which may evolve in the future):
///
/// - That all semantically required fields are present
/// - That a child_name referenced in a source actually exists in the list of children
/// - That there are no duplicate target paths.
/// - That only weak-dependency capabilities may be offered back to the
///   component that exposed them.
///
/// All checks are local to this Component.
pub fn validate(decl: &fdecl::Component) -> Result<(), ErrorList> {
    let ctx = ValidationContext::default();
    ctx.validate(decl).map_err(|errs| ErrorList::new(errs))
}

/// Validates a list of Capabilitys independently.
pub fn validate_capabilities(
    capabilities: &Vec<fdecl::Capability>,
    as_builtin: bool,
) -> Result<(), ErrorList> {
    let mut ctx = ValidationContext::default();
    for capability in capabilities {
        ctx.validate_capability_decl(capability, as_builtin);
    }
    if ctx.errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList::new(ctx.errors))
    }
}

/// An interface to call into either `check_dynamic_name()` or `check_name()`, depending on the context
/// of the caller.
type CheckChildNameFn = fn(Option<&String>, &str, &str, &mut Vec<Error>) -> bool;

pub fn validate_dynamic_child(child: &fdecl::Child) -> Result<(), ErrorList> {
    validate_child(child, check_dynamic_name)
}

/// Validates an independent Child. Performs the same validation on it as `validate`. A
/// `check_name_fn` is passed into specify the function used to validate the child name.
fn validate_child(
    child: &fdecl::Child,
    check_child_name: CheckChildNameFn,
) -> Result<(), ErrorList> {
    let mut errors = vec![];
    check_child_name(child.name.as_ref(), "Child", "name", &mut errors);
    check_url(child.url.as_ref(), "Child", "url", &mut errors);
    if child.startup.is_none() {
        errors.push(Error::missing_field("Child", "startup"));
    }
    // Allow `on_terminate` to be unset since the default is almost always desired.
    if child.environment.is_some() {
        check_name(child.environment.as_ref(), "Child", "environment", &mut errors);
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList { errs: errors })
    }
}

/// Validates a collection of dynamic offers. Dynamic offers differ from static
/// offers, in that
///
/// 1. a dynamic offer's `target` field must be omitted;
/// 2. a dynamic offer's `source` _may_ be a dynamic child;
/// 3. since this crate isn't really designed to handle dynamic children, we
///    disable the checks that ensure that the source/target exist, and that the
///    offers don't introduce any cycles.
pub fn validate_dynamic_offers(offers: &Vec<fdecl::Offer>) -> Result<(), ErrorList> {
    let mut ctx = ValidationContext::default();
    for offer in offers {
        ctx.validate_offers_decl(offer, OfferType::Dynamic)
    }
    ctx.validate_offer_group(offers);
    if ctx.errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList::new(ctx.errors))
    }
}

fn check_offer_name(
    prop: Option<&String>,
    decl: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
    offer_type: OfferType,
) -> bool {
    match offer_type {
        OfferType::Static => check_name(prop, decl, keyword, errors),
        OfferType::Dynamic => check_dynamic_name(prop, decl, keyword, errors),
    }
}

#[derive(Default)]
struct ValidationContext<'a> {
    all_children: HashMap<&'a str, &'a fdecl::Child>,
    all_collections: HashSet<&'a str>,
    all_capability_ids: HashSet<&'a str>,
    all_storage_and_sources: HashMap<&'a str, Option<&'a fdecl::Ref>>,
    all_services: HashSet<&'a str>,
    all_protocols: HashSet<&'a str>,
    all_directories: HashSet<&'a str>,
    all_runners: HashSet<&'a str>,
    all_resolvers: HashSet<&'a str>,
    all_environment_names: HashSet<&'a str>,
    all_events: HashSet<&'a str>,
    all_event_streams: HashSet<&'a str>,
    strong_dependencies: DirectedGraph<DependencyNode<'a>>,
    target_ids: IdMap<'a>,
    errors: Vec<Error>,
}

/// A node in the DependencyGraph. The first string describes the type of node and the second
/// string is the name of the node.
#[derive(Copy, Clone, Hash, Ord, Debug, PartialOrd, PartialEq, Eq)]
enum DependencyNode<'a> {
    Self_,
    Child(&'a str),
    Collection(&'a str),
    Environment(&'a str),
    /// This variant is automatically translated to the source backing the capability by
    /// `add_strong_dep`, it does not appear in the dependency graph.
    Capability(&'a str),
}

impl<'a> DependencyNode<'a> {
    fn try_from_ref(ref_: Option<&'a fdecl::Ref>) -> Option<DependencyNode<'a>> {
        if ref_.is_none() {
            return None;
        }
        match ref_.unwrap() {
            fdecl::Ref::Child(fdecl::ChildRef { name, .. }) => {
                Some(DependencyNode::Child(name.as_str()))
            }
            fdecl::Ref::Collection(fdecl::CollectionRef { name, .. }) => {
                Some(DependencyNode::Collection(name.as_str()))
            }
            fdecl::Ref::Capability(fdecl::CapabilityRef { name, .. }) => {
                Some(DependencyNode::Capability(name.as_str()))
            }
            fdecl::Ref::Self_(_) => Some(DependencyNode::Self_),
            fdecl::Ref::Parent(_) => {
                // We don't care about dependency cycles with the parent, as any potential issues
                // with that are resolved by cycle detection in the parent's manifest.
                None
            }
            fdecl::Ref::Framework(_) => {
                // We don't care about dependency cycles with the framework, as the framework
                // always outlives the component.
                None
            }
            fdecl::Ref::Debug(_) => {
                // We don't care about dependency cycles with any debug capabilities from the
                // environment, as those are put there by our parent, and any potential cycles with
                // our parent are handled by cycle detection in the parent's manifest.
                None
            }
            _ => {
                // We were unable to understand this FIDL value
                None
            }
        }
    }
}

impl<'a> fmt::Display for DependencyNode<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DependencyNode::Self_ => write!(f, "self"),
            DependencyNode::Child(name) => write!(f, "child {}", name),
            DependencyNode::Collection(name) => write!(f, "collection {}", name),
            DependencyNode::Environment(name) => write!(f, "environment {}", name),
            DependencyNode::Capability(name) => write!(f, "capability {}", name),
        }
    }
}

impl<'a> ValidationContext<'a> {
    fn validate(mut self, decl: &'a fdecl::Component) -> Result<(), Vec<Error>> {
        // Collect all environment names first, so that references to them can be checked.
        if let Some(envs) = &decl.environments {
            self.collect_environment_names(&envs);
        }

        // Validate "program".
        if let Some(program) = decl.program.as_ref() {
            self.validate_program(program);
        }

        // Validate "children" and build the set of all children.
        if let Some(children) = decl.children.as_ref() {
            for child in children {
                self.validate_child_decl(&child);
            }
        }

        // Validate "collections" and build the set of all collections.
        if let Some(collections) = decl.collections.as_ref() {
            for collection in collections {
                self.validate_collection_decl(&collection);
            }
        }

        // Validate "capabilities" and build the set of all capabilities.
        if let Some(capabilities) = decl.capabilities.as_ref() {
            for capability in capabilities {
                self.validate_capability_decl(capability, false);
            }
        }

        // Validate "uses".
        if let Some(uses) = decl.uses.as_ref() {
            self.validate_use_decls(uses);
        }

        // Validate "exposes".
        if let Some(exposes) = decl.exposes.as_ref() {
            let mut target_ids = HashMap::new();
            for expose in exposes.iter() {
                self.validate_expose_decl(&expose, &mut target_ids);
            }
        }

        // Validate "offers".
        if let Some(offers) = decl.offers.as_ref() {
            for offer in offers.iter() {
                self.validate_offers_decl(&offer, OfferType::Static);
            }
            self.validate_offer_group(offers);
        }

        // Validate "environments" after all other declarations are processed.
        if let Some(environment) = decl.environments.as_ref() {
            for environment in environment {
                self.validate_environment_decl(&environment);
            }
        }

        // Validate "config"
        if let Some(config) = decl.config.as_ref() {
            self.validate_config(&config);
        }

        // Check that there are no strong cyclical dependencies
        if let Err(e) = self.strong_dependencies.topological_sort() {
            self.errors.push(Error::dependency_cycle(e.format_cycle()));
        }

        if self.errors.is_empty() {
            Ok(())
        } else {
            Err(self.errors)
        }
    }

    /// Collects all the environment names, watching for duplicates.
    fn collect_environment_names(&mut self, envs: &'a [fdecl::Environment]) {
        for env in envs {
            if let Some(name) = env.name.as_ref() {
                if !self.all_environment_names.insert(name) {
                    self.errors.push(Error::duplicate_field("Environment", "name", name));
                }
            }
        }
    }

    // Validates a config schema. Checks that each field's layout matches the expected constraints
    // and properties.
    fn validate_config(&mut self, config: &fdecl::ConfigSchema) {
        if let Some(fields) = &config.fields {
            for field in fields {
                if field.key.is_none() {
                    self.errors.push(Error::missing_field("ConfigField", "key"));
                }
                if let Some(type_) = &field.type_ {
                    self.validate_config_type(type_, true);
                } else {
                    self.errors.push(Error::missing_field("ConfigField", "value_type"));
                }
            }
        } else {
            self.errors.push(Error::missing_field("ConfigSchema", "fields"));
        }

        if let Some(checksum) = &config.checksum {
            match checksum {
                fdecl::ConfigChecksum::Sha256(_) => {}
                fdecl::ConfigChecksumUnknown!() => {
                    self.errors.push(Error::invalid_field("ConfigSchema", "checksum"));
                }
            }
        } else {
            self.errors.push(Error::missing_field("ConfigSchema", "checksum"));
        }

        if config.value_source.is_none() {
            self.errors.push(Error::missing_field("ConfigSchema", "value_source"));
        }
    }

    fn validate_config_type(&mut self, type_: &fdecl::ConfigType, accept_vectors: bool) {
        match &type_.layout {
            fdecl::ConfigTypeLayout::Bool
            | fdecl::ConfigTypeLayout::Uint8
            | fdecl::ConfigTypeLayout::Uint16
            | fdecl::ConfigTypeLayout::Uint32
            | fdecl::ConfigTypeLayout::Uint64
            | fdecl::ConfigTypeLayout::Int8
            | fdecl::ConfigTypeLayout::Int16
            | fdecl::ConfigTypeLayout::Int32
            | fdecl::ConfigTypeLayout::Int64 => {
                // These layouts have no parameters or constraints
                if let Some(parameters) = &type_.parameters {
                    if !parameters.is_empty() {
                        self.errors.push(Error::extraneous_field("ConfigType", "parameters"));
                    }
                } else {
                    self.errors.push(Error::missing_field("ConfigType", "parameters"));
                }

                if !type_.constraints.is_empty() {
                    self.errors.push(Error::extraneous_field("ConfigType", "constraints"));
                }
            }
            fdecl::ConfigTypeLayout::String => {
                // String has exactly one constraint and no parameter
                if let Some(parameters) = &type_.parameters {
                    if !parameters.is_empty() {
                        self.errors.push(Error::extraneous_field("ConfigType", "parameters"));
                    }
                } else {
                    self.errors.push(Error::missing_field("ConfigType", "parameters"));
                }

                if type_.constraints.is_empty() {
                    self.errors.push(Error::missing_field("ConfigType", "constraints"));
                } else if type_.constraints.len() > 1 {
                    self.errors.push(Error::extraneous_field("ConfigType", "constraints"));
                } else if let fdecl::LayoutConstraint::MaxSize(_) = &type_.constraints[0] {
                } else {
                    self.errors.push(Error::invalid_field("ConfigType", "constraints"));
                }
            }
            fdecl::ConfigTypeLayout::Vector => {
                if accept_vectors {
                    // Vector has exactly one constraint and one parameter
                    if let Some(parameters) = &type_.parameters {
                        if parameters.is_empty() {
                            self.errors.push(Error::missing_field("ConfigType", "parameters"));
                        } else if parameters.len() > 1 {
                            self.errors.push(Error::extraneous_field("ConfigType", "parameters"));
                        } else if let fdecl::LayoutParameter::NestedType(nested_type) =
                            &parameters[0]
                        {
                            self.validate_config_type(nested_type, false);
                        } else {
                            self.errors.push(Error::invalid_field("ConfigType", "parameters"));
                        }
                    } else {
                        self.errors.push(Error::missing_field("ConfigType", "parameters"))
                    }

                    if type_.constraints.is_empty() {
                        self.errors.push(Error::missing_field("ConfigType", "constraints"));
                    } else if type_.constraints.len() > 1 {
                        self.errors.push(Error::extraneous_field("ConfigType", "constraints"));
                    } else if let fdecl::LayoutConstraint::MaxSize(_) = &type_.constraints[0] {
                    } else {
                        self.errors.push(Error::invalid_field("ConfigType", "constraints"));
                    }
                } else {
                    self.errors.push(Error::nested_vector());
                }
            }
            _ => self.errors.push(Error::invalid_field("ConfigType", "layout")),
        }
    }

    /// Validates an individual capability declaration as either a built-in capability or (if
    /// `as_builtin = false`) as a component or namespace capability.
    // Storage capabilities are not currently allowed as built-ins, but there's no deep reason for this.
    // Update this method to allow built-in storage capabilities as needed.
    fn validate_capability_decl(&mut self, capability: &'a fdecl::Capability, as_builtin: bool) {
        match capability {
            fdecl::Capability::Service(service) => self.validate_service_decl(&service, as_builtin),
            fdecl::Capability::Protocol(protocol) => {
                self.validate_protocol_decl(&protocol, as_builtin)
            }
            fdecl::Capability::Directory(directory) => {
                self.validate_directory_decl(&directory, as_builtin)
            }
            fdecl::Capability::Storage(storage) => {
                if as_builtin {
                    self.errors.push(Error::invalid_capability_type(
                        "RuntimeConfig",
                        "capability",
                        "storage",
                    ))
                } else {
                    self.validate_storage_decl(&storage)
                }
            }
            fdecl::Capability::Runner(runner) => self.validate_runner_decl(&runner, as_builtin),
            fdecl::Capability::Resolver(resolver) => {
                self.validate_resolver_decl(&resolver, as_builtin)
            }
            fdecl::Capability::Event(event) => {
                if as_builtin {
                    self.validate_event_decl(&event)
                } else {
                    self.errors.push(Error::invalid_capability_type(
                        "Component",
                        "capability",
                        "event",
                    ))
                }
            }
            fdecl::Capability::EventStream(event) => {
                if as_builtin {
                    self.validate_event_stream_decl(&event)
                } else {
                    self.errors.push(Error::invalid_capability_type(
                        "Component",
                        "capability",
                        "event",
                    ))
                }
            }
            _ => {
                if as_builtin {
                    self.errors.push(Error::invalid_capability_type(
                        "RuntimeConfig",
                        "capability",
                        "unknown",
                    ));
                } else {
                    self.errors.push(Error::invalid_capability_type(
                        "Component",
                        "capability",
                        "unknown",
                    ));
                }
            }
        }
    }

    fn validate_use_decls(&mut self, uses: &'a [fdecl::Use]) {
        // Validate all events first so that we keep track of them for validation of event_streams.
        for use_ in uses.iter() {
            match use_ {
                fdecl::Use::Event(e) => self.validate_event(e),
                _ => {}
            }
        }

        // Validate individual fields.
        for use_ in uses.iter() {
            self.validate_use_decl(&use_);
        }

        self.validate_use_paths(&uses);
    }

    fn validate_use_decl(&mut self, use_: &'a fdecl::Use) {
        match use_ {
            fdecl::Use::Service(u) => {
                self.validate_use_source(
                    u.source.as_ref(),
                    u.source_name.as_ref(),
                    u.dependency_type.as_ref(),
                    u.availability.as_ref(),
                    "UseService",
                    "source",
                );
                check_name(u.source_name.as_ref(), "UseService", "source_name", &mut self.errors);
                check_path(u.target_path.as_ref(), "UseService", "target_path", &mut self.errors);
            }
            fdecl::Use::Protocol(u) => {
                self.validate_use_source(
                    u.source.as_ref(),
                    u.source_name.as_ref(),
                    u.dependency_type.as_ref(),
                    u.availability.as_ref(),
                    "UseProtocol",
                    "source",
                );
                check_name(u.source_name.as_ref(), "UseProtocol", "source_name", &mut self.errors);
                check_path(u.target_path.as_ref(), "UseProtocol", "target_path", &mut self.errors);
            }
            fdecl::Use::Directory(u) => {
                self.validate_use_source(
                    u.source.as_ref(),
                    u.source_name.as_ref(),
                    u.dependency_type.as_ref(),
                    u.availability.as_ref(),
                    "UseDirectory",
                    "source",
                );
                check_name(u.source_name.as_ref(), "UseDirectory", "source_name", &mut self.errors);
                check_path(u.target_path.as_ref(), "UseDirectory", "target_path", &mut self.errors);
                if u.rights.is_none() {
                    self.errors.push(Error::missing_field("UseDirectory", "rights"));
                }
                if let Some(subdir) = u.subdir.as_ref() {
                    check_relative_path(Some(subdir), "UseDirectory", "subdir", &mut self.errors);
                }
            }
            fdecl::Use::Storage(u) => {
                check_name(u.source_name.as_ref(), "UseStorage", "source_name", &mut self.errors);
                check_path(u.target_path.as_ref(), "UseStorage", "target_path", &mut self.errors);
                check_use_availability("UseStorage", u.availability.as_ref(), &mut self.errors);
            }
            fdecl::Use::EventStreamDeprecated(e) => {
                self.validate_event_stream_deprecated(e);
                check_use_availability(
                    "UseEventStreamDeprecated",
                    e.availability.as_ref(),
                    &mut self.errors,
                );
            }
            fdecl::Use::Event(_) => {
                // Skip events. We must have already validated by this point.
                // See `validate_use_decls`.
            }
            fdecl::Use::EventStream(u) => {
                self.validate_event_stream(u);
                check_use_availability("UseEventStream", u.availability.as_ref(), &mut self.errors);
            }
            _ => {
                self.errors.push(Error::invalid_field("Component", "use"));
            }
        }
    }

    fn validate_event_stream(&mut self, u: &fdecl::UseEventStream) {
        match u.source {
            Some(fdecl::Ref::Child(_)) | Some(fdecl::Ref::Parent(_)) => {}
            Some(fdecl::Ref::Framework(_)) => match &u.scope {
                Some(value) if value.is_empty() => {
                    self.errors.push(Error::invalid_field("UseEventStream", "scope"));
                }
                Some(_) => {}
                None => {
                    self.errors.push(Error::missing_field("UseEventStream", "scope"));
                }
            },
            _ => {
                self.errors.push(Error::invalid_field("UseEventStream", "source"));
            }
        }
        if let Some(scope) = &u.scope {
            for reference in scope {
                if !matches!(reference, fdecl::Ref::Child(_) | fdecl::Ref::Collection(_)) {
                    self.errors.push(Error::invalid_field("UseEventStream", "scope"));
                }
            }
        }
        check_path(u.target_path.as_ref(), "UseEventStream", "target_path", &mut self.errors);
        if let Some(name) = &u.source_name {
            check_name(Some(name), "UseEventStream", "source_name", &mut self.errors);
        } else {
            self.errors.push(Error::missing_field("UseEventStream", "source_name"));
        }
    }

    /// Validates the "program" declaration. This does not check runner-specific properties
    /// since those are checked by the runner.
    fn validate_program(&mut self, program: &fdecl::Program) {
        if program.runner.is_none() {
            self.errors.push(Error::missing_field("Program", "runner"));
        }

        if program.info.is_none() {
            self.errors.push(Error::missing_field("Program", "info"));
        }
    }

    /// Validates that paths-based capabilities (service, directory, protocol)
    /// are different, are not prefixes of each other, and do not collide "/pkg".
    fn validate_use_paths(&mut self, uses: &[fdecl::Use]) {
        #[derive(Debug, PartialEq, Clone, Copy)]
        struct PathCapability<'a> {
            decl: &'a str,
            dir: &'a Path,
            use_: &'a fdecl::Use,
        }
        let mut used_paths = HashMap::new();
        for use_ in uses.iter() {
            match use_ {
                fdecl::Use::Service(fdecl::UseService { target_path: Some(path), .. })
                | fdecl::Use::Protocol(fdecl::UseProtocol { target_path: Some(path), .. })
                | fdecl::Use::Directory(fdecl::UseDirectory { target_path: Some(path), .. })
                | fdecl::Use::Storage(fdecl::UseStorage { target_path: Some(path), .. }) => {
                    let capability = match use_ {
                        fdecl::Use::Service(_) => {
                            let dir = match Path::new(path).parent() {
                                Some(p) => p,
                                None => continue, // Invalid path, validated elsewhere
                            };
                            PathCapability { decl: "UseService", dir, use_ }
                        }
                        fdecl::Use::Protocol(_) => {
                            let dir = match Path::new(path).parent() {
                                Some(p) => p,
                                None => continue, // Invalid path, validated elsewhere
                            };
                            PathCapability { decl: "UseProtocol", dir, use_ }
                        }
                        fdecl::Use::Directory(_) => {
                            PathCapability { decl: "UseDirectory", dir: Path::new(path), use_ }
                        }
                        fdecl::Use::Storage(_) => {
                            PathCapability { decl: "UseStorage", dir: Path::new(path), use_ }
                        }
                        _ => unreachable!(),
                    };
                    if used_paths.insert(path, capability).is_some() {
                        // Disallow multiple capabilities for the same path.
                        self.errors.push(Error::duplicate_field(capability.decl, "path", path));
                    }
                }
                _ => {}
            }
        }
        for ((&path_a, capability_a), (&path_b, capability_b)) in
            used_paths.iter().tuple_combinations()
        {
            if match (capability_a.use_, capability_b.use_) {
                // Directories and storage can't be the same or partially overlap.
                (fdecl::Use::Directory(_), fdecl::Use::Directory(_))
                | (fdecl::Use::Storage(_), fdecl::Use::Directory(_))
                | (fdecl::Use::Directory(_), fdecl::Use::Storage(_))
                | (fdecl::Use::Storage(_), fdecl::Use::Storage(_)) => {
                    capability_b.dir == capability_a.dir
                        || capability_b.dir.starts_with(capability_a.dir)
                        || capability_a.dir.starts_with(capability_b.dir)
                }

                // Protocols and Services can't overlap with Directories.
                (_, fdecl::Use::Directory(_)) | (fdecl::Use::Directory(_), _) => {
                    capability_b.dir == capability_a.dir
                        || capability_b.dir.starts_with(capability_a.dir)
                        || capability_a.dir.starts_with(capability_b.dir)
                }

                // Protocols and Services containing directories may be same, but
                // partial overlap is disallowed.
                (_, _) => {
                    capability_b.dir != capability_a.dir
                        && (capability_b.dir.starts_with(capability_a.dir)
                            || capability_a.dir.starts_with(capability_b.dir))
                }
            } {
                self.errors.push(Error::invalid_path_overlap(
                    capability_a.decl,
                    path_a,
                    capability_b.decl,
                    path_b,
                ));
            }
        }
        for (used_path, capability) in used_paths.iter() {
            if used_path.as_str() == "/pkg" || used_path.starts_with("/pkg/") {
                self.errors.push(Error::pkg_path_overlap(capability.decl, *used_path));
            }
        }
    }

    fn validate_event(&mut self, event: &'a fdecl::UseEvent) {
        self.validate_use_source(
            event.source.as_ref(),
            event.source_name.as_ref(),
            event.dependency_type.as_ref(),
            event.availability.as_ref(),
            "UseEvent",
            "source",
        );
        if let Some(fdecl::Ref::Self_(_)) = event.source {
            self.errors.push(Error::invalid_field("UseEvent", "source"));
        }
        check_name(event.source_name.as_ref(), "UseEvent", "source_name", &mut self.errors);
        check_name(event.target_name.as_ref(), "UseEvent", "target_name", &mut self.errors);
        if let Some(target_name) = event.target_name.as_ref() {
            if !self.all_events.insert(target_name) {
                self.errors.push(Error::duplicate_field("UseEvent", "target_name", target_name));
            }
        }
    }

    fn validate_event_stream_deprecated(
        &mut self,
        event_stream: &'a fdecl::UseEventStreamDeprecated,
    ) {
        check_name(event_stream.name.as_ref(), "UseEventStream", "name", &mut self.errors);
        if let Some(name) = event_stream.name.as_ref() {
            if !self.all_event_streams.insert(name) {
                self.errors.push(Error::duplicate_field("UseEventStream", "name", name));
            }
        }
        match event_stream.subscriptions.as_ref() {
            None => {
                self.errors.push(Error::missing_field("UseEventStream", "subscriptions"));
            }
            Some(subscriptions) if subscriptions.is_empty() => {
                self.errors.push(Error::empty_field("UseEventStream", "subscriptions"));
            }
            Some(subscriptions) => {
                for subscription in subscriptions {
                    check_name(
                        subscription.event_name.as_ref(),
                        "UseEventStream",
                        "event_name",
                        &mut self.errors,
                    );
                    let event_name = subscription.event_name.clone().unwrap_or_default();
                    if !self.all_events.contains(event_name.as_str()) {
                        self.errors.push(Error::event_stream_event_not_found(
                            "UseEventStream",
                            "events",
                            event_name,
                        ));
                    }
                }
            }
        }
    }

    // disallow (use from #child dependency=strong) && (offer to #child from self)
    // - err: `use` must have dependency=weak to prevent cycle
    // add strong dependencies to dependency graph, so we can check for cycles
    fn validate_use_source(
        &mut self,
        source: Option<&'a fdecl::Ref>,
        source_name: Option<&'a String>,
        dependency_type: Option<&fdecl::DependencyType>,
        availability: Option<&'a fdecl::Availability>,
        decl: &str,
        field: &str,
    ) {
        match source {
            Some(fdecl::Ref::Parent(_)) => {}
            Some(fdecl::Ref::Framework(_)) => {}
            Some(fdecl::Ref::Debug(_)) => {}
            Some(fdecl::Ref::Self_(_)) => {}
            Some(fdecl::Ref::Capability(capability)) => {
                if !self.all_capability_ids.contains(capability.name.as_str()) {
                    self.errors.push(Error::invalid_capability(decl, field, &capability.name));
                } else if dependency_type == Some(&fdecl::DependencyType::Strong) {
                    self.add_strong_dep(
                        source_name,
                        DependencyNode::try_from_ref(source),
                        Some(DependencyNode::Self_),
                    );
                }
            }
            Some(fdecl::Ref::Child(child)) => {
                if !self.all_children.contains_key(&child.name as &str) {
                    self.errors.push(Error::invalid_child(decl, field, &child.name));
                } else if dependency_type == Some(&fdecl::DependencyType::Strong) {
                    self.add_strong_dep(
                        source_name,
                        DependencyNode::try_from_ref(source),
                        Some(DependencyNode::Self_),
                    );
                }
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, field));
            }
            None => {
                self.errors.push(Error::missing_field(decl, field));
            }
        };
        check_use_availability(decl, availability, &mut self.errors);

        let is_use_from_child = match source {
            Some(fdecl::Ref::Child(_)) => true,
            _ => false,
        };
        match (is_use_from_child, dependency_type) {
            (
                false,
                Some(fdecl::DependencyType::Weak) | Some(fdecl::DependencyType::WeakForMigration),
            ) => {
                self.errors.push(Error::invalid_field(decl, "dependency_type"));
            }
            _ => {}
        }
    }

    fn validate_child_decl(&mut self, child: &'a fdecl::Child) {
        if let Err(mut e) = validate_child(child, check_name) {
            self.errors.append(&mut e.errs);
        }
        if let Some(name) = child.name.as_ref() {
            let name: &str = name;
            if self.all_children.insert(name, child).is_some() {
                self.errors.push(Error::duplicate_field("Child", "name", name));
            }
            if let Some(env) = child.environment.as_ref() {
                let source = DependencyNode::Environment(env.as_str());
                let target = DependencyNode::Child(name);
                self.add_strong_dep(None, Some(source), Some(target));
            }
        }
        if let Some(environment) = child.environment.as_ref() {
            if !self.all_environment_names.contains(environment.as_str()) {
                self.errors.push(Error::invalid_environment("Child", "environment", environment));
            }
        }
    }

    fn validate_collection_decl(&mut self, collection: &'a fdecl::Collection) {
        let name = collection.name.as_ref();
        if check_name(name, "Collection", "name", &mut self.errors) {
            let name: &str = name.unwrap();
            if !self.all_collections.insert(name) {
                self.errors.push(Error::duplicate_field("Collection", "name", name));
            }
        }
        if collection.durability.is_none() {
            self.errors.push(Error::missing_field("Collection", "durability"));
        }
        if let Some(environment) = collection.environment.as_ref() {
            if !self.all_environment_names.contains(environment.as_str()) {
                self.errors.push(Error::invalid_environment(
                    "Collection",
                    "environment",
                    environment,
                ));
            }
            if let Some(name) = collection.name.as_ref() {
                let source = DependencyNode::Environment(environment.as_str());
                let target = DependencyNode::Collection(name.as_str());
                self.add_strong_dep(None, Some(source), Some(target));
            }
        }
        // Allow `allowed_offers` & `allow_long_names` to be unset/unvalidated, for backwards compatibility.
    }

    fn validate_environment_decl(&mut self, environment: &'a fdecl::Environment) {
        let name = environment.name.as_ref();
        check_name(name, "Environment", "name", &mut self.errors);
        if environment.extends.is_none() {
            self.errors.push(Error::missing_field("Environment", "extends"));
        }
        if let Some(runners) = environment.runners.as_ref() {
            let mut registered_runners = HashSet::new();
            for runner in runners {
                self.validate_runner_registration(runner, name.clone(), &mut registered_runners);
            }
        }
        if let Some(resolvers) = environment.resolvers.as_ref() {
            let mut registered_schemes = HashSet::new();
            for resolver in resolvers {
                self.validate_resolver_registration(
                    resolver,
                    name.clone(),
                    &mut registered_schemes,
                );
            }
        }

        match environment.extends.as_ref() {
            Some(fdecl::EnvironmentExtends::None) => {
                if environment.stop_timeout_ms.is_none() {
                    self.errors.push(Error::missing_field("Environment", "stop_timeout_ms"));
                }
            }
            None | Some(fdecl::EnvironmentExtends::Realm) => {}
        }

        if let Some(debugs) = environment.debug_capabilities.as_ref() {
            for debug in debugs {
                self.validate_environment_debug_registration(debug, name.clone());
            }
        }
    }

    fn validate_runner_registration(
        &mut self,
        runner_registration: &'a fdecl::RunnerRegistration,
        environment_name: Option<&'a String>,
        runner_names: &mut HashSet<&'a str>,
    ) {
        check_name(
            runner_registration.source_name.as_ref(),
            "RunnerRegistration",
            "source_name",
            &mut self.errors,
        );
        self.validate_registration_source(
            environment_name,
            runner_registration.source.as_ref(),
            "RunnerRegistration",
        );
        // If the source is `self`, ensure we have a corresponding Runner.
        if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) =
            (&runner_registration.source, &runner_registration.source_name)
        {
            if !self.all_runners.contains(name as &str) {
                self.errors.push(Error::invalid_runner("RunnerRegistration", "source_name", name));
            }
        }

        check_name(
            runner_registration.target_name.as_ref(),
            "RunnerRegistration",
            "target_name",
            &mut self.errors,
        );
        if let Some(name) = runner_registration.target_name.as_ref() {
            if !runner_names.insert(name.as_str()) {
                self.errors.push(Error::duplicate_field("RunnerRegistration", "target_name", name));
            }
        }
    }

    fn validate_resolver_registration(
        &mut self,
        resolver_registration: &'a fdecl::ResolverRegistration,
        environment_name: Option<&'a String>,
        schemes: &mut HashSet<&'a str>,
    ) {
        check_name(
            resolver_registration.resolver.as_ref(),
            "ResolverRegistration",
            "resolver",
            &mut self.errors,
        );
        self.validate_registration_source(
            environment_name,
            resolver_registration.source.as_ref(),
            "ResolverRegistration",
        );
        check_url_scheme(
            resolver_registration.scheme.as_ref(),
            "ResolverRegistration",
            "scheme",
            &mut self.errors,
        );
        if let Some(scheme) = resolver_registration.scheme.as_ref() {
            if !schemes.insert(scheme.as_str()) {
                self.errors.push(Error::duplicate_field("ResolverRegistration", "scheme", scheme));
            }
        }
    }

    fn validate_registration_source(
        &mut self,
        environment_name: Option<&'a String>,
        source: Option<&'a fdecl::Ref>,
        ty: &str,
    ) {
        match source {
            Some(fdecl::Ref::Parent(_)) => {}
            Some(fdecl::Ref::Self_(_)) => {}
            Some(fdecl::Ref::Child(child_ref)) => {
                // Make sure the child is valid.
                self.validate_child_ref(ty, "source", &child_ref);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(ty, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(ty, "source"));
            }
        }

        let source = DependencyNode::try_from_ref(source);
        if let Some(source) = source {
            if let Some(env_name) = &environment_name {
                let target = DependencyNode::Environment(env_name);
                self.strong_dependencies.add_edge(source, target);
            }
        }
    }

    fn validate_service_decl(&mut self, service: &'a fdecl::Service, as_builtin: bool) {
        if check_name(service.name.as_ref(), "Service", "name", &mut self.errors) {
            let name = service.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("Service", "name", name.as_str()));
            }
            self.all_services.insert(name);
        }
        match as_builtin {
            true => {
                if let Some(path) = service.source_path.as_ref() {
                    self.errors.push(Error::extraneous_source_path("Service", path))
                }
            }
            false => {
                check_path(
                    service.source_path.as_ref(),
                    "Service",
                    "source_path",
                    &mut self.errors,
                );
            }
        }
    }

    fn validate_protocol_decl(&mut self, protocol: &'a fdecl::Protocol, as_builtin: bool) {
        if check_name(protocol.name.as_ref(), "Protocol", "name", &mut self.errors) {
            let name = protocol.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("Protocol", "name", name.as_str()));
            }
            self.all_protocols.insert(name);
        }
        match as_builtin {
            true => {
                if let Some(path) = protocol.source_path.as_ref() {
                    self.errors.push(Error::extraneous_source_path("Protocol", path))
                }
            }
            false => {
                check_path(
                    protocol.source_path.as_ref(),
                    "Protocol",
                    "source_path",
                    &mut self.errors,
                );
            }
        }
    }

    fn validate_directory_decl(&mut self, directory: &'a fdecl::Directory, as_builtin: bool) {
        if check_name(directory.name.as_ref(), "Directory", "name", &mut self.errors) {
            let name = directory.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("Directory", "name", name.as_str()));
            }
            self.all_directories.insert(name);
        }
        match as_builtin {
            true => {
                if let Some(path) = directory.source_path.as_ref() {
                    self.errors.push(Error::extraneous_source_path("Directory", path))
                }
            }
            false => {
                check_path(
                    directory.source_path.as_ref(),
                    "Directory",
                    "source_path",
                    &mut self.errors,
                );
            }
        }
        if directory.rights.is_none() {
            self.errors.push(Error::missing_field("Directory", "rights"));
        }
    }

    fn validate_storage_decl(&mut self, storage: &'a fdecl::Storage) {
        match storage.source.as_ref() {
            Some(fdecl::Ref::Parent(_)) => {}
            Some(fdecl::Ref::Self_(_)) => {}
            Some(fdecl::Ref::Child(child)) => {
                self.validate_source_child(child, "Storage", OfferType::Static);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field("Storage", "source"));
            }
            None => {
                self.errors.push(Error::missing_field("Storage", "source"));
            }
        };
        if check_name(storage.name.as_ref(), "Storage", "name", &mut self.errors) {
            let name = storage.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("Storage", "name", name.as_str()));
            }
            self.all_storage_and_sources.insert(name, storage.source.as_ref());
        }
        if storage.storage_id.is_none() {
            self.errors.push(Error::missing_field("Storage", "storage_id"));
        }
        check_name(storage.backing_dir.as_ref(), "Storage", "backing_dir", &mut self.errors);
    }

    fn validate_runner_decl(&mut self, runner: &'a fdecl::Runner, as_builtin: bool) {
        if check_name(runner.name.as_ref(), "Runner", "name", &mut self.errors) {
            let name = runner.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("Runner", "name", name.as_str()));
            }
            self.all_runners.insert(name);
        }
        match as_builtin {
            true => {
                if let Some(path) = runner.source_path.as_ref() {
                    self.errors.push(Error::extraneous_source_path("Runner", path))
                }
            }
            false => {
                check_path(runner.source_path.as_ref(), "Runner", "source_path", &mut self.errors);
            }
        }
    }

    fn validate_resolver_decl(&mut self, resolver: &'a fdecl::Resolver, as_builtin: bool) {
        if check_name(resolver.name.as_ref(), "Resolver", "name", &mut self.errors) {
            let name = resolver.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("Resolver", "name", name.as_str()));
            }
            self.all_resolvers.insert(name);
        }
        match as_builtin {
            true => {
                if let Some(path) = resolver.source_path.as_ref() {
                    self.errors.push(Error::extraneous_source_path("Resolver", path))
                }
            }
            false => {
                check_path(
                    resolver.source_path.as_ref(),
                    "Resolver",
                    "source_path",
                    &mut self.errors,
                );
            }
        }
    }

    fn validate_environment_debug_registration(
        &mut self,
        debug: &'a fdecl::DebugRegistration,
        environment_name: Option<&'a String>,
    ) {
        match debug {
            fdecl::DebugRegistration::Protocol(o) => {
                let decl = "DebugProtocolRegistration";
                self.validate_environment_debug_fields(
                    decl,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target_name.as_ref(),
                );

                if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) = (&o.source, &o.source_name) {
                    if !self.all_protocols.contains(&name as &str) {
                        self.errors.push(Error::invalid_field(decl, "source"));
                    }
                }

                if let Some(env_name) = &environment_name {
                    let source = DependencyNode::try_from_ref(o.source.as_ref());
                    let target = Some(DependencyNode::Environment(env_name));
                    self.add_strong_dep(None, source, target);
                }
            }
            _ => {
                self.errors.push(Error::invalid_field("Environment", "debug"));
            }
        }
    }

    fn validate_environment_debug_fields(
        &mut self,
        decl: &str,
        source: Option<&fdecl::Ref>,
        source_name: Option<&String>,
        target_name: Option<&'a String>,
    ) {
        // We don't support "source" from "capability" for now.
        match source {
            Some(fdecl::Ref::Parent(_)) => {}
            Some(fdecl::Ref::Self_(_)) => {}
            Some(fdecl::Ref::Framework(_)) => {}
            Some(fdecl::Ref::Child(child)) => {
                self.validate_source_child(child, decl, OfferType::Static)
            }
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }
        check_name(source_name, decl, "source_name", &mut self.errors);
        check_name(target_name, decl, "target_name", &mut self.errors);
    }

    fn validate_event_decl(&mut self, event: &'a fdecl::Event) {
        if check_name(event.name.as_ref(), "Event", "name", &mut self.errors) {
            let name = event.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("Event", "name", name.as_str()));
            }
        }
    }

    fn validate_event_stream_decl(&mut self, event: &'a fdecl::EventStream) {
        if check_name(event.name.as_ref(), "EventStream", "name", &mut self.errors) {
            let name = event.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("EventStream", "name", name.as_str()));
            }
        }
    }

    fn validate_source_child(
        &mut self,
        child: &fdecl::ChildRef,
        decl_type: &str,
        offer_type: OfferType,
    ) {
        let mut valid = true;
        valid &= check_offer_name(
            Some(&child.name),
            decl_type,
            "source.child.name",
            &mut self.errors,
            offer_type,
        );
        match offer_type {
            OfferType::Static => {
                valid &= if child.collection.is_some() {
                    self.errors.push(Error::extraneous_field(decl_type, "source.child.collection"));
                    false
                } else {
                    true
                };
                if !valid {
                    return;
                }
                if !self.all_children.contains_key(&child.name as &str) {
                    self.errors.push(Error::invalid_child(
                        decl_type,
                        "source",
                        &child.name as &str,
                    ));
                }
            }
            OfferType::Dynamic => {}
        }
    }

    fn validate_source_collection(&mut self, collection: &fdecl::CollectionRef, decl_type: &str) {
        if !check_name(
            Some(&collection.name),
            decl_type,
            "source.collection.name",
            &mut self.errors,
        ) {
            return;
        }
        if !self.all_collections.contains(&collection.name as &str) {
            self.errors.push(Error::invalid_collection(
                decl_type,
                "source",
                &collection.name as &str,
            ));
        }
    }

    fn validate_filtered_service_fields(
        &mut self,
        decl_type: &str,
        source_instance_filter: Option<&Vec<String>>,
        renamed_instances: Option<&Vec<fdecl::NameMapping>>,
    ) {
        if let Some(source_instance_filter) = source_instance_filter {
            if source_instance_filter.is_empty() {
                // if the  source_instance_filter is empty the offered service will have 0 instances,
                // which means the offer shouldn't have been created at all.
                self.errors.push(Error::invalid_field(decl_type, "source_instance_filter"));
            }
        }
        if let Some(renamed_instances) = renamed_instances {
            // Multiple sources shouldn't map to the same target name
            let mut seen_target_names = HashSet::<String>::new();
            for mapping in renamed_instances {
                if !seen_target_names.insert(mapping.target_name.clone()) {
                    self.errors.push(Error::invalid_field(decl_type, "renamed_instances"));
                    break;
                }
            }
        }
    }

    fn validate_source_capability(
        &mut self,
        capability: &fdecl::CapabilityRef,
        decl_type: &str,
        field: &str,
    ) {
        if !self.all_capability_ids.contains(capability.name.as_str()) {
            self.errors.push(Error::invalid_capability(decl_type, field, &capability.name));
        }
    }

    fn validate_storage_source(&mut self, source_name: &String, decl_type: &str) {
        if check_name(Some(source_name), decl_type, "source.storage.name", &mut self.errors) {
            if !self.all_storage_and_sources.contains_key(source_name.as_str()) {
                self.errors.push(Error::invalid_storage(decl_type, "source", source_name));
            }
        }
    }

    fn validate_expose_decl(
        &mut self,
        expose: &'a fdecl::Expose,
        prev_target_ids: &mut HashMap<&'a str, AllowableIds>,
    ) {
        match expose {
            fdecl::Expose::Service(e) => {
                let decl = "ExposeService";
                self.validate_expose_fields(
                    decl,
                    AllowableIds::Many,
                    CollectionSource::Allow,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding Service.
                // TODO: Consider bringing this bit into validate_expose_fields.
                if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_services.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fdecl::Expose::Protocol(e) => {
                let decl = "ExposeProtocol";
                self.validate_expose_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding Protocol.
                // TODO: Consider bringing this bit into validate_expose_fields.
                if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_protocols.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fdecl::Expose::Directory(e) => {
                let decl = "ExposeDirectory";
                self.validate_expose_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding Directory.
                // TODO: Consider bringing this bit into validate_expose_fields.
                if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_directories.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                    if name.starts_with('/') && e.rights.is_none() {
                        self.errors.push(Error::missing_field(decl, "rights"));
                    }
                }

                // Subdir makes sense when routing, but when exposing to framework the subdirectory
                // can be exposed directly.
                match e.target.as_ref() {
                    Some(fdecl::Ref::Framework(_)) => {
                        if e.subdir.is_some() {
                            self.errors.push(Error::invalid_field(decl, "subdir"));
                        }
                    }
                    _ => {}
                }

                if let Some(subdir) = e.subdir.as_ref() {
                    check_relative_path(Some(subdir), decl, "subdir", &mut self.errors);
                }
            }
            fdecl::Expose::Runner(e) => {
                let decl = "ExposeRunner";
                self.validate_expose_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding Runner.
                if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_runners.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fdecl::Expose::Resolver(e) => {
                let decl = "ExposeResolver";
                self.validate_expose_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding Resolver.
                if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_resolvers.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fdecl::Expose::EventStream(e) => {
                let decl = "ExposeEventStream";
                self.validate_expose_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // Exposing to framework from framework should never be valid.
                if e.target == Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})) {
                    self.errors.push(Error::invalid_field("ExposeEventStream", "target"));
                }
                if let Some(scope) = &e.scope {
                    if scope.is_empty() {
                        self.errors.push(Error::invalid_field(decl, "scope"));
                    }
                    for value in scope {
                        match value {
                            fdecl::Ref::Child(child) => {
                                self.validate_child_ref("ExposeEventStream", "scope", &child);
                            }
                            fdecl::Ref::Collection(child) => {
                                self.validate_collection_ref("ExposeEventStream", "scope", &child);
                            }
                            _ => {
                                self.errors
                                    .push(Error::invalid_field("ExposeEventStream", "scope"));
                            }
                        }
                    }
                }
                if e.source == Some(fdecl::Ref::Framework(fdecl::FrameworkRef {}))
                    && e.scope == None
                {
                    self.errors.push(Error::invalid_field(decl, "source"));
                }
            }
            _ => {
                self.errors.push(Error::invalid_field("Component", "expose"));
            }
        }
    }

    fn validate_expose_fields(
        &mut self,
        decl: &str,
        allowable_ids: AllowableIds,
        collection_source: CollectionSource,
        source: Option<&fdecl::Ref>,
        source_name: Option<&String>,
        target_name: Option<&'a String>,
        target: Option<&fdecl::Ref>,
        prev_child_target_ids: &mut HashMap<&'a str, AllowableIds>,
    ) {
        match source {
            Some(r) => match r {
                fdecl::Ref::Self_(_) => {}
                fdecl::Ref::Framework(_) => {}
                fdecl::Ref::Child(child) => {
                    self.validate_source_child(child, decl, OfferType::Static);
                }
                fdecl::Ref::Capability(c) => {
                    self.validate_source_capability(c, decl, "source");
                }
                fdecl::Ref::Collection(c) if collection_source == CollectionSource::Allow => {
                    self.validate_source_collection(c, decl);
                }
                _ => {
                    self.errors.push(Error::invalid_field(decl, "source"));
                }
            },
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        }
        match target {
            Some(r) => match r {
                fdecl::Ref::Parent(_) => {}
                fdecl::Ref::Framework(_) => {
                    if source != Some(&fdecl::Ref::Self_(fdecl::SelfRef {})) {
                        self.errors.push(Error::invalid_field(decl, "target"));
                    }
                }
                _ => {
                    self.errors.push(Error::invalid_field(decl, "target"));
                }
            },
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
            }
        }
        check_name(source_name, decl, "source_name", &mut self.errors);
        if check_name(target_name, decl, "target_name", &mut self.errors) {
            // TODO: This logic needs to pair the target name with the target before concluding
            // there's a duplicate.
            let target_name = target_name.unwrap();
            if let Some(prev_state) = prev_child_target_ids.insert(target_name, allowable_ids) {
                if prev_state == AllowableIds::One || prev_state != allowable_ids {
                    self.errors.push(Error::duplicate_field(decl, "target_name", target_name));
                }
            }
        }
    }

    /// Adds a strong dependency between two nodes in the dependency graph between `source` and
    /// `target`.
    ///
    /// `source_name` is the name of the capability being routed (if applicable). The function is
    /// a no-op if `source` or `target` is `None`; this behavior is a convenience so that the
    /// caller can directly pass the result of `DependencyNode::try_from_ref`.
    fn add_strong_dep(
        &mut self,
        source_name: Option<&'a String>,
        source: Option<DependencyNode<'a>>,
        target: Option<DependencyNode<'a>>,
    ) {
        if source.is_none() || target.is_none() {
            return;
        }
        let source = source.unwrap();
        let target = target.unwrap();

        let source = {
            // A dependency on a storage capability from `self` is really a dependency on the
            // backing dir.  Perform that translation here.
            let possible_storage_name = match (source, source_name) {
                (DependencyNode::Capability(name), _) => Some(name),
                (DependencyNode::Self_, Some(name)) => Some(name.as_str()),
                _ => None,
            };
            let possible_storage_source =
                possible_storage_name.map(|name| self.all_storage_and_sources.get(name)).flatten();
            let source = possible_storage_source
                .map(|r| DependencyNode::try_from_ref(*r))
                .unwrap_or(Some(source));
            if source.is_none() {
                return;
            }
            source.unwrap()
        };

        if source == target {
            // This is already its own error, or is a valid `use from self`, don't report this as a
            // cycle.
        } else {
            self.strong_dependencies.add_edge(source, target);
        }
    }

    // Checks a group of offer decls to confirm that any duplicate offers are
    // valid aggregate offer declarations.
    fn validate_offer_group(&mut self, offers: &Vec<fdecl::Offer>) {
        let mut offer_groups: HashMap<String, Vec<fdecl::OfferService>> = HashMap::new();
        let service_offers = offers.into_iter().filter_map(|o| {
            if let fdecl::Offer::Service(s) = o {
                Some(s)
            } else {
                None
            }
        });
        for offer in service_offers {
            let offer_group_key = format!("{:?}_{:?}", offer.target_name, offer.target);
            offer_groups.entry(offer_group_key).or_insert_with(|| vec![]).push(offer.clone());
        }
        for (_, offer_group) in offer_groups {
            if offer_group.len() == 1 {
                // If there is not multiple offers for a  target_name, target pair then there are no
                // aggregation conditions to check.
                continue;
            }
            let mut source_instance_filter_entries: HashSet<String> = HashSet::new();
            let mut service_source_names: HashSet<String> = HashSet::new();
            for o in offer_group {
                // Currently only service capabilities can be aggregated
                match o.source_instance_filter {
                    None => {
                        self.errors.push(Error::invalid_aggregate_offer(
                            "source_instance_filter must be set for all aggregate service offers",
                        ));
                    }
                    Some(source_instance_filter) => {
                        for instance_name in source_instance_filter {
                            if !source_instance_filter_entries.insert(instance_name.clone()) {
                                // If the source instance in the filter has been seen before this means there is a conflicting
                                // aggregate service offer.
                                self.errors.push(Error::invalid_aggregate_offer(format!("Conflicting source_instance_filter in aggregate service offer, instance_name '{}' seen in filter lists multiple times", instance_name)));
                            }
                        }
                    }
                }
                service_source_names.insert(
                    o.source_name
                        .expect("Offer Service declarations must always contain source_name"),
                );
            }
            if service_source_names.len() > 1 {
                self.errors.push(Error::invalid_aggregate_offer(format!(
                    "All aggregate service offers must have the same source_name, saw {}. Use renamed_instances to rename instance names to avoid conflict.",
                    service_source_names.into_iter().sorted().collect::<Vec<String>>().join(", ")
                )));
            }
        }
    }
    fn validate_offers_decl(&mut self, offer: &'a fdecl::Offer, offer_type: OfferType) {
        match offer {
            fdecl::Offer::Service(o) => {
                let decl = "OfferService";
                self.validate_offer_fields(
                    decl,
                    AllowableIds::Many,
                    CollectionSource::Allow,
                    offer_type,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                    o.availability.as_ref(),
                    None,
                );
                self.validate_filtered_service_fields(
                    decl,
                    o.source_instance_filter.as_ref(),
                    o.renamed_instances.as_ref(),
                );
                match offer_type {
                    OfferType::Static => {
                        // If the offer source is `self`, ensure we have a corresponding Service.
                        // TODO: Consider bringing this bit into validate_offer_fields
                        if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) =
                            (&o.source, &o.source_name)
                        {
                            if !self.all_services.contains(&name as &str) {
                                self.errors.push(Error::invalid_field(decl, "source"));
                            }
                        }
                        self.add_strong_dep(
                            o.source_name.as_ref(),
                            DependencyNode::try_from_ref(o.source.as_ref()),
                            DependencyNode::try_from_ref(o.target.as_ref()),
                        );
                    }
                    OfferType::Dynamic => {}
                }
            }
            fdecl::Offer::Protocol(o) => {
                let decl = "OfferProtocol";
                self.validate_offer_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    offer_type,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                    o.availability.as_ref(),
                    o.dependency_type,
                );
                if o.dependency_type.is_none() {
                    self.errors.push(Error::missing_field(decl, "dependency_type"));
                } else if o.dependency_type == Some(fdecl::DependencyType::Strong) {
                    match offer_type {
                        OfferType::Static => {
                            self.add_strong_dep(
                                o.source_name.as_ref(),
                                DependencyNode::try_from_ref(o.source.as_ref()),
                                DependencyNode::try_from_ref(o.target.as_ref()),
                            );
                        }
                        OfferType::Dynamic => {}
                    }
                }
                match offer_type {
                    OfferType::Static => {
                        // If the offer source is `self`, ensure we have a
                        // corresponding Protocol.
                        // TODO: Consider bringing this bit into validate_offer_fields.
                        if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) =
                            (&o.source, &o.source_name)
                        {
                            if !self.all_protocols.contains(&name as &str) {
                                self.errors.push(Error::invalid_capability(decl, "source", name));
                            }
                        }
                    }
                    OfferType::Dynamic => {}
                }
            }
            fdecl::Offer::Directory(o) => {
                let decl = "OfferDirectory";
                self.validate_offer_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    offer_type,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                    o.availability.as_ref(),
                    o.dependency_type,
                );
                if o.dependency_type.is_none() {
                    self.errors.push(Error::missing_field(decl, "dependency_type"));
                } else if o.dependency_type == Some(fdecl::DependencyType::Strong) {
                    match offer_type {
                        OfferType::Static => {
                            self.add_strong_dep(
                                o.source_name.as_ref(),
                                DependencyNode::try_from_ref(o.source.as_ref()),
                                DependencyNode::try_from_ref(o.target.as_ref()),
                            );
                            // If the offer source is `self`, ensure we have a corresponding
                            // Directory.
                            //
                            // TODO: Consider bringing this bit into validate_offer_fields.
                            if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) =
                                (&o.source, &o.source_name)
                            {
                                if !self.all_directories.contains(&name as &str) {
                                    self.errors
                                        .push(Error::invalid_capability(decl, "source", name));
                                }
                            }
                        }
                        OfferType::Dynamic => {}
                    }
                }

                if let Some(subdir) = o.subdir.as_ref() {
                    check_relative_path(Some(subdir), "OfferDirectory", "subdir", &mut self.errors);
                }
            }
            fdecl::Offer::Storage(o) => {
                self.validate_storage_offer_fields(
                    "OfferStorage",
                    offer_type,
                    o.source_name.as_ref(),
                    o.source.as_ref(),
                    o.target.as_ref(),
                    o.availability.as_ref(),
                );

                match offer_type {
                    OfferType::Static => {
                        // Storage capabilities with a source of `Ref::Self_`
                        // don't interact with the component's runtime in any
                        // way, they're actually synthesized by the framework
                        // out of a pre-existing directory capability. Thus, its
                        // actual source is the backing directory capability.
                        match (o.source.as_ref(), o.source_name.as_ref()) {
                            (Some(fdecl::Ref::Self_ { .. }), Some(source_name)) => {
                                if let Some(source) = DependencyNode::try_from_ref(
                                    *self
                                        .all_storage_and_sources
                                        .get(source_name.as_str())
                                        .unwrap_or(&None),
                                ) {
                                    if let Some(target) =
                                        DependencyNode::try_from_ref(o.target.as_ref())
                                    {
                                        self.strong_dependencies.add_edge(source, target);
                                    }
                                }
                            }
                            _ => self.add_strong_dep(
                                o.source_name.as_ref(),
                                DependencyNode::try_from_ref(o.source.as_ref()),
                                DependencyNode::try_from_ref(o.target.as_ref()),
                            ),
                        }
                    }
                    OfferType::Dynamic => {}
                }
            }
            fdecl::Offer::Runner(o) => {
                let decl = "OfferRunner";
                self.validate_offer_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    offer_type,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                    Some(&fdecl::Availability::Required),
                    None,
                );
                match offer_type {
                    OfferType::Static => {
                        // If the offer source is `self`, ensure we have a corresponding Runner.
                        if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) =
                            (&o.source, &o.source_name)
                        {
                            if !self.all_runners.contains(&name as &str) {
                                self.errors.push(Error::invalid_capability(decl, "source", name));
                            }
                        }
                        self.add_strong_dep(
                            o.source_name.as_ref(),
                            DependencyNode::try_from_ref(o.source.as_ref()),
                            DependencyNode::try_from_ref(o.target.as_ref()),
                        );
                    }
                    OfferType::Dynamic => {}
                }
            }
            fdecl::Offer::Resolver(o) => {
                let decl = "OfferResolver";
                self.validate_offer_fields(
                    decl,
                    AllowableIds::One,
                    CollectionSource::Deny,
                    offer_type,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                    Some(&fdecl::Availability::Required),
                    None,
                );

                match offer_type {
                    OfferType::Static => {
                        // If the offer source is `self`, ensure we have a
                        // corresponding Resolver.
                        if let (Some(fdecl::Ref::Self_(_)), Some(ref name)) =
                            (&o.source, &o.source_name)
                        {
                            if !self.all_resolvers.contains(&name as &str) {
                                self.errors.push(Error::invalid_capability(decl, "source", name));
                            }
                        }
                        self.add_strong_dep(
                            o.source_name.as_ref(),
                            DependencyNode::try_from_ref(o.source.as_ref()),
                            DependencyNode::try_from_ref(o.target.as_ref()),
                        );
                    }
                    OfferType::Dynamic => {}
                }
            }
            fdecl::Offer::Event(e) => {
                self.validate_event_offer_fields(e, offer_type);
            }
            fdecl::Offer::EventStream(e) => {
                self.validate_event_stream_offer_fields(e, offer_type);
            }
            _ => {
                self.errors.push(Error::invalid_field("Component", "offer"));
            }
        }
    }

    /// Validates that the offer target is to a valid child or collection.
    fn validate_offer_target(
        &mut self,
        target: &'a Option<fdecl::Ref>,
        decl_type: &str,
        field_name: &str,
    ) -> Option<TargetId<'a>> {
        match target.as_ref() {
            Some(fdecl::Ref::Child(child)) => {
                if self.validate_child_ref(decl_type, field_name, &child) {
                    Some(TargetId::Component(&child.name))
                } else {
                    None
                }
            }
            Some(fdecl::Ref::Collection(collection)) => {
                if self.validate_collection_ref(decl_type, field_name, &collection) {
                    Some(TargetId::Collection(&collection.name))
                } else {
                    None
                }
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl_type, field_name));
                None
            }
            None => {
                self.errors.push(Error::missing_field(decl_type, field_name));
                None
            }
        }
    }

    fn validate_offer_fields(
        &mut self,
        decl: &str,
        allowable_names: AllowableIds,
        collection_source: CollectionSource,
        offer_type: OfferType,
        source: Option<&fdecl::Ref>,
        source_name: Option<&String>,
        target: Option<&'a fdecl::Ref>,
        target_name: Option<&'a String>,
        availability: Option<&fdecl::Availability>,
        dependency: Option<fdecl::DependencyType>,
    ) {
        match source {
            Some(fdecl::Ref::Parent(_))
            | Some(fdecl::Ref::Self_(_))
            | Some(fdecl::Ref::VoidType(_))
            | Some(fdecl::Ref::Framework(_)) => {}
            Some(fdecl::Ref::Child(child)) => self.validate_source_child(child, decl, offer_type),
            Some(fdecl::Ref::Capability(c)) => self.validate_source_capability(c, decl, "source"),
            Some(fdecl::Ref::Collection(c)) if collection_source == CollectionSource::Allow => {
                self.validate_source_collection(c, decl)
            }
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }
        check_offer_availability(decl, availability, source, source_name, &mut self.errors);
        check_offer_name(source_name, decl, "source_name", &mut self.errors, offer_type);
        match (offer_type, target) {
            (OfferType::Static, Some(fdecl::Ref::Child(c))) => {
                self.validate_target_child(
                    decl,
                    allowable_names,
                    c,
                    source,
                    target_name,
                    dependency,
                );
            }
            (OfferType::Static, Some(fdecl::Ref::Collection(c))) => {
                self.validate_target_collection(decl, allowable_names, c, target_name);
            }
            (OfferType::Static, Some(_)) => {
                self.errors.push(Error::invalid_field(decl, "target"));
            }
            (OfferType::Static, None) => {
                self.errors.push(Error::missing_field(decl, "target"));
            }

            (OfferType::Dynamic, Some(_)) => {
                self.errors.push(Error::extraneous_field(decl, "target"));
            }
            (OfferType::Dynamic, None) => {}
        }
        check_offer_name(target_name, decl, "target_name", &mut self.errors, offer_type);
    }

    fn validate_storage_offer_fields(
        &mut self,
        decl: &str,
        offer_type: OfferType,
        source_name: Option<&'a String>,
        source: Option<&'a fdecl::Ref>,
        target: Option<&'a fdecl::Ref>,
        availability: Option<&fdecl::Availability>,
    ) {
        if source_name.is_none() {
            self.errors.push(Error::missing_field(decl, "source_name"));
        }
        match source {
            Some(fdecl::Ref::Parent(_) | fdecl::Ref::VoidType(_)) => (),
            Some(fdecl::Ref::Self_(_)) => {
                self.validate_storage_source(source_name.unwrap(), decl);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        }
        check_offer_availability(decl, availability, source, source_name, &mut self.errors);
        match offer_type {
            OfferType::Static => {
                self.validate_storage_target(decl, target);
            }
            OfferType::Dynamic => {
                if target.is_some() {
                    self.errors.push(Error::extraneous_field(decl, "target"));
                }
            }
        }
    }

    fn validate_event_stream_offer_fields(
        &mut self,
        event_stream: &'a fdecl::OfferEventStream,
        offer_type: OfferType,
    ) {
        let decl = "OfferEventStream";
        check_name(event_stream.source_name.as_ref(), decl, "source_name", &mut self.errors);
        if event_stream.target == Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})) {
            // Expose to framework from framework is never valid.
            self.errors.push(Error::invalid_field("OfferEventStream", "target"));
        }
        let source_name =
            event_stream.source_name.as_ref().map(|value| value.as_str()).unwrap_or("");
        match (&event_stream.filter, source_name, &event_stream.source) {
            (Some(_), "capability_requested", _) | (Some(_), "directory_ready", _) => {}
            (Some(_), _, Some(fdecl::Ref::Framework(_))) => {
                self.errors.push(Error::invalid_field(decl, "filter"));
            }
            _ => {}
        }
        if let Some(scope) = &event_stream.scope {
            if scope.is_empty() {
                self.errors.push(Error::invalid_field(decl, "scope"));
            }
            for value in scope {
                match value {
                    fdecl::Ref::Child(child) => {
                        self.validate_child_ref("OfferEventStream", "scope", &child);
                    }
                    fdecl::Ref::Collection(collection) => {
                        self.validate_collection_ref("OfferEventStream", "scope", &collection);
                    }
                    _ => {
                        self.errors.push(Error::invalid_field("OfferEventStream", "scope"));
                    }
                }
            }
        }
        // Only parent, framework, child, and void are valid.
        match event_stream.source {
            Some(
                fdecl::Ref::Parent(_)
                | fdecl::Ref::Framework(_)
                | fdecl::Ref::Child(_)
                | fdecl::Ref::VoidType(_),
            ) => {}
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        };

        check_offer_availability(
            decl,
            event_stream.availability.as_ref(),
            event_stream.source.as_ref(),
            event_stream.source_name.as_ref(),
            &mut self.errors,
        );

        match offer_type {
            OfferType::Static => {
                let target_id = self.validate_offer_target(&event_stream.target, decl, "target");
                if let (Some(target_id), Some(target_name)) =
                    (target_id, event_stream.target_name.as_ref())
                {
                    // Assuming the target_name is valid, ensure the target_name isn't already used.
                    if let Some(_) = self
                        .target_ids
                        .entry(target_id)
                        .or_insert(HashMap::new())
                        .insert(target_name, AllowableIds::One)
                    {
                        self.errors.push(Error::duplicate_field(
                            decl,
                            "target_name",
                            target_name as &str,
                        ));
                    }
                }
            }
            OfferType::Dynamic => {
                if event_stream.target.is_some() {
                    self.errors.push(Error::extraneous_field(decl, "target"));
                }
            }
        }
        check_name(event_stream.target_name.as_ref(), decl, "target_name", &mut self.errors);
    }

    fn validate_event_offer_fields(&mut self, event: &'a fdecl::OfferEvent, offer_type: OfferType) {
        let decl = "OfferEvent";
        check_offer_name(
            event.source_name.as_ref(),
            decl,
            "source_name",
            &mut self.errors,
            offer_type,
        );

        // Only parent, framework, and void are valid.
        match event.source {
            Some(fdecl::Ref::Parent(_) | fdecl::Ref::Framework(_) | fdecl::Ref::VoidType(_)) => {}
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        };

        check_offer_availability(
            decl,
            event.availability.as_ref(),
            event.source.as_ref(),
            event.source_name.as_ref(),
            &mut self.errors,
        );

        match offer_type {
            OfferType::Static => {
                let target_id = self.validate_offer_target(&event.target, decl, "target");
                if let (Some(target_id), Some(target_name)) =
                    (target_id, event.target_name.as_ref())
                {
                    // Assuming the target_name is valid, ensure the target_name isn't already used.
                    if let Some(_) = self
                        .target_ids
                        .entry(target_id)
                        .or_insert(HashMap::new())
                        .insert(target_name, AllowableIds::One)
                    {
                        self.errors.push(Error::duplicate_field(
                            decl,
                            "target_name",
                            target_name as &str,
                        ));
                    }
                }
            }
            OfferType::Dynamic => {
                if event.target.is_some() {
                    self.errors.push(Error::extraneous_field(decl, "target"));
                }
            }
        }
        check_offer_name(
            event.target_name.as_ref(),
            decl,
            "target_name",
            &mut self.errors,
            offer_type,
        );
    }

    /// Check a `ChildRef` contains a valid child that exists.
    ///
    /// We ensure the target child is statically defined (i.e., not a dynamic child inside
    /// a collection).
    fn validate_child_ref(
        &mut self,
        decl: &str,
        field_name: &str,
        child: &fdecl::ChildRef,
    ) -> bool {
        // Ensure the name is valid, and the reference refers to a static child.
        //
        // We attempt to list all errors if possible.
        let mut valid = true;
        if !check_name(
            Some(&child.name),
            decl,
            &format!("{}.child.name", field_name),
            &mut self.errors,
        ) {
            valid = false;
        }
        if child.collection.is_some() {
            self.errors
                .push(Error::extraneous_field(decl, format!("{}.child.collection", field_name)));
            valid = false;
        }
        if !valid {
            return false;
        }

        // Ensure the child exists.
        let name: &str = &child.name;
        if !self.all_children.contains_key(name) {
            self.errors.push(Error::invalid_child(decl, field_name, name));
            return false;
        }

        true
    }

    /// Check a `CollectionRef` is valid and refers to an existing collection.
    fn validate_collection_ref(
        &mut self,
        decl: &str,
        field_name: &str,
        collection: &fdecl::CollectionRef,
    ) -> bool {
        // Ensure the name is valid.
        if !check_name(
            Some(&collection.name),
            decl,
            &format!("{}.collection.name", field_name),
            &mut self.errors,
        ) {
            return false;
        }

        // Ensure the collection exists.
        if !self.all_collections.contains(&collection.name as &str) {
            self.errors.push(Error::invalid_collection(decl, field_name, &collection.name as &str));
            return false;
        }

        true
    }

    fn validate_target_child(
        &mut self,
        decl: &str,
        allowable_names: AllowableIds,
        child: &'a fdecl::ChildRef,
        source: Option<&fdecl::Ref>,
        target_name: Option<&'a String>,
        dependency: Option<fdecl::DependencyType>,
    ) {
        if !self.validate_child_ref(decl, "target", child) {
            return;
        }
        if let Some(target_name) = target_name {
            let names_for_target =
                self.target_ids.entry(TargetId::Component(&child.name)).or_insert(HashMap::new());
            if let Some(prev_state) = names_for_target.insert(target_name, allowable_names) {
                if prev_state == AllowableIds::One || prev_state != allowable_names {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_name",
                        target_name as &str,
                    ));
                }
            }
            if let Some(fdecl::Ref::Child(source_child)) = source {
                if source_child.name == child.name
                    && dependency.unwrap_or(fdecl::DependencyType::Strong)
                        == fdecl::DependencyType::Strong
                {
                    self.errors.push(Error::offer_target_equals_source(decl, &child.name as &str));
                }
            }
        }
    }

    fn validate_target_collection(
        &mut self,
        decl: &str,
        allowable_names: AllowableIds,
        collection: &'a fdecl::CollectionRef,
        target_name: Option<&'a String>,
    ) {
        if !self.validate_collection_ref(decl, "target", &collection) {
            return;
        }
        if let Some(target_name) = target_name {
            let names_for_target = self
                .target_ids
                .entry(TargetId::Collection(&collection.name))
                .or_insert(HashMap::new());
            if let Some(prev_state) = names_for_target.insert(target_name, allowable_names) {
                if prev_state == AllowableIds::One || prev_state != allowable_names {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_name",
                        target_name as &str,
                    ));
                }
            }
        }
    }

    fn validate_storage_target(&mut self, decl: &str, target: Option<&'a fdecl::Ref>) {
        match target {
            Some(fdecl::Ref::Child(c)) => {
                self.validate_child_ref(decl, "target", &c);
            }
            Some(fdecl::Ref::Collection(c)) => {
                self.validate_collection_ref(decl, "target", &c);
            }
            Some(_) => self.errors.push(Error::invalid_field(decl, "target")),
            None => self.errors.push(Error::missing_field(decl, "target")),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::error::{Error, ErrorList},
        fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
        test_case::test_case,
    };

    macro_rules! test_validate {
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
                    validate_test($input, $result);
                }
            )+
        }
    }

    macro_rules! test_validate_any_result {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    results = $results:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_test_any_result($input, $results);
                }
            )+
        }
    }

    macro_rules! test_validate_values_data {
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
                    validate_values_data_test($input, $result);
                }
            )+
        }
    }

    macro_rules! test_validate_capabilities {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    as_builtin = $as_builtin:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_capabilities_test($input, $as_builtin, $result);
                }
            )+
        }
    }

    macro_rules! test_dependency {
        (
            $(
                ($test_name:ident) => {
                    ty = $ty:expr,
                    offer_decl = $offer_decl:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let mut decl = new_component_decl();
                    let dependencies = vec![
                        ("a", "b"),
                        ("b", "a"),
                    ];
                    let offers = dependencies.into_iter().map(|(from,to)| {
                        let mut offer_decl = $offer_decl;
                        offer_decl.source = Some(fdecl::Ref::Child(
                           fdecl::ChildRef { name: from.to_string(), collection: None },
                        ));
                        offer_decl.target = Some(fdecl::Ref::Child(
                           fdecl::ChildRef { name: to.to_string(), collection: None },
                        ));
                        $ty(offer_decl)
                    }).collect();
                    let children = ["a", "b"].iter().map(|name| {
                        fdecl::Child {
                            name: Some(name.to_string()),
                            url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            environment: None,
                            ..fdecl::Child::EMPTY
                        }
                    }).collect();
                    decl.offers = Some(offers);
                    decl.children = Some(children);
                    let result = Err(ErrorList::new(vec![
                        Error::dependency_cycle(
                            directed_graph::Error::CyclesDetected([vec!["child a", "child b", "child a"]].iter().cloned().collect()).format_cycle()),
                    ]));
                    validate_test(decl, result);
                }
            )+
        }
    }

    macro_rules! test_weak_dependency {
        (
            $(
                ($test_name:ident) => {
                    ty = $ty:expr,
                    offer_decl = $offer_decl:expr,
                },
            )+
        ) => {
            $(
                #[test_case(fdecl::DependencyType::Weak)]
                #[test_case(fdecl::DependencyType::WeakForMigration)]
                fn $test_name(weak_dep: fdecl::DependencyType) {
                    let mut decl = new_component_decl();
                    let offers = vec![
                        {
                            let mut offer_decl = $offer_decl;
                            offer_decl.source = Some(fdecl::Ref::Child(
                               fdecl::ChildRef { name: "a".to_string(), collection: None },
                            ));
                            offer_decl.target = Some(fdecl::Ref::Child(
                               fdecl::ChildRef { name: "b".to_string(), collection: None },
                            ));
                            offer_decl.dependency_type = Some(fdecl::DependencyType::Strong);
                            $ty(offer_decl)
                        },
                        {
                            let mut offer_decl = $offer_decl;
                            offer_decl.source = Some(fdecl::Ref::Child(
                               fdecl::ChildRef { name: "b".to_string(), collection: None },
                            ));
                            offer_decl.target = Some(fdecl::Ref::Child(
                               fdecl::ChildRef { name: "a".to_string(), collection: None },
                            ));
                            offer_decl.dependency_type = Some(weak_dep);
                            $ty(offer_decl)
                        },
                    ];
                    let children = ["a", "b"].iter().map(|name| {
                        fdecl::Child {
                            name: Some(name.to_string()),
                            url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            environment: None,
                            ..fdecl::Child::EMPTY
                        }
                    }).collect();
                    decl.offers = Some(offers);
                    decl.children = Some(children);
                    let result = Ok(());
                    validate_test(decl, result);
                }
            )+
        }
    }

    fn validate_test(input: fdecl::Component, expected_res: Result<(), ErrorList>) {
        let res = validate(&input);
        assert_eq!(res, expected_res);
    }

    fn validate_test_any_result(input: fdecl::Component, expected_res: Vec<Result<(), ErrorList>>) {
        let res = format!("{:?}", validate(&input));
        let expected_res_debug = format!("{:?}", expected_res);

        let matched_exp =
            expected_res.into_iter().find(|expected| res == format!("{:?}", expected));

        assert!(
            matched_exp.is_some(),
            "assertion failed: Expected one of:\n{:?}\nActual:\n{:?}",
            expected_res_debug,
            res
        );
    }

    fn validate_values_data_test(input: fconfig::ValuesData, expected_res: Result<(), ErrorList>) {
        let res = validate_values_data(&input);
        assert_eq!(res, expected_res);
    }

    fn validate_capabilities_test(
        input: Vec<fdecl::Capability>,
        as_builtin: bool,
        expected_res: Result<(), ErrorList>,
    ) {
        let res = validate_capabilities(&input, as_builtin);
        assert_eq!(res, expected_res);
    }

    fn new_component_decl() -> fdecl::Component {
        fdecl::Component {
            program: None,
            uses: None,
            exposes: None,
            offers: None,
            facets: None,
            capabilities: None,
            children: None,
            collections: None,
            environments: None,
            ..fdecl::Component::EMPTY
        }
    }

    test_validate_any_result! {
        test_validate_use_disallows_nested_dirs => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar/baz".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectory", "/foo/bar/baz", "UseDirectory", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectory", "/foo/bar", "UseDirectory", "/foo/bar/baz"),
                ])),
            ],
        },
        test_validate_use_disallows_nested_dirs_storage => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        ..fdecl::UseStorage::EMPTY
                    }),
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar/baz".to_string()),
                        ..fdecl::UseStorage::EMPTY
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseStorage", "/foo/bar/baz", "UseStorage", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseStorage", "/foo/bar", "UseStorage", "/foo/bar/baz"),
                ])),
            ],
        },
        test_validate_use_disallows_nested_dirs_directory_and_storage => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar/baz".to_string()),
                        ..fdecl::UseStorage::EMPTY
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseStorage", "/foo/bar/baz", "UseDirectory", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectory", "/foo/bar", "UseStorage", "/foo/bar/baz"),
                ])),
            ],
        },
        test_validate_use_disallows_common_prefixes_protocol => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                    fdecl::Use::Protocol(fdecl::UseProtocol {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("crow".to_string()),
                        target_path: Some("/foo/bar/fuchsia.2".to_string()),
                        ..fdecl::UseProtocol::EMPTY
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseProtocol", "/foo/bar/fuchsia.2", "UseDirectory", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectory", "/foo/bar", "UseProtocol", "/foo/bar/fuchsia.2"),
                ])),
            ],
        },
        test_validate_use_disallows_common_prefixes_service => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                    fdecl::Use::Service(fdecl::UseService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("space".to_string()),
                        target_path: Some("/foo/bar/baz/fuchsia.logger.Log".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::UseService::EMPTY
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseService", "/foo/bar/baz/fuchsia.logger.Log", "UseDirectory", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectory", "/foo/bar", "UseService", "/foo/bar/baz/fuchsia.logger.Log"),
                ])),
            ],
        },
        test_validate_use_disallows_pkg => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("abc".to_string()),
                        target_path: Some("/pkg".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::pkg_path_overlap("UseDirectory", "/pkg"),
                ])),
            ],
        },
        test_validate_use_disallows_pkg_overlap => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("abc".to_string()),
                        target_path: Some("/pkg/foo".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::pkg_path_overlap("UseDirectory", "/pkg/foo"),
                ])),
            ],
        },
    }

    test_validate_values_data! {
        test_values_data_ok => {
            input = fconfig::ValuesData {
                values: Some(vec![
                    fconfig::ValueSpec {
                        value: Some(fconfig::Value::Single(fconfig::SingleValue::Bool(true))),
                        ..fconfig::ValueSpec::EMPTY
                    }
                ]),
                checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                ..fconfig::ValuesData::EMPTY
            },
            result = Ok(()),
        },
        test_values_data_no_checksum => {
            input = fconfig::ValuesData {
                values: Some(vec![]),
                checksum: None,
                ..fconfig::ValuesData::EMPTY
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ValuesData", "checksum")
            ])),
        },
        test_values_data_unknown_checksum => {
            input = fconfig::ValuesData {
                values: Some(vec![]),
                checksum: Some(fdecl::ConfigChecksum::unknown(0, vec![])),
                ..fconfig::ValuesData::EMPTY
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ValuesData", "checksum")
            ])),
        },
        test_values_data_no_values => {
            input = fconfig::ValuesData {
                values: None,
                checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                ..fconfig::ValuesData::EMPTY
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ValuesData", "values")
            ])),
        },
        test_values_data_no_inner_value => {
            input = fconfig::ValuesData {
                values: Some(vec![
                    fconfig::ValueSpec::EMPTY
                ]),
                checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                ..fconfig::ValuesData::EMPTY
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ValueSpec", "value")
            ])),
        },
        test_values_data_unknown_inner_value => {
            input = fconfig::ValuesData {
                values: Some(vec![
                    fconfig::ValueSpec {
                        value: Some(fconfig::Value::unknown(0, vec![])),
                        ..fconfig::ValueSpec::EMPTY
                    }
                ]),
                checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                ..fconfig::ValuesData::EMPTY
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ValueSpec", "value")
            ])),
        },
        test_values_data_unknown_single_value => {
            input = fconfig::ValuesData {
                values: Some(vec![
                    fconfig::ValueSpec {
                        value: Some(fconfig::Value::Single(fconfig::SingleValue::unknown(0, vec![]))),
                        ..fconfig::ValueSpec::EMPTY
                    }
                ]),
                checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                ..fconfig::ValuesData::EMPTY
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ValueSpec", "value")
            ])),
        },
        test_values_data_unknown_list_value => {
            input = fconfig::ValuesData {
                values: Some(vec![
                    fconfig::ValueSpec {
                        value: Some(fconfig::Value::Vector(fconfig::VectorValue::unknown(0, vec![]))),
                        ..fconfig::ValueSpec::EMPTY
                    }
                ]),
                checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                ..fconfig::ValuesData::EMPTY
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ValueSpec", "value")
            ])),
        },
    }

    test_validate! {
        // uses
        test_validate_uses_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.program = Some(fdecl::Program {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: None,
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fdecl::Program::EMPTY
                });
                decl.uses = Some(vec![
                    fdecl::Use::Service(fdecl::UseService {
                        source: None,
                        source_name: None,
                        target_path: None,
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::UseService::EMPTY
                    }),
                    fdecl::Use::Protocol(fdecl::UseProtocol {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: None,
                        source_name: None,
                        target_path: None,
                        ..fdecl::UseProtocol::EMPTY
                    }),
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: None,
                        source_name: None,
                        target_path: None,
                        rights: None,
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: None,
                        target_path: None,
                        ..fdecl::UseStorage::EMPTY
                    }),
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: Some("cache".to_string()),
                        target_path: None,
                        ..fdecl::UseStorage::EMPTY
                    }),
                    fdecl::Use::Event(fdecl::UseEvent {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: None,
                        source_name: None,
                        target_name: None,
                        filter: None,
                        ..fdecl::UseEvent::EMPTY
                    }),
                    fdecl::Use::EventStreamDeprecated(fdecl::UseEventStreamDeprecated {
                        name: None,
                        subscriptions: None,
                        ..fdecl::UseEventStreamDeprecated::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("UseEvent", "source"),
                Error::missing_field("UseEvent", "source_name"),
                Error::missing_field("UseEvent", "target_name"),
                Error::missing_field("UseService", "source"),
                Error::missing_field("UseService", "source_name"),
                Error::missing_field("UseService", "target_path"),
                Error::missing_field("UseProtocol", "source"),
                Error::missing_field("UseProtocol", "source_name"),
                Error::missing_field("UseProtocol", "target_path"),
                Error::missing_field("UseDirectory", "source"),
                Error::missing_field("UseDirectory", "source_name"),
                Error::missing_field("UseDirectory", "target_path"),
                Error::missing_field("UseDirectory", "rights"),
                Error::missing_field("UseStorage", "source_name"),
                Error::missing_field("UseStorage", "target_path"),
                Error::missing_field("UseStorage", "target_path"),
                Error::missing_field("UseEventStream", "name"),
                Error::missing_field("UseEventStream", "subscriptions"),
            ])),
        },
        test_validate_missing_program_info => {
            input = {
                let mut decl = new_component_decl();
                decl.program = Some(fdecl::Program {
                    runner: Some("runner".to_string()),
                    info: None,
                    ..fdecl::Program::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("Program", "info")
            ])),
        },
        test_validate_uses_invalid_identifiers_service => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Service(fdecl::UseService {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                        source_name: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::UseService::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseService", "source_name"),
                Error::invalid_field("UseService", "target_path"),
            ])),
        },
        test_validate_uses_invalid_identifiers_protocol => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Protocol(fdecl::UseProtocol {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                        source_name: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        ..fdecl::UseProtocol::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseProtocol", "source_name"),
                Error::invalid_field("UseProtocol", "target_path"),
            ])),
        },
        test_validate_uses_invalid_identifiers_directory => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                        source_name: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: Some("/foo".to_string()),
                        ..fdecl::UseDirectory::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseDirectory", "source_name"),
                Error::invalid_field("UseDirectory", "target_path"),
                Error::invalid_field("UseDirectory", "subdir"),
            ])),
        },
        test_validate_uses_invalid_identifiers_storage => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: Some("/cache".to_string()),
                        target_path: Some("/".to_string()),
                        ..fdecl::UseStorage::EMPTY
                    }),
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: Some("temp".to_string()),
                        target_path: Some("tmp".to_string()),
                        ..fdecl::UseStorage::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseStorage", "source_name"),
                Error::invalid_field("UseStorage", "target_path"),
                Error::invalid_field("UseStorage", "target_path"),
            ])),
        },
        test_validate_uses_invalid_identifiers_event => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Event(fdecl::UseEvent {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                        source_name: Some("/foo".to_string()),
                        target_name: Some("/foo".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        ..fdecl::UseEvent::EMPTY
                    }),
                    fdecl::Use::Event(fdecl::UseEvent {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                        source_name: Some("started".to_string()),
                        target_name: Some("started".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        ..fdecl::UseEvent::EMPTY
                    }),
                    fdecl::Use::EventStreamDeprecated(fdecl::UseEventStreamDeprecated {
                        name: Some("bar".to_string()),
                        subscriptions: Some(vec!["a".to_string(), "b".to_string()].into_iter().map(|name| fdecl::EventSubscription {
                            event_name: Some(name),
                            ..fdecl::EventSubscription::EMPTY
                        }).collect()),
                        ..fdecl::UseEventStreamDeprecated::EMPTY
                    }),
                    fdecl::Use::EventStreamDeprecated(fdecl::UseEventStreamDeprecated {
                        name: Some("bleep".to_string()),
                        subscriptions: Some(vec![fdecl::EventSubscription {
                            event_name: Some("started".to_string()),
                            ..fdecl::EventSubscription::EMPTY
                        }]),
                        ..fdecl::UseEventStreamDeprecated::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseEvent", "source"),
                Error::invalid_field("UseEvent", "source_name"),
                Error::invalid_field("UseEvent", "target_name"),
                Error::event_stream_event_not_found("UseEventStream", "events", "a".to_string()),
                Error::event_stream_event_not_found("UseEventStream", "events", "b".to_string()),
            ])),
        },
        test_validate_uses_missing_source => {
            input = {
                fdecl::Component {
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Capability(fdecl::CapabilityRef {
                                name: "this-storage-doesnt-exist".to_string(),
                            })),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        })
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("UseProtocol", "source", "this-storage-doesnt-exist"),
            ])),
        },
        test_validate_uses_invalid_child => {
            input = {
                fdecl::Component {
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                        fdecl::Use::Service(fdecl::UseService {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                            source_name: Some("service_name".to_string()),
                            target_path: Some("/svc/service_name".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            ..fdecl::UseService::EMPTY
                        }),
                        fdecl::Use::Directory(fdecl::UseDirectory {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                            source_name: Some("DirectoryName".to_string()),
                            target_path: Some("/data/DirectoryName".to_string()),
                            rights: Some(fio::Operations::CONNECT),
                            subdir: None,
                            ..fdecl::UseDirectory::EMPTY
                        }),
                        fdecl::Use::Event(fdecl::UseEvent {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                            source_name: Some("abc".to_string()),
                            target_name: Some("abc".to_string()),
                            filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                            ..fdecl::UseEvent::EMPTY
                        }),
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("UseEvent", "source", "no-such-child"),
                Error::invalid_child("UseProtocol", "source", "no-such-child"),
                Error::invalid_child("UseService", "source", "no-such-child"),
                Error::invalid_child("UseDirectory", "source", "no-such-child"),
            ])),
        },
        test_validate_use_from_child_offer_to_child_strong_cycle => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Service(fdecl::Service {
                            name: Some("a".to_string()),
                            source_path: Some("/a".to_string()),
                            ..fdecl::Service::EMPTY
                        })]),
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                        fdecl::Use::Service(fdecl::UseService {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("service_name".to_string()),
                            target_path: Some("/svc/service_name".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            ..fdecl::UseService::EMPTY
                        }),
                        fdecl::Use::Directory(fdecl::UseDirectory {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("DirectoryName".to_string()),
                            target_path: Some("/data/DirectoryName".to_string()),
                            rights: Some(fio::Operations::CONNECT),
                            subdir: None,
                            ..fdecl::UseDirectory::EMPTY
                        }),
                        fdecl::Use::Event(fdecl::UseEvent {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("abc".to_string()),
                            target_name: Some("abc".to_string()),
                            filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                            ..fdecl::UseEvent::EMPTY
                        })
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("a".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                            target_name: Some("a".to_string()),
                            ..fdecl::OfferService::EMPTY
                        })
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
            ])),
        },
        test_validate_use_from_child_storage_no_cycle => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Storage(fdecl::Storage {
                            name: Some("cdata".to_string()),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child2".to_string(), collection: None } )),
                            backing_dir: Some("minfs".to_string()),
                            storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                            ..fdecl::Storage::EMPTY
                        }),
                        fdecl::Capability::Storage(fdecl::Storage {
                            name: Some("pdata".to_string()),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            backing_dir: Some("minfs".to_string()),
                            storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                            ..fdecl::Storage::EMPTY
                        }),
                    ]),
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child1".to_string(), collection: None})),
                            source_name: Some("a".to_string()),
                            target_path: Some("/svc/a".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Storage(fdecl::OfferStorage {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("cdata".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            target_name: Some("cdata".to_string()),
                            ..fdecl::OfferStorage::EMPTY
                        }),
                        fdecl::Offer::Storage(fdecl::OfferStorage {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("pdata".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            target_name: Some("pdata".to_string()),
                            ..fdecl::OfferStorage::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child1".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        },
                        fdecl::Child {
                            name: Some("child2".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Ok(()),
        },
        test_validate_use_from_child_storage_cycle => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Storage(fdecl::Storage {
                            name: Some("data".to_string()),
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                            backing_dir: Some("minfs".to_string()),
                            storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                            ..fdecl::Storage::EMPTY
                        }),
                    ]),
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("a".to_string()),
                            target_path: Some("/svc/a".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Storage(fdecl::OfferStorage {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("data".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                            target_name: Some("data".to_string()),
                            ..fdecl::OfferStorage::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        },
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
            ])),
        },
        test_validate_storage_strong_cycle_between_children => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Storage(fdecl::Storage {
                            name: Some("data".to_string()),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None } )),
                            backing_dir: Some("minfs".to_string()),
                            storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                            ..fdecl::Storage::EMPTY
                        })
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Storage(fdecl::OfferStorage {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("data".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child2".to_string(), collection: None })),
                            target_name: Some("data".to_string()),
                            ..fdecl::OfferStorage::EMPTY
                        }),
                        fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child2".to_string(), collection: None })),
                            source_name: Some("a".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            target_name: Some("a".to_string()),
                            ..fdecl::OfferService::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child1".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        },
                        fdecl::Child {
                            name: Some("child2".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{child child1 -> child child2 -> child child1}}".to_string()),
            ])),
        },
        test_validate_strong_cycle_between_children_through_environment_debug => {
            input = {
                fdecl::Component {
                    environments: Some(vec![
                        fdecl::Environment {
                            name: Some("env".to_string()),
                            extends: Some(fdecl::EnvironmentExtends::Realm),
                            debug_capabilities: Some(vec![
                                fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                    source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                                    source_name: Some("fuchsia.foo.Bar".to_string()),
                                    target_name: Some("fuchsia.foo.Bar".to_string()),
                                    ..fdecl::DebugProtocolRegistration::EMPTY
                                }),
                            ]),
                            ..fdecl::Environment::EMPTY
                        },
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child2".to_string(), collection: None })),
                            source_name: Some("a".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            target_name: Some("a".to_string()),
                            ..fdecl::OfferService::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child1".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        },
                        fdecl::Child {
                            name: Some("child2".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            environment: Some("env".to_string()),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{child child1 -> environment env -> child child2 -> child child1}}".to_string()),
            ])),
        },
        test_validate_strong_cycle_between_children_through_environment_runner => {
            input = {
                fdecl::Component {
                    environments: Some(vec![
                        fdecl::Environment {
                            name: Some("env".to_string()),
                            extends: Some(fdecl::EnvironmentExtends::Realm),
                            runners: Some(vec![
                                fdecl::RunnerRegistration {
                                    source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                                    source_name: Some("coff".to_string()),
                                    target_name: Some("coff".to_string()),
                                    ..fdecl::RunnerRegistration::EMPTY
                                }
                            ]),
                            ..fdecl::Environment::EMPTY
                        },
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child2".to_string(), collection: None })),
                            source_name: Some("a".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            target_name: Some("a".to_string()),
                            ..fdecl::OfferService::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child1".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        },
                        fdecl::Child {
                            name: Some("child2".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            environment: Some("env".to_string()),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{child child1 -> environment env -> child child2 -> child child1}}".to_string()),
            ])),
        },
        test_validate_strong_cycle_between_children_through_environment_resolver => {
            input = {
                fdecl::Component {
                    environments: Some(vec![
                        fdecl::Environment {
                            name: Some("env".to_string()),
                            extends: Some(fdecl::EnvironmentExtends::Realm),
                            resolvers: Some(vec![
                                fdecl::ResolverRegistration {
                                    resolver: Some("gopher".to_string()),
                                    source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                                    scheme: Some("gopher".to_string()),
                                    ..fdecl::ResolverRegistration::EMPTY
                                }
                            ]),
                            ..fdecl::Environment::EMPTY
                        },
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child2".to_string(), collection: None })),
                            source_name: Some("a".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            target_name: Some("a".to_string()),
                            ..fdecl::OfferService::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child1".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        },
                        fdecl::Child {
                            name: Some("child2".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            environment: Some("env".to_string()),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{child child1 -> environment env -> child child2 -> child child1}}".to_string()),
            ])),
        },
        test_validate_strong_cycle_between_self_and_two_children => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Protocol(fdecl::Protocol {
                            name: Some("fuchsia.foo.Bar".to_string()),
                            source_path: Some("/svc/fuchsia.foo.Bar".to_string()),
                            ..fdecl::Protocol::EMPTY
                        })
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Protocol(fdecl::OfferProtocol {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("fuchsia.foo.Bar".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            target_name: Some("fuchsia.foo.Bar".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            ..fdecl::OfferProtocol::EMPTY
                        }),
                        fdecl::Offer::Protocol(fdecl::OfferProtocol {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child1".to_string(), collection: None })),
                            source_name: Some("fuchsia.bar.Baz".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child2".to_string(), collection: None })),
                            target_name: Some("fuchsia.bar.Baz".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            ..fdecl::OfferProtocol::EMPTY
                        }),
                    ]),
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child2".to_string(), collection: None})),
                            source_name: Some("fuchsia.baz.Foo".to_string()),
                            target_path: Some("/svc/fuchsia.baz.Foo".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child1".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        },
                        fdecl::Child {
                            name: Some("child2".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{self -> child child1 -> child child2 -> self}}".to_string()),
            ])),
        },
        test_validate_strong_cycle_with_self_storage => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Storage(fdecl::Storage {
                            name: Some("data".to_string()),
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            backing_dir: Some("minfs".to_string()),
                            storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                            ..fdecl::Storage::EMPTY
                        }),
                        fdecl::Capability::Directory(fdecl::Directory {
                            name: Some("minfs".to_string()),
                            source_path: Some("/minfs".to_string()),
                            rights: Some(fio::RW_STAR_DIR),
                            ..fdecl::Directory::EMPTY
                        }),
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Storage(fdecl::OfferStorage {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("data".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                            target_name: Some("data".to_string()),
                            ..fdecl::OfferStorage::EMPTY
                        }),
                    ]),
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("fuchsia.foo.Bar".to_string()),
                            target_path: Some("/svc/fuchsia.foo.Bar".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            ..fdecl::Child::EMPTY
                        },
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
            ])),
        },
        test_validate_strong_cycle_with_self_storage_admin_protocol => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Storage(fdecl::Storage {
                            name: Some("data".to_string()),
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            backing_dir: Some("minfs".to_string()),
                            storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                            ..fdecl::Storage::EMPTY
                        }),
                        fdecl::Capability::Directory(fdecl::Directory {
                            name: Some("minfs".to_string()),
                            source_path: Some("/minfs".to_string()),
                            rights: Some(fio::RW_STAR_DIR),
                            ..fdecl::Directory::EMPTY
                        }),
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Protocol(fdecl::OfferProtocol {
                            source: Some(fdecl::Ref::Capability(fdecl::CapabilityRef { name: "data".to_string() })),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                            target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            ..fdecl::OfferProtocol::EMPTY
                        }),
                    ]),
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("fuchsia.foo.Bar".to_string()),
                            target_path: Some("/svc/fuchsia.foo.Bar".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            ..fdecl::Child::EMPTY
                        },
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
            ])),
        },
        test_validate_use_from_child_offer_to_child_weak_cycle => {
            input = {
                fdecl::Component {
                    capabilities: Some(vec![
                        fdecl::Capability::Service(fdecl::Service {
                            name: Some("a".to_string()),
                            source_path: Some("/a".to_string()),
                            ..fdecl::Service::EMPTY
                        })]),
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Weak),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                        fdecl::Use::Service(fdecl::UseService {
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("service_name".to_string()),
                            target_path: Some("/svc/service_name".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Weak),
                            ..fdecl::UseService::EMPTY
                        }),
                        fdecl::Use::Directory(fdecl::UseDirectory {
                            dependency_type: Some(fdecl::DependencyType::WeakForMigration),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("DirectoryName".to_string()),
                            target_path: Some("/data/DirectoryName".to_string()),
                            rights: Some(fio::Operations::CONNECT),
                            subdir: None,
                            ..fdecl::UseDirectory::EMPTY
                        }),
                        fdecl::Use::Event(fdecl::UseEvent {
                            dependency_type: Some(fdecl::DependencyType::WeakForMigration),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{ name: "child".to_string(), collection: None})),
                            source_name: Some("abc".to_string()),
                            target_name: Some("abc".to_string()),
                            filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                            ..fdecl::UseEvent::EMPTY
                        })
                    ]),
                    offers: Some(vec![
                        fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            source_name: Some("a".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                            target_name: Some("a".to_string()),
                            ..fdecl::OfferService::EMPTY
                        })
                    ]),
                    children: Some(vec![
                        fdecl::Child {
                            name: Some("child".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                            startup: Some(fdecl::StartupMode::Lazy),
                            on_terminate: None,
                            ..fdecl::Child::EMPTY
                        }
                    ]),
                    ..new_component_decl()
                }
            },
            result = Ok(()),
        },
        test_validate_use_from_not_child_weak => {
            input = {
                fdecl::Component {
                    uses: Some(vec![
                        fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Weak),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        }),
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseProtocol", "dependency_type"),
            ])),
        },
        test_validate_has_events_in_event_stream => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::EventStreamDeprecated(fdecl::UseEventStreamDeprecated {
                        name: Some("bar".to_string()),
                        subscriptions: None,
                        ..fdecl::UseEventStreamDeprecated::EMPTY
                    }),
                    fdecl::Use::EventStreamDeprecated(fdecl::UseEventStreamDeprecated {
                        name: Some("barbar".to_string()),
                        subscriptions: Some(vec![]),
                        ..fdecl::UseEventStreamDeprecated::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("UseEventStream", "subscriptions"),
                Error::empty_field("UseEventStream", "subscriptions"),
            ])),
        },
        test_validate_event_stream_offer_valid_decls => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Ok(()),
        },
        test_validate_event_stream_offer_to_framework_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "OfferEventStream".to_string(), field: "target".to_string() }),
                Error::InvalidField(DeclField { decl: "OfferEventStream".to_string(), field: "target".to_string() }),
            ])),
        },
        test_validate_event_stream_offer_to_scope_zero_length_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        scope: Some(vec![]),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "OfferEventStream".to_string(), field: "scope".to_string() }),
            ])),
        },
        test_validate_event_stream_offer_to_scope_framework_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        scope: Some(vec![fdecl::Ref::Framework(fdecl::FrameworkRef{})]),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "OfferEventStream".to_string(), field: "scope".to_string() }),
            ])),
        },
        test_validate_event_stream_offer_to_scope_valid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        scope: Some(vec![fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})]),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Ok(()),
        },
        test_validate_event_stream_offer_to_scope_with_capability_requested => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("capability_requested".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        scope: Some(vec![fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})]),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("directory_ready".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Ok(()),
        },
        test_validate_event_stream_offer_to_scope_with_invalid_capability_name => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("some_invalid_capability".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        scope: Some(vec![fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})]),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("directory_ready".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "OfferEventStream".to_string(), field: "filter".to_string() }),
            ])),
        },
        test_validate_event_stream_offer_with_no_source_name_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: None,
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        scope: Some(vec![fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})]),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::MissingField(DeclField { decl: "OfferEventStream".to_string(), field: "source_name".to_string() }),
            ])),
        },
        test_validate_event_stream_offer_duplicate_target => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::DuplicateField(DeclField { decl: "OfferEventStream".to_string(), field: "target_name".to_string() }, "started".to_string()),
            ])),
        },
        test_validate_event_stream_offer_invalid_source => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        source: Some(fdecl::Ref::Debug(fdecl::DebugRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "OfferEventStream".to_string(), field: "source".to_string() }),
            ])),
        },

        test_validate_event_stream_offer_missing_source => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("started".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                    fdecl::Offer::EventStream(fdecl::OfferEventStream {
                        source_name: Some("diagnostics_ready".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test2".to_string(), collection: None})),
                        target_name: Some("diagnostics_ready".to_string()),
                        ..fdecl::OfferEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fdecl::Child{
                    name: Some("test2".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/fake_component#meta/fake_component.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::MissingField(DeclField { decl: "OfferEventStream".to_string(), field: "source".to_string() }),
            ])),
        },

        test_validate_event_stream_expose_to_framework_from_framework_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::EventStream(fdecl::ExposeEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::ExposeEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "ExposeEventStream".to_string(), field: "target".to_string() }),
                Error::InvalidField(DeclField { decl: "ExposeEventStream".to_string(), field: "target".to_string() }),
                Error::InvalidField(DeclField { decl: "ExposeEventStream".to_string(), field: "source".to_string() }),
            ])),
        },
        test_validate_event_stream_expose_to_framework_from_other_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::EventStream(fdecl::ExposeEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::ExposeEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "ExposeEventStream".to_string(), field: "source".to_string() }),
                Error::InvalidField(DeclField { decl: "ExposeEventStream".to_string(), field: "target".to_string() }),
                Error::InvalidField(DeclField { decl: "ExposeEventStream".to_string(), field: "target".to_string() }),
            ])),
        },
        test_validate_event_stream_scope_must_be_non_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::EventStream(fdecl::ExposeEventStream {
                        source_name: Some("stopped".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "test".to_string(), collection: None})),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        scope: Some(vec![]),
                        target_name: Some("stopped".to_string()),
                        ..fdecl::ExposeEventStream::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("test".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "ExposeEventStream".to_string(), field: "scope".to_string() }),
            ])),
        },
        test_validate_event_stream_must_have_target_path => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::EventStream(fdecl::UseEventStream {
                        source_name: Some("bar".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        ..fdecl::UseEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::MissingField(DeclField { decl: "UseEventStream".to_string(), field: "target_path".to_string() })
            ])),
        },
        test_validate_event_stream_must_have_source_names => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::EventStream(fdecl::UseEventStream {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target_path: Some("/svc/something".to_string()),
                        ..fdecl::UseEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::MissingField(DeclField { decl: "UseEventStream".to_string(), field: "source_name".to_string() })
            ])),
        },
        test_validate_event_stream_scope_must_be_child_or_collection => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::EventStream(fdecl::UseEventStream {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target_path: Some("/svc/something".to_string()),
                        source_name: Some("some_source".to_string()),
                        scope: Some(vec![fdecl::Ref::Framework(fdecl::FrameworkRef{})]),
                        ..fdecl::UseEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "UseEventStream".to_string(), field: "scope".to_string() })
            ])),
        },
        test_validate_event_stream_source_must_be_parent_framework_or_child => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::EventStream(fdecl::UseEventStream {
                        source: Some(fdecl::Ref::Debug(fdecl::DebugRef{})),
                        target_path: Some("/svc/something".to_string()),
                        source_name: Some("some_source".to_string()),
                        scope: Some(vec![]),
                        ..fdecl::UseEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "UseEventStream".to_string(), field: "source".to_string() })
            ])),
        },
        test_validate_event_stream_source_framework_must_have_nonempty_scope => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::EventStream(fdecl::UseEventStream {
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target_path: Some("/svc/something".to_string()),
                        source_name: Some("some_source".to_string()),
                        scope: Some(vec![]),
                        ..fdecl::UseEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::InvalidField(DeclField { decl: "UseEventStream".to_string(), field: "scope".to_string() })
            ])),
        },
        test_validate_event_stream_source_framework_must_specify_scope => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::EventStream(fdecl::UseEventStream {
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                        target_path: Some("/svc/something".to_string()),
                        source_name: Some("some_source".to_string()),
                        ..fdecl::UseEventStream::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::MissingField(DeclField { decl: "UseEventStream".to_string(), field: "scope".to_string() })
            ])),
        },
        test_validate_uses_no_runner => {
            input = {
                let mut decl = new_component_decl();
                decl.program = Some(fdecl::Program {
                    runner: None,
                    info: Some(fdata::Dictionary {
                        entries: None,
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fdecl::Program::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("Program", "runner"),
            ])),
        },
        test_validate_uses_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.program = Some(fdecl::Program {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: None,
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fdecl::Program::EMPTY
                });
                decl.uses = Some(vec![
                    fdecl::Use::Service(fdecl::UseService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_path: Some(format!("/s/{}", "b".repeat(1024))),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::UseService::EMPTY
                    }),
                    fdecl::Use::Protocol(fdecl::UseProtocol {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_path: Some(format!("/p/{}", "c".repeat(1024))),
                        ..fdecl::UseProtocol::EMPTY
                    }),
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_path: Some(format!("/d/{}", "d".repeat(1024))),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                    fdecl::Use::Storage(fdecl::UseStorage {
                        source_name: Some("cache".to_string()),
                        target_path: Some(format!("/{}", "e".repeat(1024))),
                        ..fdecl::UseStorage::EMPTY
                    }),
                    fdecl::Use::Event(fdecl::UseEvent {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_name: Some(format!("{}", "a".repeat(101))),
                        filter: None,
                        ..fdecl::UseEvent::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long_with_max("UseEvent", "source_name", 100),
                Error::field_too_long_with_max("UseEvent", "target_name", 100),
                Error::field_too_long_with_max("UseService", "source_name", 100),
                Error::field_too_long_with_max("UseService", "target_path", 1024),
                Error::field_too_long_with_max("UseProtocol", "source_name", 100),
                Error::field_too_long_with_max("UseProtocol", "target_path", 1024),
                Error::field_too_long_with_max("UseDirectory", "source_name", 100),
                Error::field_too_long_with_max("UseDirectory", "target_path", 1024),
                Error::field_too_long_with_max("UseStorage", "target_path", 1024),
            ])),
        },
        test_validate_conflicting_paths => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Service(fdecl::UseService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("foo".to_string()),
                        target_path: Some("/bar".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::UseService::EMPTY
                    }),
                    fdecl::Use::Protocol(fdecl::UseProtocol {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("space".to_string()),
                        target_path: Some("/bar".to_string()),
                        ..fdecl::UseProtocol::EMPTY
                    }),
                    fdecl::Use::Directory(fdecl::UseDirectory {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("crow".to_string()),
                        target_path: Some("/bar".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::UseDirectory::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("UseProtocol", "path", "/bar"),
                Error::duplicate_field("UseDirectory", "path", "/bar"),
            ])),
        },
        test_validate_events_can_come_before_or_after_event_stream => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Event(fdecl::UseEvent {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                        source_name: Some("started".to_string()),
                        target_name: Some("started".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        ..fdecl::UseEvent::EMPTY
                    }),
                    fdecl::Use::EventStreamDeprecated(fdecl::UseEventStreamDeprecated {
                        name: Some("bar".to_string()),
                        subscriptions: Some(
                            vec!["started".to_string(), "stopped".to_string()]
                                .into_iter()
                                .map(|name| fdecl::EventSubscription {
                                    event_name: Some(name),
                                    ..fdecl::EventSubscription::EMPTY
                                })
                                .collect()
                            ),
                        ..fdecl::UseEventStreamDeprecated::EMPTY
                    }),
                    fdecl::Use::Event(fdecl::UseEvent {
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                        source_name: Some("stopped".to_string()),
                        target_name: Some("stopped".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        ..fdecl::UseEvent::EMPTY
                    }),
                ]);
                decl
            },
            result = Ok(()),
        },
        test_validate_uses_invalid_self_source => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    fdecl::Use::Event(fdecl::UseEvent {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                        source_name: Some("started".to_string()),
                        target_name: Some("foo_started".to_string()),
                        ..fdecl::UseEvent::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseEvent", "source"),
            ])),
        },
        // exposes
        test_validate_exposes_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: None,
                        source_name: None,
                        target_name: None,
                        target: None,
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: None,
                        source_name: None,
                        target_name: None,
                        target: None,
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: None,
                        source_name: None,
                        target_name: None,
                        target: None,
                        rights: None,
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeService", "source"),
                Error::missing_field("ExposeService", "target"),
                Error::missing_field("ExposeService", "source_name"),
                Error::missing_field("ExposeService", "target_name"),
                Error::missing_field("ExposeProtocol", "source"),
                Error::missing_field("ExposeProtocol", "target"),
                Error::missing_field("ExposeProtocol", "source_name"),
                Error::missing_field("ExposeProtocol", "target_name"),
                Error::missing_field("ExposeDirectory", "source"),
                Error::missing_field("ExposeDirectory", "target"),
                Error::missing_field("ExposeDirectory", "source_name"),
                Error::missing_field("ExposeDirectory", "target_name"),
                Error::missing_field("ExposeRunner", "source"),
                Error::missing_field("ExposeRunner", "target"),
                Error::missing_field("ExposeRunner", "source_name"),
                Error::missing_field("ExposeRunner", "target_name"),
                Error::missing_field("ExposeResolver", "source"),
                Error::missing_field("ExposeResolver", "target"),
                Error::missing_field("ExposeResolver", "source_name"),
                Error::missing_field("ExposeResolver", "target_name"),
            ])),
        },
        test_validate_exposes_extraneous => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("logger".to_string()),
                        target_name: Some("logger".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("legacy_logger".to_string()),
                        target_name: Some("legacy_logger".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("data".to_string()),
                        target_name: Some("data".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("elf".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("ExposeService", "source.child.collection"),
                Error::extraneous_field("ExposeProtocol", "source.child.collection"),
                Error::extraneous_field("ExposeDirectory", "source.child.collection"),
                Error::extraneous_field("ExposeRunner", "source.child.collection"),
                Error::extraneous_field("ExposeResolver", "source.child.collection"),
            ])),
        },
        test_validate_exposes_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target_name: Some("/".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target_name: Some("/".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target_name: Some("/".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: Some("/foo".to_string()),
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("elf!".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("pkg!".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ExposeService", "source.child.name"),
                Error::invalid_field("ExposeService", "source_name"),
                Error::invalid_field("ExposeService", "target_name"),
                Error::invalid_field("ExposeProtocol", "source.child.name"),
                Error::invalid_field("ExposeProtocol", "source_name"),
                Error::invalid_field("ExposeProtocol", "target_name"),
                Error::invalid_field("ExposeDirectory", "source.child.name"),
                Error::invalid_field("ExposeDirectory", "source_name"),
                Error::invalid_field("ExposeDirectory", "target_name"),
                Error::invalid_field("ExposeDirectory", "subdir"),
                Error::invalid_field("ExposeRunner", "source.child.name"),
                Error::invalid_field("ExposeRunner", "source_name"),
                Error::invalid_field("ExposeRunner", "target_name"),
                Error::invalid_field("ExposeResolver", "source.child.name"),
                Error::invalid_field("ExposeResolver", "source_name"),
                Error::invalid_field("ExposeResolver", "target_name"),
            ])),
        },
        test_validate_exposes_invalid_source_target => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![fdecl::Child{
                    name: Some("logger".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }]);
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: None,
                        source_name: Some("a".to_string()),
                        target_name: Some("b".to_string()),
                        target: None,
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("c".to_string()),
                        target_name: Some("d".to_string()),
                        target: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef {name: "z".to_string()})),
                        source_name: Some("e".to_string()),
                        target_name: Some("f".to_string()),
                        target: Some(fdecl::Ref::Collection(fdecl::CollectionRef {name: "z".to_string()})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("g".to_string()),
                        target_name: Some("h".to_string()),
                        target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("i".to_string()),
                        target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                        target_name: Some("j".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("k".to_string()),
                        target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                        target_name: Some("l".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("m".to_string()),
                        target_name: Some("n".to_string()),
                        target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeService", "source"),
                Error::missing_field("ExposeService", "target"),
                Error::invalid_field("ExposeProtocol", "source"),
                Error::invalid_field("ExposeProtocol", "target"),
                Error::invalid_field("ExposeDirectory", "source"),
                Error::invalid_field("ExposeDirectory", "target"),
                Error::invalid_field("ExposeDirectory", "source"),
                Error::invalid_field("ExposeDirectory", "target"),
                Error::invalid_field("ExposeRunner", "source"),
                Error::invalid_field("ExposeRunner", "target"),
                Error::invalid_field("ExposeResolver", "source"),
                Error::invalid_field("ExposeResolver", "target"),
                Error::invalid_field("ExposeDirectory", "target"),
            ])),
        },
        test_validate_exposes_invalid_source_collection => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![fdecl::Collection{
                    name: Some("col".to_string()),
                    durability: Some(fdecl::Durability::Transient),
                    allowed_offers: None,
                    allow_long_names: None,
                    ..fdecl::Collection::EMPTY
                }]);
                decl.exposes = Some(vec![
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("a".to_string()),
                        target_name: Some("a".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef {name: "col".to_string()})),
                        source_name: Some("b".to_string()),
                        target_name: Some("b".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef {name: "col".to_string()})),
                        source_name: Some("c".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("c".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef {name: "col".to_string()})),
                        source_name: Some("d".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("d".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ExposeProtocol", "source"),
                Error::invalid_field("ExposeDirectory", "source"),
                Error::invalid_field("ExposeRunner", "source"),
                Error::invalid_field("ExposeResolver", "source"),
            ])),
        },
        test_validate_exposes_sources_collection => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![
                    fdecl::Collection {
                        name: Some("col".to_string()),
                        durability: Some(fdecl::Durability::Transient),
                        allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                        allow_long_names: None,
                        ..fdecl::Collection::EMPTY
                    }
                ]);
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("a".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("a".to_string()),
                        ..fdecl::ExposeService::EMPTY
                    })
                ]);
                decl
            },
            result = Ok(()),
        },
        test_validate_exposes_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(1025))),
                        target_name: Some(format!("{}", "b".repeat(1025))),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("a".repeat(101)),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("b".repeat(101)),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("a".repeat(101)),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("b".repeat(101)),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ExposeService", "source.child.name"),
                Error::field_too_long("ExposeService", "source_name"),
                Error::field_too_long("ExposeService", "target_name"),
                Error::field_too_long("ExposeProtocol", "source.child.name"),
                Error::field_too_long("ExposeProtocol", "source_name"),
                Error::field_too_long("ExposeProtocol", "target_name"),
                Error::field_too_long("ExposeDirectory", "source.child.name"),
                Error::field_too_long("ExposeDirectory", "source_name"),
                Error::field_too_long("ExposeDirectory", "target_name"),
                Error::field_too_long("ExposeRunner", "source.child.name"),
                Error::field_too_long("ExposeRunner", "source_name"),
                Error::field_too_long("ExposeRunner", "target_name"),
                Error::field_too_long("ExposeResolver", "source.child.name"),
                Error::field_too_long("ExposeResolver", "source_name"),
                Error::field_too_long("ExposeResolver", "target_name"),
            ])),
        },
        test_validate_exposes_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("data".to_string()),
                        target_name: Some("data".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("elf".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("ExposeService", "source", "netstack"),
                Error::invalid_child("ExposeProtocol", "source", "netstack"),
                Error::invalid_child("ExposeDirectory", "source", "netstack"),
                Error::invalid_child("ExposeRunner", "source", "netstack"),
                Error::invalid_child("ExposeResolver", "source", "netstack"),
            ])),
        },
        test_validate_exposes_invalid_source_capability => {
            input = {
                fdecl::Component {
                    exposes: Some(vec![
                        fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                            source: Some(fdecl::Ref::Capability(fdecl::CapabilityRef {
                                name: "this-storage-doesnt-exist".to_string(),
                            })),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                            ..fdecl::ExposeProtocol::EMPTY
                        }),
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("ExposeProtocol", "source", "this-storage-doesnt-exist"),
            ])),
        },
        test_validate_exposes_duplicate_target => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("netstack".to_string()),
                        target_name: Some("fuchsia.net.Stack".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("netstack2".to_string()),
                        target_name: Some("fuchsia.net.Stack".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("fonts".to_string()),
                        target_name: Some("fuchsia.fonts.Provider".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("fonts2".to_string()),
                        target_name: Some("fuchsia.fonts.Provider".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("assets".to_string()),
                        target_name: Some("stuff".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        rights: None,
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("assets2".to_string()),
                        target_name: Some("stuff".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        rights: None,
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("elf".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("elf".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl.capabilities = Some(vec![
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("netstack".to_string()),
                        source_path: Some("/path".to_string()),
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("netstack2".to_string()),
                        source_path: Some("/path".to_string()),
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("fonts".to_string()),
                        source_path: Some("/path".to_string()),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("fonts2".to_string()),
                        source_path: Some("/path".to_string()),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("assets".to_string()),
                        source_path: Some("/path".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("assets2".to_string()),
                        source_path: Some("/path".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Runner(fdecl::Runner {
                        name: Some("source_elf".to_string()),
                        source_path: Some("/path".to_string()),
                        ..fdecl::Runner::EMPTY
                    }),
                    fdecl::Capability::Resolver(fdecl::Resolver {
                        name: Some("source_pkg".to_string()),
                        source_path: Some("/path".to_string()),
                        ..fdecl::Resolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                // Duplicate services are allowed.
                Error::duplicate_field("ExposeProtocol", "target_name",
                                    "fuchsia.fonts.Provider"),
                Error::duplicate_field("ExposeDirectory", "target_name",
                                    "stuff"),
                Error::duplicate_field("ExposeRunner", "target_name",
                                    "elf"),
                Error::duplicate_field("ExposeResolver", "target_name", "pkg"),
            ])),
        },
        // TODO: Add analogous test for offer
        test_validate_exposes_invalid_capability_from_self => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    fdecl::Expose::Service(fdecl::ExposeService {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("fuchsia.netstack.Netstack".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("foo".to_string()),
                        ..fdecl::ExposeService::EMPTY
                    }),
                    fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("fuchsia.netstack.Netstack".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("bar".to_string()),
                        ..fdecl::ExposeProtocol::EMPTY
                    }),
                    fdecl::Expose::Directory(fdecl::ExposeDirectory {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("dir".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("assets".to_string()),
                        rights: None,
                        subdir: None,
                        ..fdecl::ExposeDirectory::EMPTY
                    }),
                    fdecl::Expose::Runner(fdecl::ExposeRunner {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("elf".to_string()),
                        ..fdecl::ExposeRunner::EMPTY
                    }),
                    fdecl::Expose::Resolver(fdecl::ExposeResolver {
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::ExposeResolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("ExposeService", "source", "fuchsia.netstack.Netstack"),
                Error::invalid_capability("ExposeProtocol", "source", "fuchsia.netstack.Netstack"),
                Error::invalid_capability("ExposeDirectory", "source", "dir"),
                Error::invalid_capability("ExposeRunner", "source", "source_elf"),
                Error::invalid_capability("ExposeResolver", "source", "source_pkg"),
            ])),
        },

        // offers
        test_validate_offers_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        dependency_type: None,
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        rights: None,
                        subdir: None,
                        dependency_type: None,
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source_name: None,
                        source: None,
                        target: None,
                        target_name: None,
                        ..fdecl::OfferStorage::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        filter: None,
                        ..fdecl::OfferEvent::EMPTY
                    })
                ]);
                decl
            },
            // TODO(dgonyeo): we need to handle the availability being unset until we've soft
            // migrated all manifests
            result = Err(ErrorList::new(vec![
                Error::missing_field("OfferService", "source"),
                Error::missing_field("OfferService", "source_name"),
                Error::missing_field("OfferService", "target"),
                Error::missing_field("OfferService", "target_name"),
                //Error::missing_field("OfferService", "availability"),
                Error::missing_field("OfferProtocol", "source"),
                Error::missing_field("OfferProtocol", "source_name"),
                Error::missing_field("OfferProtocol", "target"),
                Error::missing_field("OfferProtocol", "target_name"),
                Error::missing_field("OfferProtocol", "dependency_type"),
                //Error::missing_field("OfferProtocol", "availability"),
                Error::missing_field("OfferDirectory", "source"),
                Error::missing_field("OfferDirectory", "source_name"),
                Error::missing_field("OfferDirectory", "target"),
                Error::missing_field("OfferDirectory", "target_name"),
                Error::missing_field("OfferDirectory", "dependency_type"),
                //Error::missing_field("OfferDirectory", "availability"),
                Error::missing_field("OfferStorage", "source_name"),
                Error::missing_field("OfferStorage", "source"),
                Error::missing_field("OfferStorage", "target"),
                //Error::missing_field("OfferStorage", "availability"),
                Error::missing_field("OfferRunner", "source"),
                Error::missing_field("OfferRunner", "source_name"),
                Error::missing_field("OfferRunner", "target"),
                Error::missing_field("OfferRunner", "target_name"),
                //Error::missing_field("OfferRunner", "availability"),
                Error::missing_field("OfferEvent", "source_name"),
                Error::missing_field("OfferEvent", "source"),
                Error::missing_field("OfferEvent", "target"),
                Error::missing_field("OfferEvent", "target_name"),
                //Error::missing_field("OfferEvent", "availability"),
            ])),
        },
        test_validate_offers_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("a".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef {
                            name: "b".repeat(101),
                        }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("a".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef {
                            name: "b".repeat(101),
                        }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("a".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef {
                            name: "b".repeat(101),
                        }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source_name: Some("data".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "b".repeat(101),
                                collection: None,
                            }
                        )),
                        target_name: Some("data".to_string()),
                        ..fdecl::OfferStorage::EMPTY
                    }),
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source_name: Some("data".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target: Some(fdecl::Ref::Collection(
                            fdecl::CollectionRef { name: "b".repeat(101) }
                        )),
                        target_name: Some("data".to_string()),
                        ..fdecl::OfferStorage::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("b".repeat(101)),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef {
                            name: "c".repeat(101),
                        }
                        )),
                        target_name: Some("d".repeat(101)),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("b".repeat(101)),
                        target: Some(fdecl::Ref::Collection(
                            fdecl::CollectionRef {
                                name: "c".repeat(101),
                            }
                        )),
                        target_name: Some("d".repeat(101)),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "a".repeat(101),
                            collection: None
                        })),
                        target_name: Some(format!("{}", "a".repeat(101))),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        ..fdecl::OfferEvent::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("OfferService", "source.child.name"),
                Error::field_too_long("OfferService", "source_name"),
                Error::field_too_long("OfferService", "target.child.name"),
                Error::field_too_long("OfferService", "target_name"),
                Error::field_too_long("OfferService", "target.collection.name"),
                Error::field_too_long("OfferService", "target_name"),
                Error::field_too_long("OfferProtocol", "source.child.name"),
                Error::field_too_long("OfferProtocol", "source_name"),
                Error::field_too_long("OfferProtocol", "target.child.name"),
                Error::field_too_long("OfferProtocol", "target_name"),
                Error::field_too_long("OfferProtocol", "target.collection.name"),
                Error::field_too_long("OfferProtocol", "target_name"),
                Error::field_too_long("OfferDirectory", "source.child.name"),
                Error::field_too_long("OfferDirectory", "source_name"),
                Error::field_too_long("OfferDirectory", "target.child.name"),
                Error::field_too_long("OfferDirectory", "target_name"),
                Error::field_too_long("OfferDirectory", "target.collection.name"),
                Error::field_too_long("OfferDirectory", "target_name"),
                Error::field_too_long("OfferStorage", "target.child.name"),
                Error::field_too_long("OfferStorage", "target.collection.name"),
                Error::field_too_long("OfferRunner", "source.child.name"),
                Error::field_too_long("OfferRunner", "source_name"),
                Error::field_too_long("OfferRunner", "target.collection.name"),
                Error::field_too_long("OfferRunner", "target_name"),
                Error::field_too_long("OfferResolver", "source.child.name"),
                Error::field_too_long("OfferResolver", "source_name"),
                Error::field_too_long("OfferResolver", "target.collection.name"),
                Error::field_too_long("OfferResolver", "target_name"),
                Error::field_too_long("OfferEvent", "source_name"),
                Error::field_too_long("OfferEvent", "target.child.name"),
                Error::field_too_long("OfferEvent", "target_name"),
            ])),
        },
        test_validate_offers_extraneous => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("assets".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("assets".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source_name: Some("data".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{ })),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("data".to_string()),
                        ..fdecl::OfferStorage::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("elf".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                ]);
                decl.capabilities = Some(vec![
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("fuchsia.logger.Log".to_string()),
                        source_path: Some("/svc/logger".to_string()),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("assets".to_string()),
                        source_path: Some("/data/assets".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("OfferService", "source.child.collection"),
                Error::extraneous_field("OfferService", "target.child.collection"),
                Error::extraneous_field("OfferProtocol", "source.child.collection"),
                Error::extraneous_field("OfferProtocol", "target.child.collection"),
                Error::extraneous_field("OfferDirectory", "source.child.collection"),
                Error::extraneous_field("OfferDirectory", "target.child.collection"),
                Error::extraneous_field("OfferStorage", "target.child.collection"),
                Error::extraneous_field("OfferRunner", "source.child.collection"),
                Error::extraneous_field("OfferRunner", "target.child.collection"),
                Error::extraneous_field("OfferResolver", "source.child.collection"),
                Error::extraneous_field("OfferResolver", "target.child.collection"),
            ])),
        },
        test_validate_offers_invalid_filtered_service_fields => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        source_instance_filter: Some(vec![]),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log1".to_string()),
                        renamed_instances: Some(vec![fdecl::NameMapping{source_name: "a".to_string(), target_name: "b".to_string()}, fdecl::NameMapping{source_name: "c".to_string(), target_name: "b".to_string()}]),
                        ..fdecl::OfferService::EMPTY
                    })
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("OfferService", "source_instance_filter"),
                Error::invalid_field("OfferService", "renamed_instances"),
            ])),
        },
        test_validate_offers_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("/".to_string()),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("/".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("/".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: Some("/foo".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("elf!".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("pkg!".to_string()),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("/path".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("/path".to_string()),
                        filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                        ..fdecl::OfferEvent::EMPTY
                    })
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("OfferService", "source.child.name"),
                Error::invalid_field("OfferService", "source_name"),
                Error::invalid_field("OfferService", "target.child.name"),
                Error::invalid_field("OfferService", "target_name"),
                Error::invalid_field("OfferProtocol", "source.child.name"),
                Error::invalid_field("OfferProtocol", "source_name"),
                Error::invalid_field("OfferProtocol", "target.child.name"),
                Error::invalid_field("OfferProtocol", "target_name"),
                Error::invalid_field("OfferDirectory", "source.child.name"),
                Error::invalid_field("OfferDirectory", "source_name"),
                Error::invalid_field("OfferDirectory", "target.child.name"),
                Error::invalid_field("OfferDirectory", "target_name"),
                Error::invalid_field("OfferDirectory", "subdir"),
                Error::invalid_field("OfferRunner", "source.child.name"),
                Error::invalid_field("OfferRunner", "source_name"),
                Error::invalid_field("OfferRunner", "target.child.name"),
                Error::invalid_field("OfferRunner", "target_name"),
                Error::invalid_field("OfferResolver", "source.child.name"),
                Error::invalid_field("OfferResolver", "source_name"),
                Error::invalid_field("OfferResolver", "target.child.name"),
                Error::invalid_field("OfferResolver", "target_name"),
                Error::invalid_field("OfferEvent", "source_name"),
                Error::invalid_field("OfferEvent", "target.child.name"),
                Error::invalid_field("OfferEvent", "target_name"),
            ])),
        },
        test_validate_offers_target_equals_source => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("logger".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("logger".to_string()),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("legacy_logger".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("weak_legacy_logger".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("legacy_logger".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("strong_legacy_logger".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("assets".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("assets".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("web".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("web".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                ]);
                decl.children = Some(vec![fdecl::Child{
                    name: Some("logger".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::offer_target_equals_source("OfferService", "logger"),
                // Only the DependencyType::Strong offer-target-equals-source
                // should result in an error.
                Error::offer_target_equals_source("OfferProtocol", "logger"),
                Error::offer_target_equals_source("OfferDirectory", "logger"),
                Error::offer_target_equals_source("OfferRunner", "logger"),
                Error::offer_target_equals_source("OfferResolver", "logger"),
            ])),
        },
        test_validate_offers_storage_target_equals_source => {
            input = fdecl::Component {
                offers: Some(vec![
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source_name: Some("data".to_string()),
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef { })),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("data".to_string()),
                        ..fdecl::OfferStorage::EMPTY
                    })
                ]),
                capabilities: Some(vec![
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("data".to_string()),
                        backing_dir: Some("minfs".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                ]),
                children: Some(vec![
                    fdecl::Child {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]),
                ..new_component_decl()
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle("{{child logger -> child logger}}".to_string()),
            ])),
        },
        test_validate_offers_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("assets".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("assets".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                ]);
                decl.capabilities = Some(vec![
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("memfs".to_string()),
                        backing_dir: Some("memfs".to_string()),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl.collections = Some(vec![
                    fdecl::Collection {
                        name: Some("modular".to_string()),
                        durability: Some(fdecl::Durability::Transient),
                        environment: None,
                        allowed_offers: Some(fdecl::AllowedOffers::StaticAndDynamic),
                        allow_long_names: None,
                        ..fdecl::Collection::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("Storage", "source", "logger"),
                Error::invalid_child("OfferService", "source", "logger"),
                Error::invalid_child("OfferProtocol", "source", "logger"),
                Error::invalid_child("OfferDirectory", "source", "logger"),
            ])),
        },
        test_validate_offers_invalid_source_capability => {
            input = {
                fdecl::Component {
                    offers: Some(vec![
                        fdecl::Offer::Protocol(fdecl::OfferProtocol {
                            source: Some(fdecl::Ref::Capability(fdecl::CapabilityRef {
                                name: "this-storage-doesnt-exist".to_string(),
                            })),
                            source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                            )),
                            target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            ..fdecl::OfferProtocol::EMPTY
                        }),
                    ]),
                    ..new_component_decl()
                }
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("OfferProtocol", "source", "this-storage-doesnt-exist"),
                Error::invalid_child("OfferProtocol", "target", "netstack"),
            ])),
        },
        test_validate_offers_target => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("logger".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        source_instance_filter: Some(vec!["instance_0".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("logger".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        source_instance_filter: Some(vec!["instance_1".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("assets".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("assets".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("assets".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("assets".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("stopped".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        target_name: Some("started".to_string()),
                        filter: None,
                        ..fdecl::OfferEvent::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        source_name: Some("started_on_x".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        target_name: Some("started".to_string()),
                        filter: None,
                        ..fdecl::OfferEvent::EMPTY
                    }),
                ]);
                decl.children = Some(vec![
                    fdecl::Child{
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Eager),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl.collections = Some(vec![
                    fdecl::Collection{
                        name: Some("modular".to_string()),
                        durability: Some(fdecl::Durability::Transient),
                        environment: None,
                        allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                        allow_long_names: None,
                        ..fdecl::Collection::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                // Duplicate services are allowed.
                Error::duplicate_field("OfferProtocol", "target_name", "fuchsia.logger.LegacyLog"),
                Error::duplicate_field("OfferDirectory", "target_name", "assets"),
                Error::duplicate_field("OfferRunner", "target_name", "duplicated"),
                Error::duplicate_field("OfferResolver", "target_name", "duplicated"),
                Error::duplicate_field("OfferEvent", "target_name", "started"),
            ])),
        },
        test_validate_offers_target_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("logger".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("logger".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("legacy_logger".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("legacy_logger".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("assets".to_string()),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        }
                        )),
                        target_name: Some("data".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("assets".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("data".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Weak),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source_name: Some("data".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("data".to_string()),
                        ..fdecl::OfferStorage::EMPTY
                    }),
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source_name: Some("data".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        target: Some(fdecl::Ref::Collection(
                            fdecl::CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("data".to_string()),
                        ..fdecl::OfferStorage::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("elf".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("elf".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("pkg".to_string()),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("started".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        filter: None,
                        ..fdecl::OfferEvent::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source_name: Some("started".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        target_name: Some("started".to_string()),
                        target: Some(fdecl::Ref::Collection(
                        fdecl::CollectionRef { name: "modular".to_string(), }
                        )),
                        filter: None,
                        ..fdecl::OfferEvent::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("OfferService", "target", "netstack"),
                Error::invalid_collection("OfferService", "target", "modular"),
                Error::invalid_child("OfferProtocol", "target", "netstack"),
                Error::invalid_collection("OfferProtocol", "target", "modular"),
                Error::invalid_child("OfferDirectory", "target", "netstack"),
                Error::invalid_collection("OfferDirectory", "target", "modular"),
                Error::invalid_child("OfferStorage", "target", "netstack"),
                Error::invalid_collection("OfferStorage", "target", "modular"),
                Error::invalid_child("OfferRunner", "target", "netstack"),
                Error::invalid_collection("OfferRunner", "target", "modular"),
                Error::invalid_child("OfferResolver", "target", "netstack"),
                Error::invalid_collection("OfferResolver", "target", "modular"),
                Error::invalid_child("OfferEvent", "target", "netstack"),
                Error::invalid_collection("OfferEvent", "target", "modular"),
            ])),
        },
        test_validate_offers_invalid_source_collection => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![
                    fdecl::Collection {
                        name: Some("col".to_string()),
                        durability: Some(fdecl::Durability::Transient),
                        allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                        allow_long_names: None,
                        ..fdecl::Collection::EMPTY
                    }
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("child".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        ..fdecl::Child::EMPTY
                    }
                ]);
                decl.offers = Some(vec![
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("a".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                        target_name: Some("a".to_string()),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    }),
                    fdecl::Offer::Directory(fdecl::OfferDirectory {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("b".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                        target_name: Some("b".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferDirectory::EMPTY
                    }),
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("c".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                        target_name: Some("c".to_string()),
                        ..fdecl::OfferStorage::EMPTY
                    }),
                    fdecl::Offer::Runner(fdecl::OfferRunner {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("d".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                        target_name: Some("d".to_string()),
                        ..fdecl::OfferRunner::EMPTY
                    }),
                    fdecl::Offer::Resolver(fdecl::OfferResolver {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("e".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                        target_name: Some("e".to_string()),
                        ..fdecl::OfferResolver::EMPTY
                    }),
                    fdecl::Offer::Event(fdecl::OfferEvent {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("f".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                        target_name: Some("f".to_string()),
                        filter: None,
                        ..fdecl::OfferEvent::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("OfferProtocol", "source"),
                Error::invalid_field("OfferDirectory", "source"),
                Error::invalid_field("OfferStorage", "source"),
                Error::invalid_field("OfferRunner", "source"),
                Error::invalid_field("OfferResolver", "source"),
                Error::invalid_field("OfferEvent", "source"),
            ])),
        },
        test_validate_offers_source_collection => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![
                    fdecl::Collection {
                        name: Some("col".to_string()),
                        durability: Some(fdecl::Durability::Transient),
                        allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                        allow_long_names: None,
                        ..fdecl::Collection::EMPTY
                    }
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("child".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        ..fdecl::Child::EMPTY
                    }
                ]);
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef { name: "col".to_string() })),
                        source_name: Some("a".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                        target_name: Some("a".to_string()),
                        ..fdecl::OfferService::EMPTY
                    })
                ]);
                decl
            },
            result = Ok(()),
        },
        test_validate_offers_event_from_realm => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(
                    vec![
                        fdecl::Ref::Self_(fdecl::SelfRef {}),
                        fdecl::Ref::Child(fdecl::ChildRef {name: "netstack".to_string(), collection: None }),
                        fdecl::Ref::Collection(fdecl::CollectionRef {name: "modular".to_string() }),
                    ]
                    .into_iter()
                    .enumerate()
                    .map(|(i, source)| {
                        fdecl::Offer::Event(fdecl::OfferEvent {
                            source: Some(source),
                            source_name: Some("started".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some(format!("started_{}", i)),

                            filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                            ..fdecl::OfferEvent::EMPTY
                        })
                    })
                    .collect());
                decl.children = Some(vec![
                    fdecl::Child{
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Eager),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl.collections = Some(vec![
                    fdecl::Collection {
                        name: Some("modular".to_string()),
                        durability: Some(fdecl::Durability::Transient),
                        environment: None,
                        allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                        allow_long_names: None,
                        ..fdecl::Collection::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("OfferEvent", "source"),
                Error::invalid_field("OfferEvent", "source"),
                Error::invalid_field("OfferEvent", "source"),
            ])),
        },
        test_validate_offers_long_dependency_cycle => {
            input = {
                let mut decl = new_component_decl();
                let dependencies = vec![
                    ("d", "b"),
                    ("a", "b"),
                    ("b", "c"),
                    ("b", "d"),
                    ("c", "a"),
                ];
                let offers = dependencies.into_iter().map(|(from,to)|
                    fdecl::Offer::Protocol(fdecl::OfferProtocol {
                        source: Some(fdecl::Ref::Child(
                        fdecl::ChildRef { name: from.to_string(), collection: None },
                        )),
                        source_name: Some(format!("thing_{}", from)),
                        target: Some(fdecl::Ref::Child(
                        fdecl::ChildRef { name: to.to_string(), collection: None },
                        )),
                        target_name: Some(format!("thing_{}", from)),
                        dependency_type: Some(fdecl::DependencyType::Strong),
                        ..fdecl::OfferProtocol::EMPTY
                    })).collect();
                let children = ["a", "b", "c", "d"].iter().map(|name| {
                    fdecl::Child {
                        name: Some(name.to_string()),
                        url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    }
                }).collect();
                decl.offers = Some(offers);
                decl.children = Some(children);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle(directed_graph::Error::CyclesDetected([vec!["child a", "child b", "child c", "child a"], vec!["child b", "child d", "child b"]].iter().cloned().collect()).format_cycle()),
            ])),
        },
        test_validate_offers_not_required_invalid_source_service => {
            input = {
                let mut decl = generate_offer_invalid_source_and_availability_decl(
                    |source, availability, target_name|
                        fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(source),
                            source_name: Some("fuchsia.examples.Echo".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                name: "sink".to_string(),
                                collection: None,
                            })),
                            target_name: Some(target_name.into()),
                            availability: Some(availability),
                            ..fdecl::OfferService::EMPTY
                        })
                );
                decl.capabilities = Some(vec![
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("fuchsia.examples.Echo".to_string()),
                        source_path: Some("/svc/fuchsia.examples.Echo".to_string()),
                        ..fdecl::Service::EMPTY
                    }),
                ]);
                decl
            },
            result = {
                Err(ErrorList::new(vec![
                    Error::availability_must_be_optional(
                        "OfferService",
                        "availability",
                        Some(&"fuchsia.examples.Echo".to_string()),
                    ),
                    Error::availability_must_be_optional(
                        "OfferService",
                        "availability",
                        Some(&"fuchsia.examples.Echo".to_string()),
                    ),
                ]))
            },
        },
        test_validate_offers_not_required_invalid_source_protocol => {
            input = {
                let mut decl = generate_offer_invalid_source_and_availability_decl(
                    |source, availability, target_name|
                        fdecl::Offer::Protocol(fdecl::OfferProtocol {
                            source: Some(source),
                            source_name: Some("fuchsia.examples.Echo".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                name: "sink".to_string(),
                                collection: None,
                            })),
                            target_name: Some(target_name.into()),
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            availability: Some(availability),
                            ..fdecl::OfferProtocol::EMPTY
                        })
                );
                decl.capabilities = Some(vec![
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("fuchsia.examples.Echo".to_string()),
                        source_path: Some("/svc/fuchsia.examples.Echo".to_string()),
                        ..fdecl::Protocol::EMPTY
                    }),
                ]);
                decl
            },
            result = {
                Err(ErrorList::new(vec![
                    Error::availability_must_be_optional(
                        "OfferProtocol",
                        "availability",
                        Some(&"fuchsia.examples.Echo".to_string()),
                    ),
                    Error::availability_must_be_optional(
                        "OfferProtocol",
                        "availability",
                        Some(&"fuchsia.examples.Echo".to_string()),
                    ),
                ]))
            },
        },
        test_validate_offers_not_required_invalid_source_directory => {
            input = {
                let mut decl = generate_offer_invalid_source_and_availability_decl(
                    |source, availability, target_name|
                        fdecl::Offer::Directory(fdecl::OfferDirectory {
                            source: Some(source),
                            source_name: Some("assets".to_string()),
                            target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                name: "sink".to_string(),
                                collection: None,
                            })),
                            target_name: Some(target_name.into()),
                            rights: Some(fio::Operations::CONNECT),
                            subdir: None,
                            dependency_type: Some(fdecl::DependencyType::Weak),
                            availability: Some(availability),
                            ..fdecl::OfferDirectory::EMPTY
                        })
                );
                decl.capabilities = Some(vec![
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("assets".to_string()),
                        source_path: Some("/assets".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                ]);
                decl
            },
            result = {
                Err(ErrorList::new(vec![
                    Error::availability_must_be_optional(
                        "OfferDirectory",
                        "availability",
                        Some(&"assets".to_string()),
                    ),
                    Error::availability_must_be_optional(
                        "OfferDirectory",
                        "availability",
                        Some(&"assets".to_string()),
                    ),
                ]))
            },
        },
        test_validate_offers_not_required_invalid_source_storage => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("sink".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/sink#meta/sink.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl.capabilities = Some(vec![
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("data".to_string()),
                        source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                        backing_dir: Some("minfs".to_string()),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                ]);
                let new_offer = |source: fdecl::Ref, availability: fdecl::Availability,
                                        target_name: &str|
                {
                    fdecl::Offer::Storage(fdecl::OfferStorage {
                        source: Some(source),
                        source_name: Some("data".to_string()),
                        target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "sink".to_string(),
                            collection: None,
                        })),
                        target_name: Some(target_name.into()),
                        availability: Some(availability),
                        ..fdecl::OfferStorage::EMPTY
                    })
                };
                decl.offers = Some(vec![
                    // These offers are fine, offers with a source of parent or void can be
                    // optional.
                    new_offer(
                        fdecl::Ref::Parent(fdecl::ParentRef {}),
                        fdecl::Availability::Required,
                        "data0",
                    ),
                    new_offer(
                        fdecl::Ref::Parent(fdecl::ParentRef {}),
                        fdecl::Availability::Optional,
                        "data1",
                    ),
                    new_offer(
                        fdecl::Ref::Parent(fdecl::ParentRef {}),
                        fdecl::Availability::SameAsTarget,
                        "data2",
                    ),
                    new_offer(
                        fdecl::Ref::VoidType(fdecl::VoidRef {}),
                        fdecl::Availability::Optional,
                        "data3",
                    ),
                    // These offers are not fine, offers with a source other than parent or void
                    // must be required.
                    new_offer(
                        fdecl::Ref::Self_(fdecl::SelfRef {}),
                        fdecl::Availability::Optional,
                        "data4",
                    ),
                    new_offer(
                        fdecl::Ref::Self_(fdecl::SelfRef {}),
                        fdecl::Availability::SameAsTarget,
                        "data5",
                    ),
                    // These offers are also not fine, offers with a source of void must be optional
                    new_offer(
                        fdecl::Ref::VoidType(fdecl::VoidRef {}),
                        fdecl::Availability::Required,
                        "data6",
                    ),
                    new_offer(
                        fdecl::Ref::VoidType(fdecl::VoidRef {}),
                        fdecl::Availability::SameAsTarget,
                        "data7",
                    ),
                ]);
                decl
            },
            result = {
                Err(ErrorList::new(vec![
                    Error::availability_must_be_optional(
                        "OfferStorage",
                        "availability",
                        Some(&"data".to_string()),
                    ),
                    Error::availability_must_be_optional(
                        "OfferStorage",
                        "availability",
                        Some(&"data".to_string()),
                    ),
                ]))
            },
        },

        test_validate_offers_valid_service_aggregation => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_a".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        source_instance_filter: Some(vec!["default".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_b".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        source_instance_filter: Some(vec!["a_different_default".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    })
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("child_a".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_b".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_c".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl
            },
            result = Ok(()),
        },

        // environments
        test_validate_environment_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: None,
                    extends: None,
                    runners: None,
                    resolvers: None,
                    stop_timeout_ms: None,
                    debug_capabilities: None,
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("Environment", "name"),
                Error::missing_field("Environment", "extends"),
            ])),
        },

        test_validate_environment_no_stop_timeout => {
            input = {  let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("env".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: None,
                    resolvers: None,
                    stop_timeout_ms: None,
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![Error::missing_field("Environment", "stop_timeout_ms")])),
        },

        test_validate_environment_extends_stop_timeout => {
            input = {  let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("env".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::Realm),
                    runners: None,
                    resolvers: None,
                    stop_timeout_ms: None,
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Ok(()),
        },
        test_validate_environment_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("a".repeat(101)),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: Some(vec![
                        fdecl::RunnerRegistration {
                            source_name: Some("a".repeat(101)),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            target_name: Some("a".repeat(101)),
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                    ]),
                    resolvers: Some(vec![
                        fdecl::ResolverRegistration {
                            resolver: Some("a".repeat(101)),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            scheme: Some("a".repeat(101)),
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("Environment", "name"),
                Error::field_too_long("RunnerRegistration", "source_name"),
                Error::field_too_long("RunnerRegistration", "target_name"),
                Error::field_too_long("ResolverRegistration", "resolver"),
                Error::field_too_long("ResolverRegistration", "scheme"),
            ])),
        },
        test_validate_environment_empty_runner_resolver_fields => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("a".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: Some(vec![
                        fdecl::RunnerRegistration {
                            source_name: None,
                            source: None,
                            target_name: None,
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                    ]),
                    resolvers: Some(vec![
                        fdecl::ResolverRegistration {
                            resolver: None,
                            source: None,
                            scheme: None,
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("RunnerRegistration", "source_name"),
                Error::missing_field("RunnerRegistration", "source"),
                Error::missing_field("RunnerRegistration", "target_name"),
                Error::missing_field("ResolverRegistration", "resolver"),
                Error::missing_field("ResolverRegistration", "source"),
                Error::missing_field("ResolverRegistration", "scheme"),
            ])),
        },
        test_validate_environment_invalid_fields => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("a".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: Some(vec![
                        fdecl::RunnerRegistration {
                            source_name: Some("^a".to_string()),
                            source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                            target_name: Some("%a".to_string()),
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                    ]),
                    resolvers: Some(vec![
                        fdecl::ResolverRegistration {
                            resolver: Some("^a".to_string()),
                            source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef{})),
                            scheme: Some("9scheme".to_string()),
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("RunnerRegistration", "source_name"),
                Error::invalid_field("RunnerRegistration", "source"),
                Error::invalid_field("RunnerRegistration", "target_name"),
                Error::invalid_field("ResolverRegistration", "resolver"),
                Error::invalid_field("ResolverRegistration", "source"),
                Error::invalid_field("ResolverRegistration", "scheme"),
            ])),
        },
        test_validate_environment_missing_runner => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("a".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: Some(vec![
                        fdecl::RunnerRegistration {
                            source_name: Some("dart".to_string()),
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                            target_name: Some("dart".to_string()),
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                    ]),
                    resolvers: None,
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_runner("RunnerRegistration", "source_name", "dart"),
            ])),
        },
        test_validate_environment_duplicate_registrations => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("a".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: Some(vec![
                        fdecl::RunnerRegistration {
                            source_name: Some("dart".to_string()),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            target_name: Some("dart".to_string()),
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                        fdecl::RunnerRegistration {
                            source_name: Some("other-dart".to_string()),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            target_name: Some("dart".to_string()),
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                    ]),
                    resolvers: Some(vec![
                        fdecl::ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            scheme: Some("fuchsia-pkg".to_string()),
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                        fdecl::ResolverRegistration {
                            resolver: Some("base_resolver".to_string()),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef{})),
                            scheme: Some("fuchsia-pkg".to_string()),
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("RunnerRegistration", "target_name", "dart"),
                Error::duplicate_field("ResolverRegistration", "scheme", "fuchsia-pkg"),
            ])),
        },
        test_validate_environment_from_missing_child => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("a".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: Some(vec![
                        fdecl::RunnerRegistration {
                            source_name: Some("elf".to_string()),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{
                                name: "missing".to_string(),
                                collection: None,
                            })),
                            target_name: Some("elf".to_string()),
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                    ]),
                    resolvers: Some(vec![
                        fdecl::ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{
                                name: "missing".to_string(),
                                collection: None,
                            })),
                            scheme: Some("fuchsia-pkg".to_string()),
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("RunnerRegistration", "source", "missing"),
                Error::invalid_child("ResolverRegistration", "source", "missing"),
            ])),
        },
        test_validate_environment_runner_child_cycle => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("env".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: Some(vec![
                        fdecl::RunnerRegistration {
                            source_name: Some("elf".to_string()),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{
                                name: "child".to_string(),
                                collection: None,
                            })),
                            target_name: Some("elf".to_string()),
                            ..fdecl::RunnerRegistration::EMPTY
                        },
                    ]),
                    resolvers: None,
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl.children = Some(vec![fdecl::Child {
                    name: Some("child".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    url: Some("fuchsia-pkg://child".to_string()),
                    environment: Some("env".to_string()),
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle(
                    directed_graph::Error::CyclesDetected([vec!["child child", "environment env", "child child"]].iter().cloned().collect()).format_cycle()
                ),
            ])),
        },
        test_validate_environment_resolver_child_cycle => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("env".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: None,
                    resolvers: Some(vec![
                        fdecl::ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{
                                name: "child".to_string(),
                                collection: None,
                            })),
                            scheme: Some("fuchsia-pkg".to_string()),
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl.children = Some(vec![fdecl::Child {
                    name: Some("child".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    url: Some("fuchsia-pkg://child".to_string()),
                    environment: Some("env".to_string()),
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle(
                    directed_graph::Error::CyclesDetected([vec!["child child", "environment env", "child child"]].iter().cloned().collect()).format_cycle()
                ),
            ])),
        },
        test_validate_environment_resolver_multiple_children_cycle => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![fdecl::Environment {
                    name: Some("env".to_string()),
                    extends: Some(fdecl::EnvironmentExtends::None),
                    runners: None,
                    resolvers: Some(vec![
                        fdecl::ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(fdecl::Ref::Child(fdecl::ChildRef{
                                name: "a".to_string(),
                                collection: None,
                            })),
                            scheme: Some("fuchsia-pkg".to_string()),
                            ..fdecl::ResolverRegistration::EMPTY
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                    ..fdecl::Environment::EMPTY
                }]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("a".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        url: Some("fuchsia-pkg://child-a".to_string()),
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("b".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        url: Some("fuchsia-pkg://child-b".to_string()),
                        environment: Some("env".to_string()),
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl.offers = Some(vec![fdecl::Offer::Service(fdecl::OfferService {
                    source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "b".to_string(),
                        collection: None,
                    })),
                    source_name: Some("thing".to_string()),
                    target: Some(fdecl::Ref::Child(
                    fdecl::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }
                    )),
                    target_name: Some("thing".to_string()),
                    ..fdecl::OfferService::EMPTY
                })]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle(
                    directed_graph::Error::CyclesDetected([vec!["child a", "environment env", "child b", "child a"]].iter().cloned().collect()).format_cycle()
                ),
            ])),
        },
        test_validate_environment_debug_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![
                    fdecl::Environment {
                        name: Some("a".to_string()),
                        extends: Some(fdecl::EnvironmentExtends::None),
                        stop_timeout_ms: Some(2),
                        debug_capabilities:Some(vec![
                            fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                source: None,
                                source_name: None,
                                target_name: None,
                                ..fdecl::DebugProtocolRegistration::EMPTY
                            }),
                    ]),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("DebugProtocolRegistration", "source"),
                Error::missing_field("DebugProtocolRegistration", "source_name"),
                Error::missing_field("DebugProtocolRegistration", "target_name"),
            ])),
        },
        test_validate_environment_debug_log_identifier => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![
                    fdecl::Environment {
                        name: Some("a".to_string()),
                        extends: Some(fdecl::EnvironmentExtends::None),
                        stop_timeout_ms: Some(2),
                        debug_capabilities:Some(vec![
                            fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                    name: "a".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                ..fdecl::DebugProtocolRegistration::EMPTY
                            }),
                            fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                                source_name: Some("a".to_string()),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                ..fdecl::DebugProtocolRegistration::EMPTY
                            }),
                    ]),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("DebugProtocolRegistration", "source.child.name"),
                Error::field_too_long("DebugProtocolRegistration", "source_name"),
                Error::field_too_long("DebugProtocolRegistration", "target_name"),
                Error::field_too_long("DebugProtocolRegistration", "target_name"),
            ])),
        },
        test_validate_environment_debug_log_extraneous => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![
                    fdecl::Environment {
                        name: Some("a".to_string()),
                        extends: Some(fdecl::EnvironmentExtends::None),
                        stop_timeout_ms: Some(2),
                        debug_capabilities:Some(vec![
                            fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("fuchsia.logger.Log".to_string()),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                ..fdecl::DebugProtocolRegistration::EMPTY
                            }),
                    ]),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("DebugProtocolRegistration", "source.child.collection"),
            ])),
        },
        test_validate_environment_debug_log_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![
                    fdecl::Environment {
                        name: Some("a".to_string()),
                        extends: Some(fdecl::EnvironmentExtends::None),
                        stop_timeout_ms: Some(2),
                        debug_capabilities:Some(vec![
                            fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("foo/".to_string()),
                                target_name: Some("/".to_string()),
                                ..fdecl::DebugProtocolRegistration::EMPTY
                            }),
                    ]),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("DebugProtocolRegistration", "source.child.name"),
                Error::invalid_field("DebugProtocolRegistration", "source_name"),
                Error::invalid_field("DebugProtocolRegistration", "target_name"),
            ])),
        },
        test_validate_environment_debug_log_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![
                    fdecl::Environment {
                        name: Some("a".to_string()),
                        extends: Some(fdecl::EnvironmentExtends::None),
                        stop_timeout_ms: Some(2),
                        debug_capabilities:Some(vec![
                            fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                ..fdecl::DebugProtocolRegistration::EMPTY
                            }),
                    ]),
                    ..fdecl::Environment::EMPTY
                }]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("DebugProtocolRegistration", "source", "logger"),

            ])),
        },
        test_validate_environment_debug_source_capability => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![
                    fdecl::Environment {
                        name: Some("a".to_string()),
                        extends: Some(fdecl::EnvironmentExtends::None),
                        stop_timeout_ms: Some(2),
                        debug_capabilities:Some(vec![
                            fdecl::DebugRegistration::Protocol(fdecl::DebugProtocolRegistration {
                                source: Some(fdecl::Ref::Capability(fdecl::CapabilityRef {
                                    name: "storage".to_string(),
                                })),
                                source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                ..fdecl::DebugProtocolRegistration::EMPTY
                            }),
                    ]),
                    ..fdecl::Environment::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("DebugProtocolRegistration", "source"),
            ])),
        },

        // children
        test_validate_children_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![fdecl::Child{
                    name: None,
                    url: None,
                    startup: None,
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("Child", "name"),
                Error::missing_field("Child", "url"),
                Error::missing_field("Child", "startup"),
                // `on_terminate` is allowed to be None
            ])),
        },
        test_validate_children_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![fdecl::Child{
                    name: Some("^bad".to_string()),
                    url: Some("bad-scheme&://blah".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("Child", "name"),
                Error::invalid_url("Child", "url", "\"bad-scheme&://blah\": Invalid scheme"),
            ])),
        },
        test_validate_children_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![fdecl::Child{
                    name: Some("a".repeat(1025)),
                    url: Some(format!("fuchsia-pkg://{}", "a".repeat(4083))),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: Some("a".repeat(1025)),
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long_with_max("Child", "name", 100),
                Error::field_too_long_with_max("Child", "url", 4096),
                Error::field_too_long_with_max("Child", "environment", 100),
                Error::invalid_environment("Child", "environment", "a".repeat(1025)),
            ])),
        },
        test_validate_child_references_unknown_env => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![fdecl::Child{
                    name: Some("foo".to_string()),
                    url: Some("fuchsia-pkg://foo".to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    on_terminate: None,
                    environment: Some("test_env".to_string()),
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_environment("Child", "environment", "test_env"),
            ])),
        },

        // collections
        test_validate_collections_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![fdecl::Collection{
                    name: None,
                    durability: None,
                    environment: None,
                    allowed_offers: None,
                    allow_long_names: None,
                    ..fdecl::Collection::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("Collection", "name"),
                Error::missing_field("Collection", "durability"),
            ])),
        },
        test_validate_collections_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![fdecl::Collection{
                    name: Some("^bad".to_string()),
                    durability: Some(fdecl::Durability::Transient),
                    environment: None,
                    allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                    allow_long_names: None,
                    ..fdecl::Collection::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("Collection", "name"),
            ])),
        },
        test_validate_collections_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![fdecl::Collection{
                    name: Some("a".repeat(1025)),
                    durability: Some(fdecl::Durability::Transient),
                    environment: None,
                    allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                    allow_long_names: None,
                    ..fdecl::Collection::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("Collection", "name"),
            ])),
        },
        test_validate_collection_references_unknown_env => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![fdecl::Collection {
                    name: Some("foo".to_string()),
                    durability: Some(fdecl::Durability::Transient),
                    environment: Some("test_env".to_string()),
                    allowed_offers: Some(fdecl::AllowedOffers::StaticOnly),
                    allow_long_names: None,
                    ..fdecl::Collection::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_environment("Collection", "environment", "test_env"),
            ])),
        },

        // capabilities
        test_validate_capabilities_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    fdecl::Capability::Service(fdecl::Service {
                        name: None,
                        source_path: None,
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: None,
                        source_path: None,
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: None,
                        source_path: None,
                        rights: None,
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: None,
                        source: None,
                        backing_dir: None,
                        subdir: None,
                        storage_id: None,
                        ..fdecl::Storage::EMPTY
                    }),
                    fdecl::Capability::Runner(fdecl::Runner {
                        name: None,
                        source_path: None,
                        ..fdecl::Runner::EMPTY
                    }),
                    fdecl::Capability::Resolver(fdecl::Resolver {
                        name: None,
                        source_path: None,
                        ..fdecl::Resolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("Service", "name"),
                Error::missing_field("Service", "source_path"),
                Error::missing_field("Protocol", "name"),
                Error::missing_field("Protocol", "source_path"),
                Error::missing_field("Directory", "name"),
                Error::missing_field("Directory", "source_path"),
                Error::missing_field("Directory", "rights"),
                Error::missing_field("Storage", "source"),
                Error::missing_field("Storage", "name"),
                Error::missing_field("Storage", "storage_id"),
                Error::missing_field("Storage", "backing_dir"),
                Error::missing_field("Runner", "name"),
                Error::missing_field("Runner", "source_path"),
                Error::missing_field("Resolver", "name"),
                Error::missing_field("Resolver", "source_path"),
            ])),
        },
        test_validate_capabilities_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("^bad".to_string()),
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef {
                            name: "/bad".to_string()
                        })),
                        backing_dir: Some("&bad".to_string()),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                    fdecl::Capability::Runner(fdecl::Runner {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                        ..fdecl::Runner::EMPTY
                    }),
                    fdecl::Capability::Resolver(fdecl::Resolver {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                        ..fdecl::Resolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("Service", "name"),
                Error::invalid_field("Service", "source_path"),
                Error::invalid_field("Protocol", "name"),
                Error::invalid_field("Protocol", "source_path"),
                Error::invalid_field("Directory", "name"),
                Error::invalid_field("Directory", "source_path"),
                Error::invalid_field("Storage", "source"),
                Error::invalid_field("Storage", "name"),
                Error::invalid_field("Storage", "backing_dir"),
                Error::invalid_field("Runner", "name"),
                Error::invalid_field("Runner", "source_path"),
                Error::invalid_field("Resolver", "name"),
                Error::invalid_field("Resolver", "source_path"),
            ])),
        },
        test_validate_capabilities_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("foo".to_string()),
                        source: Some(fdecl::Ref::Collection(fdecl::CollectionRef {
                            name: "invalid".to_string(),
                        })),
                        backing_dir: Some("foo".to_string()),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("Storage", "source"),
            ])),
        },
        test_validate_capabilities_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("a".repeat(101)),
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        backing_dir: Some(format!("{}", "c".repeat(101))),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                    fdecl::Capability::Runner(fdecl::Runner {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                        ..fdecl::Runner::EMPTY
                    }),
                    fdecl::Capability::Resolver(fdecl::Resolver {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "b".repeat(1024))),
                        ..fdecl::Resolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long_with_max("Service", "name", 100),
                Error::field_too_long_with_max("Service", "source_path", 1024),
                Error::field_too_long_with_max("Protocol", "name", 100),
                Error::field_too_long_with_max("Protocol", "source_path", 1024),
                Error::field_too_long_with_max("Directory", "name", 100),
                Error::field_too_long_with_max("Directory", "source_path", 1024),
                Error::field_too_long_with_max("Storage", "source.child.name", 100),
                Error::field_too_long_with_max("Storage", "name", 100),
                Error::field_too_long_with_max("Storage", "backing_dir", 100),
                Error::field_too_long_with_max("Runner", "name", 100),
                Error::field_too_long_with_max("Runner", "source_path", 1024),
                Error::field_too_long_with_max("Resolver", "name", 100),
                Error::field_too_long_with_max("Resolver", "source_path", 1024),
            ])),
        },
        test_validate_capabilities_duplicate_name => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("service".to_string()),
                        source_path: Some("/service".to_string()),
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("service".to_string()),
                        source_path: Some("/service".to_string()),
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("protocol".to_string()),
                        source_path: Some("/protocol".to_string()),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("protocol".to_string()),
                        source_path: Some("/protocol".to_string()),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("directory".to_string()),
                        source_path: Some("/directory".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("directory".to_string()),
                        source_path: Some("/directory".to_string()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("storage".to_string()),
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        backing_dir: Some("directory".to_string()),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                    fdecl::Capability::Storage(fdecl::Storage {
                        name: Some("storage".to_string()),
                        source: Some(fdecl::Ref::Self_(fdecl::SelfRef{})),
                        backing_dir: Some("directory".to_string()),
                        subdir: None,
                        storage_id: Some(fdecl::StorageId::StaticInstanceIdOrMoniker),
                        ..fdecl::Storage::EMPTY
                    }),
                    fdecl::Capability::Runner(fdecl::Runner {
                        name: Some("runner".to_string()),
                        source_path: Some("/runner".to_string()),
                        ..fdecl::Runner::EMPTY
                    }),
                    fdecl::Capability::Runner(fdecl::Runner {
                        name: Some("runner".to_string()),
                        source_path: Some("/runner".to_string()),
                        ..fdecl::Runner::EMPTY
                    }),
                    fdecl::Capability::Resolver(fdecl::Resolver {
                        name: Some("resolver".to_string()),
                        source_path: Some("/resolver".to_string()),
                        ..fdecl::Resolver::EMPTY
                    }),
                    fdecl::Capability::Resolver(fdecl::Resolver {
                        name: Some("resolver".to_string()),
                        source_path: Some("/resolver".to_string()),
                        ..fdecl::Resolver::EMPTY
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("Service", "name", "service"),
                Error::duplicate_field("Protocol", "name", "protocol"),
                Error::duplicate_field("Directory", "name", "directory"),
                Error::duplicate_field("Storage", "name", "storage"),
                Error::duplicate_field("Runner", "name", "runner"),
                Error::duplicate_field("Resolver", "name", "resolver"),
            ])),
        },

        test_validate_invalid_service_aggregation_missing_filter => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_a".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        source_instance_filter: Some(vec!["default".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_b".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        source_instance_filter: None,
                        ..fdecl::OfferService::EMPTY
                    }),
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("child_a".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_b".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_c".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_aggregate_offer("source_instance_filter must be set for all aggregate service offers"),
            ])),
        },

        test_validate_invalid_service_aggregation_conflicting_filter => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_a".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log1".to_string()),
                        source_instance_filter: Some(vec!["default".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_b".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log1".to_string()),
                        source_instance_filter: Some(vec!["default".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    }),
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("child_a".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_b".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_c".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_aggregate_offer("Conflicting source_instance_filter in aggregate service offer, instance_name 'default' seen in filter lists multiple times"),
            ])),
        },

        test_validate_invalid_service_aggregation_conflicting_source_name => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_a".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log2".to_string()),
                        source_instance_filter: Some(vec!["default2".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    }),
                    fdecl::Offer::Service(fdecl::OfferService {
                        source: Some(fdecl::Ref::Child(fdecl::ChildRef{name: "child_b".to_string(), collection: None})),
                        source_name: Some("fuchsia.logger.LogAlt".to_string()),
                        target: Some(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "child_c".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log2".to_string()),
                        source_instance_filter: Some(vec!["default".to_string()]),
                        ..fdecl::OfferService::EMPTY
                    })
                ]);
                decl.children = Some(vec![
                    fdecl::Child {
                        name: Some("child_a".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_b".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                    fdecl::Child {
                        name: Some("child_c".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..fdecl::Child::EMPTY
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_aggregate_offer("All aggregate service offers must have the same source_name, saw fuchsia.logger.Log, fuchsia.logger.LogAlt. Use renamed_instances to rename instance names to avoid conflict."),
            ])),
        },

        test_validate_resolvers_missing_from_offer => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![fdecl::Offer::Resolver(fdecl::OfferResolver {
                    source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                    source_name: Some("a".to_string()),
                    target: Some(fdecl::Ref::Child(fdecl::ChildRef { name: "child".to_string(), collection: None })),
                    target_name: Some("a".to_string()),
                    ..fdecl::OfferResolver::EMPTY
                })]);
                decl.children = Some(vec![fdecl::Child {
                    name: Some("child".to_string()),
                    url: Some("test:///child".to_string()),
                    startup: Some(fdecl::StartupMode::Eager),
                    on_terminate: None,
                    environment: None,
                    ..fdecl::Child::EMPTY
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("OfferResolver", "source", "a"),
            ])),
        },
        test_validate_resolvers_missing_from_expose => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![fdecl::Expose::Resolver(fdecl::ExposeResolver {
                    source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                    source_name: Some("a".to_string()),
                    target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                    target_name: Some("a".to_string()),
                    ..fdecl::ExposeResolver::EMPTY
                })]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("ExposeResolver", "source", "a"),
            ])),
        },

        test_validate_config_missing_config => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: None,
                    checksum: None,
                    value_source: None,
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ConfigSchema", "fields"),
                Error::missing_field("ConfigSchema", "checksum"),
                Error::missing_field("ConfigSchema", "value_source"),
            ])),
        },

        test_validate_config_missing_config_field => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: None,
                            type_: None,
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ConfigField", "key"),
                Error::missing_field("ConfigField", "value_type"),
            ])),
        },

        test_validate_config_bool => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Bool,
                                parameters: Some(vec![]),
                                constraints: vec![]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Ok(()),
        },

        test_validate_config_bool_extra_constraint => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Bool,
                                parameters: Some(vec![]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("ConfigType", "constraints")
            ])),
        },

        test_validate_config_bool_missing_parameters => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Bool,
                                parameters: None,
                                constraints: vec![]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ConfigType", "parameters")
            ])),
        },

        test_validate_config_string => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::String,
                                parameters: Some(vec![]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Ok(()),
        },

        test_validate_config_string_missing_parameter => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::String,
                                parameters: None,
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ConfigType", "parameters")
            ])),
        },

        test_validate_config_string_missing_constraint => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::String,
                                parameters: Some(vec![]),
                                constraints: vec![]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ConfigType", "constraints")
            ])),
        },

        test_validate_config_string_extra_constraint => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::String,
                                parameters: Some(vec![]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10), fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("ConfigType", "constraints")
            ])),
        },

        test_validate_config_vector_bool => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Vector,
                                parameters: Some(vec![fdecl::LayoutParameter::NestedType(fdecl::ConfigType {
                                    layout: fdecl::ConfigTypeLayout::Bool,
                                    parameters: Some(vec![]),
                                    constraints: vec![],
                                })]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Ok(()),
        },

        test_validate_config_vector_extra_parameter => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Vector,
                                parameters: Some(vec![fdecl::LayoutParameter::NestedType(fdecl::ConfigType {
                                    layout: fdecl::ConfigTypeLayout::Bool,
                                    parameters: Some(vec![]),
                                    constraints: vec![],
                                }), fdecl::LayoutParameter::NestedType(fdecl::ConfigType {
                                    layout: fdecl::ConfigTypeLayout::Uint8,
                                    parameters: Some(vec![]),
                                    constraints: vec![],
                                })]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("ConfigType", "parameters")
            ])),
        },

        test_validate_config_vector_missing_parameter => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Vector,
                                parameters: Some(vec![]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ConfigType", "parameters")
            ])),
        },

        test_validate_config_vector_string => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Vector,
                                parameters: Some(vec![fdecl::LayoutParameter::NestedType(fdecl::ConfigType {
                                    layout: fdecl::ConfigTypeLayout::String,
                                    parameters: Some(vec![]),
                                    constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                                })]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Ok(()),
        },

        test_validate_config_vector_vector => {
            input = {
                let mut decl = new_component_decl();
                decl.config = Some(fdecl::ConfigSchema{
                    fields: Some(vec![
                        fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Vector,
                                parameters: Some(vec![fdecl::LayoutParameter::NestedType(fdecl::ConfigType {
                                    layout: fdecl::ConfigTypeLayout::Vector,
                                    parameters: Some(vec![fdecl::LayoutParameter::NestedType(fdecl::ConfigType {
                                        layout: fdecl::ConfigTypeLayout::String,
                                        parameters: Some(vec![]),
                                        constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                                    })]),
                                    constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                                })]),
                                constraints: vec![fdecl::LayoutConstraint::MaxSize(10)]
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }
                    ]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    value_source: Some(fdecl::ConfigValueSource::PackagePath("config/test.cvf".to_string())),
                    ..fdecl::ConfigSchema::EMPTY
                });
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::nested_vector()
            ])),
        },
    }

    test_validate_capabilities! {
        test_validate_capabilities_individually_ok => {
            input = vec![
                fdecl::Capability::Protocol(fdecl::Protocol {
                    name: Some("foo_svc".into()),
                    source_path: Some("/svc/foo".into()),
                    ..fdecl::Protocol::EMPTY
                }),
                fdecl::Capability::Directory(fdecl::Directory {
                    name: Some("foo_dir".into()),
                    source_path: Some("/foo".into()),
                    rights: Some(fio::Operations::CONNECT),
                    ..fdecl::Directory::EMPTY
                }),
            ],
            as_builtin = false,
            result = Ok(()),
        },
        test_validate_capabilities_individually_err => {
            input = vec![
                fdecl::Capability::Protocol(fdecl::Protocol {
                    name: None,
                    source_path: None,
                    ..fdecl::Protocol::EMPTY
                }),
                fdecl::Capability::Directory(fdecl::Directory {
                    name: None,
                    source_path: None,
                    rights: None,
                    ..fdecl::Directory::EMPTY
                }),
                fdecl::Capability::Event(fdecl::Event {
                    name: None,
                    ..fdecl::Event::EMPTY
                }),
            ],
            as_builtin = false,
            result = Err(ErrorList::new(vec![
                Error::missing_field("Protocol", "name"),
                Error::missing_field("Protocol", "source_path"),
                Error::missing_field("Directory", "name"),
                Error::missing_field("Directory", "source_path"),
                Error::missing_field("Directory", "rights"),
                Error::invalid_capability_type("Component", "capability", "event")
            ])),
        },
        test_validate_builtin_capabilities_individually_ok => {
            input = vec![
                fdecl::Capability::Protocol(fdecl::Protocol {
                    name: Some("foo_protocol".into()),
                    source_path: None,
                    ..fdecl::Protocol::EMPTY
                }),
                fdecl::Capability::Directory(fdecl::Directory {
                    name: Some("foo_dir".into()),
                    source_path: None,
                    rights: Some(fio::Operations::CONNECT),
                    ..fdecl::Directory::EMPTY
                }),
                fdecl::Capability::Service(fdecl::Service {
                    name: Some("foo_svc".into()),
                    source_path: None,
                    ..fdecl::Service::EMPTY
                }),
                fdecl::Capability::Runner(fdecl::Runner {
                    name: Some("foo_runner".into()),
                    source_path: None,
                    ..fdecl::Runner::EMPTY
                }),
                fdecl::Capability::Resolver(fdecl::Resolver {
                    name: Some("foo_resolver".into()),
                    source_path: None,
                    ..fdecl::Resolver::EMPTY
                }),
                fdecl::Capability::Event(fdecl::Event {
                    name: Some("foo_event".into()),
                    ..fdecl::Event::EMPTY
                }),
            ],
            as_builtin = true,
            result = Ok(()),
        },
        test_validate_builtin_capabilities_individually_err => {
            input = vec![
                fdecl::Capability::Protocol(fdecl::Protocol {
                    name: None,
                    source_path: Some("/svc/foo".into()),
                    ..fdecl::Protocol::EMPTY
                }),
                fdecl::Capability::Directory(fdecl::Directory {
                    name: None,
                    source_path: Some("/foo".into()),
                    rights: None,
                    ..fdecl::Directory::EMPTY
                }),
                fdecl::Capability::Service(fdecl::Service {
                    name: None,
                    source_path: Some("/svc/foo".into()),
                    ..fdecl::Service::EMPTY
                }),
                fdecl::Capability::Runner(fdecl::Runner {
                    name: None,
                    source_path:  Some("/foo".into()),
                    ..fdecl::Runner::EMPTY
                }),
                fdecl::Capability::Resolver(fdecl::Resolver {
                    name: None,
                    source_path:  Some("/foo".into()),
                    ..fdecl::Resolver::EMPTY
                }),
                fdecl::Capability::Event(fdecl::Event {
                    name: None,
                    ..fdecl::Event::EMPTY
                }),
                fdecl::Capability::Storage(fdecl::Storage {
                    name: None,
                    ..fdecl::Storage::EMPTY
                }),
            ],
            as_builtin = true,
            result = Err(ErrorList::new(vec![
                Error::missing_field("Protocol", "name"),
                Error::extraneous_source_path("Protocol", "/svc/foo"),
                Error::missing_field("Directory", "name"),
                Error::extraneous_source_path("Directory", "/foo"),
                Error::missing_field("Directory", "rights"),
                Error::missing_field("Service", "name"),
                Error::extraneous_source_path("Service", "/svc/foo"),
                Error::missing_field("Runner", "name"),
                Error::extraneous_source_path("Runner", "/foo"),
                Error::missing_field("Resolver", "name"),
                Error::extraneous_source_path("Resolver", "/foo"),
                Error::missing_field("Event", "name"),
                Error::invalid_capability_type("RuntimeConfig", "capability", "storage"),
            ])),
        },
    }

    fn generate_offer_invalid_source_and_availability_decl(
        new_offer: impl Fn(fdecl::Ref, fdecl::Availability, &str) -> fdecl::Offer,
    ) -> fdecl::Component {
        let mut decl = new_component_decl();
        decl.children = Some(vec![
            fdecl::Child {
                name: Some("source".to_string()),
                url: Some("fuchsia-pkg://fuchsia.com/source#meta/source.cm".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                on_terminate: None,
                environment: None,
                ..fdecl::Child::EMPTY
            },
            fdecl::Child {
                name: Some("sink".to_string()),
                url: Some("fuchsia-pkg://fuchsia.com/sink#meta/sink.cm".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                on_terminate: None,
                environment: None,
                ..fdecl::Child::EMPTY
            },
        ]);
        decl.offers = Some(vec![
            // These offers are fine, offers with a source of parent or void can be
            // optional.
            new_offer(
                fdecl::Ref::Parent(fdecl::ParentRef {}),
                fdecl::Availability::Required,
                "fuchsia.examples.Echo0",
            ),
            new_offer(
                fdecl::Ref::Parent(fdecl::ParentRef {}),
                fdecl::Availability::Optional,
                "fuchsia.examples.Echo1",
            ),
            new_offer(
                fdecl::Ref::Parent(fdecl::ParentRef {}),
                fdecl::Availability::SameAsTarget,
                "fuchsia.examples.Echo2",
            ),
            new_offer(
                fdecl::Ref::VoidType(fdecl::VoidRef {}),
                fdecl::Availability::Optional,
                "fuchsia.examples.Echo3",
            ),
            // These offers are not fine, offers with a source other than parent or void
            // must be required.
            new_offer(
                fdecl::Ref::Self_(fdecl::SelfRef {}),
                fdecl::Availability::Optional,
                "fuchsia.examples.Echo4",
            ),
            new_offer(
                fdecl::Ref::Self_(fdecl::SelfRef {}),
                fdecl::Availability::SameAsTarget,
                "fuchsia.examples.Echo5",
            ),
            new_offer(
                fdecl::Ref::Framework(fdecl::FrameworkRef {}),
                fdecl::Availability::Optional,
                "fuchsia.examples.Echo6",
            ),
            new_offer(
                fdecl::Ref::Framework(fdecl::FrameworkRef {}),
                fdecl::Availability::SameAsTarget,
                "fuchsia.examples.Echo7",
            ),
            new_offer(
                fdecl::Ref::Child(fdecl::ChildRef { name: "source".to_string(), collection: None }),
                fdecl::Availability::Optional,
                "fuchsia.examples.Echo8",
            ),
            new_offer(
                fdecl::Ref::Child(fdecl::ChildRef { name: "source".to_string(), collection: None }),
                fdecl::Availability::SameAsTarget,
                "fuchsia.examples.Echo9",
            ),
            // These offers are also not fine, offers with a source of void must be optional
            new_offer(
                fdecl::Ref::VoidType(fdecl::VoidRef {}),
                fdecl::Availability::Required,
                "fuchsia.examples.Echo10",
            ),
            new_offer(
                fdecl::Ref::VoidType(fdecl::VoidRef {}),
                fdecl::Availability::SameAsTarget,
                "fuchsia.examples.Echo11",
            ),
        ]);
        decl
    }

    #[test]
    fn test_validate_dynamic_offers_empty() {
        assert_eq!(validate_dynamic_offers(&vec![]), Ok(()));
    }

    #[test]
    fn test_validate_dynamic_offers_okay() {
        assert_eq!(
            validate_dynamic_offers(&vec![
                fdecl::Offer::Protocol(fdecl::OfferProtocol {
                    dependency_type: Some(fdecl::DependencyType::Strong),
                    source: Some(fdecl::Ref::Self_(fdecl::SelfRef)),
                    source_name: Some("thing".repeat(26)),
                    target_name: Some("thing".repeat(26)),
                    ..fdecl::OfferProtocol::EMPTY
                }),
                fdecl::Offer::Service(fdecl::OfferService {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    source_name: Some("thang".repeat(26)),
                    target_name: Some("thang".repeat(26)),
                    ..fdecl::OfferService::EMPTY
                }),
                fdecl::Offer::Directory(fdecl::OfferDirectory {
                    dependency_type: Some(fdecl::DependencyType::Strong),
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    source_name: Some("thung1".repeat(26)),
                    target_name: Some("thung1".repeat(26)),
                    ..fdecl::OfferDirectory::EMPTY
                }),
                fdecl::Offer::Storage(fdecl::OfferStorage {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    source_name: Some("thung2".repeat(26)),
                    target_name: Some("thung2".repeat(26)),
                    ..fdecl::OfferStorage::EMPTY
                }),
                fdecl::Offer::Runner(fdecl::OfferRunner {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    source_name: Some("thung3".repeat(26)),
                    target_name: Some("thung3".repeat(26)),
                    ..fdecl::OfferRunner::EMPTY
                }),
                fdecl::Offer::Resolver(fdecl::OfferResolver {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    source_name: Some("thung4".repeat(26)),
                    target_name: Some("thung4".repeat(26)),
                    ..fdecl::OfferResolver::EMPTY
                }),
                fdecl::Offer::Event(fdecl::OfferEvent {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    source_name: Some("thung5".repeat(26)),
                    target_name: Some("thung5".repeat(26)),
                    ..fdecl::OfferEvent::EMPTY
                }),
            ]),
            Ok(())
        );
    }

    #[test]
    fn test_validate_dynamic_offers_specify_target() {
        assert_eq!(
            validate_dynamic_offers(&vec![
                fdecl::Offer::Protocol(fdecl::OfferProtocol {
                    dependency_type: Some(fdecl::DependencyType::Strong),
                    source: Some(fdecl::Ref::Self_(fdecl::SelfRef)),
                    target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "foo".to_string(),
                        collection: None
                    })),
                    source_name: Some("thing".to_string()),
                    target_name: Some("thing".to_string()),
                    ..fdecl::OfferProtocol::EMPTY
                }),
                fdecl::Offer::Service(fdecl::OfferService {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "bar".to_string(),
                        collection: Some("baz".to_string())
                    })),
                    source_name: Some("thang".to_string()),
                    target_name: Some("thang".to_string()),
                    ..fdecl::OfferService::EMPTY
                }),
                fdecl::Offer::Directory(fdecl::OfferDirectory {
                    dependency_type: Some(fdecl::DependencyType::Strong),
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    target: Some(fdecl::Ref::Framework(fdecl::FrameworkRef)),
                    source_name: Some("thung1".to_string()),
                    target_name: Some("thung1".to_string()),
                    ..fdecl::OfferDirectory::EMPTY
                }),
                fdecl::Offer::Storage(fdecl::OfferStorage {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "bar".to_string(),
                        collection: Some("baz".to_string())
                    })),
                    source_name: Some("thung2".to_string()),
                    target_name: Some("thung2".to_string()),
                    ..fdecl::OfferStorage::EMPTY
                }),
                fdecl::Offer::Runner(fdecl::OfferRunner {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "bar".to_string(),
                        collection: Some("baz".to_string())
                    })),
                    source_name: Some("thung3".to_string()),
                    target_name: Some("thung3".to_string()),
                    ..fdecl::OfferRunner::EMPTY
                }),
                fdecl::Offer::Resolver(fdecl::OfferResolver {
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "bar".to_string(),
                        collection: Some("baz".to_string())
                    })),
                    source_name: Some("thung4".to_string()),
                    target_name: Some("thung4".to_string()),
                    ..fdecl::OfferResolver::EMPTY
                }),
                fdecl::Offer::Event(fdecl::OfferEvent {
                    target: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "bar".to_string(),
                        collection: Some("baz".to_string())
                    })),
                    source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                    source_name: Some("thung5".to_string()),
                    target_name: Some("thung5".to_string()),
                    ..fdecl::OfferEvent::EMPTY
                }),
            ]),
            Err(ErrorList::new(vec![
                Error::extraneous_field("OfferProtocol", "target"),
                Error::extraneous_field("OfferService", "target"),
                Error::extraneous_field("OfferDirectory", "target"),
                Error::extraneous_field("OfferStorage", "target"),
                Error::extraneous_field("OfferRunner", "target"),
                Error::extraneous_field("OfferResolver", "target"),
                Error::extraneous_field("OfferEvent", "target"),
            ]))
        );
    }

    #[test]
    fn test_validate_dynamic_child() {
        assert_eq!(
            Ok(()),
            validate_dynamic_child(&fdecl::Child {
                name: Some("a".repeat(MAX_DYNAMIC_NAME_LENGTH).to_string()),
                url: Some("test:///child".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                on_terminate: None,
                environment: None,
                ..fdecl::Child::EMPTY
            })
        );
    }

    #[test]
    fn test_validate_dynamic_offers_missing_stuff() {
        assert_eq!(
            validate_dynamic_offers(&vec![
                fdecl::Offer::Protocol(fdecl::OfferProtocol::EMPTY),
                fdecl::Offer::Service(fdecl::OfferService::EMPTY),
                fdecl::Offer::Directory(fdecl::OfferDirectory::EMPTY),
                fdecl::Offer::Storage(fdecl::OfferStorage::EMPTY),
                fdecl::Offer::Runner(fdecl::OfferRunner::EMPTY),
                fdecl::Offer::Resolver(fdecl::OfferResolver::EMPTY),
                fdecl::Offer::Event(fdecl::OfferEvent::EMPTY),
            ]),
            Err(ErrorList::new(vec![
                Error::missing_field("OfferProtocol", "source"),
                Error::missing_field("OfferProtocol", "source_name"),
                Error::missing_field("OfferProtocol", "target_name"),
                Error::missing_field("OfferProtocol", "dependency_type"),
                Error::missing_field("OfferService", "source"),
                Error::missing_field("OfferService", "source_name"),
                Error::missing_field("OfferService", "target_name"),
                Error::missing_field("OfferDirectory", "source"),
                Error::missing_field("OfferDirectory", "source_name"),
                Error::missing_field("OfferDirectory", "target_name"),
                Error::missing_field("OfferDirectory", "dependency_type"),
                Error::missing_field("OfferStorage", "source_name"),
                Error::missing_field("OfferStorage", "source"),
                Error::missing_field("OfferRunner", "source"),
                Error::missing_field("OfferRunner", "source_name"),
                Error::missing_field("OfferRunner", "target_name"),
                Error::missing_field("OfferResolver", "source"),
                Error::missing_field("OfferResolver", "source_name"),
                Error::missing_field("OfferResolver", "target_name"),
                Error::missing_field("OfferEvent", "source_name"),
                Error::missing_field("OfferEvent", "source"),
                Error::missing_field("OfferEvent", "target_name"),
            ]))
        );
    }

    test_dependency! {
        (test_validate_offers_protocol_dependency_cycle) => {
            ty = fdecl::Offer::Protocol,
            offer_decl = fdecl::OfferProtocol {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
                dependency_type: Some(fdecl::DependencyType::Strong),
                ..fdecl::OfferProtocol::EMPTY
            },
        },
        (test_validate_offers_directory_dependency_cycle) => {
            ty = fdecl::Offer::Directory,
            offer_decl = fdecl::OfferDirectory {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
                rights: Some(fio::Operations::CONNECT),
                subdir: None,
                dependency_type: Some(fdecl::DependencyType::Strong),
                ..fdecl::OfferDirectory::EMPTY
            },
        },
        (test_validate_offers_service_dependency_cycle) => {
            ty = fdecl::Offer::Service,
            offer_decl = fdecl::OfferService {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
                ..fdecl::OfferService::EMPTY
            },
        },
        (test_validate_offers_runner_dependency_cycle) => {
            ty = fdecl::Offer::Runner,
            offer_decl = fdecl::OfferRunner {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
                ..fdecl::OfferRunner::EMPTY
            },
        },
        (test_validate_offers_resolver_dependency_cycle) => {
            ty = fdecl::Offer::Resolver,
            offer_decl = fdecl::OfferResolver {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
                ..fdecl::OfferResolver::EMPTY
            },
        },
    }
    test_weak_dependency! {
        (test_validate_offers_protocol_weak_dependency_cycle) => {
            ty = fdecl::Offer::Protocol,
            offer_decl = fdecl::OfferProtocol {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
                dependency_type: None, // Filled by macro
                ..fdecl::OfferProtocol::EMPTY
            },
        },
        (test_validate_offers_directory_weak_dependency_cycle) => {
            ty = fdecl::Offer::Directory,
            offer_decl = fdecl::OfferDirectory {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
                rights: Some(fio::Operations::CONNECT),
                subdir: None,
                dependency_type: None,  // Filled by macro
                ..fdecl::OfferDirectory::EMPTY
            },
        },
    }
}
