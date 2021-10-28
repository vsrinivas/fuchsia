// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[allow(dead_code)]
pub(crate) mod convert;
pub(crate) mod util;

pub mod error;

#[cfg(test)]
mod tests;

macro_rules! cm_fidl_validator {
    ($namespace:ident) => {
        pub mod $namespace {

            // `fidl_fuchsia_sys2` is unused when generating `fdecl` code;
            // `fdecl` is unused when generating `fsys` code.
            #[allow(unused_imports)]
            use {
                crate::{convert as fdecl, error::*, util::*},
                directed_graph::DirectedGraph,
                fidl_fuchsia_sys2 as fsys,
                itertools::Itertools,
                std::{
                    collections::{HashMap, HashSet},
                    fmt,
                    path::Path,
                },
            };

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
            pub fn validate(decl: &$namespace::ComponentDecl) -> Result<(), ErrorList> {
                let ctx = ValidationContext::default();
                ctx.validate(decl).map_err(|errs| ErrorList::new(errs))
            }

            /// Validates a list of CapabilityDecls independently.
            pub fn validate_capabilities(
                capabilities: &Vec<$namespace::CapabilityDecl>,
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

            /// Validates an independent ChildDecl. Performs the same validation on it as `validate`.
            pub fn validate_child(child: &$namespace::ChildDecl) -> Result<(), ErrorList> {
                let mut errors = vec![];
                check_name(child.name.as_ref(), "ChildDecl", "name", &mut errors);
                check_url(child.url.as_ref(), "ChildDecl", "url", &mut errors);
                if child.startup.is_none() {
                    errors.push(Error::missing_field("ChildDecl", "startup"));
                }
                // Allow `on_terminate` to be unset since the default is almost always desired.
                if child.environment.is_some() {
                    check_name(child.environment.as_ref(), "ChildDecl", "environment", &mut errors);
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
            pub fn validate_dynamic_offers(
                offers: &Vec<$namespace::OfferDecl>,
            ) -> Result<(), ErrorList> {
                let mut ctx = ValidationContext::default();
                for offer in offers {
                    ctx.validate_offers_decl(offer, OfferType::Dynamic)
                }
                if ctx.errors.is_empty() {
                    Ok(())
                } else {
                    Err(ErrorList::new(ctx.errors))
                }
            }

            #[derive(Default)]
            struct ValidationContext<'a> {
                all_children: HashMap<&'a str, &'a $namespace::ChildDecl>,
                all_collections: HashSet<&'a str>,
                all_capability_ids: HashSet<&'a str>,
                all_storage_and_sources: HashMap<&'a str, Option<&'a $namespace::Ref>>,
                all_services: HashSet<&'a str>,
                all_protocols: HashSet<&'a str>,
                all_directories: HashSet<&'a str>,
                all_runners: HashSet<&'a str>,
                all_resolvers: HashSet<&'a str>,
                all_environment_names: HashSet<&'a str>,
                all_events: HashMap<&'a str, $namespace::EventMode>,
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
                fn try_from_ref(ref_: Option<&'a $namespace::Ref>) -> Option<DependencyNode<'a>> {
                    if ref_.is_none() {
                        return None;
                    }
                    match ref_.unwrap() {
                        $namespace::Ref::Child($namespace::ChildRef { name, .. }) => {
                            Some(DependencyNode::Child(name.as_str()))
                        }
                        $namespace::Ref::Collection($namespace::CollectionRef { name, .. }) => {
                            Some(DependencyNode::Collection(name.as_str()))
                        }
                        $namespace::Ref::Capability($namespace::CapabilityRef { name, .. }) => {
                            Some(DependencyNode::Capability(name.as_str()))
                        }
                        $namespace::Ref::Self_(_) => Some(DependencyNode::Self_),
                        $namespace::Ref::Parent(_) => {
                            // We don't care about dependency cycles with the parent, as any potential issues
                            // with that are resolved by cycle detection in the parent's manifest.
                            None
                        }
                        $namespace::Ref::Framework(_) => {
                            // We don't care about dependency cycles with the framework, as the framework
                            // always outlives the component.
                            None
                        }
                        $namespace::Ref::Debug(_) => {
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
                fn validate(
                    mut self,
                    decl: &'a $namespace::ComponentDecl,
                ) -> Result<(), Vec<Error>> {
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
                    }

                    // Validate "environments" after all other declarations are processed.
                    if let Some(environment) = decl.environments.as_ref() {
                        for environment in environment {
                            self.validate_environment_decl(&environment);
                        }
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
                fn collect_environment_names(&mut self, envs: &'a [$namespace::EnvironmentDecl]) {
                    for env in envs {
                        if let Some(name) = env.name.as_ref() {
                            if !self.all_environment_names.insert(name) {
                                self.errors.push(Error::duplicate_field(
                                    "EnvironmentDecl",
                                    "name",
                                    name,
                                ));
                            }
                        }
                    }
                }

                /// Validates an individual capability declaration as either a built-in capability or (if
                /// `as_builtin = false`) as a component or namespace capability.
                // Storage capabilities are not currently allowed as built-ins, but there's no deep reason for this.
                // Update this method to allow built-in storage capabilities as needed.
                fn validate_capability_decl(
                    &mut self,
                    capability: &'a $namespace::CapabilityDecl,
                    as_builtin: bool,
                ) {
                    match capability {
                        $namespace::CapabilityDecl::Service(service) => {
                            self.validate_service_decl(&service, as_builtin)
                        }
                        $namespace::CapabilityDecl::Protocol(protocol) => {
                            self.validate_protocol_decl(&protocol, as_builtin)
                        }
                        $namespace::CapabilityDecl::Directory(directory) => {
                            self.validate_directory_decl(&directory, as_builtin)
                        }
                        $namespace::CapabilityDecl::Storage(storage) => {
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
                        $namespace::CapabilityDecl::Runner(runner) => {
                            self.validate_runner_decl(&runner, as_builtin)
                        }
                        $namespace::CapabilityDecl::Resolver(resolver) => {
                            self.validate_resolver_decl(&resolver, as_builtin)
                        }
                        $namespace::CapabilityDecl::Event(event) => {
                            if as_builtin {
                                self.validate_event_decl(&event)
                            } else {
                                self.errors.push(Error::invalid_capability_type(
                                    "ComponentDecl",
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
                                    "ComponentDecl",
                                    "capability",
                                    "unknown",
                                ));
                            }
                        }
                    }
                }

                fn validate_use_decls(&mut self, uses: &'a [$namespace::UseDecl]) {
                    // Validate all events first so that we keep track of them for validation of event_streams.
                    for use_ in uses.iter() {
                        match use_ {
                            $namespace::UseDecl::Event(e) => self.validate_event(e),
                            _ => {}
                        }
                    }

                    // Validate individual fields.
                    for use_ in uses.iter() {
                        self.validate_use_decl(&use_);
                    }

                    self.validate_use_paths(&uses);
                }

                fn validate_use_decl(&mut self, use_: &'a $namespace::UseDecl) {
                    match use_ {
                        $namespace::UseDecl::Service(u) => {
                            self.validate_use_source(
                                u.source.as_ref(),
                                u.source_name.as_ref(),
                                u.dependency_type.as_ref(),
                                "UseServiceDecl",
                                "source",
                            );
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
                        $namespace::UseDecl::Protocol(u) => {
                            self.validate_use_source(
                                u.source.as_ref(),
                                u.source_name.as_ref(),
                                u.dependency_type.as_ref(),
                                "UseProtocolDecl",
                                "source",
                            );
                            check_name(
                                u.source_name.as_ref(),
                                "UseProtocolDecl",
                                "source_name",
                                &mut self.errors,
                            );
                            check_path(
                                u.target_path.as_ref(),
                                "UseProtocolDecl",
                                "target_path",
                                &mut self.errors,
                            );
                        }
                        $namespace::UseDecl::Directory(u) => {
                            self.validate_use_source(
                                u.source.as_ref(),
                                u.source_name.as_ref(),
                                u.dependency_type.as_ref(),
                                "UseDirectoryDecl",
                                "source",
                            );
                            check_name(
                                u.source_name.as_ref(),
                                "UseDirectoryDecl",
                                "source_name",
                                &mut self.errors,
                            );
                            check_path(
                                u.target_path.as_ref(),
                                "UseDirectoryDecl",
                                "target_path",
                                &mut self.errors,
                            );
                            if u.rights.is_none() {
                                self.errors
                                    .push(Error::missing_field("UseDirectoryDecl", "rights"));
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
                        $namespace::UseDecl::Storage(u) => {
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
                        $namespace::UseDecl::EventStream(e) => {
                            self.validate_event_stream(e);
                        }
                        $namespace::UseDecl::Event(_) => {
                            // Skip events. We must have already validated by this point.
                            // See `validate_use_decls`.
                        }
                        _ => {
                            self.errors.push(Error::invalid_field("ComponentDecl", "use"));
                        }
                    }
                }

                /// Validates the "program" declaration. This does not check runner-specific properties
                /// since those are checked by the runner.
                fn validate_program(&mut self, program: &$namespace::ProgramDecl) {
                    if program.runner.is_none() {
                        self.errors.push(Error::missing_field("ProgramDecl", "runner"));
                    }
                }

                /// Validates that paths-based capabilities (service, directory, protocol)
                /// are different and not prefixes of each other.
                fn validate_use_paths(&mut self, uses: &[$namespace::UseDecl]) {
                    #[derive(Debug, PartialEq, Clone, Copy)]
                    struct PathCapability<'a> {
                        decl: &'a str,
                        dir: &'a Path,
                        use_: &'a $namespace::UseDecl,
                    }
                    let mut used_paths = HashMap::new();
                    for use_ in uses.iter() {
                        match use_ {
                            $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                target_path: Some(path),
                                ..
                            })
                            | $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                target_path: Some(path),
                                ..
                            })
                            | $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                target_path: Some(path),
                                ..
                            }) => {
                                let capability = match use_ {
                                    $namespace::UseDecl::Service(_) => {
                                        let dir = match Path::new(path).parent() {
                                            Some(p) => p,
                                            None => continue, // Invalid path, validated elsewhere
                                        };
                                        PathCapability { decl: "UseServiceDecl", dir, use_ }
                                    }
                                    $namespace::UseDecl::Protocol(_) => {
                                        let dir = match Path::new(path).parent() {
                                            Some(p) => p,
                                            None => continue, // Invalid path, validated elsewhere
                                        };
                                        PathCapability { decl: "UseProtocolDecl", dir, use_ }
                                    }
                                    $namespace::UseDecl::Directory(_) => PathCapability {
                                        decl: "UseDirectoryDecl",
                                        dir: Path::new(path),
                                        use_,
                                    },
                                    _ => unreachable!(),
                                };
                                if used_paths.insert(path, capability).is_some() {
                                    // Disallow multiple capabilities for the same path.
                                    self.errors.push(Error::duplicate_field(
                                        capability.decl,
                                        "path",
                                        path,
                                    ));
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
                            (
                                $namespace::UseDecl::Directory(_),
                                $namespace::UseDecl::Directory(_),
                            ) => {
                                capability_b.dir == capability_a.dir
                                    || capability_b.dir.starts_with(capability_a.dir)
                                    || capability_a.dir.starts_with(capability_b.dir)
                            }

                            // Protocols and Services can't overlap with Directories.
                            (_, $namespace::UseDecl::Directory(_))
                            | ($namespace::UseDecl::Directory(_), _) => {
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

                fn validate_event(&mut self, event: &'a $namespace::UseEventDecl) {
                    self.validate_use_source(
                        event.source.as_ref(),
                        event.source_name.as_ref(),
                        event.dependency_type.as_ref(),
                        "UseEventDecl",
                        "source",
                    );
                    if let Some($namespace::Ref::Self_(_)) = event.source {
                        self.errors.push(Error::invalid_field("UseEventDecl", "source"));
                    }
                    check_name(
                        event.source_name.as_ref(),
                        "UseEventDecl",
                        "source_name",
                        &mut self.errors,
                    );
                    check_name(
                        event.target_name.as_ref(),
                        "UseEventDecl",
                        "target_name",
                        &mut self.errors,
                    );
                    check_events_mode(&event.mode, "UseEventDecl", "mode", &mut self.errors);
                    if let Some(target_name) = event.target_name.as_ref() {
                        if self
                            .all_events
                            .insert(target_name, event.mode.unwrap_or($namespace::EventMode::Async))
                            .is_some()
                        {
                            self.errors.push(Error::duplicate_field(
                                "UseEventDecl",
                                "target_name",
                                target_name,
                            ));
                        }
                    }
                }

                fn validate_event_stream(
                    &mut self,
                    event_stream: &'a $namespace::UseEventStreamDecl,
                ) {
                    check_name(
                        event_stream.name.as_ref(),
                        "UseEventStreamDecl",
                        "name",
                        &mut self.errors,
                    );
                    if let Some(name) = event_stream.name.as_ref() {
                        if !self.all_event_streams.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "UseEventStreamDecl",
                                "name",
                                name,
                            ));
                        }
                    }
                    match event_stream.subscriptions.as_ref() {
                        None => {
                            self.errors
                                .push(Error::missing_field("UseEventStreamDecl", "subscriptions"));
                        }
                        Some(subscriptions) if subscriptions.is_empty() => {
                            self.errors
                                .push(Error::empty_field("UseEventStreamDecl", "subscriptions"));
                        }
                        Some(subscriptions) => {
                            for subscription in subscriptions {
                                check_name(
                                    subscription.event_name.as_ref(),
                                    "UseEventStreamDecl",
                                    "event_name",
                                    &mut self.errors,
                                );
                                let event_name =
                                    subscription.event_name.clone().unwrap_or_default();
                                let event_mode =
                                    subscription.mode.unwrap_or($namespace::EventMode::Async);
                                match self.all_events.get(event_name.as_str()) {
                                    Some(mode) => {
                                        if *mode != $namespace::EventMode::Sync
                                            && event_mode == $namespace::EventMode::Sync
                                        {
                                            self.errors.push(Error::event_stream_unsupported_mode(
                                                "UseEventStreamDecl",
                                                "events",
                                                event_name,
                                                format!("{:?}", event_mode),
                                            ));
                                        }
                                    }
                                    None => {
                                        self.errors.push(Error::event_stream_event_not_found(
                                            "UseEventStreamDecl",
                                            "events",
                                            event_name,
                                        ));
                                    }
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
                    source: Option<&'a $namespace::Ref>,
                    source_name: Option<&'a String>,
                    dependency_type: Option<&$namespace::DependencyType>,
                    decl: &str,
                    field: &str,
                ) {
                    match source {
                        Some($namespace::Ref::Parent(_)) => {}
                        Some($namespace::Ref::Framework(_)) => {}
                        Some($namespace::Ref::Debug(_)) => {}
                        Some($namespace::Ref::Self_(_)) => {}
                        Some($namespace::Ref::Capability(capability)) => {
                            if !self.all_capability_ids.contains(capability.name.as_str()) {
                                self.errors.push(Error::invalid_capability(
                                    decl,
                                    field,
                                    &capability.name,
                                ));
                            } else if dependency_type == Some(&$namespace::DependencyType::Strong) {
                                self.add_strong_dep(
                                    source_name,
                                    DependencyNode::try_from_ref(source),
                                    Some(DependencyNode::Self_),
                                );
                            }
                        }
                        Some($namespace::Ref::Child(child)) => {
                            if !self.all_children.contains_key(&child.name as &str) {
                                self.errors.push(Error::invalid_child(decl, field, &child.name));
                            } else if dependency_type == Some(&$namespace::DependencyType::Strong) {
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

                    let is_use_from_child = match source {
                        Some($namespace::Ref::Child(_)) => true,
                        _ => false,
                    };
                    match (is_use_from_child, dependency_type) {
                        (
                            false,
                            Some($namespace::DependencyType::Weak)
                            | Some($namespace::DependencyType::WeakForMigration),
                        ) => {
                            self.errors.push(Error::invalid_field(decl, "dependency_type"));
                        }
                        _ => {}
                    }
                }

                fn validate_child_decl(&mut self, child: &'a $namespace::ChildDecl) {
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
                            self.add_strong_dep(None, Some(source), Some(target));
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

                fn validate_collection_decl(&mut self, collection: &'a $namespace::CollectionDecl) {
                    let name = collection.name.as_ref();
                    if check_name(name, "CollectionDecl", "name", &mut self.errors) {
                        let name: &str = name.unwrap();
                        if !self.all_collections.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "CollectionDecl",
                                "name",
                                name,
                            ));
                        }
                    }
                    if collection.durability.is_none() {
                        self.errors.push(Error::missing_field("CollectionDecl", "durability"));
                    }
                    // Allow `allowed_offers` to be unset, for backwards compatibility.
                    if let Some(environment) = collection.environment.as_ref() {
                        if !self.all_environment_names.contains(environment.as_str()) {
                            self.errors.push(Error::invalid_environment(
                                "CollectionDecl",
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
                }

                fn validate_environment_decl(
                    &mut self,
                    environment: &'a $namespace::EnvironmentDecl,
                ) {
                    let name = environment.name.as_ref();
                    check_name(name, "EnvironmentDecl", "name", &mut self.errors);
                    if environment.extends.is_none() {
                        self.errors.push(Error::missing_field("EnvironmentDecl", "extends"));
                    }
                    if let Some(runners) = environment.runners.as_ref() {
                        let mut registered_runners = HashSet::new();
                        for runner in runners {
                            self.validate_runner_registration(
                                runner,
                                name.clone(),
                                &mut registered_runners,
                            );
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
                        Some($namespace::EnvironmentExtends::None) => {
                            if environment.stop_timeout_ms.is_none() {
                                self.errors.push(Error::missing_field(
                                    "EnvironmentDecl",
                                    "stop_timeout_ms",
                                ));
                            }
                        }
                        None | Some($namespace::EnvironmentExtends::Realm) => {}
                    }

                    if let Some(debugs) = environment.debug_capabilities.as_ref() {
                        for debug in debugs {
                            self.validate_environment_debug_registration(debug, name.clone());
                        }
                    }
                }

                fn validate_runner_registration(
                    &mut self,
                    runner_registration: &'a $namespace::RunnerRegistration,
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
                    if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                        (&runner_registration.source, &runner_registration.source_name)
                    {
                        if !self.all_runners.contains(name as &str) {
                            self.errors.push(Error::invalid_runner(
                                "RunnerRegistration",
                                "source_name",
                                name,
                            ));
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
                            self.errors.push(Error::duplicate_field(
                                "RunnerRegistration",
                                "target_name",
                                name,
                            ));
                        }
                    }
                }

                fn validate_resolver_registration(
                    &mut self,
                    resolver_registration: &'a $namespace::ResolverRegistration,
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
                            self.errors.push(Error::duplicate_field(
                                "ResolverRegistration",
                                "scheme",
                                scheme,
                            ));
                        }
                    }
                }

                fn validate_registration_source(
                    &mut self,
                    environment_name: Option<&'a String>,
                    source: Option<&'a $namespace::Ref>,
                    ty: &str,
                ) {
                    match source {
                        Some($namespace::Ref::Parent(_)) => {}
                        Some($namespace::Ref::Self_(_)) => {}
                        Some($namespace::Ref::Child(child_ref)) => {
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

                fn validate_service_decl(
                    &mut self,
                    service: &'a $namespace::ServiceDecl,
                    as_builtin: bool,
                ) {
                    if check_name(service.name.as_ref(), "ServiceDecl", "name", &mut self.errors) {
                        let name = service.name.as_ref().unwrap();
                        if !self.all_capability_ids.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "ServiceDecl",
                                "name",
                                name.as_str(),
                            ));
                        }
                        self.all_services.insert(name);
                    }
                    match as_builtin {
                        true => {
                            if let Some(path) = service.source_path.as_ref() {
                                self.errors.push(Error::extraneous_source_path("ServiceDecl", path))
                            }
                        }
                        false => {
                            check_path(
                                service.source_path.as_ref(),
                                "ServiceDecl",
                                "source_path",
                                &mut self.errors,
                            );
                        }
                    }
                }

                fn validate_protocol_decl(
                    &mut self,
                    protocol: &'a $namespace::ProtocolDecl,
                    as_builtin: bool,
                ) {
                    if check_name(protocol.name.as_ref(), "ProtocolDecl", "name", &mut self.errors)
                    {
                        let name = protocol.name.as_ref().unwrap();
                        if !self.all_capability_ids.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "ProtocolDecl",
                                "name",
                                name.as_str(),
                            ));
                        }
                        self.all_protocols.insert(name);
                    }
                    match as_builtin {
                        true => {
                            if let Some(path) = protocol.source_path.as_ref() {
                                self.errors
                                    .push(Error::extraneous_source_path("ProtocolDecl", path))
                            }
                        }
                        false => {
                            check_path(
                                protocol.source_path.as_ref(),
                                "ProtocolDecl",
                                "source_path",
                                &mut self.errors,
                            );
                        }
                    }
                }

                fn validate_directory_decl(
                    &mut self,
                    directory: &'a $namespace::DirectoryDecl,
                    as_builtin: bool,
                ) {
                    if check_name(
                        directory.name.as_ref(),
                        "DirectoryDecl",
                        "name",
                        &mut self.errors,
                    ) {
                        let name = directory.name.as_ref().unwrap();
                        if !self.all_capability_ids.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "DirectoryDecl",
                                "name",
                                name.as_str(),
                            ));
                        }
                        self.all_directories.insert(name);
                    }
                    match as_builtin {
                        true => {
                            if let Some(path) = directory.source_path.as_ref() {
                                self.errors
                                    .push(Error::extraneous_source_path("DirectoryDecl", path))
                            }
                        }
                        false => {
                            check_path(
                                directory.source_path.as_ref(),
                                "DirectoryDecl",
                                "source_path",
                                &mut self.errors,
                            );
                        }
                    }
                    if directory.rights.is_none() {
                        self.errors.push(Error::missing_field("DirectoryDecl", "rights"));
                    }
                }

                fn validate_storage_decl(&mut self, storage: &'a $namespace::StorageDecl) {
                    match storage.source.as_ref() {
                        Some($namespace::Ref::Parent(_)) => {}
                        Some($namespace::Ref::Self_(_)) => {}
                        Some($namespace::Ref::Child(child)) => {
                            self.validate_source_child(child, "StorageDecl", OfferType::Static);
                        }
                        Some(_) => {
                            self.errors.push(Error::invalid_field("StorageDecl", "source"));
                        }
                        None => {
                            self.errors.push(Error::missing_field("StorageDecl", "source"));
                        }
                    };
                    if check_name(storage.name.as_ref(), "StorageDecl", "name", &mut self.errors) {
                        let name = storage.name.as_ref().unwrap();
                        if !self.all_capability_ids.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "StorageDecl",
                                "name",
                                name.as_str(),
                            ));
                        }
                        self.all_storage_and_sources.insert(name, storage.source.as_ref());
                    }
                    if storage.storage_id.is_none() {
                        self.errors.push(Error::missing_field("StorageDecl", "storage_id"));
                    }
                    check_name(
                        storage.backing_dir.as_ref(),
                        "StorageDecl",
                        "backing_dir",
                        &mut self.errors,
                    );
                }

                fn validate_runner_decl(
                    &mut self,
                    runner: &'a $namespace::RunnerDecl,
                    as_builtin: bool,
                ) {
                    if check_name(runner.name.as_ref(), "RunnerDecl", "name", &mut self.errors) {
                        let name = runner.name.as_ref().unwrap();
                        if !self.all_capability_ids.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "RunnerDecl",
                                "name",
                                name.as_str(),
                            ));
                        }
                        self.all_runners.insert(name);
                    }
                    match as_builtin {
                        true => {
                            if let Some(path) = runner.source_path.as_ref() {
                                self.errors.push(Error::extraneous_source_path("RunnerDecl", path))
                            }
                        }
                        false => {
                            check_path(
                                runner.source_path.as_ref(),
                                "RunnerDecl",
                                "source_path",
                                &mut self.errors,
                            );
                        }
                    }
                }

                fn validate_resolver_decl(
                    &mut self,
                    resolver: &'a $namespace::ResolverDecl,
                    as_builtin: bool,
                ) {
                    if check_name(resolver.name.as_ref(), "ResolverDecl", "name", &mut self.errors)
                    {
                        let name = resolver.name.as_ref().unwrap();
                        if !self.all_capability_ids.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "ResolverDecl",
                                "name",
                                name.as_str(),
                            ));
                        }
                        self.all_resolvers.insert(name);
                    }
                    match as_builtin {
                        true => {
                            if let Some(path) = resolver.source_path.as_ref() {
                                self.errors
                                    .push(Error::extraneous_source_path("ResolverDecl", path))
                            }
                        }
                        false => {
                            check_path(
                                resolver.source_path.as_ref(),
                                "ResolverDecl",
                                "source_path",
                                &mut self.errors,
                            );
                        }
                    }
                }

                fn validate_environment_debug_registration(
                    &mut self,
                    debug: &'a $namespace::DebugRegistration,
                    environment_name: Option<&'a String>,
                ) {
                    match debug {
                        $namespace::DebugRegistration::Protocol(o) => {
                            let decl = "DebugProtocolRegistration";
                            self.validate_environment_debug_fields(
                                decl,
                                o.source.as_ref(),
                                o.source_name.as_ref(),
                                o.target_name.as_ref(),
                            );

                            if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                (&o.source, &o.source_name)
                            {
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
                            self.errors.push(Error::invalid_field("EnvironmentDecl", "debug"));
                        }
                    }
                }

                fn validate_environment_debug_fields(
                    &mut self,
                    decl: &str,
                    source: Option<&$namespace::Ref>,
                    source_name: Option<&String>,
                    target_name: Option<&'a String>,
                ) {
                    // We don't support "source" from "capability" for now.
                    match source {
                        Some($namespace::Ref::Parent(_)) => {}
                        Some($namespace::Ref::Self_(_)) => {}
                        Some($namespace::Ref::Framework(_)) => {}
                        Some($namespace::Ref::Child(child)) => {
                            self.validate_source_child(child, decl, OfferType::Static)
                        }
                        Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
                        None => self.errors.push(Error::missing_field(decl, "source")),
                    }
                    check_name(source_name, decl, "source_name", &mut self.errors);
                    check_name(target_name, decl, "target_name", &mut self.errors);
                }

                fn validate_event_decl(&mut self, event: &'a $namespace::EventDecl) {
                    if check_name(event.name.as_ref(), "EventDecl", "name", &mut self.errors) {
                        let name = event.name.as_ref().unwrap();
                        if !self.all_capability_ids.insert(name) {
                            self.errors.push(Error::duplicate_field(
                                "EventDecl",
                                "name",
                                name.as_str(),
                            ));
                        }
                    }
                }

                fn validate_source_child(
                    &mut self,
                    child: &$namespace::ChildRef,
                    decl_type: &str,
                    offer_type: OfferType,
                ) {
                    let mut valid = true;
                    valid &= check_name(
                        Some(&child.name),
                        decl_type,
                        "source.child.name",
                        &mut self.errors,
                    );
                    match offer_type {
                        OfferType::Static => {
                            valid &= if child.collection.is_some() {
                                self.errors.push(Error::extraneous_field(
                                    decl_type,
                                    "source.child.collection",
                                ));
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

                fn validate_source_collection(
                    &mut self,
                    collection: &$namespace::CollectionRef,
                    decl_type: &str,
                ) {
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

                fn validate_source_capability(
                    &mut self,
                    capability: &$namespace::CapabilityRef,
                    decl_type: &str,
                    field: &str,
                ) {
                    if !self.all_capability_ids.contains(capability.name.as_str()) {
                        self.errors.push(Error::invalid_capability(
                            decl_type,
                            field,
                            &capability.name,
                        ));
                    }
                }

                fn validate_storage_source(&mut self, source_name: &String, decl_type: &str) {
                    if check_name(
                        Some(source_name),
                        decl_type,
                        "source.storage.name",
                        &mut self.errors,
                    ) {
                        if !self.all_storage_and_sources.contains_key(source_name.as_str()) {
                            self.errors.push(Error::invalid_storage(
                                decl_type,
                                "source",
                                source_name,
                            ));
                        }
                    }
                }

                fn validate_expose_decl(
                    &mut self,
                    expose: &'a $namespace::ExposeDecl,
                    prev_target_ids: &mut HashMap<&'a str, AllowableIds>,
                ) {
                    match expose {
                        $namespace::ExposeDecl::Service(e) => {
                            let decl = "ExposeServiceDecl";
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
                            // If the expose source is `self`, ensure we have a corresponding ServiceDecl.
                            // TODO: Consider bringing this bit into validate_expose_fields.
                            if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                (&e.source, &e.source_name)
                            {
                                if !self.all_services.contains(&name as &str) {
                                    self.errors
                                        .push(Error::invalid_capability(decl, "source", name));
                                }
                            }
                        }
                        $namespace::ExposeDecl::Protocol(e) => {
                            let decl = "ExposeProtocolDecl";
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
                            // If the expose source is `self`, ensure we have a corresponding ProtocolDecl.
                            // TODO: Consider bringing this bit into validate_expose_fields.
                            if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                (&e.source, &e.source_name)
                            {
                                if !self.all_protocols.contains(&name as &str) {
                                    self.errors
                                        .push(Error::invalid_capability(decl, "source", name));
                                }
                            }
                        }
                        $namespace::ExposeDecl::Directory(e) => {
                            let decl = "ExposeDirectoryDecl";
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
                            // If the expose source is `self`, ensure we have a corresponding DirectoryDecl.
                            // TODO: Consider bringing this bit into validate_expose_fields.
                            if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                (&e.source, &e.source_name)
                            {
                                if !self.all_directories.contains(&name as &str) {
                                    self.errors
                                        .push(Error::invalid_capability(decl, "source", name));
                                }
                                if name.starts_with('/') && e.rights.is_none() {
                                    self.errors.push(Error::missing_field(decl, "rights"));
                                }
                            }

                            // Subdir makes sense when routing, but when exposing to framework the subdirectory
                            // can be exposed directly.
                            match e.target.as_ref() {
                                Some($namespace::Ref::Framework(_)) => {
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
                        $namespace::ExposeDecl::Runner(e) => {
                            let decl = "ExposeRunnerDecl";
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
                            // If the expose source is `self`, ensure we have a corresponding RunnerDecl.
                            if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                (&e.source, &e.source_name)
                            {
                                if !self.all_runners.contains(&name as &str) {
                                    self.errors
                                        .push(Error::invalid_capability(decl, "source", name));
                                }
                            }
                        }
                        $namespace::ExposeDecl::Resolver(e) => {
                            let decl = "ExposeResolverDecl";
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
                            // If the expose source is `self`, ensure we have a corresponding ResolverDecl.
                            if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                (&e.source, &e.source_name)
                            {
                                if !self.all_resolvers.contains(&name as &str) {
                                    self.errors
                                        .push(Error::invalid_capability(decl, "source", name));
                                }
                            }
                        }
                        _ => {
                            self.errors.push(Error::invalid_field("ComponentDecl", "expose"));
                        }
                    }
                }

                fn validate_expose_fields(
                    &mut self,
                    decl: &str,
                    allowable_ids: AllowableIds,
                    collection_source: CollectionSource,
                    source: Option<&$namespace::Ref>,
                    source_name: Option<&String>,
                    target_name: Option<&'a String>,
                    target: Option<&$namespace::Ref>,
                    prev_child_target_ids: &mut HashMap<&'a str, AllowableIds>,
                ) {
                    match source {
                        Some(r) => match r {
                            $namespace::Ref::Self_(_) => {}
                            $namespace::Ref::Framework(_) => {}
                            $namespace::Ref::Child(child) => {
                                self.validate_source_child(child, decl, OfferType::Static);
                            }
                            $namespace::Ref::Capability(c) => {
                                self.validate_source_capability(c, decl, "source");
                            }
                            $namespace::Ref::Collection(c)
                                if collection_source == CollectionSource::Allow =>
                            {
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
                            $namespace::Ref::Parent(_) => {}
                            $namespace::Ref::Framework(_) => {
                                if source != Some(&$namespace::Ref::Self_($namespace::SelfRef {})) {
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
                        if let Some(prev_state) =
                            prev_child_target_ids.insert(target_name, allowable_ids)
                        {
                            if prev_state == AllowableIds::One || prev_state != allowable_ids {
                                self.errors.push(Error::duplicate_field(
                                    decl,
                                    "target_name",
                                    target_name,
                                ));
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
                        let possible_storage_source = possible_storage_name
                            .map(|name| self.all_storage_and_sources.get(name))
                            .flatten();
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

                fn validate_offers_decl(
                    &mut self,
                    offer: &'a $namespace::OfferDecl,
                    offer_type: OfferType,
                ) {
                    match offer {
                        $namespace::OfferDecl::Service(o) => {
                            let decl = "OfferServiceDecl";
                            self.validate_offer_fields(
                                decl,
                                AllowableIds::Many,
                                CollectionSource::Allow,
                                offer_type,
                                o.source.as_ref(),
                                o.source_name.as_ref(),
                                o.target.as_ref(),
                                o.target_name.as_ref(),
                            );
                            match offer_type {
                                OfferType::Static => {
                                    // If the offer source is `self`, ensure we have a corresponding ServiceDecl.
                                    // TODO: Consider bringing this bit into validate_offer_fields
                                    if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
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
                        $namespace::OfferDecl::Protocol(o) => {
                            let decl = "OfferProtocolDecl";
                            self.validate_offer_fields(
                                decl,
                                AllowableIds::One,
                                CollectionSource::Deny,
                                offer_type,
                                o.source.as_ref(),
                                o.source_name.as_ref(),
                                o.target.as_ref(),
                                o.target_name.as_ref(),
                            );
                            if o.dependency_type.is_none() {
                                self.errors.push(Error::missing_field(decl, "dependency_type"));
                            } else if o.dependency_type == Some($namespace::DependencyType::Strong)
                            {
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
                                    // corresponding ProtocolDecl.
                                    // TODO: Consider bringing this bit into validate_offer_fields.
                                    if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                        (&o.source, &o.source_name)
                                    {
                                        if !self.all_protocols.contains(&name as &str) {
                                            self.errors.push(Error::invalid_capability(
                                                decl, "source", name,
                                            ));
                                        }
                                    }
                                }
                                OfferType::Dynamic => {}
                            }
                        }
                        $namespace::OfferDecl::Directory(o) => {
                            let decl = "OfferDirectoryDecl";
                            self.validate_offer_fields(
                                decl,
                                AllowableIds::One,
                                CollectionSource::Deny,
                                offer_type,
                                o.source.as_ref(),
                                o.source_name.as_ref(),
                                o.target.as_ref(),
                                o.target_name.as_ref(),
                            );
                            if o.dependency_type.is_none() {
                                self.errors.push(Error::missing_field(decl, "dependency_type"));
                            } else if o.dependency_type == Some($namespace::DependencyType::Strong)
                            {
                                match offer_type {
                                    OfferType::Static => {
                                        self.add_strong_dep(
                                            o.source_name.as_ref(),
                                            DependencyNode::try_from_ref(o.source.as_ref()),
                                            DependencyNode::try_from_ref(o.target.as_ref()),
                                        );
                                        // If the offer source is `self`, ensure we have a corresponding
                                        // DirectoryDecl.
                                        //
                                        // TODO: Consider bringing this bit into validate_offer_fields.
                                        if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                            (&o.source, &o.source_name)
                                        {
                                            if !self.all_directories.contains(&name as &str) {
                                                self.errors.push(Error::invalid_capability(
                                                    decl, "source", name,
                                                ));
                                            }
                                        }
                                    }
                                    OfferType::Dynamic => {}
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
                        $namespace::OfferDecl::Storage(o) => {
                            self.validate_storage_offer_fields(
                                "OfferStorageDecl",
                                offer_type,
                                o.source_name.as_ref(),
                                o.source.as_ref(),
                                o.target.as_ref(),
                            );

                            match offer_type {
                                OfferType::Static => {
                                    // Storage capabilities with a source of `Ref::Self_`
                                    // don't interact with the component's runtime in any
                                    // way, they're actually synthesized by the framework
                                    // out of a pre-existing directory capability. Thus, its
                                    // actual source is the backing directory capability.
                                    match (o.source.as_ref(), o.source_name.as_ref()) {
                                        (
                                            Some($namespace::Ref::Self_ { .. }),
                                            Some(source_name),
                                        ) => {
                                            if let Some(source) = DependencyNode::try_from_ref(
                                                *self
                                                    .all_storage_and_sources
                                                    .get(source_name.as_str())
                                                    .unwrap_or(&None),
                                            ) {
                                                if let Some(target) =
                                                    DependencyNode::try_from_ref(o.target.as_ref())
                                                {
                                                    self.strong_dependencies
                                                        .add_edge(source, target);
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
                        $namespace::OfferDecl::Runner(o) => {
                            let decl = "OfferRunnerDecl";
                            self.validate_offer_fields(
                                decl,
                                AllowableIds::One,
                                CollectionSource::Deny,
                                offer_type,
                                o.source.as_ref(),
                                o.source_name.as_ref(),
                                o.target.as_ref(),
                                o.target_name.as_ref(),
                            );
                            match offer_type {
                                OfferType::Static => {
                                    // If the offer source is `self`, ensure we have a corresponding RunnerDecl.
                                    if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                        (&o.source, &o.source_name)
                                    {
                                        if !self.all_runners.contains(&name as &str) {
                                            self.errors.push(Error::invalid_capability(
                                                decl, "source", name,
                                            ));
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
                        $namespace::OfferDecl::Resolver(o) => {
                            let decl = "OfferResolverDecl";
                            self.validate_offer_fields(
                                decl,
                                AllowableIds::One,
                                CollectionSource::Deny,
                                offer_type,
                                o.source.as_ref(),
                                o.source_name.as_ref(),
                                o.target.as_ref(),
                                o.target_name.as_ref(),
                            );

                            match offer_type {
                                OfferType::Static => {
                                    // If the offer source is `self`, ensure we have a
                                    // corresponding ResolverDecl.
                                    if let (Some($namespace::Ref::Self_(_)), Some(ref name)) =
                                        (&o.source, &o.source_name)
                                    {
                                        if !self.all_resolvers.contains(&name as &str) {
                                            self.errors.push(Error::invalid_capability(
                                                decl, "source", name,
                                            ));
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
                        $namespace::OfferDecl::Event(e) => {
                            self.validate_event_offer_fields(e, offer_type);
                        }
                        _ => {
                            self.errors.push(Error::invalid_field("ComponentDecl", "offer"));
                        }
                    }
                }

                /// Validates that the offer target is to a valid child or collection.
                fn validate_offer_target(
                    &mut self,
                    target: &'a Option<$namespace::Ref>,
                    decl_type: &str,
                    field_name: &str,
                ) -> Option<TargetId<'a>> {
                    match target.as_ref() {
                        Some($namespace::Ref::Child(child)) => {
                            if self.validate_child_ref(decl_type, field_name, &child) {
                                Some(TargetId::Component(&child.name))
                            } else {
                                None
                            }
                        }
                        Some($namespace::Ref::Collection(collection)) => {
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
                    source: Option<&$namespace::Ref>,
                    source_name: Option<&String>,
                    target: Option<&'a $namespace::Ref>,
                    target_name: Option<&'a String>,
                ) {
                    match source {
                        Some($namespace::Ref::Parent(_)) => {}
                        Some($namespace::Ref::Self_(_)) => {}
                        Some($namespace::Ref::Framework(_)) => {}
                        Some($namespace::Ref::Child(child)) => {
                            self.validate_source_child(child, decl, offer_type)
                        }
                        Some($namespace::Ref::Capability(c)) => {
                            self.validate_source_capability(c, decl, "source")
                        }
                        Some($namespace::Ref::Collection(c))
                            if collection_source == CollectionSource::Allow =>
                        {
                            self.validate_source_collection(c, decl)
                        }
                        Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
                        None => self.errors.push(Error::missing_field(decl, "source")),
                    }
                    check_name(source_name, decl, "source_name", &mut self.errors);
                    match (offer_type, target) {
                        (OfferType::Static, Some($namespace::Ref::Child(c))) => {
                            self.validate_target_child(
                                decl,
                                allowable_names,
                                c,
                                source,
                                target_name,
                            );
                        }
                        (OfferType::Static, Some($namespace::Ref::Collection(c))) => {
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
                    check_name(target_name, decl, "target_name", &mut self.errors);
                }

                fn validate_storage_offer_fields(
                    &mut self,
                    decl: &str,
                    offer_type: OfferType,
                    source_name: Option<&'a String>,
                    source: Option<&'a $namespace::Ref>,
                    target: Option<&'a $namespace::Ref>,
                ) {
                    if source_name.is_none() {
                        self.errors.push(Error::missing_field(decl, "source_name"));
                    }
                    match source {
                        Some($namespace::Ref::Parent(_)) => (),
                        Some($namespace::Ref::Self_(_)) => {
                            self.validate_storage_source(source_name.unwrap(), decl);
                        }
                        Some(_) => {
                            self.errors.push(Error::invalid_field(decl, "source"));
                        }
                        None => {
                            self.errors.push(Error::missing_field(decl, "source"));
                        }
                    }
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

                fn validate_event_offer_fields(
                    &mut self,
                    event: &'a $namespace::OfferEventDecl,
                    offer_type: OfferType,
                ) {
                    let decl = "OfferEventDecl";
                    check_name(event.source_name.as_ref(), decl, "source_name", &mut self.errors);

                    // Only parent and framework are valid.
                    match event.source {
                        Some($namespace::Ref::Parent(_)) => {}
                        Some($namespace::Ref::Framework(_)) => {}
                        Some(_) => {
                            self.errors.push(Error::invalid_field(decl, "source"));
                        }
                        None => {
                            self.errors.push(Error::missing_field(decl, "source"));
                        }
                    };

                    match offer_type {
                        OfferType::Static => {
                            let target_id =
                                self.validate_offer_target(&event.target, decl, "target");
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
                    check_name(event.target_name.as_ref(), decl, "target_name", &mut self.errors);
                    check_events_mode(&event.mode, "OfferEventDecl", "mode", &mut self.errors);
                }

                /// Check a `ChildRef` contains a valid child that exists.
                ///
                /// We ensure the target child is statically defined (i.e., not a dynamic child inside
                /// a collection).
                fn validate_child_ref(
                    &mut self,
                    decl: &str,
                    field_name: &str,
                    child: &$namespace::ChildRef,
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
                        self.errors.push(Error::extraneous_field(
                            decl,
                            format!("{}.child.collection", field_name),
                        ));
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
                    collection: &$namespace::CollectionRef,
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
                        self.errors.push(Error::invalid_collection(
                            decl,
                            field_name,
                            &collection.name as &str,
                        ));
                        return false;
                    }

                    true
                }

                fn validate_target_child(
                    &mut self,
                    decl: &str,
                    allowable_names: AllowableIds,
                    child: &'a $namespace::ChildRef,
                    source: Option<&$namespace::Ref>,
                    target_name: Option<&'a String>,
                ) {
                    if !self.validate_child_ref(decl, "target", child) {
                        return;
                    }
                    if let Some(target_name) = target_name {
                        let names_for_target = self
                            .target_ids
                            .entry(TargetId::Component(&child.name))
                            .or_insert(HashMap::new());
                        if let Some(prev_state) =
                            names_for_target.insert(target_name, allowable_names)
                        {
                            if prev_state == AllowableIds::One || prev_state != allowable_names {
                                self.errors.push(Error::duplicate_field(
                                    decl,
                                    "target_name",
                                    target_name as &str,
                                ));
                            }
                        }
                        if let Some(source) = source {
                            if let $namespace::Ref::Child(source_child) = source {
                                if source_child.name == child.name {
                                    self.errors.push(Error::offer_target_equals_source(
                                        decl,
                                        &child.name as &str,
                                    ));
                                }
                            }
                        }
                    }
                }

                fn validate_target_collection(
                    &mut self,
                    decl: &str,
                    allowable_names: AllowableIds,
                    collection: &'a $namespace::CollectionRef,
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
                        if let Some(prev_state) =
                            names_for_target.insert(target_name, allowable_names)
                        {
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
                    target: Option<&'a $namespace::Ref>,
                ) {
                    match target {
                        Some($namespace::Ref::Child(c)) => {
                            self.validate_child_ref(decl, "target", &c);
                        }
                        Some($namespace::Ref::Collection(c)) => {
                            self.validate_collection_ref(decl, "target", &c);
                        }
                        Some(_) => self.errors.push(Error::invalid_field(decl, "target")),
                        None => self.errors.push(Error::missing_field(decl, "target")),
                    }
                }
            }
            /// Events mode should be always present.
            fn check_events_mode(
                mode: &Option<$namespace::EventMode>,
                decl_type: &str,
                field_name: &str,
                errors: &mut Vec<Error>,
            ) {
                if mode.is_none() {
                    errors.push(Error::missing_field(decl_type, field_name));
                }
            }
        }
    };
}

cm_fidl_validator!(fsys);
cm_fidl_validator!(fdecl);
