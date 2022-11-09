// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/112121): Implement production [`ScrutinyApi`].

#[cfg(test)]
pub mod fake {
    use crate::api::Scrutiny as ScrutinyApi;
    use crate::blob::fake::Blob;
    use crate::component::fake::Component;
    use crate::component_capability::fake::ComponentCapability;
    use crate::component_instance::fake::ComponentInstance;
    use crate::component_instance_capability::fake::ComponentInstanceCapability;
    use crate::component_manager::fake::ComponentManager;
    use crate::component_resolver::fake::ComponentResolver;
    use crate::data_source::fake::DataSource;
    use crate::package::fake::Package;
    use crate::package_resolver::fake::PackageResolver;
    use crate::system::fake::System;
    use std::iter;

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
}
