// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    directed_graph::DirectedGraph,
    fidl_fuchsia_sys2 as fsys,
    itertools::Itertools,
    std::{
        collections::{HashMap, HashSet},
        fmt,
        path::Path,
    },
    thiserror::Error,
};

const MAX_PATH_LENGTH: usize = 1024;
const MAX_NAME_LENGTH: usize = 100;
const MAX_URL_LENGTH: usize = 4096;

/// Enum type that can represent any error encountered during validation.
#[derive(Debug, Error, PartialEq)]
pub enum Error {
    #[error("{} missing {}", .0.decl, .0.field)]
    MissingField(DeclField),
    #[error("{} has empty {}", .0.decl, .0.field)]
    EmptyField(DeclField),
    #[error("{} has extraneous {}", .0.decl, .0.field)]
    ExtraneousField(DeclField),
    #[error("\"{1}\" is a duplicate {} {}", .0.decl, .0.field)]
    DuplicateField(DeclField, String),
    #[error("{} has invalid {}", .0.decl, .0.field)]
    InvalidField(DeclField),
    #[error("{}'s {} is too long", .0.decl, .0.field)]
    FieldTooLong(DeclField),
    #[error("\"{0}\" target \"{1}\" is same as source")]
    OfferTargetEqualsSource(String, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in children")]
    InvalidChild(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in collections")]
    InvalidCollection(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in storage")]
    InvalidStorage(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in environments")]
    InvalidEnvironment(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in capabilities")]
    InvalidCapability(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in runners")]
    InvalidRunner(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in events")]
    InvalidEventStream(DeclField, String),
    #[error("{0} specifies multiple runners")]
    MultipleRunnersSpecified(String),
    #[error("dependency cycle(s) exist: {0}")]
    DependencyCycle(String),
    #[error("{} \"{}\" path overlaps with {} \"{}\"", decl, path, other_decl, other_path)]
    InvalidPathOverlap { decl: DeclField, path: String, other_decl: DeclField, other_path: String },
}

impl Error {
    pub fn missing_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::MissingField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn empty_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::EmptyField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn extraneous_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::ExtraneousField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn duplicate_field(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        value: impl Into<String>,
    ) -> Self {
        Error::DuplicateField(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            value.into(),
        )
    }

    pub fn invalid_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::InvalidField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn field_too_long(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::FieldTooLong(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn offer_target_equals_source(decl: impl Into<String>, target: impl Into<String>) -> Self {
        Error::OfferTargetEqualsSource(decl.into(), target.into())
    }

    pub fn invalid_child(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        child: impl Into<String>,
    ) -> Self {
        Error::InvalidChild(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            child.into(),
        )
    }

    pub fn invalid_collection(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        collection: impl Into<String>,
    ) -> Self {
        Error::InvalidCollection(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            collection.into(),
        )
    }

    pub fn invalid_storage(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        storage: impl Into<String>,
    ) -> Self {
        Error::InvalidStorage(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            storage.into(),
        )
    }

    pub fn invalid_environment(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        environment: impl Into<String>,
    ) -> Self {
        Error::InvalidEnvironment(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            environment.into(),
        )
    }

    // TODO: Replace with `invalid_capability`?
    pub fn invalid_runner(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        runner: impl Into<String>,
    ) -> Self {
        Error::InvalidRunner(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            runner.into(),
        )
    }

    pub fn invalid_capability(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        capability: impl Into<String>,
    ) -> Self {
        Error::InvalidCapability(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            capability.into(),
        )
    }

    pub fn invalid_event_stream(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        event_name: impl Into<String>,
    ) -> Self {
        Error::InvalidEventStream(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            event_name.into(),
        )
    }

    pub fn multiple_runners_specified(decl_type: impl Into<String>) -> Self {
        Error::MultipleRunnersSpecified(decl_type.into())
    }

    pub fn dependency_cycle(error: String) -> Self {
        Error::DependencyCycle(error)
    }

    pub fn invalid_path_overlap(
        decl: impl Into<String>,
        path: impl Into<String>,
        other_decl: impl Into<String>,
        other_path: impl Into<String>,
    ) -> Self {
        Error::InvalidPathOverlap {
            decl: DeclField { decl: decl.into(), field: "target_path".to_string() },
            path: path.into(),
            other_decl: DeclField { decl: other_decl.into(), field: "target_path".to_string() },
            other_path: other_path.into(),
        }
    }
}

#[derive(Debug, PartialEq)]
pub struct DeclField {
    pub decl: String,
    pub field: String,
}

impl fmt::Display for DeclField {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}", &self.decl, &self.field)
    }
}

/// Represents a list of errors encountered durlng validation.
#[derive(Debug, Error, PartialEq)]
pub struct ErrorList {
    errs: Vec<Error>,
}

impl ErrorList {
    fn new(errs: Vec<Error>) -> ErrorList {
        ErrorList { errs }
    }
}

impl fmt::Display for ErrorList {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let strs: Vec<String> = self.errs.iter().map(|e| format!("{}", e)).collect();
        write!(f, "{}", strs.join(", "))
    }
}

/// Validates a ComponentDecl.
///
/// The ComponentDecl may ultimately originate from a CM file, or be directly constructed by the
/// caller. Either way, a ComponentDecl should always be validated before it's used. Examples
/// of what is validated (which may evolve in the future):
///
/// - That all semantically required fields are present
/// - That a child_name referenced in a source actually exists in the list of children
/// - That there are no duplicate target paths.
/// - That a cap is not offered back to the child that exposed it.
///
/// All checks are local to this ComponentDecl.
pub fn validate(decl: &fsys::ComponentDecl) -> Result<(), ErrorList> {
    let ctx = ValidationContext::default();
    ctx.validate(decl).map_err(|errs| ErrorList::new(errs))
}

/// Validates a list of CapabilityDecls independently.
pub fn validate_capabilities(capabilities: &Vec<fsys::CapabilityDecl>) -> Result<(), ErrorList> {
    let mut ctx = ValidationContext::default();
    for capability in capabilities {
        ctx.validate_capability_decl(capability);
    }
    if ctx.errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList::new(ctx.errors))
    }
}

/// Validates an independent ChildDecl. Performs the same validation on it as `validate`.
pub fn validate_child(child: &fsys::ChildDecl) -> Result<(), ErrorList> {
    let mut errors = vec![];
    check_name(child.name.as_ref(), "ChildDecl", "name", &mut errors);
    check_url(child.url.as_ref(), "ChildDecl", "url", &mut errors);
    if child.startup.is_none() {
        errors.push(Error::missing_field("ChildDecl", "startup"));
    }
    if child.environment.is_some() {
        check_name(child.environment.as_ref(), "ChildDecl", "environment", &mut errors);
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList { errs: errors })
    }
}

#[derive(Default)]
struct ValidationContext<'a> {
    all_children: HashMap<&'a str, &'a fsys::ChildDecl>,
    all_collections: HashSet<&'a str>,
    all_capability_ids: HashSet<&'a str>,
    all_storage_and_sources: HashMap<&'a str, Option<&'a str>>,
    all_services: HashSet<&'a str>,
    all_protocols: HashSet<&'a str>,
    all_directories: HashSet<&'a str>,
    all_runners: HashSet<&'a str>,
    all_resolvers: HashSet<&'a str>,
    all_environment_names: HashSet<&'a str>,
    all_event_names: HashSet<&'a str>,
    all_event_streams: HashSet<&'a str>,
    strong_dependencies: DirectedGraph<DependencyNode<'a>>,
    target_ids: IdMap<'a>,
    errors: Vec<Error>,
}

/// A node in the DependencyGraph. The first string describes the type of node and the second
/// string is the name of the node.
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

#[derive(Clone, Copy, PartialEq)]
enum AllowableIds {
    One,
    Many,
}

#[derive(Debug, PartialEq, Eq, Hash)]
enum TargetId<'a> {
    Component(&'a str),
    Collection(&'a str),
}

type IdMap<'a> = HashMap<TargetId<'a>, HashMap<&'a str, AllowableIds>>;

impl<'a> ValidationContext<'a> {
    fn validate(mut self, decl: &'a fsys::ComponentDecl) -> Result<(), Vec<Error>> {
        // Collect all environment names first, so that references to them can be checked.
        if let Some(envs) = &decl.environments {
            self.collect_environment_names(&envs);
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
                self.validate_capability_decl(capability);
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
                self.validate_offers_decl(&offer);
            }
        }

        // Validate "environments" after all other declarations are processed.
        if let Some(environment) = decl.environments.as_ref() {
            for environment in environment {
                self.validate_environment_decl(&environment);
            }
        }

        // Check that there are no strong cyclical dependencies between children and environments
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
    fn collect_environment_names(&mut self, envs: &'a [fsys::EnvironmentDecl]) {
        for env in envs {
            if let Some(name) = env.name.as_ref() {
                if !self.all_environment_names.insert(name) {
                    self.errors.push(Error::duplicate_field("EnvironmentDecl", "name", name));
                }
            }
        }
    }

    fn validate_capability_decl(&mut self, capability: &'a fsys::CapabilityDecl) {
        match capability {
            fsys::CapabilityDecl::Service(service) => self.validate_service_decl(&service),
            fsys::CapabilityDecl::Protocol(protocol) => self.validate_protocol_decl(&protocol),
            fsys::CapabilityDecl::Directory(directory) => self.validate_directory_decl(&directory),
            fsys::CapabilityDecl::Storage(storage) => self.validate_storage_decl(&storage),
            fsys::CapabilityDecl::Runner(runner) => self.validate_runner_decl(&runner),
            fsys::CapabilityDecl::Resolver(resolver) => self.validate_resolver_decl(&resolver),
            fsys::CapabilityDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "capability"));
            }
        }
    }

    fn validate_use_decls(&mut self, uses: &'a [fsys::UseDecl]) {
        // Validate individual fields.
        for use_ in uses.iter() {
            self.validate_use_decl(&use_);
        }

        self.validate_use_has_single_runner(&uses);
        self.validate_use_paths(&uses);
    }

