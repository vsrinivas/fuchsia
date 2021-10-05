// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{component_id_index::ComponentIdIndexError, policy::PolicyError},
    clonable_error::ClonableError,
    cm_rust::{CapabilityName, EventMode},
    fidl_fuchsia_component as fcomponent, fuchsia_zircon_status as zx,
    moniker::{PartialAbsoluteMoniker, PartialChildMoniker},
    thiserror::Error,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// Errors produced by `ComponentInstanceInterface`.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Debug, Error, Clone)]
pub enum ComponentInstanceError {
    #[error("component instance {} not found", moniker)]
    InstanceNotFound { moniker: PartialAbsoluteMoniker },
    #[error("component manager instance unavailable")]
    ComponentManagerInstanceUnavailable {},
    #[error("policy checker not found for component instance {}", moniker)]
    PolicyCheckerNotFound { moniker: PartialAbsoluteMoniker },
    #[error("component ID index not found for component instance {}", moniker)]
    ComponentIdIndexNotFound { moniker: PartialAbsoluteMoniker },
    #[error("malformed url {} for component instance {}", url, moniker)]
    MalformedUrl { url: String, moniker: PartialAbsoluteMoniker },
    #[error("url {} for component {} does not resolve to an absolute url", url, moniker)]
    NoAbsoluteUrl { url: String, moniker: PartialAbsoluteMoniker },
    // The capability routing static analyzer never produces this error subtype, so we don't need
    // to serialize it.
    #[cfg_attr(feature = "serde", serde(skip))]
    #[error("Failed to resolve `{}`: {}", moniker, err)]
    ResolveFailed {
        moniker: PartialAbsoluteMoniker,
        #[source]
        err: ClonableError,
    },
}

impl ComponentInstanceError {
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            ComponentInstanceError::ResolveFailed { .. }
            | ComponentInstanceError::InstanceNotFound { .. } => zx::Status::NOT_FOUND,
            _ => zx::Status::UNAVAILABLE,
        }
    }

    pub fn instance_not_found(moniker: PartialAbsoluteMoniker) -> ComponentInstanceError {
        ComponentInstanceError::InstanceNotFound { moniker }
    }

    pub fn cm_instance_unavailable() -> ComponentInstanceError {
        ComponentInstanceError::ComponentManagerInstanceUnavailable {}
    }

    pub fn resolve_failed(moniker: PartialAbsoluteMoniker, err: impl Into<anyhow::Error>) -> Self {
        Self::ResolveFailed { moniker, err: err.into().into() }
    }
}

// Custom implementation of PartialEq in which two ComponentInstanceError::ResolveFailed errors are
// never equal.
impl PartialEq for ComponentInstanceError {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (
                Self::InstanceNotFound { moniker: self_moniker },
                Self::InstanceNotFound { moniker: other_moniker },
            ) => self_moniker.eq(other_moniker),
            (
                Self::ComponentManagerInstanceUnavailable {},
                Self::ComponentManagerInstanceUnavailable {},
            ) => true,
            (
                Self::PolicyCheckerNotFound { moniker: self_moniker },
                Self::PolicyCheckerNotFound { moniker: other_moniker },
            ) => self_moniker.eq(other_moniker),
            (
                Self::ComponentIdIndexNotFound { moniker: self_moniker },
                Self::ComponentIdIndexNotFound { moniker: other_moniker },
            ) => self_moniker.eq(other_moniker),
            (Self::ResolveFailed { .. }, Self::ResolveFailed { .. }) => false,
            _ => false,
        }
    }
}

/// Errors produced during routing.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Debug, Error, Clone, PartialEq)]
pub enum RoutingError {
    #[error("Instance identified as source of capability is not running: `{}`", moniker)]
    SourceInstanceStopped { moniker: PartialAbsoluteMoniker },

