// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod capability_source;
pub mod collection;
pub mod component_instance;
pub mod config;
pub mod environment;
pub mod error;
pub mod router;

use {
    crate::{
        environment::DebugRegistration,
        error::RoutingError,
        router::{ErrorNotFoundFromParent, ErrorNotFoundInChild},
    },
    cm_rust::{
        CapabilityName, ExposeDirectoryDecl, ExposeProtocolDecl, ExposeResolverDecl,
        ExposeRunnerDecl, ExposeServiceDecl, OfferDirectoryDecl, OfferEventDecl, OfferProtocolDecl,
        OfferResolverDecl, OfferRunnerDecl, OfferServiceDecl, OfferStorageDecl,
        RegistrationDeclCommon, RegistrationSource, ResolverRegistration, RunnerRegistration,
        SourceName, StorageDecl, StorageDirectorySource, UseDirectoryDecl, UseEventDecl,
        UseProtocolDecl, UseServiceDecl, UseStorageDecl,
    },
    moniker::{AbsoluteMoniker, PartialMoniker},
};

/// Intermediate type to masquerade as Registration-style routing start point for the storage
/// backing directory capability.
pub struct StorageDeclAsRegistration {
    source: RegistrationSource,
    name: CapabilityName,
}

impl From<StorageDecl> for StorageDeclAsRegistration {
    fn from(decl: StorageDecl) -> Self {
        Self {
            name: decl.backing_dir,
            source: match decl.source {
                StorageDirectorySource::Parent => RegistrationSource::Parent,
                StorageDirectorySource::Self_ => RegistrationSource::Self_,
                StorageDirectorySource::Child(child) => RegistrationSource::Child(child),
            },
        }
    }
}

impl SourceName for StorageDeclAsRegistration {
    fn source_name(&self) -> &CapabilityName {
        &self.name
    }
}

impl RegistrationDeclCommon for StorageDeclAsRegistration {
    const TYPE: &'static str = "storage";

    fn source(&self) -> &RegistrationSource {
        &self.source
    }
}

// Error trait impls

impl ErrorNotFoundFromParent for UseProtocolDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for DebugRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromParentNotFound {
            moniker,
            capability_name: capability_name,
            capability_type: DebugRegistration::TYPE,
        }
    }
}

impl ErrorNotFoundInChild for DebugRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name: capability_name,
            capability_type: DebugRegistration::TYPE,
        }
    }
}

impl ErrorNotFoundFromParent for OfferProtocolDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferProtocolDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeProtocolDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseServiceDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferServiceDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferServiceDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeServiceDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseDirectoryDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferDirectoryDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferDirectoryDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeDirectoryDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseStorageDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferStorageDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for StorageDeclAsRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::StorageFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for StorageDeclAsRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::StorageFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for RunnerRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromEnvironmentNotFound {
            moniker,
            capability_name,
            capability_type: "runner",
        }
    }
}

impl ErrorNotFoundInChild for RunnerRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name,
            capability_type: "runner",
        }
    }
}

impl ErrorNotFoundFromParent for OfferRunnerDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferRunnerDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeRunnerDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for ResolverRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromParentNotFound {
            moniker,
            capability_name,
            capability_type: "resolver",
        }
    }
}

impl ErrorNotFoundInChild for ResolverRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name,
            capability_type: "resolver",
        }
    }
}

impl ErrorNotFoundFromParent for OfferResolverDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferResolverDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeResolverDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseEventDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferEventDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}
