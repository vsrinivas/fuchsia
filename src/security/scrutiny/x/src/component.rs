// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/111243): Implement production component API.

#[cfg(test)]
pub mod fake {
    use crate::api::Component as ComponentApi;
    use crate::api::PackageResolverUrl;
    use crate::component_capability::fake::ComponentCapability;
    use crate::component_instance::fake::ComponentInstance;
    use crate::package::fake::Package;
    use std::iter;

    #[derive(Default)]
    pub(crate) struct Component;

    impl ComponentApi for Component {
        type Package = Package;
        type ComponentCapability = ComponentCapability;
        type ComponentInstance = ComponentInstance;

        fn packages(&self) -> Box<dyn Iterator<Item = Self::Package>> {
            Box::new(iter::empty())
        }

        fn children(&self) -> Box<dyn Iterator<Item = PackageResolverUrl>> {
            Box::new(iter::empty())
        }

        fn uses(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
            Box::new(iter::empty())
        }

        fn exposes(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
            Box::new(iter::empty())
        }

        fn offers(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
            Box::new(iter::empty())
        }

        fn capabilities(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
            Box::new(iter::empty())
        }

        fn instances(&self) -> Box<dyn Iterator<Item = Self::ComponentInstance>> {
            Box::new(iter::empty())
        }
    }
}
