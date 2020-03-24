// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    directed_graph::DirectedGraph,
    fidl_fuchsia_sys2 as fsys,
    std::{
        collections::{HashMap, HashSet},
        error, fmt,
    },
    thiserror::Error,
};

const MAX_PATH_LENGTH: usize = 1024;
const MAX_NAME_LENGTH: usize = 100;
const MAX_URL_LENGTH: usize = 4096;

/// Enum type that can represent any error encountered durlng validation.
#[derive(Debug, Error)]
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
    #[error("{} has invalid {}, unexpected character '{1}'", .0.decl, .0.field)]
    InvalidCharacterInField(DeclField, char),
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
    #[error("\"{1}\" is referenced in {0} but it does not appear in resolvers")]
    InvalidResolver(DeclField, String),
    #[error("{0} specifies multiple runners")]
    MultipleRunnersSpecified(String),
    #[error("a dependency cycle exists between resolver registrations")]
    ResolverDependencyCycle,
    #[error("a dependency cycle exists between offer declarations")]
    OfferDependencyCycle,
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

    pub fn invalid_character_in_field(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        character: char,
    ) -> Self {
        Error::InvalidCharacterInField(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            character,
        )
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

    pub fn invalid_resolver(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        resolver: impl Into<String>,
    ) -> Self {
        Error::InvalidResolver(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            resolver.into(),
        )
    }

    pub fn multiple_runners_specified(decl_type: impl Into<String>) -> Self {
        Error::MultipleRunnersSpecified(decl_type.into())
    }

    pub fn resolver_dependency_cycle() -> Self {
        Error::ResolverDependencyCycle
    }

    pub fn offer_dependency_cycle() -> Self {
        Error::OfferDependencyCycle
    }
}

#[derive(Debug)]
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
#[derive(Debug)]
pub struct ErrorList {
    errs: Vec<Error>,
}

impl ErrorList {
    fn new(errs: Vec<Error>) -> ErrorList {
        ErrorList { errs }
    }
}

impl error::Error for ErrorList {}

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
    let ctx = ValidationContext {
        decl,
        all_children: HashMap::new(),
        all_collections: HashSet::new(),
        all_storage_and_sources: HashMap::new(),
        all_runners_and_sources: HashMap::new(),
        all_resolvers: HashSet::new(),
        all_environment_names: HashSet::new(),
        strong_dependencies: DirectedGraph::new(),
        target_paths: HashMap::new(),
        offered_runner_names: HashMap::new(),
        offered_resolver_names: HashMap::new(),
        offered_event_names: HashMap::new(),
        errors: vec![],
    };
    ctx.validate().map_err(|errs| ErrorList::new(errs))
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

struct ValidationContext<'a> {
    decl: &'a fsys::ComponentDecl,
    all_children: HashMap<&'a str, &'a fsys::ChildDecl>,
    all_collections: HashSet<&'a str>,
    all_storage_and_sources: HashMap<&'a str, Option<&'a str>>,
    all_runners_and_sources: HashMap<&'a str, Option<&'a str>>,
    all_resolvers: HashSet<&'a str>,
    all_environment_names: HashSet<&'a str>,
    strong_dependencies: DirectedGraph<&'a str>,
    target_paths: PathMap<'a>,
    offered_runner_names: NameMap<'a>,
    offered_resolver_names: NameMap<'a>,
    offered_event_names: NameMap<'a>,
    errors: Vec<Error>,
}

#[derive(Clone, Copy, PartialEq)]
enum AllowablePaths {
    One,
    Many,
}

