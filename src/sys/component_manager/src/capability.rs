// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        moniker::{AbsoluteMoniker, ChildMoniker},
    },
    async_trait::async_trait,
    cm_rust::*,
    failure::Fail,
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    std::collections::HashSet,
};

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "Invalid scoped framework capability.")]
    InvalidScopedFrameworkCapability {},
    #[fail(display = "Invalid framework capability.")]
    InvalidFrameworkCapability {},
}

/// Describes the source of a capability, as determined by `find_capability_source`
#[derive(Clone, Debug)]
pub enum CapabilitySource {
    /// This capability originates from the component instance for the given Realm.
    /// point.
    Component { capability: ComponentCapability, source_moniker: AbsoluteMoniker },
    /// This capability originates from component manager itself and is optionally
    /// scoped to a component's realm.
    Framework { capability: FrameworkCapability, scope_moniker: Option<AbsoluteMoniker> },
    /// This capability originates from a storage declaration in a component's decl.  `StorageDecl`
    /// describes the backing directory capability offered to this realm, into which storage
    /// requests should be fed.
    StorageDecl(StorageDecl, AbsoluteMoniker),
}

impl CapabilitySource {
    pub fn path(&self) -> Option<&CapabilityPath> {
        match self {
            CapabilitySource::Component { capability, .. } => capability.source_path(),
            CapabilitySource::Framework { capability, .. } => capability.path(),
            CapabilitySource::StorageDecl(decl, _) => Some(&decl.source_path),
        }
    }
}

/// Describes a capability provided by the component manager which could either be
/// scoped to the realm, or global. Each capability type has a corresponding
/// `CapabilityPath` in the component manager's namespace. Note that this path may
/// not be unique as capabilities can compose.
#[derive(Debug, Clone)]
pub enum FrameworkCapability {
    Service(CapabilityPath),
    ServiceProtocol(CapabilityPath),
    Directory(CapabilityPath),
    Runner(CapabilityName),
}

impl FrameworkCapability {
    pub fn path(&self) -> Option<&CapabilityPath> {
        match self {
            FrameworkCapability::Service(source_path) => Some(&source_path),
            FrameworkCapability::ServiceProtocol(source_path) => Some(&source_path),
            FrameworkCapability::Directory(source_path) => Some(&source_path),
            _ => None,
        }
    }