    fn validate_use_decl(&mut self, use_: &'a fsys::UseDecl) {
        match use_ {
            fsys::UseDecl::Service(u) => {
                self.validate_source(u.source.as_ref(), "UseServiceDecl", "source");
                check_name(
                    u.source_name.as_ref(),
                    "UseServiceDecl",
                    "source_name",
                    &mut self.errors,
                );
                check_path(
                    u.target_path.as_ref(),
                    "UseServiceDecl",
                    "target_path",
                    &mut self.errors,
                );
            }
            fsys::UseDecl::Protocol(u) => {
                self.validate_source(u.source.as_ref(), "UseProtocolDecl", "source");
                check_name_or_path(
                    u.source_path.as_ref(),
                    "UseProtocolDecl",
                    "source_path",
                    &mut self.errors,
                );
                check_name_or_path(
                    u.target_path.as_ref(),
                    "UseProtocolDecl",
                    "target_path",
                    &mut self.errors,
                );
            }
            fsys::UseDecl::Directory(u) => {
                self.validate_source(u.source.as_ref(), "UseDirectoryDecl", "source");
                check_name_or_path(
                    u.source_path.as_ref(),
                    "UseDirectoryDecl",
                    "source_path",
                    &mut self.errors,
                );
                check_name_or_path(
                    u.target_path.as_ref(),
                    "UseDirectoryDecl",
                    "target_path",
                    &mut self.errors,
                );
                if u.rights.is_none() {
                    self.errors.push(Error::missing_field("UseDirectoryDecl", "rights"));
                }
                if let Some(subdir) = u.subdir.as_ref() {
                    check_relative_path(
                        Some(subdir),
                        "UseDirectoryDecl",
                        "subdir",
                        &mut self.errors,
                    );
                }
            }
            fsys::UseDecl::Storage(u) => {
                check_name(
                    u.source_name.as_ref(),
                    "UseStorageDecl",
                    "source_name",
                    &mut self.errors,
                );
                check_path(
                    u.target_path.as_ref(),
                    "UseStorageDecl",
                    "target_path",
                    &mut self.errors,
                );
            }
            fsys::UseDecl::Runner(r) => {
                check_name(
                    r.source_name.as_ref(),
                    "UseRunnerDecl",
                    "source_name",
                    &mut self.errors,
                );
            }
            fsys::UseDecl::Event(e) => {
                self.validate_event(e);
            }
            fsys::UseDecl::EventStream(e) => {
                self.validate_event_stream(e);
            }
            fsys::UseDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "use"));
            }
        }
    }

    /// Ensures that no more than one runner is specified.
    fn validate_use_has_single_runner(&mut self, uses: &[fsys::UseDecl]) {
        let mut runners_count: i32 = 0;
        for use_ in uses.iter() {
            if let fsys::UseDecl::Runner(_) = use_ {
                runners_count += 1;
            }
        }
        if runners_count > 1 {
            self.errors.push(Error::multiple_runners_specified("UseRunnerDecl"));
        }
    }

    /// Validates that paths-based capabilities (service, directory, protocol)
    /// are different and not prefixes of each other.
    fn validate_use_paths(&mut self, uses: &[fsys::UseDecl]) {
        #[derive(Debug, PartialEq, Clone, Copy)]
        struct PathCapability<'a> {
            decl: &'a str,
            dir: &'a Path,
            use_: &'a fsys::UseDecl,
        };
        let mut used_paths = HashMap::new();
        for use_ in uses.iter() {
            match use_ {
                fsys::UseDecl::Service(fsys::UseServiceDecl {
                    target_path: Some(path), ..
                })
                | fsys::UseDecl::Protocol(fsys::UseProtocolDecl {
                    target_path: Some(path), ..
                })
                | fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                    target_path: Some(path),
                    ..
                }) => {
                    let capability = match use_ {
                        fsys::UseDecl::Service(_) => {
                            let dir = match Path::new(path).parent() {
                                Some(p) => p,
                                None => continue, // Invalid path, validated elsewhere
                            };
                            PathCapability { decl: "UseServiceDecl", dir, use_ }
                        }
                        fsys::UseDecl::Protocol(_) => {
                            let dir = match Path::new(path).parent() {
                                Some(p) => p,
                                None => continue, // Invalid path, validated elsewhere
                            };
                            PathCapability { decl: "UseProtocolDecl", dir, use_ }
                        }
                        fsys::UseDecl::Directory(_) => {
                            PathCapability { decl: "UseDirectoryDecl", dir: Path::new(path), use_ }
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
                // Directories can't be the same or partially overlap.
                (fsys::UseDecl::Directory(_), fsys::UseDecl::Directory(_)) => {
                    capability_b.dir == capability_a.dir
                        || capability_b.dir.starts_with(capability_a.dir)
                        || capability_a.dir.starts_with(capability_b.dir)
                }

                // Protocols and Services can't overlap with Directories.
                (_, fsys::UseDecl::Directory(_)) | (fsys::UseDecl::Directory(_), _) => {
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
    }

    fn validate_event(&mut self, event: &'a fsys::UseEventDecl) {
        self.validate_source(event.source.as_ref(), "UseEventDecl", "source");
        check_name(event.source_name.as_ref(), "UseEventDecl", "source_name", &mut self.errors);
        check_name(event.target_name.as_ref(), "UseEventDecl", "target_name", &mut self.errors);
        if let Some(target_name) = event.target_name.as_ref() {
            if !self.all_event_names.insert(target_name) {
                self.errors.push(Error::duplicate_field(
                    "UseEventDecl",
                    "target_name",
                    target_name,
                ));
            }
        }
    }

    fn validate_event_stream(&mut self, event_stream: &'a fsys::UseEventStreamDecl) {
        check_path(
            event_stream.target_path.as_ref(),
            "UseEventStreamDecl",
            "target_path",
            &mut self.errors,
        );
        if let Some(target_path) = event_stream.target_path.as_ref() {
            if !self.all_event_streams.insert(target_path) {
                self.errors.push(Error::duplicate_field(
                    "UseEventStreamDecl",
                    "target_path",
                    target_path,
                ));
            }
        }
        match event_stream.events.as_ref() {
            None => {
                self.errors.push(Error::missing_field("UseEventStreamDecl", "events"));
            }
            Some(events) if events.is_empty() => {
                self.errors.push(Error::empty_field("UseEventStreamDecl", "events"));
            }
            Some(events) => {
                for event in events {
                    check_name(Some(event), "UseEventStreamDecl", "event_name", &mut self.errors);
                    if !self.all_event_names.contains(event.as_str()) {
                        self.errors.push(Error::invalid_event_stream(
                            "UseEventStreamDecl",
                            "events",
                            event,
                        ));
                    }
                }
            }
        }
    }

    fn validate_source(&mut self, source: Option<&fsys::Ref>, decl: &str, field: &str) {
        match source {
            Some(fsys::Ref::Parent(_)) => {}
            Some(fsys::Ref::Framework(_)) => {}
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, field));
            }
            None => {
                self.errors.push(Error::missing_field(decl, field));
            }
        };
    }

    fn validate_child_decl(&mut self, child: &'a fsys::ChildDecl) {
        if let Err(mut e) = validate_child(child) {
            self.errors.append(&mut e.errs);
        }
        if let Some(name) = child.name.as_ref() {
            let name: &str = name;
            if self.all_children.insert(name, child).is_some() {
                self.errors.push(Error::duplicate_field("ChildDecl", "name", name));
            }
            if let Some(env) = child.environment.as_ref() {
                let source = DependencyNode::Environment(env.as_str());
                let target = DependencyNode::Child(name);
                self.strong_dependencies.add_edge(source, target);
            }
        }
        if let Some(environment) = child.environment.as_ref() {
            if !self.all_environment_names.contains(environment.as_str()) {
                self.errors.push(Error::invalid_environment(
                    "ChildDecl",
                    "environment",
                    environment,
                ));
            }
        }
    }

    fn validate_collection_decl(&mut self, collection: &'a fsys::CollectionDecl) {
        let name = collection.name.as_ref();
        if check_name(name, "CollectionDecl", "name", &mut self.errors) {
            let name: &str = name.unwrap();
            if !self.all_collections.insert(name) {
                self.errors.push(Error::duplicate_field("CollectionDecl", "name", name));
            }
            // If there is an environment, we don't need to account for it in the dependency
            // graph because a collection is always a sink node.
        }
        if collection.durability.is_none() {
            self.errors.push(Error::missing_field("CollectionDecl", "durability"));
        }
        if let Some(environment) = collection.environment.as_ref() {
            if !self.all_environment_names.contains(environment.as_str()) {
                self.errors.push(Error::invalid_environment(
                    "CollectionDecl",
                    "environment",
                    environment,
                ));
            }
        }
    }

    fn validate_environment_decl(&mut self, environment: &'a fsys::EnvironmentDecl) {
        let name = environment.name.as_ref();
        check_name(name, "EnvironmentDecl", "name", &mut self.errors);
        if environment.extends.is_none() {
            self.errors.push(Error::missing_field("EnvironmentDecl", "extends"));
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
            Some(fsys::EnvironmentExtends::None) => {
                if environment.stop_timeout_ms.is_none() {
                    self.errors.push(Error::missing_field("EnvironmentDecl", "stop_timeout_ms"));
                }
            }
            None | Some(fsys::EnvironmentExtends::Realm) => {}
        }
    }

    fn validate_runner_registration(
        &mut self,
        runner_registration: &'a fsys::RunnerRegistration,
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
        // If the source is `self`, ensure we have a corresponding RunnerDecl.
        if let (Some(fsys::Ref::Self_(_)), Some(ref name)) =
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
        resolver_registration: &'a fsys::ResolverRegistration,
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
        source: Option<&'a fsys::Ref>,
        ty: &str,
    ) {
        match source {
            Some(fsys::Ref::Parent(_)) => {}
            Some(fsys::Ref::Self_(_)) => {}
            Some(fsys::Ref::Child(child_ref)) => {
                // Make sure the child is valid.
                if self.validate_child_ref(ty, "source", &child_ref) {
                    // Ensure there are no cycles, such as a resolver in an environment being
                    // assigned to a child which the resolver depends on.
                    let source = DependencyNode::Child(child_ref.name.as_str());
                    let target = DependencyNode::Environment(environment_name.unwrap().as_str());
                    self.strong_dependencies.add_edge(source, target);
                }
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(ty, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(ty, "source"));
            }
        }
    }

    fn validate_service_decl(&mut self, service: &'a fsys::ServiceDecl) {
        if check_name(service.name.as_ref(), "ServiceDecl", "name", &mut self.errors) {
            let name = service.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("ServiceDecl", "name", name.as_str()));
            }
            self.all_services.insert(name);
        }
        check_path(service.source_path.as_ref(), "ServiceDecl", "source_path", &mut self.errors);
    }

    fn validate_protocol_decl(&mut self, protocol: &'a fsys::ProtocolDecl) {
        if check_name(protocol.name.as_ref(), "ProtocolDecl", "name", &mut self.errors) {
            let name = protocol.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("ProtocolDecl", "name", name.as_str()));
            }
            self.all_protocols.insert(name);
        }
        check_path(protocol.source_path.as_ref(), "ProtocolDecl", "source_path", &mut self.errors);
    }

    fn validate_directory_decl(&mut self, directory: &'a fsys::DirectoryDecl) {
        if check_name(directory.name.as_ref(), "DirectoryDecl", "name", &mut self.errors) {
            let name = directory.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("DirectoryDecl", "name", name.as_str()));
            }
            self.all_directories.insert(name);
        }
        check_path(
            directory.source_path.as_ref(),
            "DirectoryDecl",
            "source_path",
            &mut self.errors,
        );
        if directory.rights.is_none() {
            self.errors.push(Error::missing_field("DirectoryDecl", "rights"));
        }
    }

    fn validate_storage_decl(&mut self, storage: &'a fsys::StorageDecl) {
        let source_child_name = match storage.source.as_ref() {
            Some(fsys::Ref::Parent(_)) => None,
            Some(fsys::Ref::Self_(_)) => None,
            Some(fsys::Ref::Child(child)) => {
                self.validate_source_child(child, "StorageDecl");
                Some(&child.name as &str)
            }
            Some(_) => {
                self.errors.push(Error::invalid_field("StorageDecl", "source"));
                None
            }
            None => {
                self.errors.push(Error::missing_field("StorageDecl", "source"));
                None
            }
        };
        if check_name(storage.name.as_ref(), "StorageDecl", "name", &mut self.errors) {
            let name = storage.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("StorageDecl", "name", name.as_str()));
            }
            self.all_storage_and_sources.insert(name, source_child_name);
        }
        check_name_or_path(
            storage.source_path.as_ref(),
            "StorageDecl",
            "source_path",
            &mut self.errors,
        );
    }

    fn validate_runner_decl(&mut self, runner: &'a fsys::RunnerDecl) {
        match runner.source.as_ref() {
            Some(fsys::Ref::Self_(_)) => None,
            Some(fsys::Ref::Child(child)) => {
                self.validate_source_child(child, "RunnerDecl");
                Some(&child.name as &str)
            }
            Some(_) => {
                self.errors.push(Error::invalid_field("RunnerDecl", "source"));
                None
            }
            None => {
                self.errors.push(Error::missing_field("RunnerDecl", "source"));
                None
            }
        };
        if check_name(runner.name.as_ref(), "RunnerDecl", "name", &mut self.errors) {
            let name = runner.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("RunnerDecl", "name", name.as_str()));
            }
            self.all_runners.insert(name);
        }
        check_path(runner.source_path.as_ref(), "RunnerDecl", "source_path", &mut self.errors);
    }

    fn validate_resolver_decl(&mut self, resolver: &'a fsys::ResolverDecl) {
        if check_name(resolver.name.as_ref(), "ResolverDecl", "name", &mut self.errors) {
            let name = resolver.name.as_ref().unwrap();
            if !self.all_capability_ids.insert(name) {
                self.errors.push(Error::duplicate_field("ResolverDecl", "name", name.as_str()));
            }
            self.all_resolvers.insert(name);
        }
        check_path(resolver.source_path.as_ref(), "ResolverDecl", "source_path", &mut self.errors);
    }

    fn validate_source_child(&mut self, child: &fsys::ChildRef, decl_type: &str) {
        let mut valid = true;
        valid &= check_name(Some(&child.name), decl_type, "source.child.name", &mut self.errors);
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
            self.errors.push(Error::invalid_child(decl_type, "source", &child.name as &str));
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
        expose: &'a fsys::ExposeDecl,
        prev_target_ids: &mut HashMap<&'a str, AllowableIds>,
    ) {
        match expose {
            fsys::ExposeDecl::Service(e) => {
                let decl = "ExposeServiceDecl";
                self.validate_expose_fields_with_name(
                    decl,
                    AllowableIds::Many,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding ServiceDecl.
                // TODO: Consider bringing this bit into validate_expose_fields_with_name.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_services.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fsys::ExposeDecl::Protocol(e) => {
                let decl = "ExposeProtocolDecl";
                self.validate_expose_fields_with_name_or_path(
                    decl,
                    AllowableIds::One,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding ProtocolDecl.
                // TODO: Consider bringing this bit into validate_expose_fields_with_name.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_path) {
                    if !name.starts_with('/') && !self.all_protocols.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fsys::ExposeDecl::Directory(e) => {
                let decl = "ExposeDirectoryDecl";
                self.validate_expose_fields_with_name_or_path(
                    decl,
                    AllowableIds::One,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding DirectoryDecl.
                // TODO: Consider bringing this bit into validate_expose_fields_with_name.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_path) {
                    if !name.starts_with('/') && !self.all_directories.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                    if name.starts_with('/') && e.rights.is_none() {
                        self.errors.push(Error::missing_field(decl, "rights"));
                    }
                }

                // Subdir makes sense when routing, but when exposing to framework the subdirectory
                // can be exposed directly.
                match e.target.as_ref() {
                    Some(fsys::Ref::Framework(_)) => {
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
            fsys::ExposeDecl::Runner(e) => {
                let decl = "ExposeRunnerDecl";
                self.validate_expose_fields_with_name(
                    decl,
                    AllowableIds::One,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding RunnerDecl.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_runners.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fsys::ExposeDecl::Resolver(e) => {
                let decl = "ExposeResolverDecl";
                self.validate_expose_fields_with_name(
                    decl,
                    AllowableIds::One,
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target_name.as_ref(),
                    e.target.as_ref(),
                    prev_target_ids,
                );
                // If the expose source is `self`, ensure we have a corresponding ResolverDecl.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&e.source, &e.source_name) {
                    if !self.all_resolvers.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fsys::ExposeDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "expose"));
            }
        }
    }

    fn validate_expose_fields_with_name_or_path(
        &mut self,
        decl: &str,
        allowable_ids: AllowableIds,
        source: Option<&fsys::Ref>,
        source_id: Option<&String>,
        target_id: Option<&'a String>,
        target: Option<&fsys::Ref>,
        prev_child_target_ids: &mut HashMap<&'a str, AllowableIds>,
    ) {
        match source {
            Some(r) => match r {
                fsys::Ref::Self_(_) => {}
                fsys::Ref::Framework(_) => {}
                fsys::Ref::Child(child) => {
                    self.validate_source_child(child, decl);
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
                fsys::Ref::Parent(_) => {}
                fsys::Ref::Framework(_) => {
                    if source != Some(&fsys::Ref::Self_(fsys::SelfRef {})) {
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
        check_name_or_path(source_id, decl, "source_path", &mut self.errors);
        if check_name_or_path(target_id, decl, "target_path", &mut self.errors) {
            // TODO: This logic needs to pair the target path with the target before concluding
            // there's a duplicate.
            let target_id = target_id.unwrap();
            if let Some(prev_state) = prev_child_target_ids.insert(target_id, allowable_ids) {
                if prev_state == AllowableIds::One || prev_state != allowable_ids {
                    self.errors.push(Error::duplicate_field(decl, "target_path", target_id));
                }
            }
        }
    }

    fn validate_expose_fields_with_name(
        &mut self,
        decl: &str,
        allowable_ids: AllowableIds,
        source: Option<&fsys::Ref>,
        source_name: Option<&String>,
        target_name: Option<&'a String>,
        target: Option<&fsys::Ref>,
        prev_child_target_ids: &mut HashMap<&'a str, AllowableIds>,
    ) {
        match source {
            Some(r) => match r {
                fsys::Ref::Self_(_) => {}
                fsys::Ref::Framework(_) => {}
                fsys::Ref::Child(child) => {
                    self.validate_source_child(child, decl);
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
                fsys::Ref::Parent(_) => {}
                fsys::Ref::Framework(_) => {
                    if source != Some(&fsys::Ref::Self_(fsys::SelfRef {})) {
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

    fn add_strong_dep(&mut self, from: Option<&'a fsys::Ref>, to: Option<&'a fsys::Ref>) {
        if let Some(fsys::Ref::Child(fsys::ChildRef { name: source, .. })) = from {
            if let Some(fsys::Ref::Child(fsys::ChildRef { name: target, .. })) = to {
                if source == target {
                    // This is already its own error, don't report this as a cycle.
                } else {
                    let source = DependencyNode::Child(source.as_str());
                    let target = DependencyNode::Child(target.as_str());
                    self.strong_dependencies.add_edge(source, target);
                }
            }
        }
    }

    fn validate_offers_decl(&mut self, offer: &'a fsys::OfferDecl) {
        match offer {
            fsys::OfferDecl::Service(o) => {
                let decl = "OfferServiceDecl";
                self.validate_offer_fields_with_name(
                    decl,
                    AllowableIds::Many,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                );
                // If the offer source is `self`, ensure we have a corresponding ServiceDecl.
                // TODO: Consider bringing this bit into validate_offer_fields_with_name
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&o.source, &o.source_name) {
                    if !self.all_services.contains(&name as &str) {
                        self.errors.push(Error::invalid_field(decl, "source"));
                    }
                }
                self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
            }
            fsys::OfferDecl::Protocol(o) => {
                let decl = "OfferProtocolDecl";
                self.validate_offer_fields_with_name_or_path(
                    decl,
                    AllowableIds::One,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
                if o.dependency_type.is_none() {
                    self.errors.push(Error::missing_field(decl, "dependency_type"));
                } else if o.dependency_type == Some(fsys::DependencyType::Strong) {
                    self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
                }
                // If the offer source is `self`, ensure we have a corresponding ProtocolDecl.
                // TODO: Consider bringing this bit into validate_offer_fields_with_name.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&o.source, &o.source_path) {
                    if !name.starts_with('/') && !self.all_protocols.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
            }
            fsys::OfferDecl::Directory(o) => {
                let decl = "OfferDirectoryDecl";
                self.validate_offer_fields_with_name_or_path(
                    decl,
                    AllowableIds::One,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
                if o.dependency_type.is_none() {
                    self.errors.push(Error::missing_field(decl, "dependency_type"));
                } else if o.dependency_type == Some(fsys::DependencyType::Strong) {
                    self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
                }
                // If the offer source is `self`, ensure we have a corresponding DirectoryDecl.
                // TODO: Consider bringing this bit into validate_offer_fields_with_name.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&o.source, &o.source_path) {
                    if !name.starts_with('/') && !self.all_directories.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                    if name.starts_with('/') && o.rights.is_none() {
                        self.errors.push(Error::missing_field(decl, "rights"));
                    }
                }
                if let Some(subdir) = o.subdir.as_ref() {
                    check_relative_path(
                        Some(subdir),
                        "OfferDirectoryDecl",
                        "subdir",
                        &mut self.errors,
                    );
                }
            }
            fsys::OfferDecl::Storage(o) => {
                self.validate_storage_offer_fields(
                    "OfferStorageDecl",
                    o.source_name.as_ref(),
                    o.source.as_ref(),
                    o.target.as_ref(),
                );
                self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
            }
            fsys::OfferDecl::Runner(o) => {
                let decl = "OfferRunnerDecl";
                self.validate_offer_fields_with_name(
                    decl,
                    AllowableIds::One,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                );
                // If the offer source is `self`, ensure we have a corresponding RunnerDecl.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&o.source, &o.source_name) {
                    if !self.all_runners.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
                self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
            }
            fsys::OfferDecl::Resolver(o) => {
                let decl = "OfferResolverDecl";
                self.validate_offer_fields_with_name(
                    decl,
                    AllowableIds::One,
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                );
                // If the offer source is `self`, ensure we have a corresponding ResolverDecl.
                if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&o.source, &o.source_name) {
                    if !self.all_resolvers.contains(&name as &str) {
                        self.errors.push(Error::invalid_capability(decl, "source", name));
                    }
                }
                self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
            }
            fsys::OfferDecl::Event(e) => {
                self.validate_event_offer_fields(e);
            }
            fsys::OfferDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "offer"));
            }
        }
    }

    /// Validates that the offer target is to a valid child or collection.
    fn validate_offer_target(
        &mut self,
        target: &'a Option<fsys::Ref>,
        decl_type: &str,
        field_name: &str,
    ) -> Option<TargetId<'a>> {
        match target.as_ref() {
            Some(fsys::Ref::Child(child)) => {
                if self.validate_child_ref(decl_type, field_name, &child) {
                    Some(TargetId::Component(&child.name))
                } else {
                    None
                }
            }
            Some(fsys::Ref::Collection(collection)) => {
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

    fn validate_offer_fields_with_name_or_path(
        &mut self,
        decl: &str,
        allowable_ids: AllowableIds,
        source: Option<&fsys::Ref>,
        source_id: Option<&String>,
        target: Option<&'a fsys::Ref>,
        target_id: Option<&'a String>,
    ) {
        match source {
            Some(fsys::Ref::Parent(_)) => {}
            Some(fsys::Ref::Self_(_)) => {}
            Some(fsys::Ref::Framework(_)) => {}
            Some(fsys::Ref::Child(child)) => self.validate_source_child(child, decl),
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }
        check_name_or_path(source_id, decl, "source_path", &mut self.errors);
        match target {
            Some(fsys::Ref::Child(c)) => {
                self.validate_target_child_with_path(decl, allowable_ids, c, source, target_id);
            }
            Some(fsys::Ref::Collection(c)) => {
                self.validate_target_collection_with_path(decl, allowable_ids, c, target_id);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "target"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
            }
        }
        check_name_or_path(target_id, decl, "target_path", &mut self.errors);
    }

    fn validate_offer_fields_with_name(
        &mut self,
        decl: &str,
        allowable_names: AllowableIds,
        source: Option<&fsys::Ref>,
        source_name: Option<&String>,
        target: Option<&'a fsys::Ref>,
        target_name: Option<&'a String>,
    ) {
        match source {
            Some(fsys::Ref::Parent(_)) => {}
            Some(fsys::Ref::Self_(_)) => {}
            Some(fsys::Ref::Framework(_)) => {}
            Some(fsys::Ref::Child(child)) => self.validate_source_child(child, decl),
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }
        check_name(source_name, decl, "source_name", &mut self.errors);
        match target {
            Some(fsys::Ref::Child(c)) => {
                self.validate_target_child_with_name(decl, allowable_names, c, source, target_name);
            }
            Some(fsys::Ref::Collection(c)) => {
                self.validate_target_collection_with_name(decl, allowable_names, c, target_name);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "target"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
            }
        }
        check_name(target_name, decl, "target_name", &mut self.errors);
    }

    fn validate_storage_offer_fields(
        &mut self,
        decl: &str,
        source_name: Option<&'a String>,
        source: Option<&'a fsys::Ref>,
        target: Option<&'a fsys::Ref>,
    ) {
        if source_name.is_none() {
            self.errors.push(Error::missing_field(decl, "source_name"));
        }
        match source {
            Some(fsys::Ref::Parent(_)) => (),
            Some(fsys::Ref::Self_(_)) => {
                self.validate_storage_source(source_name.unwrap(), decl);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        }
        self.validate_storage_target(decl, source_name.map(|n| n.as_str()), target);
    }

    fn validate_event_offer_fields(&mut self, event: &'a fsys::OfferEventDecl) {
        let decl = "OfferEventDecl";
        check_name(event.source_name.as_ref(), decl, "source_name", &mut self.errors);

        // Only parent and framework are valid.
        match event.source {
            Some(fsys::Ref::Parent(_)) => {}
            Some(fsys::Ref::Framework(_)) => {}
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        };

        let target_id = self.validate_offer_target(&event.target, decl, "target");
        if let (Some(target_id), Some(target_name)) = (target_id, event.target_name.as_ref()) {
            // Assuming the target_name is valid, ensure the target_name isn't already used.
            if let Some(_) = self
                .target_ids
                .entry(target_id)
                .or_insert(HashMap::new())
                .insert(target_name, AllowableIds::One)
            {
                self.errors.push(Error::duplicate_field(decl, "target_name", target_name as &str));
            }
        }

        check_name(event.target_name.as_ref(), decl, "target_name", &mut self.errors);
    }

    /// Check a `ChildRef` contains a valid child that exists.
    ///
    /// We ensure the target child is statically defined (i.e., not a dynamic child inside
    /// a collection).
    fn validate_child_ref(&mut self, decl: &str, field_name: &str, child: &fsys::ChildRef) -> bool {
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
        collection: &fsys::CollectionRef,
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

    fn validate_target_child_with_path(
        &mut self,
        decl: &str,
        allowable_ids: AllowableIds,
        child: &'a fsys::ChildRef,
        source: Option<&fsys::Ref>,
        target_path: Option<&'a String>,
    ) {
        if !self.validate_child_ref(decl, "target", child) {
            return;
        }
        if let Some(target_path) = target_path {
            let paths_for_target =
                self.target_ids.entry(TargetId::Component(&child.name)).or_insert(HashMap::new());
            if let Some(prev_state) = paths_for_target.insert(target_path, allowable_ids) {
                if prev_state == AllowableIds::One || prev_state != allowable_ids {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_path",
                        target_path as &str,
                    ));
                }
            }
            if let Some(source) = source {
                if let fsys::Ref::Child(source_child) = source {
                    if source_child.name == child.name {
                        self.errors
                            .push(Error::offer_target_equals_source(decl, &child.name as &str));
                    }
                }
            }
        }
    }

    fn validate_target_child_with_name(
        &mut self,
        decl: &str,
        allowable_names: AllowableIds,
        child: &'a fsys::ChildRef,
        source: Option<&fsys::Ref>,
        target_name: Option<&'a String>,
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
            if let Some(source) = source {
                if let fsys::Ref::Child(source_child) = source {
                    if source_child.name == child.name {
                        self.errors
                            .push(Error::offer_target_equals_source(decl, &child.name as &str));
                    }
                }
            }
        }
    }

    fn validate_target_collection_with_path(
        &mut self,
        decl: &str,
        allowable_ids: AllowableIds,
        collection: &'a fsys::CollectionRef,
        target_path: Option<&'a String>,
    ) {
        if !self.validate_collection_ref(decl, "target", &collection) {
            return;
        }
        if let Some(target_path) = target_path {
            let paths_for_target = self
                .target_ids
                .entry(TargetId::Collection(&collection.name))
                .or_insert(HashMap::new());
            if let Some(prev_state) = paths_for_target.insert(target_path, allowable_ids) {
                if prev_state == AllowableIds::One || prev_state != allowable_ids {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_path",
                        target_path as &str,
                    ));
                }
            }
        }
    }

    fn validate_target_collection_with_name(
        &mut self,
        decl: &str,
        allowable_names: AllowableIds,
        collection: &'a fsys::CollectionRef,
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

    fn validate_storage_target(
        &mut self,
        decl: &str,
        storage_source_name: Option<&'a str>,
        target: Option<&'a fsys::Ref>,
    ) {
        match target {
            Some(fsys::Ref::Child(c)) => {
                if !self.validate_child_ref(decl, "target", &c) {
                    return;
                }
                let name = &c.name;
                if let Some(source_name) = storage_source_name {
                    if self.all_storage_and_sources.get(source_name) == Some(&Some(name)) {
                        self.errors.push(Error::offer_target_equals_source(decl, name));
                    }
                }
            }
            Some(fsys::Ref::Collection(c)) => {
                self.validate_collection_ref(decl, "target", &c);
            }
            Some(_) => self.errors.push(Error::invalid_field(decl, "target")),
            None => self.errors.push(Error::missing_field(decl, "target")),
        }
    }
}

fn check_presence_and_length(
    max_len: usize,
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) {
    match prop {
        Some(prop) if prop.len() == 0 => errors.push(Error::empty_field(decl_type, keyword)),
        Some(prop) if prop.len() > max_len => {
            errors.push(Error::field_too_long(decl_type, keyword))
        }
        Some(_) => (),
        None => errors.push(Error::missing_field(decl_type, keyword)),
    }
}

fn check_path(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_PATH_LENGTH, prop, decl_type, keyword, errors);
    if let Some(path) = prop {
        // Paths must be more than 1 character long
        if path.len() < 2 {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Paths must start with `/`
        if !path.starts_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Paths cannot have two `/`s in a row
        if path.contains("//") {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Paths cannot end with `/`
        if path.ends_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
    }
    start_err_len == errors.len()
}

fn check_name_or_path(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    if let Some(prop) = prop {
        if prop.starts_with('/') {
            check_path(Some(prop), decl_type, keyword, errors);
        } else {
            check_name(Some(prop), decl_type, keyword, errors);
        }
    } else {
        errors.push(Error::missing_field(decl_type, keyword));
    }
    start_err_len == errors.len()
}

fn check_relative_path(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_PATH_LENGTH, prop, decl_type, keyword, errors);
    if let Some(path) = prop {
        // Relative paths must be nonempty
        if path.is_empty() {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Relative paths cannot start with `/`
        if path.starts_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Relative paths cannot have two `/`s in a row
        if path.contains("//") {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Relative paths cannot end with `/`
        if path.ends_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
    }
    start_err_len == errors.len()
}

fn check_name(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_NAME_LENGTH, prop, decl_type, keyword, errors);
    if let Some(name) = prop {
        for b in name.bytes() {
            let b = b as char;
            if b.is_ascii_alphanumeric() || b == '_' || b == '-' || b == '.' {
                // Ok
            } else {
                errors.push(Error::invalid_field(decl_type, keyword));
            }
        }
    }
    start_err_len == errors.len()
}

// TODO: This should probably be checking with the `url` crate
fn check_url(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_URL_LENGTH, prop, decl_type, keyword, errors);
    if let Some(url) = prop {
        let mut chars_iter = url.chars();
        let mut first_char = true;
        while let Some(c) = chars_iter.next() {
            match c {
                '0'..='9' | 'a'..='z' | '+' | '-' | '.' => first_char = false,
                ':' => {
                    if first_char {
                        // There must be at least one character in the schema
                        errors.push(Error::invalid_field(decl_type, keyword));
                        return false;
                    }
                    // Once a `:` character is found, it must be followed by two `/` characters and
                    // then at least one more character. Note that these sequential calls to
                    // `.next()` without checking the result won't panic because `Chars` implements
                    // `FusedIterator`.
                    match (chars_iter.next(), chars_iter.next(), chars_iter.next()) {
                        (Some('/'), Some('/'), Some(_)) => return start_err_len == errors.len(),
                        _ => {
                            errors.push(Error::invalid_field(decl_type, keyword));
                            return false;
                        }
                    }
                }
                _ => {
                    errors.push(Error::invalid_field(decl_type, keyword));
                    return false;
                }
            }
        }
        // If we've reached here then the string terminated unexpectedly
        errors.push(Error::invalid_field(decl_type, keyword));
    }
    start_err_len == errors.len()
}

fn check_url_scheme(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    if let Some(scheme) = prop {
        if let Err(err) = cm_types::UrlScheme::validate(scheme) {
            errors.push(match err {
                cm_types::ParseError::InvalidLength => {
                    if scheme.is_empty() {
                        Error::empty_field(decl_type, keyword)
                    } else {
                        Error::field_too_long(decl_type, keyword)
                    }
                }
                cm_types::ParseError::InvalidValue => Error::invalid_field(decl_type, keyword),
                e => {
                    panic!("unexpected parse error: {:?}", e);
                }
            });
            return false;
        }
    } else {
        errors.push(Error::missing_field(decl_type, keyword));
        return false;
    }
    true
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2::*,
        lazy_static::lazy_static, proptest::prelude::*, regex::Regex,
    };

    const PATH_REGEX_STR: &str = r"(/[^/]+)+";
    const NAME_REGEX_STR: &str = r"[0-9a-zA-Z_\-\.]+";
    const URL_REGEX_STR: &str = r"[0-9a-z\+\-\.]+://.+";

    lazy_static! {
        static ref PATH_REGEX: Regex =
            Regex::new(&("^".to_string() + PATH_REGEX_STR + "$")).unwrap();
        static ref NAME_REGEX: Regex =
            Regex::new(&("^".to_string() + NAME_REGEX_STR + "$")).unwrap();
        static ref URL_REGEX: Regex = Regex::new(&("^".to_string() + URL_REGEX_STR + "$")).unwrap();
    }

    proptest! {
        #[test]
        fn check_path_matches_regex(s in PATH_REGEX_STR) {
            if s.len() < MAX_PATH_LENGTH {
                let mut errors = vec![];
                prop_assert!(check_path(Some(&s), "", "", &mut errors));
                prop_assert!(errors.is_empty());
            }
        }
        #[test]
        fn check_name_matches_regex(s in NAME_REGEX_STR) {
            if s.len() < MAX_NAME_LENGTH {
                let mut errors = vec![];
                prop_assert!(check_name(Some(&s), "", "", &mut errors));
                prop_assert!(errors.is_empty());
            }
        }
        #[test]
        fn check_url_matches_regex(s in URL_REGEX_STR) {
            if s.len() < MAX_URL_LENGTH {
                let mut errors = vec![];
                prop_assert!(check_url(Some(&s), "", "", &mut errors));
                prop_assert!(errors.is_empty());
            }
        }
        #[test]
        fn check_path_fails_invalid_input(s in ".*") {
            if !PATH_REGEX.is_match(&s) {
                let mut errors = vec![];
                prop_assert!(!check_path(Some(&s), "", "", &mut errors));
                prop_assert!(!errors.is_empty());
            }
        }
        #[test]
        fn check_name_fails_invalid_input(s in ".*") {
            if !NAME_REGEX.is_match(&s) {
                let mut errors = vec![];
                prop_assert!(!check_name(Some(&s), "", "", &mut errors));
                prop_assert!(!errors.is_empty());
            }
        }
        #[test]
        fn check_url_fails_invalid_input(s in ".*") {
            if !URL_REGEX.is_match(&s) {
                let mut errors = vec![];
                prop_assert!(!check_url(Some(&s), "", "", &mut errors));
                prop_assert!(!errors.is_empty());
            }
        }
    }

    fn validate_test(input: ComponentDecl, expected_res: Result<(), ErrorList>) {
        let res = validate(&input);
        assert_eq!(res, expected_res);
    }

    fn validate_test_any_result(input: ComponentDecl, expected_res: Vec<Result<(), ErrorList>>) {
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

    fn validate_capabilities_test(input: Vec<CapabilityDecl>, expected_res: Result<(), ErrorList>) {
        let res = validate_capabilities(&input);
        assert_eq!(res, expected_res);
    }

    fn check_test<F>(check_fn: F, input: &str, expected_res: Result<(), ErrorList>)
    where
        F: FnOnce(Option<&String>, &str, &str, &mut Vec<Error>) -> bool,
    {
        let mut errors = vec![];
        let res: Result<(), ErrorList> =
            match check_fn(Some(&input.to_string()), "FooDecl", "foo", &mut errors) {
                true => Ok(()),
                false => Err(ErrorList::new(errors)),
            };
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    }

    fn new_component_decl() -> ComponentDecl {
        ComponentDecl {
            program: None,
            uses: None,
            exposes: None,
            offers: None,
            facets: None,
            capabilities: None,
            children: None,
            collections: None,
            environments: None,
        }
    }

    #[test]
    fn test_errors() {
        assert_eq!(format!("{}", Error::missing_field("Decl", "keyword")), "Decl missing keyword");
        assert_eq!(format!("{}", Error::empty_field("Decl", "keyword")), "Decl has empty keyword");
        assert_eq!(
            format!("{}", Error::duplicate_field("Decl", "keyword", "foo")),
            "\"foo\" is a duplicate Decl keyword"
        );
        assert_eq!(
            format!("{}", Error::invalid_field("Decl", "keyword")),
            "Decl has invalid keyword"
        );
        assert_eq!(
            format!("{}", Error::field_too_long("Decl", "keyword")),
            "Decl's keyword is too long"
        );
        assert_eq!(
            format!("{}", Error::invalid_child("Decl", "source", "child")),
            "\"child\" is referenced in Decl.source but it does not appear in children"
        );
        assert_eq!(
            format!("{}", Error::invalid_collection("Decl", "source", "child")),
            "\"child\" is referenced in Decl.source but it does not appear in collections"
        );
        assert_eq!(
            format!("{}", Error::invalid_storage("Decl", "source", "name")),
            "\"name\" is referenced in Decl.source but it does not appear in storage"
        );
        assert_eq!(
            format!("{}", Error::multiple_runners_specified("Decl")),
            "Decl specifies multiple runners"
        );
    }

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

    macro_rules! test_validate_capabilities {
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
                    validate_capabilities_test($input, $result);
                }
            )+
        }
    }

    macro_rules! test_string_checks {
        (
            $(
                $test_name:ident => {
                    check_fn = $check_fn:expr,
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    check_test($check_fn, $input, $result);
                }
            )+
        }
    }

    macro_rules! test_dependency {
        (
            $(
                $test_name:ident => {
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
                        offer_decl.source = Some(Ref::Child(
                           ChildRef { name: from.to_string(), collection: None },
                        ));
                        offer_decl.target = Some(Ref::Child(
                           ChildRef { name: to.to_string(), collection: None },
                        ));
                        $ty(offer_decl)
                    }).collect();
                    let children = ["a", "b"].iter().map(|name| {
                        ChildDecl {
                            name: Some(name.to_string()),
                            url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                            startup: Some(StartupMode::Lazy),
                            environment: None,
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
                $test_name:ident => {
                    ty = $ty:expr,
                    offer_decl = $offer_decl:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let mut decl = new_component_decl();
                    let offers = vec![
                        {
                            let mut offer_decl = $offer_decl;
                            offer_decl.source = Some(Ref::Child(
                               ChildRef { name: "a".to_string(), collection: None },
                            ));
                            offer_decl.target = Some(Ref::Child(
                               ChildRef { name: "b".to_string(), collection: None },
                            ));
                            offer_decl.dependency_type = Some(DependencyType::Strong);
                            $ty(offer_decl)
                        },
                        {
                            let mut offer_decl = $offer_decl;
                            offer_decl.source = Some(Ref::Child(
                               ChildRef { name: "b".to_string(), collection: None },
                            ));
                            offer_decl.target = Some(Ref::Child(
                               ChildRef { name: "a".to_string(), collection: None },
                            ));
                            offer_decl.dependency_type = Some(DependencyType::WeakForMigration);
                            $ty(offer_decl)
                        },
                    ];
                    let children = ["a", "b"].iter().map(|name| {
                        ChildDecl {
                            name: Some(name.to_string()),
                            url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                            startup: Some(StartupMode::Lazy),
                            environment: None,
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

    test_string_checks! {
        // path
        test_identifier_path_valid => {
            check_fn = check_path,
            input = "/foo/bar",
            result = Ok(()),
        },
        test_identifier_path_invalid_empty => {
            check_fn = check_path,
            input = "",
            result = Err(ErrorList::new(vec![
                Error::empty_field("FooDecl", "foo"),
                Error::invalid_field("FooDecl", "foo"),
            ])),
        },
        test_identifier_path_invalid_root => {
            check_fn = check_path,
            input = "/",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_invalid_relative => {
            check_fn = check_path,
            input = "foo/bar",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_invalid_trailing => {
            check_fn = check_path,
            input = "/foo/bar/",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_too_long => {
            check_fn = check_path,
            input = &format!("/{}", "a".repeat(1024)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },

        // name
        test_identifier_name_valid => {
            check_fn = check_name,
            input = "abcdefghijklmnopqrstuvwxyz0123456789_-.",
            result = Ok(()),
        },
        test_identifier_name_invalid => {
            check_fn = check_name,
            input = "^bad",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_name_too_long => {
            check_fn = check_name,
            input = &format!("{}", "a".repeat(101)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },

        // url
        test_identifier_url_valid => {
            check_fn = check_url,
            input = "my+awesome-scheme.2://abc123!@#$%.com",
            result = Ok(()),
        },
        test_identifier_url_invalid => {
            check_fn = check_url,
            input = "fuchsia-pkg://",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_url_too_long => {
            check_fn = check_url,
            input = &format!("fuchsia-pkg://{}", "a".repeat(4083)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },
    }

    test_validate_any_result! {
        test_validate_use_disallows_nested_dirs => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some("/abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some("/abc".to_string()),
                        target_path: Some("/foo/bar/baz".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectoryDecl", "/foo/bar/baz", "UseDirectoryDecl", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectoryDecl", "/foo/bar", "UseDirectoryDecl", "/foo/bar/baz"),
                ])),
            ],
        },
        test_validate_use_disallows_common_prefixes_protocol => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some("/abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some("/crow".to_string()),
                        target_path: Some("/foo/bar/fuchsia.2".to_string()),
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseProtocolDecl", "/foo/bar/fuchsia.2", "UseDirectoryDecl", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectoryDecl", "/foo/bar", "UseProtocolDecl", "/foo/bar/fuchsia.2"),
                ])),
            ],
        },
        test_validate_use_disallows_common_prefixes_service => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some("/abc".to_string()),
                        target_path: Some("/foo/bar".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_name: Some("space".to_string()),
                        target_path: Some("/foo/bar/baz/fuchsia.logger.Log".to_string()),
                    }),
                ]);
                decl
            },
            results = vec![
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseServiceDecl", "/foo/bar/baz/fuchsia.logger.Log", "UseDirectoryDecl", "/foo/bar"),
                ])),
                Err(ErrorList::new(vec![
                    Error::invalid_path_overlap(
                        "UseDirectoryDecl", "/foo/bar", "UseServiceDecl", "/foo/bar/baz/fuchsia.logger.Log"),
                ])),
            ],
        },
    }

    test_validate! {
        // uses
        test_validate_uses_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: None,
                        source_name: None,
                        target_path: None,
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                        rights: None,
                        subdir: None,
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        source_name: None,
                        target_path: None,
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        source_name: Some("cache".to_string()),
                        target_path: None,
                    }),
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: None,
                    }),
                    UseDecl::Event(UseEventDecl {
                        source: None,
                        source_name: None,
                        target_name: None,
                        filter: None,
                    }),
                    UseDecl::EventStream(UseEventStreamDecl {
                        target_path: None,
                        events: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("UseServiceDecl", "source"),
                Error::missing_field("UseServiceDecl", "source_name"),
                Error::missing_field("UseServiceDecl", "target_path"),
                Error::missing_field("UseProtocolDecl", "source"),
                Error::missing_field("UseProtocolDecl", "source_path"),
                Error::missing_field("UseProtocolDecl", "target_path"),
                Error::missing_field("UseDirectoryDecl", "source"),
                Error::missing_field("UseDirectoryDecl", "source_path"),
                Error::missing_field("UseDirectoryDecl", "target_path"),
                Error::missing_field("UseDirectoryDecl", "rights"),
                Error::missing_field("UseStorageDecl", "source_name"),
                Error::missing_field("UseStorageDecl", "target_path"),
                Error::missing_field("UseStorageDecl", "target_path"),
                Error::missing_field("UseRunnerDecl", "source_name"),
                Error::missing_field("UseEventDecl", "source"),
                Error::missing_field("UseEventDecl", "source_name"),
                Error::missing_field("UseEventDecl", "target_name"),
                Error::missing_field("UseEventStreamDecl", "target_path"),
                Error::missing_field("UseEventStreamDecl", "events"),
            ])),
        },
        test_validate_uses_invalid_identifiers_service => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_name: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseServiceDecl", "source"),
                Error::invalid_field("UseServiceDecl", "source_name"),
                Error::invalid_field("UseServiceDecl", "target_path"),
            ])),
        },
        test_validate_uses_invalid_identifiers_protocol => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseProtocolDecl", "source"),
                Error::invalid_field("UseProtocolDecl", "source_path"),
                Error::invalid_field("UseProtocolDecl", "target_path"),
            ])),
        },
        test_validate_uses_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("/foo".to_string()),
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        source_name: Some("/cache".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        source_name: Some("temp".to_string()),
                        target_path: Some("tmp".to_string()),
                    }),
                    UseDecl::Event(UseEventDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_name: Some("/foo".to_string()),
                        target_name: Some("/foo".to_string()),
                        filter: Some(fdata::Dictionary { entries: None }),
                    }),
                    UseDecl::EventStream(UseEventStreamDecl {
                        target_path: Some("/bar".to_string()),
                        events: Some(vec!["/a".to_string(), "/b".to_string()]),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseDirectoryDecl", "source"),
                Error::invalid_field("UseDirectoryDecl", "source_path"),
                Error::invalid_field("UseDirectoryDecl", "target_path"),
                Error::invalid_field("UseDirectoryDecl", "subdir"),
                Error::invalid_field("UseStorageDecl", "source_name"),
                Error::invalid_field("UseStorageDecl", "target_path"),
                Error::invalid_field("UseStorageDecl", "target_path"),
                Error::invalid_field("UseEventDecl", "source"),
                Error::invalid_field("UseEventDecl", "source_name"),
                Error::invalid_field("UseEventDecl", "target_name"),
                Error::invalid_field("UseEventStreamDecl", "event_name"),
                Error::invalid_event_stream("UseEventStreamDecl", "events", "/a".to_string()),
                Error::invalid_field("UseEventStreamDecl", "event_name"),
                Error::invalid_event_stream("UseEventStreamDecl", "events", "/b".to_string()),
            ])),
        },
        test_validate_has_events_in_event_stream => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::EventStream(UseEventStreamDecl {
                        target_path: Some("/bar".to_string()),
                        events: None,
                    }),
                    UseDecl::EventStream(UseEventStreamDecl {
                        target_path: Some("/barbar".to_string()),
                        events: Some(vec![]),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("UseEventStreamDecl", "events"),
                Error::empty_field("UseEventStreamDecl", "events"),
            ])),
        },
        test_validate_uses_multiple_runners => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: Some("elf".to_string()),
                    }),
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::multiple_runners_specified("UseRunnerDecl"),
            ])),
        },
        test_validate_uses_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_path: Some(format!("/s/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/p/{}", "c".repeat(1024))),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/d/{}", "d".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        source_name: Some("cache".to_string()),
                        target_path: Some(format!("/{}", "e".repeat(1024))),
                    }),
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: Some(format!("{}", "a".repeat(101))),
                    }),
                    UseDecl::Event(UseEventDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_name: Some(format!("{}", "a".repeat(101))),
                        filter: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("UseServiceDecl", "source_name"),
                Error::field_too_long("UseServiceDecl", "target_path"),
                Error::field_too_long("UseProtocolDecl", "source_path"),
                Error::field_too_long("UseProtocolDecl", "target_path"),
                Error::field_too_long("UseDirectoryDecl", "source_path"),
                Error::field_too_long("UseDirectoryDecl", "target_path"),
                Error::field_too_long("UseStorageDecl", "target_path"),
                Error::field_too_long("UseRunnerDecl", "source_name"),
                Error::field_too_long("UseEventDecl", "source_name"),
                Error::field_too_long("UseEventDecl", "target_name"),
            ])),
        },
        test_validate_conflicting_paths => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_name: Some("foo".to_string()),
                        target_path: Some("/bar".to_string()),
                    }),
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_name: Some("space".to_string()),
                        target_path: Some("/bar".to_string()),
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some("/space".to_string()),
                        target_path: Some("/bar".to_string()),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                        source_path: Some("/crow".to_string()),
                        target_path: Some("/bar".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("UseServiceDecl", "path", "/bar"),
                Error::duplicate_field("UseProtocolDecl", "path", "/bar"),
                Error::duplicate_field("UseDirectoryDecl", "path", "/bar"),
            ])),
        },

        // exposes
        test_validate_exposes_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: None,
                        source_name: None,
                        target_name: None,
                        target: None,
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                        target: None,
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                        target: None,
                        rights: None,
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeServiceDecl", "source"),
                Error::missing_field("ExposeServiceDecl", "target"),
                Error::missing_field("ExposeServiceDecl", "source_name"),
                Error::missing_field("ExposeServiceDecl", "target_name"),
                Error::missing_field("ExposeProtocolDecl", "source"),
                Error::missing_field("ExposeProtocolDecl", "target"),
                Error::missing_field("ExposeProtocolDecl", "source_path"),
                Error::missing_field("ExposeProtocolDecl", "target_path"),
                Error::missing_field("ExposeDirectoryDecl", "source"),
                Error::missing_field("ExposeDirectoryDecl", "target"),
                Error::missing_field("ExposeDirectoryDecl", "source_path"),
                Error::missing_field("ExposeDirectoryDecl", "target_path"),
                Error::missing_field("ExposeRunnerDecl", "source"),
                Error::missing_field("ExposeRunnerDecl", "target"),
                Error::missing_field("ExposeRunnerDecl", "source_name"),
                Error::missing_field("ExposeRunnerDecl", "target_name"),
                Error::missing_field("ExposeResolverDecl", "source"),
                Error::missing_field("ExposeResolverDecl", "target"),
                Error::missing_field("ExposeResolverDecl", "source_name"),
                Error::missing_field("ExposeResolverDecl", "target_name"),
            ])),
        },
        test_validate_exposes_extraneous => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("logger".to_string()),
                        target_name: Some("logger".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/data".to_string()),
                        target_path: Some("/data".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("pkg".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("ExposeServiceDecl", "source.child.collection"),
                Error::extraneous_field("ExposeProtocolDecl", "source.child.collection"),
                Error::extraneous_field("ExposeDirectoryDecl", "source.child.collection"),
                Error::extraneous_field("ExposeRunnerDecl", "source.child.collection"),
                Error::extraneous_field("ExposeResolverDecl", "source.child.collection"),
            ])),
        },
        test_validate_exposes_rights => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/a".to_string()),
                        target_path: Some("/data/b".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        rights: None,
                        subdir: None,
                    })
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeDirectoryDecl", "rights"),
            ])),
        },
        test_validate_exposes_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target_name: Some("/".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("/foo".to_string()),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("elf!".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("pkg!".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ExposeServiceDecl", "source.child.name"),
                Error::invalid_field("ExposeServiceDecl", "source_name"),
                Error::invalid_field("ExposeServiceDecl", "target_name"),
                Error::invalid_field("ExposeProtocolDecl", "source.child.name"),
                Error::invalid_field("ExposeProtocolDecl", "source_path"),
                Error::invalid_field("ExposeProtocolDecl", "target_path"),
                Error::invalid_field("ExposeDirectoryDecl", "source.child.name"),
                Error::invalid_field("ExposeDirectoryDecl", "source_path"),
                Error::invalid_field("ExposeDirectoryDecl", "target_path"),
                Error::invalid_field("ExposeDirectoryDecl", "subdir"),
                Error::invalid_field("ExposeRunnerDecl", "source.child.name"),
                Error::invalid_field("ExposeRunnerDecl", "source_name"),
                Error::invalid_field("ExposeRunnerDecl", "target_name"),
                Error::invalid_field("ExposeResolverDecl", "source.child.name"),
                Error::invalid_field("ExposeResolverDecl", "source_name"),
                Error::invalid_field("ExposeResolverDecl", "target_name"),
            ])),
        },
        test_validate_exposes_invalid_source_target => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("logger".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(StartupMode::Lazy),
                    environment: None,
                }]);
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: None,
                        source_name: Some("a".to_string()),
                        target_name: Some("b".to_string()),
                        target: None,
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_path: Some("/c".to_string()),
                        target_path: Some("/d".to_string()),
                        target: Some(Ref::Self_(SelfRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Collection(CollectionRef {name: "z".to_string()})),
                        source_path: Some("/e".to_string()),
                        target_path: Some("/f".to_string()),
                        target: Some(Ref::Collection(CollectionRef {name: "z".to_string()})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_path: Some("/g".to_string()),
                        target_path: Some("/h".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_name: Some("c".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        target_name: Some("d".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_name: Some("e".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        target_name: Some("f".to_string()),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/i".to_string()),
                        target_path: Some("/j".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeServiceDecl", "source"),
                Error::missing_field("ExposeServiceDecl", "target"),
                Error::invalid_field("ExposeProtocolDecl", "source"),
                Error::invalid_field("ExposeProtocolDecl", "target"),
                Error::invalid_field("ExposeDirectoryDecl", "source"),
                Error::invalid_field("ExposeDirectoryDecl", "target"),
                Error::invalid_field("ExposeDirectoryDecl", "source"),
                Error::invalid_field("ExposeDirectoryDecl", "target"),
                Error::invalid_field("ExposeRunnerDecl", "source"),
                Error::invalid_field("ExposeRunnerDecl", "target"),
                Error::invalid_field("ExposeResolverDecl", "source"),
                Error::invalid_field("ExposeResolverDecl", "target"),
                Error::invalid_field("ExposeDirectoryDecl", "target"),
            ])),
        },
        test_validate_exposes_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(1025))),
                        target_name: Some(format!("{}", "b".repeat(1025))),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Parent(ParentRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("a".repeat(101)),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("b".repeat(101)),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("a".repeat(101)),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("b".repeat(101)),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ExposeServiceDecl", "source.child.name"),
                Error::field_too_long("ExposeServiceDecl", "source_name"),
                Error::field_too_long("ExposeServiceDecl", "target_name"),
                Error::field_too_long("ExposeProtocolDecl", "source.child.name"),
                Error::field_too_long("ExposeProtocolDecl", "source_path"),
                Error::field_too_long("ExposeProtocolDecl", "target_path"),
                Error::field_too_long("ExposeDirectoryDecl", "source.child.name"),
                Error::field_too_long("ExposeDirectoryDecl", "source_path"),
                Error::field_too_long("ExposeDirectoryDecl", "target_path"),
                Error::field_too_long("ExposeRunnerDecl", "source.child.name"),
                Error::field_too_long("ExposeRunnerDecl", "source_name"),
                Error::field_too_long("ExposeRunnerDecl", "target_name"),
                Error::field_too_long("ExposeResolverDecl", "source.child.name"),
                Error::field_too_long("ExposeResolverDecl", "source_name"),
                Error::field_too_long("ExposeResolverDecl", "target_name"),
            ])),
        },
        test_validate_exposes_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                        target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/data/netstack".to_string()),
                        target_path: Some("/data".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("pkg".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("ExposeServiceDecl", "source", "netstack"),
                Error::invalid_child("ExposeProtocolDecl", "source", "netstack"),
                Error::invalid_child("ExposeDirectoryDecl", "source", "netstack"),
                Error::invalid_child("ExposeRunnerDecl", "source", "netstack"),
                Error::invalid_child("ExposeResolverDecl", "source", "netstack"),
            ])),
        },
        test_validate_exposes_duplicate_target => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("netstack".to_string()),
                        target_name: Some("fuchsia.net.Stack".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("netstack2".to_string()),
                        target_name: Some("fuchsia.net.Stack".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("fonts".to_string()),
                        target_path: Some("fuchsia.fonts.Provider".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("fonts2".to_string()),
                        target_path: Some("fuchsia.fonts.Provider".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("assets".to_string()),
                        target_path: Some("stuff".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        rights: None,
                        subdir: None,
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("assets2".to_string()),
                        target_path: Some("stuff".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        rights: None,
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("pkg".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("pkg".to_string()),
                    }),
                ]);
                decl.capabilities = Some(vec![
                    CapabilityDecl::Service(ServiceDecl {
                        name: Some("netstack".to_string()),
                        source_path: Some("/path".to_string()),
                    }),
                    CapabilityDecl::Service(ServiceDecl {
                        name: Some("netstack2".to_string()),
                        source_path: Some("/path".to_string()),
                    }),
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: Some("fonts".to_string()),
                        source_path: Some("/path".to_string()),
                    }),
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: Some("fonts2".to_string()),
                        source_path: Some("/path".to_string()),
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("assets".to_string()),
                        source_path: Some("/path".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("assets2".to_string()),
                        source_path: Some("/path".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    CapabilityDecl::Runner(RunnerDecl {
                        name: Some("source_elf".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/path".to_string()),
                    }),
                    CapabilityDecl::Resolver(ResolverDecl {
                        name: Some("source_pkg".to_string()),
                        source_path: Some("/path".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                // Duplicate services are allowed.
                Error::duplicate_field("ExposeProtocolDecl", "target_path",
                                       "fuchsia.fonts.Provider"),
                Error::duplicate_field("ExposeDirectoryDecl", "target_path",
                                       "stuff"),
                Error::duplicate_field("ExposeRunnerDecl", "target_name",
                                       "elf"),
                Error::duplicate_field("ExposeResolverDecl", "target_name", "pkg"),
            ])),
        },
        // TODO: Add analogous test for offer
        test_validate_exposes_invalid_capability_from_self => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("fuchsia.netstack.Netstack".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("foo".to_string()),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("fuchsia.netstack.Netstack".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_path: Some("bar".to_string()),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("dir".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_path: Some("assets".to_string()),
                        rights: None,
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("pkg".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("ExposeServiceDecl", "source", "fuchsia.netstack.Netstack"),
                Error::invalid_capability("ExposeProtocolDecl", "source", "fuchsia.netstack.Netstack"),
                Error::invalid_capability("ExposeDirectoryDecl", "source", "dir"),
                Error::invalid_capability("ExposeRunnerDecl", "source", "source_elf"),
                Error::invalid_capability("ExposeResolverDecl", "source", "source_pkg"),
            ])),
        },
        test_validate_exposes_invalid_subdir => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef {})),
                        source_path: Some("/foo".to_string()),
                        target_path: Some("/foo".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("bar".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ExposeDirectoryDecl", "subdir"),
            ])),
        },

        // offers
        test_validate_offers_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: None,
                        source_path: None,
                        target: None,
                        target_path: None,
                        dependency_type: None,
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: None,
                        source_path: None,
                        target: None,
                        target_path: None,
                        rights: None,
                        subdir: None,
                        dependency_type: None,
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        source_name: None,
                        source: None,
                        target: None,
                        target_name: None,
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                        filter: None,
                    })
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("OfferServiceDecl", "source"),
                Error::missing_field("OfferServiceDecl", "source_name"),
                Error::missing_field("OfferServiceDecl", "target"),
                Error::missing_field("OfferServiceDecl", "target_name"),
                Error::missing_field("OfferProtocolDecl", "source"),
                Error::missing_field("OfferProtocolDecl", "source_path"),
                Error::missing_field("OfferProtocolDecl", "target"),
                Error::missing_field("OfferProtocolDecl", "target_path"),
                Error::missing_field("OfferProtocolDecl", "dependency_type"),
                Error::missing_field("OfferDirectoryDecl", "source"),
                Error::missing_field("OfferDirectoryDecl", "source_path"),
                Error::missing_field("OfferDirectoryDecl", "target"),
                Error::missing_field("OfferDirectoryDecl", "target_path"),
                Error::missing_field("OfferDirectoryDecl", "dependency_type"),
                Error::missing_field("OfferStorageDecl", "source_name"),
                Error::missing_field("OfferStorageDecl", "source"),
                Error::missing_field("OfferStorageDecl", "target"),
                Error::missing_field("OfferRunnerDecl", "source"),
                Error::missing_field("OfferRunnerDecl", "source_name"),
                Error::missing_field("OfferRunnerDecl", "target"),
                Error::missing_field("OfferRunnerDecl", "target_name"),
                Error::missing_field("OfferEventDecl", "source_name"),
                Error::missing_field("OfferEventDecl", "source"),
                Error::missing_field("OfferEventDecl", "target"),
                Error::missing_field("OfferEventDecl", "target_name"),
            ])),
        },
        test_validate_offers_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "b".repeat(101),
                               collection: None,
                           }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_name: Some("a".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "b".repeat(101),
                           }
                        )),
                        target_name: Some(format!("{}", "b".repeat(101))),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "b".repeat(101),
                               collection: None,
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Self_(SelfRef {})),
                        source_path: Some("/a".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "b".repeat(101),
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "b".repeat(101),
                               collection: None,
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef {})),
                        source_path: Some("/a".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "b".repeat(101),
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        source_name: Some("data".to_string()),
                        source: Some(Ref::Parent(ParentRef {})),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "b".repeat(101),
                                collection: None,
                            }
                        )),
                        target_name: Some("data".to_string()),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        source_name: Some("data".to_string()),
                        source: Some(Ref::Parent(ParentRef {})),
                        target: Some(Ref::Collection(
                            CollectionRef { name: "b".repeat(101) }
                        )),
                        target_name: Some("data".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("b".repeat(101)),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "c".repeat(101),
                           }
                        )),
                        target_name: Some("d".repeat(101)),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("b".repeat(101)),
                        target: Some(Ref::Collection(
                            CollectionRef {
                                name: "c".repeat(101),
                            }
                        )),
                        target_name: Some("d".repeat(101)),
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None
                        })),
                        target_name: Some(format!("{}", "a".repeat(101))),
                        filter: Some(fdata::Dictionary { entries: None }),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("OfferServiceDecl", "source.child.name"),
                Error::field_too_long("OfferServiceDecl", "source_name"),
                Error::field_too_long("OfferServiceDecl", "target.child.name"),
                Error::field_too_long("OfferServiceDecl", "target_name"),
                Error::field_too_long("OfferServiceDecl", "target.collection.name"),
                Error::field_too_long("OfferServiceDecl", "target_name"),
                Error::field_too_long("OfferProtocolDecl", "source.child.name"),
                Error::field_too_long("OfferProtocolDecl", "source_path"),
                Error::field_too_long("OfferProtocolDecl", "target.child.name"),
                Error::field_too_long("OfferProtocolDecl", "target_path"),
                Error::field_too_long("OfferProtocolDecl", "target.collection.name"),
                Error::field_too_long("OfferProtocolDecl", "target_path"),
                Error::field_too_long("OfferDirectoryDecl", "source.child.name"),
                Error::field_too_long("OfferDirectoryDecl", "source_path"),
                Error::field_too_long("OfferDirectoryDecl", "target.child.name"),
                Error::field_too_long("OfferDirectoryDecl", "target_path"),
                Error::field_too_long("OfferDirectoryDecl", "target.collection.name"),
                Error::field_too_long("OfferDirectoryDecl", "target_path"),
                Error::field_too_long("OfferStorageDecl", "target.child.name"),
                Error::field_too_long("OfferStorageDecl", "target.collection.name"),
                Error::field_too_long("OfferRunnerDecl", "source.child.name"),
                Error::field_too_long("OfferRunnerDecl", "source_name"),
                Error::field_too_long("OfferRunnerDecl", "target.collection.name"),
                Error::field_too_long("OfferRunnerDecl", "target_name"),
                Error::field_too_long("OfferResolverDecl", "source.child.name"),
                Error::field_too_long("OfferResolverDecl", "source_name"),
                Error::field_too_long("OfferResolverDecl", "target.collection.name"),
                Error::field_too_long("OfferResolverDecl", "target_name"),
                Error::field_too_long("OfferEventDecl", "source_name"),
                Error::field_too_long("OfferEventDecl", "target.child.name"),
                Error::field_too_long("OfferEventDecl", "target_name"),
            ])),
        },
        test_validate_offers_rights => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: None,
                        subdir: None,
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("assets".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("assets".to_string()),
                        rights: None,
                        subdir: None,
                        dependency_type: Some(DependencyType::Strong),
                    }),
                ]);
                decl.capabilities = Some(vec![
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("assets".to_string()),
                        source_path: Some("/data/assets".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                ]);
                decl.children = Some(vec![ChildDecl{
                    name: Some("logger".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(StartupMode::Lazy),
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("OfferDirectoryDecl", "rights"),
            ])),
        },
        test_validate_offers_extraneous => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("fuchsia.logger.Log".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("assets".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("assets".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        source_name: Some("data".to_string()),
                        source: Some(Ref::Parent(ParentRef{ })),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("data".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("elf".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("pkg".to_string()),
                    }),
                ]);
                decl.capabilities = Some(vec![
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: Some("fuchsia.logger.Log".to_string()),
                        source_path: Some("/svc/logger".to_string()),
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("assets".to_string()),
                        source_path: Some("/data/assets".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("OfferServiceDecl", "source.child.collection"),
                Error::extraneous_field("OfferServiceDecl", "target.child.collection"),
                Error::extraneous_field("OfferProtocolDecl", "source.child.collection"),
                Error::extraneous_field("OfferProtocolDecl", "target.child.collection"),
                Error::extraneous_field("OfferDirectoryDecl", "source.child.collection"),
                Error::extraneous_field("OfferDirectoryDecl", "target.child.collection"),
                Error::extraneous_field("OfferStorageDecl", "target.child.collection"),
                Error::extraneous_field("OfferRunnerDecl", "source.child.collection"),
                Error::extraneous_field("OfferRunnerDecl", "target.child.collection"),
                Error::extraneous_field("OfferResolverDecl", "source.child.collection"),
                Error::extraneous_field("OfferResolverDecl", "target.child.collection"),
            ])),
        },
        test_validate_offers_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("foo/".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("/".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_path: Some("/".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_path: Some("/".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("/foo".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("elf!".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("pkg!".to_string()),
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_name: Some("/path".to_string()),
                        filter: Some(fdata::Dictionary { entries: None }),
                    })
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("OfferServiceDecl", "source.child.name"),
                Error::invalid_field("OfferServiceDecl", "source_name"),
                Error::invalid_field("OfferServiceDecl", "target.child.name"),
                Error::invalid_field("OfferServiceDecl", "target_name"),
                Error::invalid_field("OfferProtocolDecl", "source.child.name"),
                Error::invalid_field("OfferProtocolDecl", "source_path"),
                Error::invalid_field("OfferProtocolDecl", "target.child.name"),
                Error::invalid_field("OfferProtocolDecl", "target_path"),
                Error::invalid_field("OfferDirectoryDecl", "source.child.name"),
                Error::invalid_field("OfferDirectoryDecl", "source_path"),
                Error::invalid_field("OfferDirectoryDecl", "target.child.name"),
                Error::invalid_field("OfferDirectoryDecl", "target_path"),
                Error::invalid_field("OfferDirectoryDecl", "subdir"),
                Error::invalid_field("OfferRunnerDecl", "source.child.name"),
                Error::invalid_field("OfferRunnerDecl", "source_name"),
                Error::invalid_field("OfferRunnerDecl", "target.child.name"),
                Error::invalid_field("OfferRunnerDecl", "target_name"),
                Error::invalid_field("OfferResolverDecl", "source.child.name"),
                Error::invalid_field("OfferResolverDecl", "source_name"),
                Error::invalid_field("OfferResolverDecl", "target.child.name"),
                Error::invalid_field("OfferResolverDecl", "target_name"),
                Error::invalid_field("OfferEventDecl", "source_name"),
                Error::invalid_field("OfferEventDecl", "target.child.name"),
                Error::invalid_field("OfferEventDecl", "target_name"),
            ])),
        },
        test_validate_offers_target_equals_source => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("logger".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("legacy_logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("legacy_logger".to_string()),
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("assets".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("assets".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("web".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("web".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("pkg".to_string()),
                    }),
                ]);
                decl.children = Some(vec![ChildDecl{
                    name: Some("logger".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(StartupMode::Lazy),
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::offer_target_equals_source("OfferServiceDecl", "logger"),
                Error::offer_target_equals_source("OfferProtocolDecl", "logger"),
                Error::offer_target_equals_source("OfferDirectoryDecl", "logger"),
                Error::offer_target_equals_source("OfferRunnerDecl", "logger"),
                Error::offer_target_equals_source("OfferResolverDecl", "logger"),
            ])),
        },
        test_validate_offers_storage_target_equals_source => {
            input = ComponentDecl {
                offers: Some(vec![
                    OfferDecl::Storage(OfferStorageDecl {
                        source_name: Some("data".to_string()),
                        source: Some(Ref::Self_(SelfRef { })),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("data".to_string()),
                    })
                ]),
                capabilities: Some(vec![
                    CapabilityDecl::Storage(StorageDecl {
                        name: Some("data".to_string()),
                        source_path: Some("/minfs".to_string()),
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        subdir: None,
                    }),
                ]),
                children: Some(vec![
                    ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(StartupMode::Lazy),
                        environment: None,
                    },
                ]),
                ..new_component_decl()
            },
            result = Err(ErrorList::new(vec![
                Error::offer_target_equals_source("OfferStorageDecl", "logger"),
            ])),
        },
        test_validate_offers_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("assets".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                ]);
                decl.capabilities = Some(vec![
                    CapabilityDecl::Storage(StorageDecl {
                        name: Some("memfs".to_string()),
                        source_path: Some("/memfs".to_string()),
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        subdir: None,
                    }),
                ]);
                decl.children = Some(vec![
                    ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(StartupMode::Lazy),
                        environment: None,
                    },
                ]);
                decl.collections = Some(vec![
                    CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(Durability::Persistent),
                        environment: None,
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("StorageDecl", "source", "logger"),
                Error::invalid_child("OfferServiceDecl", "source", "logger"),
                Error::invalid_child("OfferProtocolDecl", "source", "logger"),
                Error::invalid_child("OfferDirectoryDecl", "source", "logger"),
            ])),
        },
        test_validate_offers_target => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("logger2".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_path: Some("fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_path: Some("fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_path: Some("assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("assets".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_path: Some("assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("assets".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_name: Some("stopped".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        target_name: Some("started".to_string()),
                        filter: None,
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source: Some(Ref::Parent(ParentRef {})),
                        source_name: Some("started_on_x".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        target_name: Some("started".to_string()),
                        filter: None,
                    }),
                ]);
                decl.children = Some(vec![
                    ChildDecl{
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(StartupMode::Eager),
                        environment: None,
                    },
                ]);
                decl.collections = Some(vec![
                    CollectionDecl{
                        name: Some("modular".to_string()),
                        durability: Some(Durability::Persistent),
                        environment: None,
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                // Duplicate services are allowed.
                Error::duplicate_field("OfferProtocolDecl", "target_path", "fuchsia.logger.LegacyLog"),
                Error::duplicate_field("OfferDirectoryDecl", "target_path", "assets"),
                Error::duplicate_field("OfferRunnerDecl", "target_name", "duplicated"),
                Error::duplicate_field("OfferResolverDecl", "target_name", "duplicated"),
                Error::duplicate_field("OfferEventDecl", "target_name", "started"),
            ])),
        },
        test_validate_offers_target_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("logger".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        source_name: Some("data".to_string()),
                        source: Some(Ref::Parent(ParentRef{})),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("data".to_string()),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        source_name: Some("data".to_string()),
                        source: Some(Ref::Parent(ParentRef{})),
                        target: Some(Ref::Collection(
                            CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("data".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("elf".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("elf".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("pkg".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Parent(ParentRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("pkg".to_string()),
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source_name: Some("started".to_string()),
                        source: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("started".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        filter: None,
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source_name: Some("started".to_string()),
                        source: Some(Ref::Parent(ParentRef {})),
                        target_name: Some("started".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        filter: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("OfferServiceDecl", "target", "netstack"),
                Error::invalid_collection("OfferServiceDecl", "target", "modular"),
                Error::invalid_child("OfferProtocolDecl", "target", "netstack"),
                Error::invalid_collection("OfferProtocolDecl", "target", "modular"),
                Error::invalid_child("OfferDirectoryDecl", "target", "netstack"),
                Error::invalid_collection("OfferDirectoryDecl", "target", "modular"),
                Error::invalid_child("OfferStorageDecl", "target", "netstack"),
                Error::invalid_collection("OfferStorageDecl", "target", "modular"),
                Error::invalid_child("OfferRunnerDecl", "target", "netstack"),
                Error::invalid_collection("OfferRunnerDecl", "target", "modular"),
                Error::invalid_child("OfferResolverDecl", "target", "netstack"),
                Error::invalid_collection("OfferResolverDecl", "target", "modular"),
                Error::invalid_child("OfferEventDecl", "target", "netstack"),
                Error::invalid_collection("OfferEventDecl", "target", "modular"),
            ])),
        },
        test_validate_offers_event_from_realm => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(
                    vec![
                        Ref::Self_(SelfRef {}),
                        Ref::Child(ChildRef {name: "netstack".to_string(), collection: None }),
                        Ref::Collection(CollectionRef {name: "modular".to_string() }),
                    ]
                    .into_iter()
                    .enumerate()
                    .map(|(i, source)| {
                        OfferDecl::Event(OfferEventDecl {
                            source: Some(source),
                            source_name: Some("started".to_string()),
                            target: Some(Ref::Child(ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            })),
                            target_name: Some(format!("started_{}", i)),
                            filter: Some(fdata::Dictionary { entries: None }),
                        })
                    })
                    .collect());
                decl.children = Some(vec![
                    ChildDecl{
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(StartupMode::Eager),
                        environment: None,
                    },
                ]);
                decl.collections = Some(vec![
                    CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(Durability::Persistent),
                        environment: None,
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("OfferEventDecl", "source"),
                Error::invalid_field("OfferEventDecl", "source"),
                Error::invalid_field("OfferEventDecl", "source"),
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
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(
                           ChildRef { name: from.to_string(), collection: None },
                        )),
                        source_path: Some(format!("/svc/thing_{}", from)),
                        target: Some(Ref::Child(
                           ChildRef { name: to.to_string(), collection: None },
                        )),
                        target_path: Some(format!("/svc/thing_{}", from)),
                        dependency_type: Some(DependencyType::Strong),
                    })).collect();
                let children = ["a", "b", "c", "d"].iter().map(|name| {
                    ChildDecl {
                        name: Some(name.to_string()),
                        url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                        startup: Some(StartupMode::Lazy),
                        environment: None,
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

        // environments
        test_validate_environment_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: None,
                    extends: None,
                    runners: None,
                    resolvers: None,
                    stop_timeout_ms: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("EnvironmentDecl", "name"),
                Error::missing_field("EnvironmentDecl", "extends"),
            ])),
        },

        test_validate_environment_no_stop_timeout => {
            input = {  let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("env".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: None,
                    resolvers: None,
                    stop_timeout_ms: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![Error::missing_field("EnvironmentDecl", "stop_timeout_ms")])),
        },

        test_validate_environment_extends_stop_timeout => {
            input = {  let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("env".to_string()),
                    extends: Some(EnvironmentExtends::Realm),
                    runners: None,
                    resolvers: None,
                    stop_timeout_ms: None,
                }]);
                decl
            },
            result = Ok(()),
        },
        test_validate_environment_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".repeat(1025)),
                    extends: Some(EnvironmentExtends::None),
                    runners: Some(vec![
                        RunnerRegistration {
                            source_name: Some("a".repeat(101)),
                            source: Some(Ref::Parent(ParentRef{})),
                            target_name: Some("a".repeat(101)),
                        },
                    ]),
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("a".repeat(101)),
                            source: Some(Ref::Parent(ParentRef{})),
                            scheme: Some("a".repeat(101)),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("EnvironmentDecl", "name"),
                Error::field_too_long("RunnerRegistration", "source_name"),
                Error::field_too_long("RunnerRegistration", "target_name"),
                Error::field_too_long("ResolverRegistration", "resolver"),
                Error::field_too_long("ResolverRegistration", "scheme"),
            ])),
        },
        test_validate_environment_empty_runner_resolver_fields => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: Some(vec![
                        RunnerRegistration {
                            source_name: None,
                            source: None,
                            target_name: None,
                        },
                    ]),
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: None,
                            source: None,
                            scheme: None,
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
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
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: Some(vec![
                        RunnerRegistration {
                            source_name: Some("^a".to_string()),
                            source: Some(Ref::Framework(fsys::FrameworkRef{})),
                            target_name: Some("%a".to_string()),
                        },
                    ]),
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("^a".to_string()),
                            source: Some(Ref::Framework(fsys::FrameworkRef{})),
                            scheme: Some("9scheme".to_string()),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
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
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: Some(vec![
                        RunnerRegistration {
                            source_name: Some("dart".to_string()),
                            source: Some(Ref::Self_(SelfRef{})),
                            target_name: Some("dart".to_string()),
                        },
                    ]),
                    resolvers: None,
                    stop_timeout_ms: Some(1234),
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
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: Some(vec![
                        RunnerRegistration {
                            source_name: Some("dart".to_string()),
                            source: Some(Ref::Parent(ParentRef{})),
                            target_name: Some("dart".to_string()),
                        },
                        RunnerRegistration {
                            source_name: Some("other-dart".to_string()),
                            source: Some(Ref::Parent(ParentRef{})),
                            target_name: Some("dart".to_string()),
                        },
                    ]),
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(Ref::Parent(ParentRef{})),
                            scheme: Some("fuchsia-pkg".to_string()),
                        },
                        ResolverRegistration {
                            resolver: Some("base_resolver".to_string()),
                            source: Some(Ref::Parent(ParentRef{})),
                            scheme: Some("fuchsia-pkg".to_string()),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
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
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: Some(vec![
                        RunnerRegistration {
                            source_name: Some("elf".to_string()),
                            source: Some(Ref::Child(ChildRef{
                                name: "missing".to_string(),
                                collection: None,
                            })),
                            target_name: Some("elf".to_string()),
                        },
                    ]),
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(Ref::Child(ChildRef{
                                name: "missing".to_string(),
                                collection: None,
                            })),
                            scheme: Some("fuchsia-pkg".to_string()),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
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
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("env".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: Some(vec![
                        RunnerRegistration {
                            source_name: Some("elf".to_string()),
                            source: Some(Ref::Child(ChildRef{
                                name: "child".to_string(),
                                collection: None,
                            })),
                            target_name: Some("elf".to_string()),
                        },
                    ]),
                    resolvers: None,
                    stop_timeout_ms: Some(1234),
                }]);
                decl.children = Some(vec![ChildDecl {
                    name: Some("child".to_string()),
                    startup: Some(StartupMode::Lazy),
                    url: Some("fuchsia-pkg://child".to_string()),
                    environment: Some("env".to_string()),
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
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("env".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: None,
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(Ref::Child(ChildRef{
                                name: "child".to_string(),
                                collection: None,
                            })),
                            scheme: Some("fuchsia-pkg".to_string()),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                }]);
                decl.children = Some(vec![ChildDecl {
                    name: Some("child".to_string()),
                    startup: Some(StartupMode::Lazy),
                    url: Some("fuchsia-pkg://child".to_string()),
                    environment: Some("env".to_string()),
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
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("env".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    runners: None,
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(Ref::Child(ChildRef{
                                name: "a".to_string(),
                                collection: None,
                            })),
                            scheme: Some("fuchsia-pkg".to_string()),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                }]);
                decl.children = Some(vec![
                    ChildDecl {
                        name: Some("a".to_string()),
                        startup: Some(StartupMode::Lazy),
                        url: Some("fuchsia-pkg://child-a".to_string()),
                        environment: None,
                    },
                    ChildDecl {
                        name: Some("b".to_string()),
                        startup: Some(StartupMode::Lazy),
                        url: Some("fuchsia-pkg://child-b".to_string()),
                        environment: Some("env".to_string()),
                    },
                ]);
                decl.offers = Some(vec![OfferDecl::Service(OfferServiceDecl {
                    source: Some(Ref::Child(ChildRef {
                        name: "b".to_string(),
                        collection: None,
                    })),
                    source_name: Some("thing".to_string()),
                    target: Some(Ref::Child(
                       ChildRef {
                           name: "a".to_string(),
                           collection: None,
                       }
                    )),
                    target_name: Some("thing".to_string()),
                })]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::dependency_cycle(
                    directed_graph::Error::CyclesDetected([vec!["child a", "environment env", "child b", "child a"]].iter().cloned().collect()).format_cycle()
                ),
            ])),
        },

        // children
        test_validate_children_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: None,
                    url: None,
                    startup: None,
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ChildDecl", "name"),
                Error::missing_field("ChildDecl", "url"),
                Error::missing_field("ChildDecl", "startup"),
            ])),
        },
        test_validate_children_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("^bad".to_string()),
                    url: Some("bad-scheme&://blah".to_string()),
                    startup: Some(StartupMode::Lazy),
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ChildDecl", "name"),
                Error::invalid_field("ChildDecl", "url"),
            ])),
        },
        test_validate_children_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("a".repeat(1025)),
                    url: Some(format!("fuchsia-pkg://{}", "a".repeat(4083))),
                    startup: Some(StartupMode::Lazy),
                    environment: Some("a".repeat(1025)),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ChildDecl", "name"),
                Error::field_too_long("ChildDecl", "url"),
                Error::field_too_long("ChildDecl", "environment"),
                Error::invalid_environment("ChildDecl", "environment", "a".repeat(1025)),
            ])),
        },
        test_validate_child_references_unknown_env => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("foo".to_string()),
                    url: Some("fuchsia-pkg://foo".to_string()),
                    startup: Some(StartupMode::Lazy),
                    environment: Some("test_env".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_environment("ChildDecl", "environment", "test_env"),
            ])),
        },

        // collections
        test_validate_collections_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl{
                    name: None,
                    durability: None,
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("CollectionDecl", "name"),
                Error::missing_field("CollectionDecl", "durability"),
            ])),
        },
        test_validate_collections_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl{
                    name: Some("^bad".to_string()),
                    durability: Some(Durability::Persistent),
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("CollectionDecl", "name"),
            ])),
        },
        test_validate_collections_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl{
                    name: Some("a".repeat(1025)),
                    durability: Some(Durability::Transient),
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("CollectionDecl", "name"),
            ])),
        },
        test_validate_collection_references_unknown_env => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl {
                    name: Some("foo".to_string()),
                    durability: Some(Durability::Transient),
                    environment: Some("test_env".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_environment("CollectionDecl", "environment", "test_env"),
            ])),
        },

        // capabilities
        test_validate_capabilities_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    CapabilityDecl::Service(ServiceDecl {
                        name: None,
                        source_path: None,
                    }),
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: None,
                        source_path: None,
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: None,
                        source_path: None,
                        rights: None,
                    }),
                    CapabilityDecl::Storage(StorageDecl {
                        name: None,
                        source: None,
                        source_path: None,
                        subdir: None,
                    }),
                    CapabilityDecl::Runner(RunnerDecl {
                        name: None,
                        source: None,
                        source_path: None,
                    }),
                    CapabilityDecl::Resolver(ResolverDecl {
                        name: None,
                        source_path: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ServiceDecl", "name"),
                Error::missing_field("ServiceDecl", "source_path"),
                Error::missing_field("ProtocolDecl", "name"),
                Error::missing_field("ProtocolDecl", "source_path"),
                Error::missing_field("DirectoryDecl", "name"),
                Error::missing_field("DirectoryDecl", "source_path"),
                Error::missing_field("DirectoryDecl", "rights"),
                Error::missing_field("StorageDecl", "source"),
                Error::missing_field("StorageDecl", "name"),
                Error::missing_field("StorageDecl", "source_path"),
                Error::missing_field("RunnerDecl", "source"),
                Error::missing_field("RunnerDecl", "name"),
                Error::missing_field("RunnerDecl", "source_path"),
                Error::missing_field("ResolverDecl", "name"),
                Error::missing_field("ResolverDecl", "source_path"),
            ])),
        },
        test_validate_capabilities_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    CapabilityDecl::Service(ServiceDecl {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                    }),
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    CapabilityDecl::Storage(StorageDecl {
                        name: Some("^bad".to_string()),
                        source: Some(Ref::Collection(CollectionRef {
                            name: "/bad".to_string()
                        })),
                        source_path: Some("&bad".to_string()),
                        subdir: None,
                    }),
                    CapabilityDecl::Runner(RunnerDecl {
                        name: Some("^bad".to_string()),
                        source: Some(Ref::Collection(CollectionRef {
                            name: "/bad".to_string()
                        })),
                        source_path: Some("&bad".to_string()),
                    }),
                    CapabilityDecl::Resolver(ResolverDecl {
                        name: Some("^bad".to_string()),
                        source_path: Some("&bad".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ServiceDecl", "name"),
                Error::invalid_field("ServiceDecl", "source_path"),
                Error::invalid_field("ProtocolDecl", "name"),
                Error::invalid_field("ProtocolDecl", "source_path"),
                Error::invalid_field("DirectoryDecl", "name"),
                Error::invalid_field("DirectoryDecl", "source_path"),
                Error::invalid_field("StorageDecl", "source"),
                Error::invalid_field("StorageDecl", "name"),
                Error::invalid_field("StorageDecl", "source_path"),
                Error::invalid_field("RunnerDecl", "source"),
                Error::invalid_field("RunnerDecl", "name"),
                Error::invalid_field("RunnerDecl", "source_path"),
                Error::invalid_field("ResolverDecl", "name"),
                Error::invalid_field("ResolverDecl", "source_path"),
            ])),
        },
        test_validate_capabilities_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    CapabilityDecl::Storage(StorageDecl {
                        name: Some("foo".to_string()),
                        source: Some(Ref::Collection(CollectionRef {
                            name: "invalid".to_string(),
                        })),
                        source_path: Some("/foo".to_string()),
                        subdir: None,
                    }),
                    CapabilityDecl::Runner(RunnerDecl {
                        name: Some("bar".to_string()),
                        source: Some(Ref::Collection(CollectionRef {
                            name: "invalid".to_string(),
                        })),
                        source_path: Some("/foo".to_string()),
                    }),
                    CapabilityDecl::Resolver(ResolverDecl {
                        name: Some("baz".to_string()),
                        source_path: Some("/foo".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("StorageDecl", "source"),
                Error::invalid_field("RunnerDecl", "source"),
            ])),
        },
        test_validate_capabilities_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    CapabilityDecl::Service(ServiceDecl {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                    }),
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    CapabilityDecl::Storage(StorageDecl {
                        name: Some("a".repeat(101)),
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                        subdir: None,
                    }),
                    CapabilityDecl::Runner(RunnerDecl {
                        name: Some("a".repeat(101)),
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                    }),
                    CapabilityDecl::Resolver(ResolverDecl {
                        name: Some("a".repeat(101)),
                        source_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ServiceDecl", "name"),
                Error::field_too_long("ServiceDecl", "source_path"),
                Error::field_too_long("ProtocolDecl", "name"),
                Error::field_too_long("ProtocolDecl", "source_path"),
                Error::field_too_long("DirectoryDecl", "name"),
                Error::field_too_long("DirectoryDecl", "source_path"),
                Error::field_too_long("StorageDecl", "source.child.name"),
                Error::field_too_long("StorageDecl", "name"),
                Error::field_too_long("StorageDecl", "source_path"),
                Error::field_too_long("RunnerDecl", "source.child.name"),
                Error::field_too_long("RunnerDecl", "name"),
                Error::field_too_long("RunnerDecl", "source_path"),
                Error::field_too_long("ResolverDecl", "name"),
                Error::field_too_long("ResolverDecl", "source_path"),
            ])),
        },
        test_validate_capabilities_duplicate_name => {
            input = {
                let mut decl = new_component_decl();
                decl.capabilities = Some(vec![
                    CapabilityDecl::Service(ServiceDecl {
                        name: Some("service".to_string()),
                        source_path: Some("/service".to_string()),
                    }),
                    CapabilityDecl::Service(ServiceDecl {
                        name: Some("service".to_string()),
                        source_path: Some("/service".to_string()),
                    }),
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: Some("protocol".to_string()),
                        source_path: Some("/protocol".to_string()),
                    }),
                    CapabilityDecl::Protocol(ProtocolDecl {
                        name: Some("protocol".to_string()),
                        source_path: Some("/protocol".to_string()),
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("directory".to_string()),
                        source_path: Some("/directory".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    CapabilityDecl::Directory(DirectoryDecl {
                        name: Some("directory".to_string()),
                        source_path: Some("/directory".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    CapabilityDecl::Storage(StorageDecl {
                        name: Some("storage".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/storage".to_string()),
                        subdir: None,
                    }),
                    CapabilityDecl::Storage(StorageDecl {
                        name: Some("storage".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/storage".to_string()),
                        subdir: None,
                    }),
                    CapabilityDecl::Runner(RunnerDecl {
                        name: Some("runner".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/runner".to_string()),
                    }),
                    CapabilityDecl::Runner(RunnerDecl {
                        name: Some("runner".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/runner".to_string()),
                    }),
                    CapabilityDecl::Resolver(ResolverDecl {
                        name: Some("resolver".to_string()),
                        source_path: Some("/resolver".to_string()),
                    }),
                    CapabilityDecl::Resolver(ResolverDecl {
                        name: Some("resolver".to_string()),
                        source_path: Some("/resolver".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("ServiceDecl", "name", "service"),
                Error::duplicate_field("ProtocolDecl", "name", "protocol"),
                Error::duplicate_field("DirectoryDecl", "name", "directory"),
                Error::duplicate_field("StorageDecl", "name", "storage"),
                Error::duplicate_field("RunnerDecl", "name", "runner"),
                Error::duplicate_field("ResolverDecl", "name", "resolver"),
            ])),
        },

        test_validate_resolvers_missing_from_offer => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![OfferDecl::Resolver(OfferResolverDecl {
                    source: Some(Ref::Self_(SelfRef {})),
                    source_name: Some("a".to_string()),
                    target: Some(Ref::Child(ChildRef { name: "child".to_string(), collection: None })),
                    target_name: Some("a".to_string()),
                })]);
                decl.children = Some(vec![ChildDecl {
                    name: Some("child".to_string()),
                    url: Some("test:///child".to_string()),
                    startup: Some(StartupMode::Eager),
                    environment: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("OfferResolverDecl", "source", "a"),
            ])),
        },
        test_validate_resolvers_missing_from_expose => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![ExposeDecl::Resolver(ExposeResolverDecl {
                    source: Some(Ref::Self_(SelfRef {})),
                    source_name: Some("a".to_string()),
                    target: Some(Ref::Parent(ParentRef {})),
                    target_name: Some("a".to_string()),
                })]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_capability("ExposeResolverDecl", "source", "a"),
            ])),
        },
    }

    test_validate_capabilities! {
        test_validate_capabilities_individually_ok => {
            input = vec![
                CapabilityDecl::Protocol(ProtocolDecl {
                    name: Some("foo_svc".into()),
                    source_path: Some("/svc/foo".into()),
                }),
                CapabilityDecl::Directory(DirectoryDecl {
                    name: Some("foo_dir".into()),
                    source_path: Some("/foo".into()),
                    rights: Some(fio2::Operations::Connect),
                }),
            ],
            result = Ok(()),
        },
        test_validate_capabilities_individually_err => {
            input = vec![
                CapabilityDecl::Protocol(ProtocolDecl {
                    name: None,
                    source_path: None,
                }),
                CapabilityDecl::Directory(DirectoryDecl {
                    name: None,
                    source_path: None,
                    rights: None,
                }),
            ],
            result = Err(ErrorList::new(vec![
                Error::missing_field("ProtocolDecl", "name"),
                Error::missing_field("ProtocolDecl", "source_path"),
                Error::missing_field("DirectoryDecl", "name"),
                Error::missing_field("DirectoryDecl", "source_path"),
                Error::missing_field("DirectoryDecl", "rights"),
            ])),
        },
    }

    test_dependency! {
        test_validate_offers_protocol_dependency_cycle => {
            ty = OfferDecl::Protocol,
            offer_decl = OfferProtocolDecl {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_path: Some(format!("/thing")),
                target_path: Some(format!("/thing")),
                dependency_type: Some(DependencyType::Strong),
            },
        },
        test_validate_offers_directory_dependency_cycle => {
            ty = OfferDecl::Directory,
            offer_decl = OfferDirectoryDecl {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_path: Some(format!("/thing")),
                target_path: Some(format!("/thing")),
                rights: Some(fio2::Operations::Connect),
                subdir: None,
                dependency_type: Some(DependencyType::Strong),
            },
        },
        test_validate_offers_service_dependency_cycle => {
            ty = OfferDecl::Service,
            offer_decl = OfferServiceDecl {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
            },
        },
        test_validate_offers_runner_dependency_cycle => {
            ty = OfferDecl::Runner,
            offer_decl = OfferRunnerDecl {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
            },
        },
        test_validate_offers_resolver_dependency_cycle => {
            ty = OfferDecl::Resolver,
            offer_decl = OfferResolverDecl {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_name: Some(format!("thing")),
                target_name: Some(format!("thing")),
            },
        },
    }
    test_weak_dependency! {
        test_validate_offers_protocol_weak_dependency_cycle => {
            ty = OfferDecl::Protocol,
            offer_decl = OfferProtocolDecl {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_path: Some(format!("/thing")),
                target_path: Some(format!("/thing")),
                dependency_type: None, // Filled by macro
            },
        },
        test_validate_offers_directory_weak_dependency_cycle => {
            ty = OfferDecl::Directory,
            offer_decl = OfferDirectoryDecl {
                source: None,  // Filled by macro
                target: None,  // Filled by macro
                source_path: Some(format!("/thing")),
                target_path: Some(format!("/thing")),
                rights: Some(fio2::Operations::Connect),
                subdir: None,
                dependency_type: None,  // Filled by macro
            },
        },
    }
}