#[derive(Debug, PartialEq, Eq, Hash)]
enum TargetId<'a> {
    Component(&'a str),
    Collection(&'a str),
}

type PathMap<'a> = HashMap<TargetId<'a>, HashMap<&'a str, AllowablePaths>>;
type NameMap<'a> = HashMap<TargetId<'a>, HashSet<&'a str>>;

impl<'a> ValidationContext<'a> {
    fn validate(mut self) -> Result<(), Vec<Error>> {
        // Collect all environment names first, so that references to them can be checked.
        if let Some(envs) = &self.decl.environments {
            self.collect_environment_names(&envs);
        }

        // Validate "children" and build the set of all children.
        if let Some(children) = self.decl.children.as_ref() {
            for child in children {
                self.validate_child_decl(&child);
            }
        }

        // Validate "collections" and build the set of all collections.
        if let Some(collections) = self.decl.collections.as_ref() {
            for collection in collections {
                self.validate_collection_decl(&collection);
            }
        }

        // Validate "storage" and build the set of all storage sections.
        if let Some(storage) = self.decl.storage.as_ref() {
            for storage in storage {
                self.validate_storage_decl(&storage);
            }
        }

        // Validate "runners" and build the set of all runners.
        if let Some(runners) = self.decl.runners.as_ref() {
            for runner in runners {
                self.validate_runner_decl(&runner);
            }
        }

        // Validate "resolvers" and build the set of all resolvers.
        if let Some(resolvers) = self.decl.resolvers.as_ref() {
            for resolver in resolvers {
                self.validate_resolver_decl(&resolver);
            }
        }

        // Validate "uses".
        if let Some(uses) = self.decl.uses.as_ref() {
            self.validate_use_decls(uses);
        }

        // Validate "exposes".
        if let Some(exposes) = self.decl.exposes.as_ref() {
            let mut target_paths = HashMap::new();
            let mut runner_names = HashSet::new();
            let mut resolver_names = HashSet::new();
            for expose in exposes.iter() {
                self.validate_expose_decl(
                    &expose,
                    &mut target_paths,
                    &mut runner_names,
                    &mut resolver_names,
                );
            }
        }

        // Validate "offers".
        if let Some(offers) = self.decl.offers.as_ref() {
            for offer in offers.iter() {
                self.validate_offers_decl(&offer);
            }
            if let Err(_) = self.strong_dependencies.topological_sort() {
                self.errors.push(Error::offer_dependency_cycle());
            }
        }

        // Validate "environments" after all other declarations are processed.
        if let Some(environment) = self.decl.environments.as_ref() {
            for environment in environment {
                self.validate_environment_decl(&environment);
            }
        }

        if self.errors.is_empty() {
            Ok(())
        } else {
            Err(self.errors)
        }
    }

    // Collects all the environment names, watching for duplicates.
    fn collect_environment_names(&mut self, envs: &'a [fsys::EnvironmentDecl]) {
        for env in envs {
            if let Some(name) = env.name.as_ref() {
                if !self.all_environment_names.insert(name) {
                    self.errors.push(Error::duplicate_field("EnvironmentDecl", "name", name));
                }
            }
        }
    }

    fn validate_use_decls(&mut self, uses: &[fsys::UseDecl]) {
        // Validate individual fields.
        for use_ in uses.iter() {
            self.validate_use_decl(&use_);
        }

        // Ensure that no more than one runner is specified.
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

    fn validate_use_decl(&mut self, use_: &fsys::UseDecl) {
        match use_ {
            fsys::UseDecl::Service(u) => {
                self.validate_use_fields(
                    "UseServiceDecl",
                    u.source.as_ref(),
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
                );
            }
            fsys::UseDecl::Protocol(u) => {
                self.validate_use_fields(
                    "UseProtocolDecl",
                    u.source.as_ref(),
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
                );
            }
            fsys::UseDecl::Directory(u) => {
                self.validate_use_fields(
                    "UseDirectoryDecl",
                    u.source.as_ref(),
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
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
            fsys::UseDecl::Storage(u) => match u.type_ {
                None => self.errors.push(Error::missing_field("UseStorageDecl", "type")),
                Some(fsys::StorageType::Meta) => {
                    if u.target_path.is_some() {
                        self.errors.push(Error::invalid_field("UseStorageDecl", "target_path"));
                    }
                }
                _ => {
                    check_path(
                        u.target_path.as_ref(),
                        "UseStorageDecl",
                        "target_path",
                        &mut self.errors,
                    );
                }
            },
            fsys::UseDecl::Runner(r) => {
                check_name(
                    r.source_name.as_ref(),
                    "UseRunnerDecl",
                    "source_name",
                    &mut self.errors,
                );
            }
            fsys::UseDecl::Event(e) => {
                self.validate_source(e.source.as_ref(), "UseEventDecl", "source");
                check_name(e.source_name.as_ref(), "UseEventDecl", "source_name", &mut self.errors);
                check_name(e.target_name.as_ref(), "UseEventDecl", "target_name", &mut self.errors);
            }
            fsys::UseDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "use"));
            }
        }
    }

    fn validate_use_fields(
        &mut self,
        decl: &str,
        source: Option<&fsys::Ref>,
        source_path: Option<&String>,
        target_path: Option<&String>,
    ) {
        self.validate_source(source, decl, "source");
        check_path(source_path, decl, "source_path", &mut self.errors);
        check_path(target_path, decl, "target_path", &mut self.errors);
    }

    fn validate_source(&mut self, source: Option<&fsys::Ref>, decl: &str, field: &str) {
        match source {
            Some(fsys::Ref::Realm(_)) => {}
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
        }
        if collection.durability.is_none() {
            self.errors.push(Error::missing_field("CollectionDecl", "durability"));
        }
    }

    fn validate_environment_decl(&mut self, environment: &'a fsys::EnvironmentDecl) {
        let name = environment.name.as_ref();
        check_name(name, "EnvironmentDecl", "name", &mut self.errors);
        if environment.extends.is_none() {
            self.errors.push(Error::missing_field("EnvironmentDecl", "extends"));
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
        match &resolver_registration.source {
            Some(fsys::Ref::Realm(_)) => {}
            Some(fsys::Ref::Child(child_ref)) => {
                // Make sure the child is valid.
                if self.validate_child_ref("ResolverRegistration", "source", &child_ref) {
                    // Ensure there are no cycles, eg:
                    // environment is assigned to a child, but the environment contains a resolver
                    // provided by the same child.
                    // TODO(fxb/48128): Replace with cycle detection algorithm using //src/lib/directed_graph.
                    let child_name = child_ref.name.as_str();
                    if let Some(child_decl) = self.all_children.get(child_name) {
                        match (environment_name, child_decl.environment.as_ref()) {
                            (Some(environment_name), Some(child_environment_name))
                                if environment_name == child_environment_name =>
                            {
                                self.errors.push(Error::resolver_dependency_cycle());
                            }
                            _ => {}
                        }
                    }
                }
            }
            Some(_) => {
                self.errors.push(Error::invalid_field("ResolverRegistration", "source"));
            }
            None => {
                self.errors.push(Error::missing_field("ResolverRegistration", "source"));
            }
        };
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

    fn validate_storage_decl(&mut self, storage: &'a fsys::StorageDecl) {
        check_path(storage.source_path.as_ref(), "StorageDecl", "source_path", &mut self.errors);
        let source_child_name = match storage.source.as_ref() {
            Some(fsys::Ref::Realm(_)) => None,
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
            if self.all_storage_and_sources.insert(name, source_child_name).is_some() {
                self.errors.push(Error::duplicate_field("StorageDecl", "name", name.as_str()));
            }
        }
    }

    fn validate_runner_decl(&mut self, runner: &'a fsys::RunnerDecl) {
        let runner_source = match runner.source.as_ref() {
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
            if self.all_runners_and_sources.insert(name, runner_source).is_some() {
                self.errors.push(Error::duplicate_field("RunnerDecl", "name", name.as_str()));
            }
        }
        check_path(runner.source_path.as_ref(), "RunnerDecl", "source_path", &mut self.errors);
    }

    fn validate_resolver_decl(&mut self, resolver: &'a fsys::ResolverDecl) {
        if check_name(resolver.name.as_ref(), "ResolverDecl", "name", &mut self.errors) {
            let name = resolver.name.as_ref().unwrap();
            if !self.all_resolvers.insert(name) {
                self.errors.push(Error::duplicate_field("ResolverDecl", "name", name.as_str()));
            }
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

    fn validate_storage_source(&mut self, source: &fsys::StorageRef, decl_type: &str) {
        if check_name(Some(&source.name), decl_type, "source.storage.name", &mut self.errors) {
            if !self.all_storage_and_sources.contains_key(&source.name as &str) {
                self.errors.push(Error::invalid_storage(decl_type, "source", &source.name as &str));
            }
        }
    }

    fn validate_expose_decl(
        &mut self,
        expose: &'a fsys::ExposeDecl,
        prev_target_paths: &mut HashMap<&'a str, AllowablePaths>,
        prev_runner_names: &mut HashSet<&'a str>,
        prev_resolver_names: &mut HashSet<&'a str>,
    ) {
        match expose {
            fsys::ExposeDecl::Service(e) => {
                self.validate_expose_fields(
                    "ExposeServiceDecl",
                    AllowablePaths::Many,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::ExposeDecl::Protocol(e) => {
                self.validate_expose_fields(
                    "ExposeProtocolDecl",
                    AllowablePaths::One,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::ExposeDecl::Directory(e) => {
                self.validate_expose_fields(
                    "ExposeDirectoryDecl",
                    AllowablePaths::One,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_paths,
                );

                match e.source.as_ref() {
                    Some(fsys::Ref::Self_(_)) => {
                        if e.rights.is_none() {
                            self.errors.push(Error::missing_field("ExposeDirectoryDecl", "rights"));
                        }
                    }
                    _ => {}
                }

                if let Some(subdir) = e.subdir.as_ref() {
                    check_relative_path(
                        Some(subdir),
                        "ExposeDirectoryDecl",
                        "subdir",
                        &mut self.errors,
                    );
                }
            }
            fsys::ExposeDecl::Runner(e) => {
                self.validate_expose_runner_fields(e, prev_runner_names);
            }
            fsys::ExposeDecl::Resolver(e) => {
                self.validate_expose_resolver_fields(e, prev_resolver_names);
            }
            fsys::ExposeDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "expose"));
            }
        }
    }

    fn validate_expose_fields(
        &mut self,
        decl: &str,
        allowable_paths: AllowablePaths,
        source: Option<&fsys::Ref>,
        source_path: Option<&String>,
        target_path: Option<&'a String>,
        target: Option<&fsys::Ref>,
        prev_child_target_paths: &mut HashMap<&'a str, AllowablePaths>,
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
                fsys::Ref::Realm(_) => {}
                fsys::Ref::Framework(_) => {}
                _ => {
                    self.errors.push(Error::invalid_field(decl, "target"));
                }
            },
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
            }
        }
        check_path(source_path, decl, "source_path", &mut self.errors);
        if check_path(target_path, decl, "target_path", &mut self.errors) {
            let target_path = target_path.unwrap();
            if let Some(prev_state) = prev_child_target_paths.insert(target_path, allowable_paths) {
                if prev_state == AllowablePaths::One || prev_state != allowable_paths {
                    self.errors.push(Error::duplicate_field(decl, "target_path", target_path));
                }
            }
        }
    }

    /// Validates that the expose source is from `self`, `framework`, or a valid child.
    fn validate_expose_source(
        &mut self,
        source: &Option<fsys::Ref>,
        decl_type: &str,
        field_name: &str,
    ) {
        match source.as_ref() {
            Some(fsys::Ref::Self_(_)) | Some(fsys::Ref::Framework(_)) => {}
            Some(fsys::Ref::Child(child)) => {
                self.validate_source_child(child, decl_type);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl_type, field_name));
            }
            None => {
                self.errors.push(Error::missing_field(decl_type, field_name));
            }
        };
    }

    /// Validates that the expose target is to `realm` or `framework`.
    fn validate_expose_target(
        &mut self,
        target: &Option<fsys::Ref>,
        decl_type: &str,
        field_name: &str,
    ) {
        match target.as_ref() {
            Some(fsys::Ref::Realm(_)) => {}
            Some(_) => {
                self.errors.push(Error::invalid_field(decl_type, field_name));
            }
            None => {
                self.errors.push(Error::missing_field(decl_type, field_name));
            }
        };
    }

    fn validate_expose_resolver_fields(
        &mut self,
        resolver: &'a fsys::ExposeResolverDecl,
        prev_resolver_names: &mut HashSet<&'a str>,
    ) {
        let decl = "ExposeResolverDecl";
        self.validate_expose_source(&resolver.source, decl, "source");
        self.validate_expose_target(&resolver.target, decl, "target");
        check_name(resolver.source_name.as_ref(), decl, "source_name", &mut self.errors);
        if check_name(resolver.target_name.as_ref(), decl, "target_name", &mut self.errors) {
            // Ensure that target_name hasn't already been exposed.
            let target_name = resolver.target_name.as_ref().unwrap();
            if !prev_resolver_names.insert(target_name) {
                self.errors.push(Error::duplicate_field(decl, "target_name", target_name));
            }
        }

        // If the expose source is `self`, ensure we have a corresponding ResolverDecl.
        if let (Some(fsys::Ref::Self_(_)), Some(ref name)) =
            (&resolver.source, &resolver.source_name)
        {
            if !self.all_resolvers.contains(name as &str) {
                self.errors.push(Error::invalid_resolver(decl, "source", name));
            }
        }
    }

    fn validate_expose_runner_fields(
        &mut self,
        runner: &'a fsys::ExposeRunnerDecl,
        prev_runner_names: &mut HashSet<&'a str>,
    ) {
        let decl = "ExposeRunnerDecl";
        self.validate_expose_source(&runner.source, decl, "source");
        self.validate_expose_target(&runner.target, decl, "target");
        check_name(runner.source_name.as_ref(), decl, "source_name", &mut self.errors);
        if check_name(runner.target_name.as_ref(), decl, "target_name", &mut self.errors) {
            // Ensure that target_name hasn't already been exposed.
            let target_name = runner.target_name.as_ref().unwrap();
            if !prev_runner_names.insert(target_name) {
                self.errors.push(Error::duplicate_field(decl, "target_name", target_name));
            }
        }

        // If the expose source is `self`, ensure we have a corresponding RunnerDecl.
        if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&runner.source, &runner.source_name) {
            if !self.all_runners_and_sources.contains_key(&name as &str) {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
        }
    }

    fn add_strong_dep(&mut self, from: Option<&'a fsys::Ref>, to: Option<&'a fsys::Ref>) {
        if let Some(fsys::Ref::Child(fsys::ChildRef { name: source, .. })) = from {
            if let Some(fsys::Ref::Child(fsys::ChildRef { name: target, .. })) = to {
                if source == target {
                    // This is already its own error, don't report this as a cycle.
                } else {
                    self.strong_dependencies.add_edge(source.as_str(), target.as_str());
                }
            }
        }
    }

    fn validate_offers_decl(&mut self, offer: &'a fsys::OfferDecl) {
        match offer {
            fsys::OfferDecl::Service(o) => {
                self.validate_offers_fields(
                    "OfferServiceDecl",
                    AllowablePaths::Many,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
                self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
            }
            fsys::OfferDecl::Protocol(o) => {
                self.validate_offers_fields(
                    "OfferProtocolDecl",
                    AllowablePaths::One,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
                if o.dependency_type.is_none() {
                    self.errors.push(Error::missing_field("OfferProtocolDecl", "dependency_type"));
                } else if o.dependency_type == Some(fsys::DependencyType::Strong) {
                    self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
                }
            }
            fsys::OfferDecl::Directory(o) => {
                self.validate_offers_fields(
                    "OfferDirectoryDecl",
                    AllowablePaths::One,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
                if o.dependency_type.is_none() {
                    self.errors.push(Error::missing_field("OfferDirectoryDecl", "dependency_type"));
                } else if o.dependency_type == Some(fsys::DependencyType::Strong) {
                    self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
                }
                match o.source.as_ref() {
                    Some(fsys::Ref::Self_(_)) => {
                        if o.rights.is_none() {
                            self.errors.push(Error::missing_field("OfferDirectoryDecl", "rights"));
                        }
                    }
                    _ => {}
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
                    o.type_.as_ref(),
                    o.source.as_ref(),
                    o.target.as_ref(),
                );
                self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
            }
            fsys::OfferDecl::Runner(o) => {
                self.validate_runner_offer_fields(o);
                self.add_strong_dep(o.source.as_ref(), o.target.as_ref());
            }
            fsys::OfferDecl::Resolver(o) => {
                self.validate_resolver_offer_fields(o);
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

    /// Validates that the offer source is from `self`, `framework`, `realm`, or a valid child.
    fn validate_offer_source(
        &mut self,
        source: &Option<fsys::Ref>,
        decl_type: &str,
        field_name: &str,
    ) {
        match source.as_ref() {
            Some(fsys::Ref::Self_(_))
            | Some(fsys::Ref::Framework(_))
            | Some(fsys::Ref::Realm(_)) => {}
            Some(fsys::Ref::Child(child)) => {
                self.validate_source_child(child, decl_type);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl_type, field_name));
            }
            None => {
                self.errors.push(Error::missing_field(decl_type, field_name));
            }
        };
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

    fn validate_offers_fields(
        &mut self,
        decl: &str,
        allowable_paths: AllowablePaths,
        source: Option<&fsys::Ref>,
        source_path: Option<&String>,
        target: Option<&'a fsys::Ref>,
        target_path: Option<&'a String>,
    ) {
        match source {
            Some(fsys::Ref::Realm(_)) => {}
            Some(fsys::Ref::Self_(_)) => {}
            Some(fsys::Ref::Framework(_)) => {}
            Some(fsys::Ref::Child(child)) => self.validate_source_child(child, decl),
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }
        check_path(source_path, decl, "source_path", &mut self.errors);
        match target {
            Some(fsys::Ref::Child(c)) => {
                self.validate_target_child(decl, allowable_paths, c, source, target_path);
            }
            Some(fsys::Ref::Collection(c)) => {
                self.validate_target_collection(decl, allowable_paths, c, target_path);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "target"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
            }
        }
        check_path(target_path, decl, "target_path", &mut self.errors);
    }

    fn validate_storage_offer_fields(
        &mut self,
        decl: &str,
        type_: Option<&fsys::StorageType>,
        source: Option<&'a fsys::Ref>,
        target: Option<&'a fsys::Ref>,
    ) {
        if type_.is_none() {
            self.errors.push(Error::missing_field(decl, "type"));
        }
        let storage_source_name = match source {
            Some(fsys::Ref::Realm(_)) => None,
            Some(fsys::Ref::Storage(s)) => {
                self.validate_storage_source(s, decl);
                Some(&s.name as &str)
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
                None
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
                None
            }
        };
        self.validate_storage_target(decl, storage_source_name, target);
    }

    fn validate_runner_offer_fields(&mut self, runner: &'a fsys::OfferRunnerDecl) {
        let decl = "OfferRunnerDecl";
        self.validate_offer_source(&runner.source, decl, "source");
        check_name(runner.source_name.as_ref(), decl, "source_name", &mut self.errors);
        // If the offer source is `self`, ensure we have a corresponding RunnerDecl.
        if let (Some(fsys::Ref::Self_(_)), Some(ref name)) = (&runner.source, &runner.source_name) {
            if !self.all_runners_and_sources.contains_key(&name as &str) {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
        }
        let target_id = self.validate_offer_target(&runner.target, decl, "target");
        check_name(runner.target_name.as_ref(), decl, "target_name", &mut self.errors);
        if let (Some(target_id), Some(target_name)) = (target_id, runner.target_name.as_ref()) {
            // Assuming the target_name is valid, ensure the target_name isn't already used.
            if !self
                .offered_runner_names
                .entry(target_id)
                .or_insert(HashSet::new())
                .insert(target_name)
            {
                self.errors.push(Error::duplicate_field(decl, "target_name", target_name as &str));
            }
        }
        check_offer_target_is_not_source(&runner.target, &runner.source, decl, &mut self.errors);
    }

    fn validate_resolver_offer_fields(&mut self, resolver: &'a fsys::OfferResolverDecl) {
        let decl = "OfferResolverDecl";
        self.validate_offer_source(&resolver.source, decl, "source");
        check_name(resolver.source_name.as_ref(), decl, "source_name", &mut self.errors);
        // If the offer source is `self`, ensure we have a corresponding ResolverDecl.
        if let (Some(fsys::Ref::Self_(_)), Some(ref name)) =
            (&resolver.source, &resolver.source_name)
        {
            if !self.all_resolvers.contains(&name as &str) {
                self.errors.push(Error::invalid_resolver(decl, "source", name));
            }
        }
        let target_id = self.validate_offer_target(&resolver.target, decl, "target");
        check_name(resolver.target_name.as_ref(), decl, "target_name", &mut self.errors);
        if let (Some(target_id), Some(target_name)) = (target_id, resolver.target_name.as_ref()) {
            // Assuming the target_name is valid, ensure the target_name isn't already used.
            if !self
                .offered_resolver_names
                .entry(target_id)
                .or_insert(HashSet::new())
                .insert(target_name)
            {
                self.errors.push(Error::duplicate_field(decl, "target_name", target_name as &str));
            }
        }
        check_offer_target_is_not_source(
            &resolver.target,
            &resolver.source,
            decl,
            &mut self.errors,
        );
    }

    fn validate_event_offer_fields(&mut self, event: &'a fsys::OfferEventDecl) {
        let decl = "OfferEventDecl";
        check_name(event.source_name.as_ref(), decl, "source_name", &mut self.errors);

        // Only realm and framework are valid.
        match event.source {
            Some(fsys::Ref::Realm(_)) => {}
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
            if !self
                .offered_event_names
                .entry(target_id)
                .or_insert(HashSet::new())
                .insert(target_name)
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

    fn validate_target_child(
        &mut self,
        decl: &str,
        allowable_paths: AllowablePaths,
        child: &'a fsys::ChildRef,
        source: Option<&fsys::Ref>,
        target_path: Option<&'a String>,
    ) {
        if !self.validate_child_ref(decl, "target", child) {
            return;
        }
        if let Some(target_path) = target_path {
            let paths_for_target =
                self.target_paths.entry(TargetId::Component(&child.name)).or_insert(HashMap::new());
            if let Some(prev_state) = paths_for_target.insert(target_path, allowable_paths) {
                if prev_state == AllowablePaths::One || prev_state != allowable_paths {
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

    fn validate_target_collection(
        &mut self,
        decl: &str,
        allowable_paths: AllowablePaths,
        collection: &'a fsys::CollectionRef,
        target_path: Option<&'a String>,
    ) {
        if !self.validate_collection_ref(decl, "target", &collection) {
            return;
        }
        if let Some(target_path) = target_path {
            let paths_for_target = self
                .target_paths
                .entry(TargetId::Collection(&collection.name))
                .or_insert(HashMap::new());
            if let Some(prev_state) = paths_for_target.insert(target_path, allowable_paths) {
                if prev_state == AllowablePaths::One || prev_state != allowable_paths {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_path",
                        target_path as &str,
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
            match b as char {
                '0'..='9' | 'a'..='z' | '_' | '-' | '.' => (),
                c => {
                    errors.push(Error::invalid_character_in_field(decl_type, keyword, c));
                    return false;
                }
            }
        }
    }
    start_err_len == errors.len()
}

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
                c => {
                    errors.push(Error::invalid_character_in_field(decl_type, keyword, c));
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
                cm_types::UrlSchemeValidationError::InvalidLength(0) => {
                    Error::empty_field(decl_type, keyword)
                }
                cm_types::UrlSchemeValidationError::InvalidLength(_) => {
                    Error::field_too_long(decl_type, keyword)
                }
                cm_types::UrlSchemeValidationError::MalformedUrlScheme(c) => {
                    Error::invalid_character_in_field(decl_type, keyword, c)
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

/// Checks that the offer target is not the same as the offer source.
fn check_offer_target_is_not_source(
    target: &Option<fsys::Ref>,
    source: &Option<fsys::Ref>,
    decl_type: &str,
    errors: &mut Vec<Error>,
) -> bool {
    match (source, target) {
        (Some(fsys::Ref::Child(ref source_child)), Some(fsys::Ref::Child(ref target_child))) => {
            if source_child.name == target_child.name {
                errors.push(Error::offer_target_equals_source(decl_type, &target_child.name));
                return false;
            }
        }
        _ => {}
    };
    true
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2,
        fidl_fuchsia_sys2::{
            ChildDecl, ChildRef, CollectionDecl, CollectionRef, ComponentDecl, DependencyType,
            Durability, EnvironmentDecl, EnvironmentExtends, ExposeDecl, ExposeDirectoryDecl,
            ExposeProtocolDecl, ExposeResolverDecl, ExposeRunnerDecl, ExposeServiceDecl,
            FrameworkRef, OfferDecl, OfferDirectoryDecl, OfferEventDecl, OfferProtocolDecl,
            OfferResolverDecl, OfferRunnerDecl, OfferServiceDecl, OfferStorageDecl, RealmRef, Ref,
            ResolverDecl, ResolverRegistration, RunnerDecl, SelfRef, StartupMode, StorageDecl,
            StorageRef, StorageType, UseDecl, UseDirectoryDecl, UseEventDecl, UseProtocolDecl,
            UseRunnerDecl, UseServiceDecl, UseStorageDecl,
        },
        lazy_static::lazy_static,
        proptest::prelude::*,
        regex::Regex,
    };

    const PATH_REGEX_STR: &str = r"(/[^/]+)+";
    const NAME_REGEX_STR: &str = r"[0-9a-z_\-\.]+";
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
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
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
            storage: None,
            children: None,
            collections: None,
            runners: None,
            resolvers: None,
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
                        Error::offer_dependency_cycle(),
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
            result = Err(ErrorList::new(vec![Error::invalid_character_in_field("FooDecl", "foo", '^')])),
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

    test_validate! {
        // uses
        test_validate_uses_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: None,
                        source_path: None,
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
                        type_: None,
                        target_path: None,
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Cache),
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
                    })
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("UseServiceDecl", "source"),
                Error::missing_field("UseServiceDecl", "source_path"),
                Error::missing_field("UseServiceDecl", "target_path"),
                Error::missing_field("UseProtocolDecl", "source"),
                Error::missing_field("UseProtocolDecl", "source_path"),
                Error::missing_field("UseProtocolDecl", "target_path"),
                Error::missing_field("UseDirectoryDecl", "source"),
                Error::missing_field("UseDirectoryDecl", "source_path"),
                Error::missing_field("UseDirectoryDecl", "target_path"),
                Error::missing_field("UseDirectoryDecl", "rights"),
                Error::missing_field("UseStorageDecl", "type"),
                Error::missing_field("UseStorageDecl", "target_path"),
                Error::missing_field("UseRunnerDecl", "source_name"),
                Error::missing_field("UseEventDecl", "source"),
                Error::missing_field("UseEventDecl", "source_name"),
                Error::missing_field("UseEventDecl", "target_name"),
            ])),
        },
        test_validate_uses_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("/foo".to_string()),
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Cache),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Meta),
                        target_path: Some("/meta".to_string()),
                    }),
                    UseDecl::Event(UseEventDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_name: Some("/foo".to_string()),
                        target_name: Some("/foo".to_string()),
                        filter: Some(fdata::Dictionary { entries: None }),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseServiceDecl", "source"),
                Error::invalid_field("UseServiceDecl", "source_path"),
                Error::invalid_field("UseServiceDecl", "target_path"),
                Error::invalid_field("UseProtocolDecl", "source"),
                Error::invalid_field("UseProtocolDecl", "source_path"),
                Error::invalid_field("UseProtocolDecl", "target_path"),
                Error::invalid_field("UseDirectoryDecl", "source"),
                Error::invalid_field("UseDirectoryDecl", "source_path"),
                Error::invalid_field("UseDirectoryDecl", "target_path"),
                Error::invalid_field("UseDirectoryDecl", "subdir"),
                Error::invalid_field("UseStorageDecl", "target_path"),
                Error::invalid_field("UseStorageDecl", "target_path"),
                Error::invalid_field("UseEventDecl", "source"),
                Error::invalid_character_in_field("UseEventDecl", "source_name", '/'),
                Error::invalid_character_in_field("UseEventDecl", "target_name", '/'),
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
                        source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Cache),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: Some(format!("{}", "a".repeat(101))),
                    }),
                    UseDecl::Event(UseEventDecl {
                        source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                        source_name: Some(format!("{}", "a".repeat(101))),
                        target_name: Some(format!("{}", "a".repeat(101))),
                        filter: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("UseServiceDecl", "source_path"),
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

        // exposes
        test_validate_exposes_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
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
                Error::missing_field("ExposeServiceDecl", "source_path"),
                Error::missing_field("ExposeServiceDecl", "target_path"),
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
                        source_path: Some("/svc/logger".to_string()),
                        target_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/data".to_string()),
                        target_path: Some("/data".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
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
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("/foo".to_string()),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf!".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("pkg!".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_character_in_field("ExposeServiceDecl", "source.child.name", '^'),
                Error::invalid_field("ExposeServiceDecl", "source_path"),
                Error::invalid_field("ExposeServiceDecl", "target_path"),
                Error::invalid_character_in_field("ExposeProtocolDecl", "source.child.name", '^'),
                Error::invalid_field("ExposeProtocolDecl", "source_path"),
                Error::invalid_field("ExposeProtocolDecl", "target_path"),
                Error::invalid_character_in_field("ExposeDirectoryDecl", "source.child.name", '^'),
                Error::invalid_field("ExposeDirectoryDecl", "source_path"),
                Error::invalid_field("ExposeDirectoryDecl", "target_path"),
                Error::invalid_field("ExposeDirectoryDecl", "subdir"),
                Error::invalid_character_in_field("ExposeRunnerDecl", "source.child.name", '^'),
                Error::invalid_character_in_field("ExposeRunnerDecl", "source_name", '/'),
                Error::invalid_character_in_field("ExposeRunnerDecl", "target_name", '!'),
                Error::invalid_character_in_field("ExposeResolverDecl", "source.child.name", '^'),
                Error::invalid_character_in_field("ExposeResolverDecl", "source_name", '/'),
                Error::invalid_character_in_field("ExposeResolverDecl", "target_name", '!'),
            ])),
        },
        test_validate_exposes_invalid_source_target => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: None,
                        source_path: Some("/a".to_string()),
                        target_path: Some("/b".to_string()),
                        target: None,
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Realm(RealmRef {})),
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
                        source: Some(Ref::Storage(StorageRef {name: "a".to_string()})),
                        source_path: Some("/g".to_string()),
                        target_path: Some("/h".to_string()),
                        target: Some(Ref::Storage(StorageRef {name: "a".to_string()})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Storage(StorageRef {name: "a".to_string()})),
                        source_name: Some("a".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        target_name: Some("b".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Storage(StorageRef {name: "a".to_string()})),
                        source_name: Some("a".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        target_name: Some("b".to_string()),
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
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("a".repeat(101)),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("b".repeat(101)),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("a".repeat(101)),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("b".repeat(101)),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ExposeServiceDecl", "source.child.name"),
                Error::field_too_long("ExposeServiceDecl", "source_path"),
                Error::field_too_long("ExposeServiceDecl", "target_path"),
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
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                        target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/data/netstack".to_string()),
                        target_path: Some("/data".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
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
                decl.runners = Some(vec![RunnerDecl{
                    name: Some("source_elf".to_string()),
                    source: Some(Ref::Self_(SelfRef{})),
                    source_path: Some("/path".to_string()),
                }]);
                decl.resolvers = Some(vec![ResolverDecl {
                    name: Some("source_pkg".to_string()),
                    source_path: Some("/path".to_string()),
                }]);
                decl.exposes = Some(vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger2".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger3".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }),
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/netstack".to_string()),
                        target_path: Some("/svc/fuchsia.net.Stack".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/netstack2".to_string()),
                        target_path: Some("/svc/fuchsia.net.Stack".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("pkg".to_string()),
                    }),
                    ExposeDecl::Resolver(ExposeResolverDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_pkg".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("pkg".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("ExposeServiceDecl", "target_path",
                                       "/svc/fuchsia.logger.Log"),
                Error::duplicate_field("ExposeDirectoryDecl", "target_path",
                                       "/svc/fuchsia.logger.Log"),
                Error::duplicate_field("ExposeRunnerDecl", "target_name",
                                       "elf"),
                Error::duplicate_field("ExposeResolverDecl", "target_name", "pkg"),
            ])),
        },
        test_validate_exposes_invalid_runner_from_self => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                // We are attempting to expose a runner from "self", but we don't
                // acutally declare a runner.
                Error::invalid_field("ExposeRunnerDecl", "source"),
            ])),
        },

        // offers
        test_validate_offers_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: None,
                        source_path: None,
                        target: None,
                        target_path: None,
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
                        type_: None,
                        source: None,
                        target: None,
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
                Error::missing_field("OfferServiceDecl", "source_path"),
                Error::missing_field("OfferServiceDecl", "target"),
                Error::missing_field("OfferServiceDecl", "target_path"),
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
                Error::missing_field("OfferStorageDecl", "type"),
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
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "b".repeat(101),
                               collection: None,
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef {})),
                        source_path: Some("/a".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "b".repeat(101),
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
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
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef {})),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "b".repeat(101),
                                collection: None,
                            }
                        )),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef {})),
                        target: Some(Ref::Collection(
                            CollectionRef { name: "b".repeat(101) }
                        )),
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
                        source: Some(Ref::Realm(RealmRef {})),
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
                Error::field_too_long("OfferServiceDecl", "source_path"),
                Error::field_too_long("OfferServiceDecl", "target.child.name"),
                Error::field_too_long("OfferServiceDecl", "target_path"),
                Error::field_too_long("OfferServiceDecl", "target.collection.name"),
                Error::field_too_long("OfferServiceDecl", "target_path"),
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
                decl.offers = Some(vec![OfferDecl::Directory(OfferDirectoryDecl {
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
                })]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("OfferDirectoryDecl", "target", "logger"),
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
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("/data/realm_assets".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("/data/realm_assets".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef{ })),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
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
                        source_path: Some("foo/".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "%bad".to_string(),
                            collection: None,
                        })),
                        target_path: Some("/".to_string()),
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
                        source: Some(Ref::Realm(RealmRef {})),
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
                Error::invalid_character_in_field("OfferServiceDecl", "source.child.name", '^'),
                Error::invalid_field("OfferServiceDecl", "source_path"),
                Error::invalid_character_in_field("OfferServiceDecl", "target.child.name", '%'),
                Error::invalid_field("OfferServiceDecl", "target_path"),
                Error::invalid_character_in_field("OfferProtocolDecl", "source.child.name", '^'),
                Error::invalid_field("OfferProtocolDecl", "source_path"),
                Error::invalid_character_in_field("OfferProtocolDecl", "target.child.name", '%'),
                Error::invalid_field("OfferProtocolDecl", "target_path"),
                Error::invalid_character_in_field("OfferDirectoryDecl", "source.child.name", '^'),
                Error::invalid_field("OfferDirectoryDecl", "source_path"),
                Error::invalid_character_in_field("OfferDirectoryDecl", "target.child.name", '%'),
                Error::invalid_field("OfferDirectoryDecl", "target_path"),
                Error::invalid_field("OfferDirectoryDecl", "subdir"),
                Error::invalid_character_in_field("OfferRunnerDecl", "source.child.name", '^'),
                Error::invalid_character_in_field("OfferRunnerDecl", "source_name", '/'),
                Error::invalid_character_in_field("OfferRunnerDecl", "target.child.name", '%'),
                Error::invalid_character_in_field("OfferRunnerDecl", "target_name", '!'),
                Error::invalid_character_in_field("OfferResolverDecl", "source.child.name", '^'),
                Error::invalid_character_in_field("OfferResolverDecl", "source_name", '/'),
                Error::invalid_character_in_field("OfferResolverDecl", "target.child.name", '%'),
                Error::invalid_character_in_field("OfferResolverDecl", "target_name", '!'),
                Error::invalid_character_in_field("OfferEventDecl", "source_name", '/'),
                Error::invalid_character_in_field("OfferEventDecl", "target.child.name", '%'),
                Error::invalid_character_in_field("OfferEventDecl", "target_name", '/'),
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
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/logger".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/legacy_logger".to_string()),
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data".to_string()),
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
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Storage(StorageRef {
                            name: "minfs".to_string(),
                        })),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            }
                        )),
                    })
                ]),
                children: Some(vec![
                    ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(StartupMode::Lazy),
                        environment: None,
                    },
                ]),
                storage: Some(vec![
                    StorageDecl {
                        name: Some("minfs".to_string()),
                        source_path: Some("/minfs".to_string()),
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                    }
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
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data/realm_assets".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data/legacy_realm_assets".to_string()),
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                ]);
                decl.storage = Some(vec![
                    StorageDecl {
                        name: Some("memfs".to_string()),
                        source_path: Some("/memfs".to_string()),
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                    },
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
        test_validate_offers_target_duplicate_path => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::Strong),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some(DependencyType::WeakForMigration),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Realm(RealmRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Realm(RealmRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Realm(RealmRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Realm(RealmRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source: Some(Ref::Realm(RealmRef {})),
                        source_name: Some("stopped".to_string()),
                        target: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        target_name: Some("started".to_string()),
                        filter: None,
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source: Some(Ref::Realm(RealmRef {})),
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
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("OfferServiceDecl", "target_path", "/data"),
                Error::duplicate_field("OfferDirectoryDecl", "target_path", "/data"),
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
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
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
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef{})),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef{})),
                        target: Some(Ref::Collection(
                            CollectionRef { name: "modular".to_string(), }
                        )),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Realm(RealmRef{})),
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
                        source: Some(Ref::Realm(RealmRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("elf".to_string()),
                    }),
                    OfferDecl::Resolver(OfferResolverDecl {
                        source: Some(Ref::Realm(RealmRef{})),
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
                        source: Some(Ref::Realm(RealmRef{})),
                        source_name: Some("pkg".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("pkg".to_string()),
                    }),
                    OfferDecl::Event(OfferEventDecl {
                        source_name: Some("started".to_string()),
                        source: Some(Ref::Realm(RealmRef {})),
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
                        source: Some(Ref::Realm(RealmRef {})),
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
                        Ref::Storage(StorageRef {name: "a".to_string()}),
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
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("OfferEventDecl", "source"),
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
                Error::offer_dependency_cycle(),
            ])),
        },

        // environments
        test_validate_environment_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: None,
                    extends: None,
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
                    resolvers: None,
                    stop_timeout_ms: Some(1234),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("EnvironmentDecl", "name"),
            ])),
        },
        test_validate_environment_empty_resolver_fields => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
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
                Error::missing_field("ResolverRegistration", "resolver"),
                Error::missing_field("ResolverRegistration", "source"),
                Error::missing_field("ResolverRegistration", "scheme"),
            ])),
        },
        test_validate_environment_long_resolver_fields => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("a".repeat(101)),
                            source: Some(Ref::Realm(RealmRef{})),
                            scheme: Some("a".repeat(101)),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ResolverRegistration", "resolver"),
                Error::field_too_long("ResolverRegistration", "scheme"),
            ])),
        },
        test_validate_environment_invalid_resolver_fields => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
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
                Error::invalid_character_in_field("ResolverRegistration", "resolver", '^'),
                Error::invalid_field("ResolverRegistration", "source"),
                Error::invalid_character_in_field("ResolverRegistration", "scheme", '9'),
            ])),
        },
        test_validate_environment_duplicate_resolver_schemes => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
                    resolvers: Some(vec![
                        ResolverRegistration {
                            resolver: Some("pkg_resolver".to_string()),
                            source: Some(Ref::Realm(RealmRef{})),
                            scheme: Some("fuchsia-pkg".to_string()),
                        },
                        ResolverRegistration {
                            resolver: Some("base_resolver".to_string()),
                            source: Some(Ref::Realm(RealmRef{})),
                            scheme: Some("fuchsia-pkg".to_string()),
                        },
                    ]),
                    stop_timeout_ms: Some(1234),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("ResolverRegistration", "scheme", "fuchsia-pkg"),
            ])),
        },
        test_validate_environment_resolver_from_missing_child => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("a".to_string()),
                    extends: Some(EnvironmentExtends::None),
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
                Error::invalid_child("ResolverRegistration", "source", "missing"),
            ])),
        },
        test_validate_environment_resolver_child_cycle => {
            input = {
                let mut decl = new_component_decl();
                decl.environments = Some(vec![EnvironmentDecl {
                    name: Some("env".to_string()),
                    extends: Some(EnvironmentExtends::None),
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
                Error::resolver_dependency_cycle(),
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
                Error::invalid_character_in_field("ChildDecl", "name", '^'),
                Error::invalid_character_in_field("ChildDecl", "url", '&'),
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
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_character_in_field("CollectionDecl", "name", '^'),
            ])),
        },
        test_validate_collections_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl{
                    name: Some("a".repeat(1025)),
                    durability: Some(Durability::Transient),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("CollectionDecl", "name"),
            ])),
        },

        // storage
        test_validate_storage_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.storage = Some(vec![StorageDecl{
                    name: Some("a".repeat(101)),
                    source_path: Some(format!("/{}", "a".repeat(1024))),
                    source: Some(Ref::Self_(SelfRef{})),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("StorageDecl", "source_path"),
                Error::field_too_long("StorageDecl", "name"),
            ])),
        },

        // runners
        test_validate_runners_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![RunnerDecl{
                    name: None,
                    source: None,
                    source_path: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("RunnerDecl", "source"),
                Error::missing_field("RunnerDecl", "name"),
                Error::missing_field("RunnerDecl", "source_path"),
            ])),
        },
        test_validate_runners_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![RunnerDecl{
                    name: Some("^bad".to_string()),
                    source: Some(Ref::Collection(CollectionRef {
                        name: "/bad".to_string()
                    })),
                    source_path: Some("&bad".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("RunnerDecl", "source"),
                Error::invalid_character_in_field("RunnerDecl", "name", '^'),
                Error::invalid_field("RunnerDecl", "source_path"),
            ])),
        },
        test_validate_runners_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![RunnerDecl{
                    name: Some("elf".to_string()),
                    source: Some(Ref::Collection(CollectionRef {
                        name: "invalid".to_string(),
                    })),
                    source_path: Some("/elf".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("RunnerDecl", "source"),
            ])),
        },
        test_validate_runners_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![
                    RunnerDecl{
                        name: Some("a".repeat(101)),
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("RunnerDecl", "source.child.name"),
                Error::field_too_long("RunnerDecl", "name"),
                Error::field_too_long("RunnerDecl", "source_path"),
            ])),
        },
        test_validate_runners_duplicate_name => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![
                    RunnerDecl {
                        name: Some("elf".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/elf".to_string()),
                    },
                    RunnerDecl {
                        name: Some("elf".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/elf2".to_string()),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("RunnerDecl", "name", "elf"),
            ])),
        },

        // Resolvers
        test_validate_resolvers_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.resolvers = Some(vec![ResolverDecl{
                    name: None,
                    source_path: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ResolverDecl", "name"),
                Error::missing_field("ResolverDecl", "source_path")
            ])),
        },
        test_validate_resolvers_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.resolvers = Some(vec![ResolverDecl{
                    name: Some("^bad".to_string()),
                    source_path: Some("&bad".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_character_in_field("ResolverDecl", "name", '^'),
                Error::invalid_field("ResolverDecl", "source_path")
            ])),
        },
        test_validate_resolvers_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.resolvers = Some(vec![ResolverDecl{
                    name: Some("a".repeat(101)),
                    source_path: Some(format!("/{}", "f".repeat(1024))),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ResolverDecl", "name"),
                Error::field_too_long("ResolverDecl", "source_path")
            ])),
        },
        test_validate_resolvers_duplicate_name => {
            input = {
                let mut decl = new_component_decl();
                decl.resolvers = Some(vec![ResolverDecl{
                    name: Some("a".to_string()),
                    source_path: Some("/foo".to_string()),
                },
                ResolverDecl {
                    name: Some("a".to_string()),
                    source_path: Some("/bar".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("ResolverDecl", "name", "a"),
            ])),
        },
        test_validate_resolvers_missing_from_offer => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![OfferDecl::Resolver(OfferResolverDecl{
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
                Error::invalid_resolver("OfferResolverDecl", "source", "a"),
            ])),
        },
        test_validate_resolvers_missing_from_expose => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![ExposeDecl::Resolver(ExposeResolverDecl{
                    source: Some(Ref::Self_(SelfRef {})),
                    source_name: Some("a".to_string()),
                    target: Some(Ref::Realm(RealmRef {})),
                    target_name: Some("a".to_string()),
                })]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_resolver("ExposeResolverDecl", "source", "a"),
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
                source_path: Some(format!("/thing")),
                target_path: Some(format!("/thing")),
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