    pub fn builtin_from_use_decl(decl: &UseDecl) -> Result<Self, Error> {
        match decl {
            UseDecl::Service(s) if s.source == UseSource::Realm => {
                Ok(FrameworkCapability::Service(s.source_path.clone()))
            }
            UseDecl::ServiceProtocol(s) if s.source == UseSource::Realm => {
                Ok(FrameworkCapability::ServiceProtocol(s.source_path.clone()))
            }
            UseDecl::Directory(d) if d.source == UseSource::Realm => {
                Ok(FrameworkCapability::Directory(d.source_path.clone()))
            }
            UseDecl::Runner(s) => Ok(FrameworkCapability::Runner(s.source_name.clone())),
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }

    pub fn builtin_from_offer_decl(decl: &OfferDecl) -> Result<Self, Error> {
        match decl {
            OfferDecl::ServiceProtocol(s) if s.source == OfferServiceSource::Realm => {
                Ok(FrameworkCapability::ServiceProtocol(s.source_path.clone()))
            }
            OfferDecl::Directory(d) if d.source == OfferDirectorySource::Realm => {
                Ok(FrameworkCapability::Directory(d.source_path.clone()))
            }
            OfferDecl::Runner(s) if s.source == OfferRunnerSource::Realm => {
                Ok(FrameworkCapability::Runner(s.source_name.clone()))
            }
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }

    pub fn framework_from_use_decl(decl: &UseDecl) -> Result<Self, Error> {
        match decl {
            UseDecl::Service(s) if s.source == UseSource::Framework => {
                Ok(FrameworkCapability::Service(s.source_path.clone()))
            }
            UseDecl::ServiceProtocol(s) if s.source == UseSource::Framework => {
                Ok(FrameworkCapability::ServiceProtocol(s.source_path.clone()))
            }
            UseDecl::Directory(d) if d.source == UseSource::Framework => {
                Ok(FrameworkCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidScopedFrameworkCapability {});
            }
        }
    }

    pub fn framework_from_offer_decl(decl: &OfferDecl) -> Result<Self, Error> {
        match decl {
            OfferDecl::ServiceProtocol(s) if s.source == OfferServiceSource::Realm => {
                Ok(FrameworkCapability::ServiceProtocol(s.source_path.clone()))
            }
            OfferDecl::Directory(d) if d.source == OfferDirectorySource::Framework => {
                Ok(FrameworkCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidScopedFrameworkCapability {});
            }
        }
    }

    pub fn framework_from_expose_decl(decl: &ExposeDecl) -> Result<Self, Error> {
        match decl {
            ExposeDecl::ServiceProtocol(d) if d.source == ExposeSource::Framework => {
                Ok(FrameworkCapability::ServiceProtocol(d.source_path.clone()))
            }
            ExposeDecl::Directory(d) if d.source == ExposeSource::Framework => {
                Ok(FrameworkCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidScopedFrameworkCapability {});
            }
        }
    }
}

/// The server-side of a capability implements this trait.
/// Multiple `CapabilityProvider` objects can compose with one another for a single
/// capability request. For example, a `CapabitilityProvider` can be interposed
/// between the primary `CapabilityProvider and the client for the purpose of
/// logging and testing. A `CapabilityProvider` is typically provided by a
/// corresponding `Hook` in response to the `RouteCapability` event.
/// A capability provider is used exactly once as a result of exactly one route.
#[async_trait]
pub trait CapabilityProvider: Send + Sync {
    // Called to bind a server end of a zx::Channel to the provided capability.
    // If the capability is a directory, then |flags|, |open_mode| and |relative_path|
    // will be propagated along to open the appropriate directory.
    async fn open(
        self: Box<Self>,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError>;
}

/// A capability being routed from a component.
#[derive(Clone, Debug)]
pub enum ComponentCapability {
    Use(UseDecl),
    Expose(ExposeDecl),
    Offer(OfferDecl),
    Storage(StorageDecl),
    Runner(RunnerDecl),
}

impl ComponentCapability {
    /// Returns the source path of the capability, if one exists.
    pub fn source_path(&self) -> Option<&CapabilityPath> {
        match self {
            ComponentCapability::Use(use_) => match use_ {
                UseDecl::ServiceProtocol(UseServiceProtocolDecl { source_path, .. }) => {
                    Some(source_path)
                }
                UseDecl::Directory(UseDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            ComponentCapability::Expose(expose) => match expose {
                ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl { source_path, .. }) => {
                    Some(source_path)
                }
                ExposeDecl::Directory(ExposeDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            ComponentCapability::Offer(offer) => match offer {
                OfferDecl::ServiceProtocol(OfferServiceProtocolDecl { source_path, .. }) => {
                    Some(source_path)
                }
                OfferDecl::Directory(OfferDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            ComponentCapability::Runner(RunnerDecl { source_path, .. }) => Some(source_path),
            ComponentCapability::Storage(_) => None,
        }
    }

    /// Return the source name of the capability, if one exists.
    pub fn source_name<'a>(&self) -> Option<&CapabilityName> {
        match self {
            ComponentCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
                source_name,
                ..
            })) => Some(source_name),
            ComponentCapability::Offer(OfferDecl::Runner(OfferRunnerDecl {
                source_name, ..
            })) => Some(source_name),
            _ => None,
        }
    }

    /// Returns the `ExposeDecl` that exposes the capability, if it exists.
    pub fn find_expose_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a ExposeDecl> {
        decl.exposes.iter().find(|&expose| match (self, expose) {
            // ServiceProtocol exposed to me that has a matching `expose` or `offer`.
            (
                ComponentCapability::Offer(OfferDecl::ServiceProtocol(parent_offer)),
                ExposeDecl::ServiceProtocol(expose),
            ) => parent_offer.source_path == expose.target_path,
            (
                ComponentCapability::Expose(ExposeDecl::ServiceProtocol(parent_expose)),
                ExposeDecl::ServiceProtocol(expose),
            ) => parent_expose.source_path == expose.target_path,
            // Directory exposed to me that matches a directory `expose` or `offer`.
            (
                ComponentCapability::Offer(OfferDecl::Directory(parent_offer)),
                ExposeDecl::Directory(expose),
            ) => parent_offer.source_path == expose.target_path,
            (
                ComponentCapability::Expose(ExposeDecl::Directory(parent_expose)),
                ExposeDecl::Directory(expose),
            ) => parent_expose.source_path == expose.target_path,
            // Runner exposed to me that has a matching `expose` or `offer`.
            (
                ComponentCapability::Offer(OfferDecl::Runner(parent_offer)),
                ExposeDecl::Runner(expose),
            ) => parent_offer.source_name == expose.target_name,
            (
                ComponentCapability::Expose(ExposeDecl::Runner(parent_expose)),
                ExposeDecl::Runner(expose),
            ) => parent_expose.source_name == expose.target_name,
            // Directory exposed to me that matches a `storage` declaration which consumes it.
            (ComponentCapability::Storage(parent_storage), ExposeDecl::Directory(expose)) => {
                parent_storage.source_path == expose.target_path
            }
            _ => false,
        })
    }

    /// Returns the set of `ExposeServiceDecl`s that expose the service capability, if they exist.
    #[allow(unused)]
    pub fn find_expose_service_sources<'a>(
        &self,
        decl: &'a ComponentDecl,
    ) -> Vec<&'a ExposeServiceDecl> {
        let paths: HashSet<_> = match self {
            ComponentCapability::Offer(OfferDecl::Service(parent_offer)) => {
                parent_offer.sources.iter().map(|s| &s.source_path).collect()
            }
            ComponentCapability::Expose(ExposeDecl::Service(parent_expose)) => {
                parent_expose.sources.iter().map(|s| &s.source_path).collect()
            }
            _ => panic!("Expected an offer or expose of a service capability, found: {:?}", self),
        };
        decl.exposes
            .iter()
            .filter_map(|expose| match expose {
                ExposeDecl::Service(expose) if paths.contains(&expose.target_path) => Some(expose),
                _ => None,
            })
            .collect()
    }

    /// Given a parent ComponentDecl, returns the `OfferDecl` that offers this capability to
    /// `child_moniker`, if it exists.
    pub fn find_offer_source<'a>(
        &self,
        decl: &'a ComponentDecl,
        child_moniker: &ChildMoniker,
    ) -> Option<&'a OfferDecl> {
        decl.offers.iter().find(|&offer| match (self, offer) {
            // ServiceProtocol offered to me that matches a service `use` or `offer` declaration.
            (
                ComponentCapability::Use(UseDecl::ServiceProtocol(child_use)),
                OfferDecl::ServiceProtocol(offer),
            ) => Self::is_offer_service_protocol_or_directory_match(
                child_moniker,
                &child_use.source_path,
                &offer.target,
                &offer.target_path,
            ),
            (
                ComponentCapability::Offer(OfferDecl::ServiceProtocol(child_offer)),
                OfferDecl::ServiceProtocol(offer),
            ) => Self::is_offer_service_protocol_or_directory_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a directory `use` or `offer` declaration.
            (
                ComponentCapability::Use(UseDecl::Directory(child_use)),
                OfferDecl::Directory(offer),
            ) => Self::is_offer_service_protocol_or_directory_match(
                child_moniker,
                &child_use.source_path,
                &offer.target,
                &offer.target_path,
            ),
            (
                ComponentCapability::Offer(OfferDecl::Directory(child_offer)),
                OfferDecl::Directory(offer),
            ) => Self::is_offer_service_protocol_or_directory_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a `storage` declaration which consumes it.
            (ComponentCapability::Storage(child_storage), OfferDecl::Directory(offer)) => {
                Self::is_offer_service_protocol_or_directory_match(
                    child_moniker,
                    &child_storage.source_path,
                    &offer.target,
                    &offer.target_path,
                )
            }
            // Storage offered to me.
            (ComponentCapability::Use(UseDecl::Storage(child_use)), OfferDecl::Storage(offer)) => {
                Self::is_offer_storage_match(
                    child_moniker,
                    child_use.type_(),
                    offer.target(),
                    offer.type_(),
                )
            }
            (
                ComponentCapability::Offer(OfferDecl::Storage(child_offer)),
                OfferDecl::Storage(offer),
            ) => Self::is_offer_storage_match(
                child_moniker,
                child_offer.type_(),
                offer.target(),
                offer.type_(),
            ),
            // Runners offered from parent.
            (ComponentCapability::Use(UseDecl::Runner(child_use)), OfferDecl::Runner(offer)) => {
                Self::is_offer_runner_match(
                    child_moniker,
                    &child_use.source_name,
                    &offer.target,
                    &offer.target_name,
                )
            }
            (
                ComponentCapability::Offer(OfferDecl::Runner(child_offer)),
                OfferDecl::Runner(parent_offer),
            ) => Self::is_offer_runner_match(
                child_moniker,
                &child_offer.source_name,
                &parent_offer.target,
                &parent_offer.target_name,
            ),
            _ => false,
        })
    }

