// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::moniker::{AbsoluteMoniker, ExtendedMoniker, PartialMoniker, RelativeMoniker},
    anyhow::Error,
    clonable_error::ClonableError,
    fidl_fuchsia_component as fcomponent, fuchsia_zircon as zx,
    thiserror::Error,
};

/// Errors produced during routing.
#[derive(Debug, Error, Clone)]
pub enum RoutingError {
    #[error("Instance identified as source of capability is not running: `{}`", moniker)]
    SourceInstanceStopped { moniker: AbsoluteMoniker },
    #[error(
        "Instance identified as source of capability is a non-executable component: `{}`",
        moniker
    )]
    SourceInstanceNotExecutable { moniker: AbsoluteMoniker },
    #[error("Failed to open path `{}` in outgoing directory of `{}`: {}", path, moniker, err)]
    OpenOutgoingFailed {
        moniker: AbsoluteMoniker,
        path: String,
        #[source]
        err: ClonableError,
    },
    #[error("Failed to open path `{}` in component manager's namespace: {}", path, err)]
    OpenComponentManagerNamespaceFailed {
        path: String,
        #[source]
        err: ClonableError,
    },
    #[error(
        "Failed to open path `{}`, in storage directory for `{}` backed by `{}`: {}",
        path,
        relative_moniker,
        moniker,
        err
    )]
    OpenStorageFailed {
        moniker: ExtendedMoniker,
        relative_moniker: RelativeMoniker,
        path: String,
        #[source]
        err: ClonableError,
    },
    #[error("Source for storage capability must be a component, but was `{}`", source_type)]
    StorageSourceIsNotComponent { source_type: &'static str },
    #[error(
        "Source for directory backing storage from `{}` must be a component or component manager's namespace, but was {}",
        storage_moniker,
        source_type
    )]
    StorageDirectorySourceInvalid { source_type: &'static str, storage_moniker: AbsoluteMoniker },

    #[error(
        "Source for directory storage from `{}` was a child `{}`, but this child was not found",
        storage_moniker,
        child_moniker
    )]
    StorageDirectorySourceChildNotFound {
        storage_moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
    },

    #[error(
        "A `storage` declaration with a backing directory from child `{}` was found at `{}` for \
        `{}`, but no matching `expose` declaration was found in the child",
        child_moniker,
        moniker,
        capability_id
    )]
    StorageFromChildExposeNotFound {
        child_moniker: PartialMoniker,
        moniker: AbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "A `use from parent` declaration was found at `/` for `{}`, \
        but no built-in capability matches",
        capability_id
    )]
    UseFromComponentManagerNotFound { capability_id: String },

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
    UseFromParentNotFound { moniker: AbsoluteMoniker, capability_id: String },

    #[error(
        "A `use` declaration was found at `{}` for {} `{}`, but no matching \
        {} registration was found in the component's environment",
        moniker,
        capability_type,
        capability_id,
        capability_type
    )]
    UseFromEnvironmentNotFound {
        moniker: AbsoluteMoniker,
        capability_type: String,
        capability_id: String,
    },

    #[error(
        "An `environment` {} registration from `parent` was found at `{}` for `{}`, but no \
        matching `offer` declaration was found in the parent",
        capability_type,
        moniker,
        capability_id
    )]
    EnvironmentFromParentNotFound {
        moniker: AbsoluteMoniker,
        capability_type: String,
        capability_id: String,
    },

    #[error(
        "An `environment` {} registration from `#{}` was found at `{}` for `{}`, but no matching \
        `expose` declaration was found in the child",
        capability_type,
        child_moniker,
        moniker,
        capability_id
    )]
    EnvironmentFromChildExposeNotFound {
        child_moniker: PartialMoniker,
        moniker: AbsoluteMoniker,
        capability_type: String,
        capability_id: String,
    },

    #[error(
        "An `offer from parent` declaration was found at `{}` for `{}`, but no matching \
        `offer` declaration was found in the parent",
        moniker,
        capability_id
    )]
    OfferFromParentNotFound { moniker: AbsoluteMoniker, capability_id: String },

    #[error(
        "A `storage` declaration with a backing directory from `parent` was found at `{}` for `{}`,
        but no matching `offer` declaration was found in the parent",
        moniker,
        capability_id
    )]
    StorageFromParentNotFound { moniker: AbsoluteMoniker, capability_id: String },

    #[error(
        "An `offer from #{}` declaration was found at `{}` for `{}`, but no matching child was \
        found",
        child_moniker,
        moniker,
        capability_id
    )]
    OfferFromChildInstanceNotFound {
        child_moniker: PartialMoniker,
        moniker: AbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "An `offer from #{}` declaration was found at `{}` for `{}`, but no matching `expose` \
        declaration was found in the child",
        child_moniker,
        moniker,
        capability_id
    )]
    OfferFromChildExposeNotFound {
        child_moniker: PartialMoniker,
        moniker: AbsoluteMoniker,
        capability_id: String,
    },

    // TODO: Could this be distinguished by use/offer/expose?
    #[error(
        "A framework capability was sourced to `{}` with id `{}`, but no such \
        framework capability was found",
        moniker,
        capability_id
    )]
    CapabilityFromFrameworkNotFound { moniker: AbsoluteMoniker, capability_id: String },

    #[error(
        "A capability was sourced to a base capability `{}` from `{}`, but this is unsupported",
        capability_id,
        moniker
    )]
    CapabilityFromCapabilityNotFound { moniker: AbsoluteMoniker, capability_id: String },

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
    UsedExposeNotFound { moniker: AbsoluteMoniker, capability_id: String },

    #[error(
        "An `expose from #{}` declaration was found at `{}` for `{}`, but no matching child was \
        found",
        child_moniker,
        moniker,
        capability_id
    )]
    ExposeFromChildInstanceNotFound {
        child_moniker: PartialMoniker,
        moniker: AbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "An `expose from #{}` declaration was found at `{}` for `{}`, but no matching `expose` \
        declaration was found in the child",
        child_moniker,
        moniker,
        capability_id
    )]
    ExposeFromChildExposeNotFound {
        child_moniker: PartialMoniker,
        moniker: AbsoluteMoniker,
        capability_id: String,
    },

    #[error(
        "An `expose from framework` declaration was found at `{}` for `{}`, but no matching \
        framework capability was found",
        moniker,
        capability_id
    )]
    ExposeFromFrameworkNotFound { moniker: AbsoluteMoniker, capability_id: String },
}

