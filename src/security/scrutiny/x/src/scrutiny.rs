// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::Scrutiny as ScrutinyApi;
use super::blob::Blob;
use super::component::Component;
use super::component_capability::ComponentCapability;
use super::component_instance::ComponentInstance;
use super::component_instance_capability::ComponentInstanceCapability;
use super::component_manager::ComponentManager;
use super::component_resolver::ComponentResolver;
use super::data_source::DataSource;
use super::package::Package;
use super::package_resolver::PackageResolver;
use super::system::System;
use std::iter;

/// TODO(fxbug.dev/112121): Implement production [`ScrutinyApi`].
#[derive(Default)]
pub(crate) struct Scrutiny;

impl ScrutinyApi for Scrutiny {
    type Blob = Blob;
    type Package = Package;
    type PackageResolver = PackageResolver;
    type Component = Component;
    type ComponentResolver = ComponentResolver;
    type ComponentCapability = ComponentCapability;
    type ComponentInstance = ComponentInstance;
    type ComponentInstanceCapability = ComponentInstanceCapability;
    type System = System;
    type ComponentManager = ComponentManager;
    type DataSource = DataSource;

    fn system(&self) -> Self::System {
        System::default()
    }

    fn component_manager(&self) -> Self::ComponentManager {
        ComponentManager::default()
    }

    fn data_sources(&self) -> Box<dyn Iterator<Item = Self::DataSource>> {
        Box::new(iter::empty())
    }

    fn blobs(&self) -> Box<dyn Iterator<Item = Self::Blob>> {
        Box::new(iter::empty())
    }

    fn packages(&self) -> Box<dyn Iterator<Item = Self::Package>> {
        Box::new(iter::empty())
    }

    fn package_resolvers(&self) -> Box<dyn Iterator<Item = Self::PackageResolver>> {
        Box::new(iter::empty())
    }

    fn components(&self) -> Box<dyn Iterator<Item = Self::Component>> {
        Box::new(iter::empty())
    }

    fn component_resolvers(&self) -> Box<dyn Iterator<Item = Self::ComponentResolver>> {
        Box::new(iter::empty())
    }

    fn component_capabilities(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
        Box::new(iter::empty())
    }

    fn component_instances(&self) -> Box<dyn Iterator<Item = Self::ComponentInstance>> {
        Box::new(iter::empty())
    }

    fn component_instance_capabilities(
        &self,
    ) -> Box<dyn Iterator<Item = Self::ComponentInstanceCapability>> {
        Box::new(iter::empty())
    }
}
