use {
    crate::model::*,
    cm_rust::{
        self, CapabilityPath, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeLegacyServiceDecl, ExposeServiceDecl, OfferDecl, OfferDirectoryDecl,
        OfferLegacyServiceDecl, OfferServiceDecl, OfferTarget, StorageDecl, UseDecl,
        UseDirectoryDecl, UseLegacyServiceDecl,
    },
    fidl_fuchsia_sys2 as fsys,
    std::collections::HashSet,
};

/// A capability being routed, which is represented by one of `use`, `offer`, or `expose`,
/// or `storage`.
#[derive(Debug)]
pub enum RoutedCapability {
    Use(UseDecl),
    Expose(ExposeDecl),
    Offer(OfferDecl),
    Storage(StorageDecl),
}

impl RoutedCapability {
    /// Returns the source path of the capability, if one exists.
    pub fn source_path(&self) -> Option<&CapabilityPath> {
        match self {
            RoutedCapability::Use(use_) => match use_ {
                UseDecl::LegacyService(UseLegacyServiceDecl { source_path, .. }) => {
                    Some(source_path)
                }
                UseDecl::Directory(UseDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            RoutedCapability::Expose(expose) => match expose {
                ExposeDecl::LegacyService(ExposeLegacyServiceDecl { source_path, .. }) => {
                    Some(source_path)
                }
                ExposeDecl::Directory(ExposeDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            RoutedCapability::Offer(offer) => match offer {
                OfferDecl::LegacyService(OfferLegacyServiceDecl { source_path, .. }) => {
                    Some(source_path)
                }
                OfferDecl::Directory(OfferDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            RoutedCapability::Storage(_) => None,
        }
    }

    /// Returns the `ExposeDecl` that exposes the capability, if it exists.
    pub fn find_expose_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a ExposeDecl> {
        decl.exposes.iter().find(|&expose| match (self, expose) {
            // LegacyService exposed to me that has a matching `expose` or `offer`.
            (
                RoutedCapability::Offer(OfferDecl::LegacyService(parent_offer)),
                ExposeDecl::LegacyService(expose),
            ) => parent_offer.source_path == expose.target_path,
            (
                RoutedCapability::Expose(ExposeDecl::LegacyService(parent_expose)),
                ExposeDecl::LegacyService(expose),
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
            // Directory exposed to me that matches a `storage` declaration which consumes it.
            (RoutedCapability::Storage(parent_storage), ExposeDecl::Directory(expose)) => {
                parent_storage.source_path == expose.target_path
            }
            _ => false,
        })
    }

    /// Returns the set of `ExposeServiceDecl`s that expose the service capability, if they exist.
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
            // LegacyService offered to me that matches a service `use` or `offer` declaration.
            (
                RoutedCapability::Use(UseDecl::LegacyService(child_use)),
                OfferDecl::LegacyService(offer),
            ) => Self::is_offer_legacy_service_or_directory_match(
                child_moniker,
                &child_use.source_path,
                &offer.target,
                &offer.target_path,
            ),
            (
                RoutedCapability::Offer(OfferDecl::LegacyService(child_offer)),
                OfferDecl::LegacyService(offer),
            ) => Self::is_offer_legacy_service_or_directory_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a directory `use` or `offer` declaration.
            (RoutedCapability::Use(UseDecl::Directory(child_use)), OfferDecl::Directory(offer)) => {
                Self::is_offer_legacy_service_or_directory_match(
                    child_moniker,
                    &child_use.source_path,
                    &offer.target,
                    &offer.target_path,
                )
            }
            (
                RoutedCapability::Offer(OfferDecl::Directory(child_offer)),
                OfferDecl::Directory(offer),
            ) => Self::is_offer_legacy_service_or_directory_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a `storage` declaration which consumes it.
            (RoutedCapability::Storage(child_storage), OfferDecl::Directory(offer)) => {
                Self::is_offer_legacy_service_or_directory_match(
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
            _ => false,
        })
    }

    /// Returns the set of `OfferServiceDecl`s that offer the service capability, if they exist.
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

    fn is_offer_service_match(
        child_moniker: &ChildMoniker,
        paths: &HashSet<&CapabilityPath>,
        target: &OfferTarget,
        target_path: &CapabilityPath,
    ) -> bool {
        paths.contains(target_path) && target_matches_moniker(target, child_moniker)
    }

    fn is_offer_legacy_service_or_directory_match(
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
        cm_rust::{
            ExposeSource, ExposeTarget, OfferServiceSource, ServiceSource, StorageDirectorySource,
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
            program: None,
            uses: vec![],
            exposes: vec![
                ExposeDecl::Service(net_service.clone()),
                ExposeDecl::Service(log_service.clone()),
                ExposeDecl::Service(unmatched_service.clone()),
            ],
            offers: vec![],
            children: vec![],
            collections: vec![],
            storage: vec![],
            facets: None,
            runners: vec![],
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
        let decl = ComponentDecl {
            program: None,
            uses: vec![],
            exposes: vec![],
            offers: vec![],
            children: vec![],
            collections: vec![],
            storage: vec![],
            facets: None,
            runners: vec![],
        };
        capability.find_expose_service_sources(&decl);
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
            program: None,
            uses: vec![],
            exposes: vec![],
            offers: vec![
                OfferDecl::Service(net_service.clone()),
                OfferDecl::Service(log_service.clone()),
                OfferDecl::Service(unmatched_service.clone()),
            ],
            children: vec![],
            collections: vec![],
            storage: vec![],
            facets: None,
            runners: vec![],
        };
        let moniker = ChildMoniker::new("child".to_string(), None, 0);
        let sources = capability.find_offer_service_sources(&decl, &moniker);
        assert_eq!(sources, vec![&net_service, &log_service])
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
        let decl = ComponentDecl {
            program: None,
            uses: vec![],
            exposes: vec![],
            offers: vec![],
            children: vec![],
            collections: vec![],
            storage: vec![],
            facets: None,
            runners: vec![],
        };
        let moniker = ChildMoniker::new("".to_string(), None, 0);
        capability.find_offer_service_sources(&decl, &moniker);
    }
}