impl RoutingError {
    /// Convert this error into its approximate `fuchsia.component.Error` equivalent.
    pub fn as_fidl_error(&self) -> fcomponent::Error {
        fcomponent::Error::ResourceUnavailable
    }

    /// Convert this error into its approximate `zx::Status` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        zx::Status::UNAVAILABLE
    }

    pub fn source_instance_stopped(moniker: &AbsoluteMoniker) -> Self {
        Self::SourceInstanceStopped { moniker: moniker.clone() }
    }

    pub fn source_instance_not_executable(moniker: &AbsoluteMoniker) -> Self {
        Self::SourceInstanceNotExecutable { moniker: moniker.clone() }
    }

    pub fn open_outgoing_failed(
        moniker: &AbsoluteMoniker,
        path: impl Into<String>,
        err: impl Into<Error>,
    ) -> Self {
        Self::OpenOutgoingFailed {
            moniker: moniker.clone(),
            path: path.into(),
            err: err.into().into(),
        }
    }

    pub fn open_component_manager_namespace_failed(
        path: impl Into<String>,
        err: impl Into<Error>,
    ) -> Self {
        Self::OpenComponentManagerNamespaceFailed { path: path.into(), err: err.into().into() }
    }

    pub fn open_storage_failed(
        moniker: &ExtendedMoniker,
        relative_moniker: &RelativeMoniker,
        path: impl Into<String>,
        err: impl Into<Error>,
    ) -> Self {
        Self::OpenStorageFailed {
            moniker: moniker.clone(),
            relative_moniker: relative_moniker.clone(),
            path: path.into(),
            err: err.into().into(),
        }
    }

    pub fn storage_source_is_not_component(source_type: &'static str) -> Self {
        Self::StorageSourceIsNotComponent { source_type }
    }

    pub fn storage_directory_source_invalid(
        source_type: &'static str,
        storage_moniker: &AbsoluteMoniker,
    ) -> Self {
        Self::StorageDirectorySourceInvalid {
            source_type,
            storage_moniker: storage_moniker.clone(),
        }
    }

    pub fn storage_directory_source_child_not_found(
        storage_moniker: &AbsoluteMoniker,
        child_moniker: &PartialMoniker,
    ) -> Self {
        Self::StorageDirectorySourceChildNotFound {
            storage_moniker: storage_moniker.clone(),
            child_moniker: child_moniker.clone(),
        }
    }

    pub fn storage_from_child_expose_not_found(
        child_moniker: &PartialMoniker,
        moniker: &AbsoluteMoniker,
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

    pub fn offer_from_component_manager_not_found(capability_id: impl Into<String>) -> Self {
        Self::OfferFromComponentManagerNotFound { capability_id: capability_id.into() }
    }

    pub fn storage_from_component_manager_not_found(capability_id: impl Into<String>) -> Self {
        Self::StorageFromComponentManagerNotFound { capability_id: capability_id.into() }
    }

    pub fn use_from_parent_not_found(
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::UseFromParentNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn use_from_environment_not_found(
        moniker: &AbsoluteMoniker,
        capability_type: impl Into<String>,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::UseFromEnvironmentNotFound {
            moniker: moniker.clone(),
            capability_type: capability_type.into(),
            capability_id: capability_id.into(),
        }
    }

    pub fn environment_from_parent_not_found(
        moniker: &AbsoluteMoniker,
        capability_type: impl Into<String>,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::EnvironmentFromParentNotFound {
            moniker: moniker.clone(),
            capability_type: capability_type.into(),
            capability_id: capability_id.into(),
        }
    }

    pub fn environment_from_child_expose_not_found(
        child_moniker: &PartialMoniker,
        moniker: &AbsoluteMoniker,
        capability_type: impl Into<String>,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::EnvironmentFromChildExposeNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_type: capability_type.into(),
            capability_id: capability_id.into(),
        }
    }

    pub fn offer_from_parent_not_found(
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::OfferFromParentNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn storage_from_parent_not_found(
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::StorageFromParentNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn offer_from_child_instance_not_found(
        child_moniker: &PartialMoniker,
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::OfferFromChildInstanceNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn offer_from_child_expose_not_found(
        child_moniker: &PartialMoniker,
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::OfferFromChildExposeNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn used_expose_not_found(
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::UsedExposeNotFound { moniker: moniker.clone(), capability_id: capability_id.into() }
    }

    pub fn expose_from_child_instance_not_found(
        child_moniker: &PartialMoniker,
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::ExposeFromChildInstanceNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn expose_from_child_expose_not_found(
        child_moniker: &PartialMoniker,
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::ExposeFromChildExposeNotFound {
            child_moniker: child_moniker.clone(),
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn capability_from_framework_not_found(
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::CapabilityFromFrameworkNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }

    pub fn capability_from_capability_not_found(
        moniker: &AbsoluteMoniker,
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
        moniker: &AbsoluteMoniker,
        capability_id: impl Into<String>,
    ) -> Self {
        Self::ExposeFromFrameworkNotFound {
            moniker: moniker.clone(),
            capability_id: capability_id.into(),
        }
    }
}