    /// Returns the set of `OfferServiceDecl`s that offer the service capability, if they exist.
    #[allow(unused)]
    pub fn find_offer_service_sources<'a>(
        &self,
        decl: &'a ComponentDecl,
        child_moniker: &ChildMoniker,
    ) -> Vec<&'a OfferServiceDecl> {
        let paths: HashSet<_> = match self {
            ComponentCapability::Use(UseDecl::Service(child_use)) => {
                vec![&child_use.source_path].into_iter().collect()
            }
            ComponentCapability::Offer(OfferDecl::Service(child_offer)) => {
                child_offer.sources.iter().map(|s| &s.source_path).collect()
            }
            _ => panic!("Expected a use or offer of a service capability, found: {:?}", self),
        };
        decl.offers
            .iter()
            .filter_map(|offer| match offer {
                OfferDecl::Service(offer)
                    if Self::is_offer_service_match(
                        child_moniker,
                        &paths,
                        &offer.target,
                        &offer.target_path,
                    ) =>
                {
                    Some(offer)
                }
                _ => None,
            })
            .collect()
    }

    /// Given a offer/expose of a Runner from `Self`, return the associated RunnerDecl,
    /// if it exists.
    pub fn find_runner_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a RunnerDecl> {
        decl.find_runner_source(self.source_name()?.str())
    }

    fn is_offer_service_match(
        child_moniker: &ChildMoniker,
        paths: &HashSet<&CapabilityPath>,
        target: &OfferTarget,
        target_path: &CapabilityPath,
    ) -> bool {
        paths.contains(target_path) && target_matches_moniker(target, child_moniker)
    }

    fn is_offer_service_protocol_or_directory_match(
        child_moniker: &ChildMoniker,
        path: &CapabilityPath,
        target: &OfferTarget,
        target_path: &CapabilityPath,
    ) -> bool {
        path == target_path && target_matches_moniker(target, child_moniker)
    }

    fn is_offer_storage_match(
        child_moniker: &ChildMoniker,
        child_type: fsys::StorageType,
        parent_target: &OfferTarget,
        parent_type: fsys::StorageType,
    ) -> bool {
        // The types must match...
        parent_type == child_type &&
        // ...and the child/collection names must match.
        target_matches_moniker(parent_target, child_moniker)
    }

    fn is_offer_runner_match(
        child_moniker: &ChildMoniker,
        source_name: &CapabilityName,
        target: &OfferTarget,
        target_name: &CapabilityName,
    ) -> bool {
        source_name == target_name && target_matches_moniker(target, child_moniker)
    }
}

