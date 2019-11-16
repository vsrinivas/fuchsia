// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::error::ModelError,
    cm_rust::{
        CapabilityPath, ExposeDecl, ExposeSource, OfferDecl, OfferDirectorySource,
        OfferServiceSource, UseDecl, UseSource,
    },
    failure::Fail,
    fuchsia_zircon as zx,
    futures::future::BoxFuture,
};

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "Invalid framework capability.")]
    InvalidFrameworkCapability {},
    #[fail(display = "Invalid Built-in capability.")]
    InvalidBuiltinCapability {},
}

/// Describes the type of capability provided by the component manager which
/// could either be a realm-scoped framework capability, or a builtin-capability.
/// Each capability type has a corresponding `CapabilityPath` in the component
/// manager's namespace. Note that this path may not be unique as capabilities can
/// compose.
#[derive(Debug, Clone)]
pub enum ComponentManagerCapability {
    Service(CapabilityPath),
    LegacyService(CapabilityPath),
    Directory(CapabilityPath),
}

impl ComponentManagerCapability {
    pub fn path(&self) -> &CapabilityPath {
        match self {
            ComponentManagerCapability::Service(source_path) => &source_path,
            ComponentManagerCapability::LegacyService(source_path) => &source_path,
            ComponentManagerCapability::Directory(source_path) => &source_path,
        }
    }

    pub fn builtin_from_use_decl(decl: &UseDecl) -> Result<Self, Error> {
        match decl {
            UseDecl::Service(s) if s.source == UseSource::Realm => {
                Ok(ComponentManagerCapability::Service(s.source_path.clone()))
            }
            UseDecl::LegacyService(s) if s.source == UseSource::Realm => {
                Ok(ComponentManagerCapability::LegacyService(s.source_path.clone()))
            }
            UseDecl::Directory(d) if d.source == UseSource::Realm => {
                Ok(ComponentManagerCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidBuiltinCapability {});
            }
        }
    }

    pub fn builtin_from_offer_decl(decl: &OfferDecl) -> Result<Self, Error> {
        match decl {
            OfferDecl::LegacyService(s) if s.source == OfferServiceSource::Realm => {
                Ok(ComponentManagerCapability::LegacyService(s.source_path.clone()))
            }
            OfferDecl::Directory(d) if d.source == OfferDirectorySource::Realm => {
                Ok(ComponentManagerCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidBuiltinCapability {});
            }
        }
    }

    pub fn framework_from_use_decl(decl: &UseDecl) -> Result<Self, Error> {
        match decl {
            UseDecl::Service(s) if s.source == UseSource::Framework => {
                Ok(ComponentManagerCapability::Service(s.source_path.clone()))
            }
            UseDecl::LegacyService(s) if s.source == UseSource::Framework => {
                Ok(ComponentManagerCapability::LegacyService(s.source_path.clone()))
            }
            UseDecl::Directory(d) if d.source == UseSource::Framework => {
                Ok(ComponentManagerCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }

    pub fn framework_from_offer_decl(decl: &OfferDecl) -> Result<Self, Error> {
        match decl {
            OfferDecl::Directory(d) if d.source == OfferDirectorySource::Framework => {
                Ok(ComponentManagerCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }

    pub fn framework_from_expose_decl(decl: &ExposeDecl) -> Result<Self, Error> {
        match decl {
            ExposeDecl::Directory(d) if d.source == ExposeSource::Framework => {
                Ok(ComponentManagerCapability::Directory(d.source_path.clone()))
            }
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }
}

/// The server-side of a component manager capability implements this trait.
/// Multiple `ComponentManagerCapabilityProvider` objects can compose with one another for a single
/// framework capability request. For example, a `ComponentManagerCapabitilityProvider` can be
/// interposed between the primary `ComponentManagerCapabilityProvider and the client for the
/// purpose of logging and testing. A `ComponentManagerCapabilityProvider` is typically provided
/// by a corresponding `Hook` in response to the `RouteFrameworkCapability` or
/// `RouteBuiltinCapability` event.
pub trait ComponentManagerCapabilityProvider: Send + Sync {
    // Called to bind a server end of a zx::Channel to the provided framework capability.
    // If the capability is a directory, then |flags|, |open_mode| and |relative_path|
    // will be propagated along to open the appropriate directory.
    fn open(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_end: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>>;
}
