use {
    crate::model::moniker::ChildMoniker,
    cm_rust::{
        self, CapabilityName, CapabilityPath, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeRunnerDecl, ExposeServiceDecl, ExposeServiceProtocolDecl, OfferDecl,
        OfferDirectoryDecl, OfferRunnerDecl, OfferServiceDecl, OfferServiceProtocolDecl,
        OfferTarget, RunnerDecl, StorageDecl, UseDecl, UseDirectoryDecl, UseServiceProtocolDecl,
    },
    fidl_fuchsia_sys2 as fsys,
    std::collections::HashSet,
};

/// A capability being routed.
#[derive(Debug)]
pub(super) enum RoutedCapability {
    Use(UseDecl),
    Expose(ExposeDecl),
    Offer(OfferDecl),
    Storage(StorageDecl),
    Runner(RunnerDecl),
}

impl RoutedCapability {
    /// Returns the source path of the capability, if one exists.
    pub fn source_path(&self) -> Option<&CapabilityPath> {
        match self {
            RoutedCapability::Use(use_) => match use_ {
                UseDecl::ServiceProtocol(UseServiceProtocolDecl { source_path, .. }) => {
                    Some(source_path)
                }
                UseDecl::Directory(UseDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            RoutedCapability::Expose(expose) => match expose {
                ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl { source_path, .. }) => {
                    Some(source_path)
                }
                ExposeDecl::Directory(ExposeDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            RoutedCapability::Offer(offer) => match offer {
                OfferDecl::ServiceProtocol(OfferServiceProtocolDecl { source_path, .. }) => {
                    Some(source_path)
                }
                OfferDecl::Directory(OfferDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            RoutedCapability::Runner(RunnerDecl { source_path, .. }) => Some(source_path),
            RoutedCapability::Storage(_) => None,
        }
    }

    /// Return the source name of the capability, if one exists.
    pub fn source_name<'a>(&self) -> Option<&CapabilityName> {
        match self {
            RoutedCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
                source_name, ..
            })) => Some(source_name),
            RoutedCapability::Offer(OfferDecl::Runner(OfferRunnerDecl { source_name, .. })) => {
                Some(source_name)
            }
            _ => None,
        }
    }

    /// Returns the `ExposeDecl` that exposes the capability, if it exists.
    pub fn find_expose_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a ExposeDecl> {
        decl.exposes.iter().find(|&expose| match (self, expose) {
            // ServiceProtocol exposed to me that has a matching `expose` or `offer`.
            (
                RoutedCapability::Offer(OfferDecl::ServiceProtocol(parent_offer)),
                ExposeDecl::ServiceProtocol(expose),
            ) => parent_offer.source_path == expose.target_path,
            (
                RoutedCapability::Expose(ExposeDecl::ServiceProtocol(parent_expose)),
                ExposeDecl::ServiceProtocol(expose),
            ) => parent_expose.source_path == expose.target_path,
            // Directory exposed to me that matches a directory `expose` or `offer`.
            (
                RoutedCapability::Offer(OfferDecl::Directory(parent_offer)),
                ExposeDecl::Directory(expose),
            ) => parent_offer.source_path == expose.target_path,
            (
                RoutedCapability::Expose(ExposeDecl::Directory(parent_expose)),
                ExposeDecl::Directory(expose),
            ) => parent_expose.source_path == expose.target_path,
            // Runner exposed to me that has a matching `expose` or `offer`.
            (
                RoutedCapability::Offer(OfferDecl::Runner(parent_offer)),
                ExposeDecl::Runner(expose),
            ) => parent_offer.source_name == expose.target_name,
            (
                RoutedCapability::Expose(ExposeDecl::Runner(parent_expose)),
                ExposeDecl::Runner(expose),
            ) => parent_expose.source_name == expose.target_name,
            // Directory exposed to me that matches a `storage` declaration which consumes it.
            (RoutedCapability::Storage(parent_storage), ExposeDecl::Directory(expose)) => {
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
            RoutedCapability::Offer(OfferDecl::Service(parent_offer)) => {
                parent_offer.sources.iter().map(|s| &s.source_path).collect()
            }
            RoutedCapability::Expose(ExposeDecl::Service(parent_expose)) => {
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
                RoutedCapability::Use(UseDecl::ServiceProtocol(child_use)),
                OfferDecl::ServiceProtocol(offer),
            ) => Self::is_offer_service_protocol_or_directory_match(
                child_moniker,
                &child_use.source_path,
                &offer.target,
                &offer.target_path,
            ),
            (
                RoutedCapability::Offer(OfferDecl::ServiceProtocol(child_offer)),
                OfferDecl::ServiceProtocol(offer),
            ) => Self::is_offer_service_protocol_or_directory_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a directory `use` or `offer` declaration.
            (RoutedCapability::Use(UseDecl::Directory(child_use)), OfferDecl::Directory(offer)) => {
                Self::is_offer_service_protocol_or_directory_match(
                    child_moniker,
                    &child_use.source_path,
                    &offer.target,
                    &offer.target_path,
                )
            }
            (
                RoutedCapability::Offer(OfferDecl::Directory(child_offer)),
                OfferDecl::Directory(offer),
            ) => Self::is_offer_service_protocol_or_directory_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a `storage` declaration which consumes it.
            (RoutedCapability::Storage(child_storage), OfferDecl::Directory(offer)) => {
                Self::is_offer_service_protocol_or_directory_match(
                    child_moniker,
                    &child_storage.source_path,
                    &offer.target,
                    &offer.target_path,
                )
            }
            // Storage offered to me.
            (RoutedCapability::Use(UseDecl::Storage(child_use)), OfferDecl::Storage(offer)) => {
                Self::is_offer_storage_match(
                    child_moniker,
                    child_use.type_(),
                    offer.target(),
                    offer.type_(),
                )
            }
            (
                RoutedCapability::Offer(OfferDecl::Storage(child_offer)),
                OfferDecl::Storage(offer),
            ) => Self::is_offer_storage_match(
                child_moniker,
                child_offer.type_(),
                offer.target(),
                offer.type_(),
            ),
            // Runners offered from parent.
            (RoutedCapability::Use(UseDecl::Runner(child_use)), OfferDecl::Runner(offer)) => {
                Self::is_offer_runner_match(
                    child_moniker,
                    &child_use.source_name,
                    &offer.target,
                    &offer.target_name,
                )
            }
            (
                RoutedCapability::Offer(OfferDecl::Runner(child_offer)),
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
            RoutedCapability::Use(UseDecl::Service(child_use)) => {
                vec![&child_use.source_path].into_iter().collect()
            }
            RoutedCapability::Offer(OfferDecl::Service(child_offer)) => {
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
        let capability = RoutedCapability::Expose(ExposeDecl::Service(ExposeServiceDecl {
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
        let capability = RoutedCapability::Storage(StorageDecl {
            name: "".to_string(),
            source: StorageDirectorySource::Realm,
            source_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
        });
        capability.find_expose_service_sources(&default_component_decl());
    }

    #[test]
    fn find_offer_service_sources() {
        let capability = RoutedCapability::Offer(OfferDecl::Service(OfferServiceDecl {
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
            RoutedCapability::Use(UseDecl::Runner(UseRunnerDecl { source_name: "elf".into() }));
        assert_eq!(
            child_cap.find_offer_source(&parent_decl, &"child:0".into()),
            Some(&parent_decl.offers[0])
        );

        // Mismatched child name.
        assert_eq!(child_cap.find_offer_source(&parent_decl, &"other-child:0".into()), None);

        // Mismatched cap name.
        let misnamed_child_cap =
            RoutedCapability::Use(UseDecl::Runner(UseRunnerDecl { source_name: "dwarf".into() }));
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
        let parent_cap = RoutedCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
            source: ExposeSource::Child("child".into()),
            source_name: "elf".into(),
            target: ExposeTarget::Realm,
            target_name: "parent_elf".into(),
        }));
        assert_eq!(parent_cap.find_expose_source(&child_decl), Some(&child_decl.exposes[0]));

        // If the name is mismatched, we shouldn't find anything though.
        let misnamed_parent_cap = RoutedCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
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
        let capability = RoutedCapability::Storage(StorageDecl {
            name: "".to_string(),
            source: StorageDirectorySource::Realm,
            source_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
        });
        let moniker = ChildMoniker::new("".to_string(), None, 0);
        capability.find_offer_service_sources(&default_component_decl(), &moniker);
    }
}