/// Returns if `parent_target` refers to a the child `child_moniker`.
fn target_matches_moniker(parent_target: &OfferTarget, child_moniker: &ChildMoniker) -> bool {
    match (parent_target, child_moniker.collection()) {
        (OfferTarget::Child(target_child_name), None) => target_child_name == child_moniker.name(),
        (OfferTarget::Collection(target_collection_name), Some(collection)) => {
            target_collection_name == collection
        }
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use cm_rust::OfferServiceDecl;
    use {
        super::*,
        crate::model::testing::test_helpers::default_component_decl,
        cm_rust::{
            ExposeRunnerDecl, ExposeSource, ExposeTarget, OfferServiceSource, ServiceSource,
            StorageDirectorySource, UseRunnerDecl,
        },
    };

    #[test]
    fn find_expose_service_sources() {
        let capability = ComponentCapability::Expose(ExposeDecl::Service(ExposeServiceDecl {
            sources: vec![
                ServiceSource {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath {
                        dirname: "/svc".to_string(),
                        basename: "net".to_string(),
                    },
                },
                ServiceSource {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath {
                        dirname: "/svc".to_string(),
                        basename: "log".to_string(),
                    },
                },
                ServiceSource {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath {
                        dirname: "/svc".to_string(),
                        basename: "unmatched-source".to_string(),
                    },
                },
            ],
            target: ExposeTarget::Realm,
            target_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
        }));
        let net_service = ExposeServiceDecl {
            sources: vec![],
            target: ExposeTarget::Realm,
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: "net".to_string(),
            },
        };
        let log_service = ExposeServiceDecl {
            sources: vec![],
            target: ExposeTarget::Realm,
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: "log".to_string(),
            },
        };
        let unmatched_service = ExposeServiceDecl {
            sources: vec![],
            target: ExposeTarget::Realm,
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: "unmatched-target".to_string(),
            },
        };
        let decl = ComponentDecl {
            exposes: vec![
                ExposeDecl::Service(net_service.clone()),
                ExposeDecl::Service(log_service.clone()),
                ExposeDecl::Service(unmatched_service.clone()),
            ],
            ..default_component_decl()
        };
        let sources = capability.find_expose_service_sources(&decl);
        assert_eq!(sources, vec![&net_service, &log_service])
    }

    #[test]
    #[should_panic]
    #[ignore] // fxb/40189
    fn find_expose_service_sources_with_unexpected_capability() {
        let capability = ComponentCapability::Storage(StorageDecl {
            name: "".to_string(),
            source: StorageDirectorySource::Realm,
            source_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
        });
        capability.find_expose_service_sources(&default_component_decl());
    }

    #[test]
    fn find_offer_service_sources() {
        let capability = ComponentCapability::Offer(OfferDecl::Service(OfferServiceDecl {
            sources: vec![
                ServiceSource {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath {
                        dirname: "/svc".to_string(),
                        basename: "net".to_string(),
                    },
                },
                ServiceSource {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath {
                        dirname: "/svc".to_string(),
                        basename: "log".to_string(),
                    },
                },
                ServiceSource {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath {
                        dirname: "/svc".to_string(),
                        basename: "unmatched-source".to_string(),
                    },
                },
            ],
            target: OfferTarget::Child("".to_string()),
            target_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
        }));
        let net_service = OfferServiceDecl {
            sources: vec![],
            target: OfferTarget::Child("child".to_string()),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: "net".to_string(),
            },
        };
        let log_service = OfferServiceDecl {
            sources: vec![],
            target: OfferTarget::Child("child".to_string()),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: "log".to_string(),
            },
        };
        let unmatched_service = OfferServiceDecl {
            sources: vec![],
            target: OfferTarget::Child("child".to_string()),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: "unmatched-target".to_string(),
            },
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Service(net_service.clone()),
                OfferDecl::Service(log_service.clone()),
                OfferDecl::Service(unmatched_service.clone()),
            ],
            ..default_component_decl()
        };
        let moniker = ChildMoniker::new("child".to_string(), None, 0);
        let sources = capability.find_offer_service_sources(&decl, &moniker);
        assert_eq!(sources, vec![&net_service, &log_service])
    }

    #[test]
    fn find_offer_source_runner() {
        // Parents offers runner named "elf" to "child".
        let parent_decl = ComponentDecl {
            offers: vec![
                // Offer as "elf" to child "child".
                OfferDecl::Runner(cm_rust::OfferRunnerDecl {
                    source: cm_rust::OfferRunnerSource::Self_,
                    source_name: "source".into(),
                    target: cm_rust::OfferTarget::Child("child".to_string()),
                    target_name: "elf".into(),
                }),
            ],
            ..default_component_decl()
        };

        // A child named "child" uses a runner "elf" offered by its parent. Should successfully
        // match the declaration.
        let child_cap =
            ComponentCapability::Use(UseDecl::Runner(UseRunnerDecl { source_name: "elf".into() }));
        assert_eq!(
            child_cap.find_offer_source(&parent_decl, &"child:0".into()),
            Some(&parent_decl.offers[0])
        );

        // Mismatched child name.
        assert_eq!(child_cap.find_offer_source(&parent_decl, &"other-child:0".into()), None);

        // Mismatched cap name.
        let misnamed_child_cap = ComponentCapability::Use(UseDecl::Runner(UseRunnerDecl {
            source_name: "dwarf".into(),
        }));
        assert_eq!(misnamed_child_cap.find_offer_source(&parent_decl, &"child:0".into()), None);
    }

    #[test]
    fn find_expose_source_runner() {
        // A child named "child" exposes a runner "elf" to its parent.
        let child_decl = ComponentDecl {
            exposes: vec![
                // Expose as "elf" to Realm.
                ExposeDecl::Runner(cm_rust::ExposeRunnerDecl {
                    source: cm_rust::ExposeSource::Self_,
                    source_name: "source".into(),
                    target: cm_rust::ExposeTarget::Realm,
                    target_name: "elf".into(),
                }),
            ],
            ..default_component_decl()
        };

        // A parent exposes a runner "elf" with a child as its source. Should successfully match the
        // declaration.
        let parent_cap = ComponentCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
            source: ExposeSource::Child("child".into()),
            source_name: "elf".into(),
            target: ExposeTarget::Realm,
            target_name: "parent_elf".into(),
        }));
        assert_eq!(parent_cap.find_expose_source(&child_decl), Some(&child_decl.exposes[0]));

        // If the name is mismatched, we shouldn't find anything though.
        let misnamed_parent_cap =
            ComponentCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
                source: ExposeSource::Child("child".into()),
                source_name: "dwarf".into(),
                target: ExposeTarget::Realm,
                target_name: "parent_elf".into(),
            }));
        assert_eq!(misnamed_parent_cap.find_expose_source(&child_decl), None);
    }

    #[test]
    #[should_panic]
    #[ignore] // fxb/40189
    fn find_offer_service_sources_with_unexpected_capability() {
        let capability = ComponentCapability::Storage(StorageDecl {
            name: "".to_string(),
            source: StorageDirectorySource::Realm,
            source_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
        });
        let moniker = ChildMoniker::new("".to_string(), None, 0);
        capability.find_offer_service_sources(&default_component_decl(), &moniker);
    }
}