    #[error(
        "Instance identified as source of capability is a non-executable component: `{}`",
        moniker
    )]
    SourceInstanceNotExecutable { moniker: PartialAbsoluteMoniker },

    #[error(
        "Source for directory backing storage from `{}` must be a component or component manager's namespace, but was {}",
        storage_moniker,
        source_type
    )]
    StorageDirectorySourceInvalid { source_type: String, storage_moniker: PartialAbsoluteMoniker },

    #[error(
        "Source for directory storage from `{}` was a child `{}`, but this child was not found",
        storage_moniker,
        child_moniker
    )]
    StorageDirectorySourceChildNotFound {
        storage_moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
    },

    #[error(
        "A `storage` declaration with a backing directory from child `{}` was found at `{}` for \
        `{}`, but no matching `expose` declaration was found in the child",
        child_moniker,
        moniker,
        capability_id
    )]
    StorageFromChildExposeNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "Component {} is trying to use a storage capability which is restricted to the component ID index.",
        moniker
    )]
    ComponentNotInIdIndex { moniker: PartialAbsoluteMoniker },

    #[error(
        "A `use from parent` declaration was found at `/` for `{}`, or a `register from parent` \
        declaration was found in the root environment or one of the root component's declared environments, \
        but no built-in capability matches",
        capability_id
    )]
    UseFromComponentManagerNotFound { capability_id: String },

    #[error(
        "A `register from parent` declaration was found at `/` or in one of the root component's declared \
        environments for `{}` but no built-in capability matches",
        capability_id
    )]
    RegisterFromComponentManagerNotFound { capability_id: String },

    #[error(
        "An `offer from parent` declaration was found at `/` for `{}`, \
        but no built-in capability matches",
        capability_id
    )]
    OfferFromComponentManagerNotFound { capability_id: String },

    #[error(
        "A `storage` declaration with a backing directory was found at `/` for `{}`, \
        but no built-in capability matches",
        capability_id
    )]
    StorageFromComponentManagerNotFound { capability_id: String },

    #[error(
        "A `use from parent` declaration was found at `{}` for `{}`, but no matching \
        `offer` declaration was found in the parent",
        moniker,
        capability_id
    )]
    UseFromParentNotFound { moniker: PartialAbsoluteMoniker, capability_id: String },

    #[error(
        "A `use from child` declaration was found at `{}` for `{}`, but no matching \
        `expose` declaration was found in child `#{}`>",
        moniker,
        capability_id,
        child
    )]
    UseFromChildNotFound { moniker: PartialAbsoluteMoniker, capability_id: String, child: String },

    #[error(
        "A `use` declaration was found at `{}` for {} `{}`, but no matching \
        {} registration was found in the component's environment",
        moniker,
        capability_type,
        capability_name,
        capability_type
    )]
    UseFromEnvironmentNotFound {
        moniker: PartialAbsoluteMoniker,
        capability_type: String,
        capability_name: CapabilityName,
    },

    #[error(
        "A `use` declaration was found at `{}` for {} `{}`, and a corresponding \
        entry was found in the root environment, which is not allowed.",
        moniker,
        capability_type,
        capability_name
    )]
    UseFromRootEnvironmentNotAllowed {
        moniker: PartialAbsoluteMoniker,
        capability_type: String,
        capability_name: CapabilityName,
    },

    #[error(
        "An `environment` {} registration from `parent` was found at `{}` for `{}`, but no \
        matching `offer` declaration was found in the parent",
        capability_type,
        moniker,
        capability_name
    )]
    EnvironmentFromParentNotFound {
        moniker: PartialAbsoluteMoniker,
        capability_type: String,
        capability_name: CapabilityName,
    },

    #[error(
        "An `environment` {} registration from `#{}` was found at `{}` for `{}`, but no matching \
        `expose` declaration was found in the child",
        capability_type,
        child_moniker,
        moniker,
        capability_name
    )]
    EnvironmentFromChildExposeNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_type: String,
        capability_name: CapabilityName,
    },

    #[error(
        "An `environment` {} registration from `#{}` was found at `{}` for `{}`, but no matching \
        child was found",
        capability_type,
        child_moniker,
        moniker,
        capability_name
    )]
    EnvironmentFromChildInstanceNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
        capability_type: String,
    },

    #[error(
        "An `offer from parent` declaration was found at `{}` for `{}`, but no matching \
        `offer` declaration was found in the parent",
        moniker,
        capability_id
    )]
    OfferFromParentNotFound { moniker: PartialAbsoluteMoniker, capability_id: String },

    #[error(
        "A `storage` declaration with a backing directory from `parent` was found at `{}` for `{}`,
        but no matching `offer` declaration was found in the parent",
        moniker,
        capability_id
    )]
    StorageFromParentNotFound { moniker: PartialAbsoluteMoniker, capability_id: String },

    #[error(
        "An `offer from #{}` declaration was found at `{}` for `{}`, but no matching child was \
        found",
        child_moniker,
        moniker,
        capability_id
    )]
    OfferFromChildInstanceNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "An `offer from #{}` declaration was found at `{}` for `{}`, but no matching collection \
        was found",
        collection,
        moniker,
        capability
    )]
    OfferFromCollectionNotFound {
        collection: String,
        moniker: PartialAbsoluteMoniker,
        capability: CapabilityName,
    },

    #[error(
        "An `offer from #{}` declaration was found at `{}` for `{}`, but no matching `expose` \
        declaration was found in the child",
        child_moniker,
        moniker,
        capability_id
    )]
    OfferFromChildExposeNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_id: String,
    },

    // TODO: Could this be distinguished by use/offer/expose?
    #[error(
        "A framework capability was sourced to `{}` with id `{}`, but no such \
        framework capability was found",
        moniker,
        capability_id
    )]
    CapabilityFromFrameworkNotFound { moniker: PartialAbsoluteMoniker, capability_id: String },

    #[error(
        "A capability was sourced to a base capability `{}` from `{}`, but this is unsupported",
        capability_id,
        moniker
    )]
    CapabilityFromCapabilityNotFound { moniker: PartialAbsoluteMoniker, capability_id: String },

    // TODO: Could this be distinguished by use/offer/expose?
    #[error(
        "A capability was sourced to component manager with id `{}`, but no matching \
        capability was found",
        capability_id
    )]
    CapabilityFromComponentManagerNotFound { capability_id: String },

    #[error(
        "A capability was sourced to storage capability `{}` with id `{}`, but no matching \
        capability was found",
        storage_capability,
        capability_id
    )]
    CapabilityFromStorageCapabilityNotFound { storage_capability: String, capability_id: String },

    #[error(
        "An exposed capability `{}` was used at `{}`, but no matching `expose` \
        declaration was found",
        capability_id,
        moniker
    )]
    UsedExposeNotFound { moniker: PartialAbsoluteMoniker, capability_id: String },

    #[error(
        "An `expose from #{}` declaration was found at `{}` for `{}`, but no matching child was \
        found",
        child_moniker,
        moniker,
        capability_id
    )]
    ExposeFromChildInstanceNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "An `expose from #{}` declaration was found at `{}` for `{}`, but no matching collection \
        was found",
        collection,
        moniker,
        capability
    )]
    ExposeFromCollectionNotFound {
        collection: String,
        moniker: PartialAbsoluteMoniker,
        capability: CapabilityName,
    },

    #[error(
        "An `expose from #{}` declaration was found at `{}` for `{}`, but no matching `expose` \
        declaration was found in the child",
        child_moniker,
        moniker,
        capability_id
    )]
    ExposeFromChildExposeNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "An `expose from framework` declaration was found at `{}` for `{}`, but no matching \
        framework capability was found",
        moniker,
        capability_id
    )]
    ExposeFromFrameworkNotFound { moniker: PartialAbsoluteMoniker, capability_id: String },

    #[error(
        "A `use from #{}` declaration was found at `{}` for `{}`, but no matching \
        `expose` declaration was found in the child",
        child_moniker,
        moniker,
        capability_id
    )]
    UseFromChildExposeNotFound {
        child_moniker: PartialChildMoniker,
        moniker: PartialAbsoluteMoniker,
        capability_id: String,
    },

    #[error("Routing a capability from an unsupported source type: {}", source_type)]
    UnsupportedRouteSource { source_type: String },

    #[error(transparent)]
    ComponentInstanceError(#[from] ComponentInstanceError),

    #[error(transparent)]
    EventsRoutingError(#[from] EventsRoutingError),

    #[error(transparent)]
    RightsRoutingError(#[from] RightsRoutingError),

    #[error(transparent)]
    PolicyError(#[from] PolicyError),

    #[error(transparent)]
    ComponentIdIndexError(#[from] ComponentIdIndexError),
}

impl RoutingError {
    /// Convert this error into its approximate `fuchsia.component.Error` equivalent.
    pub fn as_fidl_error(&self) -> fcomponent::Error {
        fcomponent::Error::ResourceUnavailable
    }

    /// Convert this error into its approximate `zx::Status` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            RoutingError::PolicyError(_) => zx::Status::ACCESS_DENIED,
            RoutingError::ComponentInstanceError(err) => err.as_zx_status(),
            _ => zx::Status::UNAVAILABLE,
        }
    }

    pub fn source_instance_stopped(moniker: &PartialAbsoluteMoniker) -> Self {
        Self::SourceInstanceStopped { moniker: moniker.clone() }
    }

    pub fn source_instance_not_executable(moniker: &PartialAbsoluteMoniker) -> Self {
        Self::SourceInstanceNotExecutable { moniker: moniker.clone() }
    }

    pub fn storage_directory_source_invalid(
        source_type: impl Into<String>,
        storage_moniker: &PartialAbsoluteMoniker,
    ) -> Self {
        Self::StorageDirectorySourceInvalid {
            source_type: source_type.into(),
            storage_moniker: storage_moniker.clone(),
        }
    }

    pub fn storage_directory_source_child_not_found(
        storage_moniker: &PartialAbsoluteMoniker,
        child_moniker: &PartialChildMoniker,
    ) -> Self {
        Self::StorageDirectorySourceChildNotFound {
            storage_moniker: storage_moniker.clone(),
            child_moniker: child_moniker.clone(),
        }
    }

    pub fn storage_from_child_expose_not_found(
        child_moniker: &PartialChildMoniker,
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::StorageFromChildExposeNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn use_from_component_manager_not_found(capability_id: impl Into<String>) -> Self {
        Self::UseFromComponentManagerNotFound { capability_id: capability_id.into() }
    }

    pub fn register_from_component_manager_not_found(capability_id: impl Into<String>) -> Self {
        Self::RegisterFromComponentManagerNotFound { capability_id: capability_id.into() }
    }

    pub fn offer_from_component_manager_not_found(capability_id: impl Into<String>) -> Self {
        Self::OfferFromComponentManagerNotFound { capability_id: capability_id.into() }
    }

    pub fn storage_from_component_manager_not_found(capability_id: impl Into<String>) -> Self {
        Self::StorageFromComponentManagerNotFound { capability_id: capability_id.into() }
    }

    pub fn use_from_parent_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::UseFromParentNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn use_from_child_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
        child: String,
    ) -> Self {
        Self::UseFromChildNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
            child,
        }
    }

    pub fn offer_from_parent_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::OfferFromParentNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn storage_from_parent_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::StorageFromParentNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn offer_from_child_instance_not_found(
        child_moniker: &PartialChildMoniker,
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::OfferFromChildInstanceNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn offer_from_child_expose_not_found(
        child_moniker: &PartialChildMoniker,
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::OfferFromChildExposeNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn used_expose_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::UsedExposeNotFound { moniker: moniker.clone(), capability_id: capability_id.into() }
    }

    pub fn expose_from_child_instance_not_found(
        child_moniker: &PartialChildMoniker,
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::ExposeFromChildInstanceNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn expose_from_child_expose_not_found(
        child_moniker: &PartialChildMoniker,
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::ExposeFromChildExposeNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn capability_from_framework_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::CapabilityFromFrameworkNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn capability_from_capability_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::CapabilityFromCapabilityNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn capability_from_component_manager_not_found(capability_id: impl Into<String>) -> Self {
        Self::CapabilityFromComponentManagerNotFound { capability_id: capability_id.into() }
    }

    pub fn capability_from_storage_capability_not_found(
        storage_capability: impl Into<String>,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::CapabilityFromStorageCapabilityNotFound {
            storage_capability: storage_capability.into(),
            capability_id: capability_id.into(),
        }
    }

    pub fn expose_from_framework_not_found(
        moniker: &PartialAbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::ExposeFromFrameworkNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn unsupported_route_source(source: impl Into<String>) -> Self {
        Self::UnsupportedRouteSource { source_type: source.into() }
    }
}

/// Errors produced during routing specific to events.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Error, Debug, Clone, PartialEq)]
pub enum EventsRoutingError {
    #[error("Filter is not a subset")]
    InvalidFilter,

    #[error("Event routes must end at source with a filter declaration")]
    MissingFilter,

    #[error("Event mode, `{:?}`, cannot be propagated", event_mode)]
    CannotPropagateEventMode { event_mode: EventMode },

    #[error("Event routes must end at source with a modes declaration")]
    MissingModes,
}

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Debug, Error, Clone, PartialEq)]
pub enum RightsRoutingError {
    #[error("Requested rights greater than provided rights")]
    Invalid,

    #[error("Directory routes must end at source with a rights declaration")]
    MissingRightsSource,
}

impl RightsRoutingError {
    /// Convert this error into its approximate `zx::Status` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        zx::Status::UNAVAILABLE
    }
}
