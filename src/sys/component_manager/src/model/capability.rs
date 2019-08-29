use {
    crate::model::*,
    cm_rust::{
        self, CapabilityPath, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeLegacyServiceDecl, ExposeServiceDecl, OfferDecl, OfferDirectoryDecl,
        OfferLegacyServiceDecl, OfferServiceDecl, OfferTarget, StorageDecl, UseDecl,
        UseDirectoryDecl, UseLegacyServiceDecl, UseServiceDecl,
    },
    fidl_fuchsia_sys2 as fsys,
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
                UseDecl::Service(UseServiceDecl { source_path, .. }) => Some(source_path),
                UseDecl::LegacyService(UseLegacyServiceDecl { source_path, .. }) => {
                    Some(source_path)
                }
                UseDecl::Directory(UseDirectoryDecl { source_path, .. }) => Some(source_path),
                _ => None,
            },
            RoutedCapability::Expose(expose) => match expose {
                ExposeDecl::Service(ExposeServiceDecl { source_path, .. }) => Some(source_path),
                ExposeDecl::LegacyService(ExposeLegacyServiceDecl { source_path, .. }) => {
                    Some(source_path)
                }
                ExposeDecl::Directory(ExposeDirectoryDecl { source_path, .. }) => Some(source_path),
            },
            RoutedCapability::Offer(offer) => match offer {
                OfferDecl::Service(OfferServiceDecl { source_path, .. }) => Some(source_path),
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
            // Service exposed to me that has a matching `expose` or `offer`.
            (
                RoutedCapability::Offer(OfferDecl::Service(parent_offer)),
                ExposeDecl::Service(expose),
            ) => parent_offer.source_path == expose.target_path,
            (
                RoutedCapability::Expose(ExposeDecl::Service(parent_expose)),
                ExposeDecl::Service(expose),
            ) => parent_expose.source_path == expose.target_path,
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

    /// Returns the `OfferDecl` that offers the capability in `child_offer` to `child_name`, if it
    /// exists.
    pub fn find_offer_source<'a>(
        &self,
        decl: &'a ComponentDecl,
        child_moniker: &ChildMoniker,
    ) -> Option<&'a OfferDecl> {
        decl.offers.iter().find(|&offer| match (self, offer) {
            // Service offered to me that matches a service `use` or `offer` declaration.
            (RoutedCapability::Use(UseDecl::Service(child_use)), OfferDecl::Service(offer)) => {
                Self::is_offer_service_or_dir_match(
                    child_moniker,
                    &child_use.source_path,
                    &offer.target,
                    &offer.target_path,
                )
            }
            (
                RoutedCapability::Offer(OfferDecl::Service(child_offer)),
                OfferDecl::Service(offer),
            ) => Self::is_offer_service_or_dir_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // LegacyService offered to me that matches a service `use` or `offer` declaration.
            (
                RoutedCapability::Use(UseDecl::LegacyService(child_use)),
                OfferDecl::LegacyService(offer),
            ) => Self::is_offer_service_or_dir_match(
                child_moniker,
                &child_use.source_path,
                &offer.target,
                &offer.target_path,
            ),
            (
                RoutedCapability::Offer(OfferDecl::LegacyService(child_offer)),
                OfferDecl::LegacyService(offer),
            ) => Self::is_offer_service_or_dir_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a directory `use` or `offer` declaration.
            (RoutedCapability::Use(UseDecl::Directory(child_use)), OfferDecl::Directory(offer)) => {
                Self::is_offer_service_or_dir_match(
                    child_moniker,
                    &child_use.source_path,
                    &offer.target,
                    &offer.target_path,
                )
            }
            (
                RoutedCapability::Offer(OfferDecl::Directory(child_offer)),
                OfferDecl::Directory(offer),
            ) => Self::is_offer_service_or_dir_match(
                child_moniker,
                &child_offer.source_path,
                &offer.target,
                &offer.target_path,
            ),
            // Directory offered to me that matches a `storage` declaration which consumes it.
            (RoutedCapability::Storage(child_storage), OfferDecl::Directory(offer)) => {
                Self::is_offer_service_or_dir_match(
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

    fn is_offer_service_or_dir_match(
        child_moniker: &ChildMoniker,
        path: &CapabilityPath,
        target: &OfferTarget,
        target_path: &CapabilityPath,
    ) -> bool {
        match target {
            OfferTarget::Child(target_child_name) => match child_moniker.collection() {
                Some(_) => false,
                None => target_path == path && target_child_name == child_moniker.name(),
            },
            OfferTarget::Collection(target_collection_name) => match child_moniker.collection() {
                Some(collection) => target_path == path && target_collection_name == collection,
                None => false,
            },
        }
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
        match (parent_target, child_moniker.collection()) {
            (OfferTarget::Child(target_child_name), None) =>
                target_child_name == child_moniker.name(),
            (OfferTarget::Collection(target_collection_name), Some(collection)) =>
                target_collection_name == collection,
            _ => false
        }
    }
}
