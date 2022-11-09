// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/111245): Implement production component instance API.

#[cfg(test)]
pub mod fake {
    use crate::api::ComponentInstance as ComponentInstanceApi;
    use crate::api::Environment as EnvironmentApi;
    use crate::api::Moniker as MonikerApi;
    use crate::component::fake::Component;
    use crate::component_instance_capability::fake::ComponentInstanceCapability;
    use std::iter;

    #[derive(Default)]
    pub(crate) struct ComponentInstance;

    impl ComponentInstanceApi for ComponentInstance {
        type Moniker = Moniker;
        type Environment = Environment;
        type Component = Component;
        type ComponentInstanceCapability = ComponentInstanceCapability;

        fn moniker(&self) -> Self::Moniker {
            Moniker::default()
        }

        fn environment(&self) -> Self::Environment {
            Environment::default()
        }

        fn component(&self) -> Self::Component {
            Component::default()
        }

        fn parent(
            &self,
        ) -> Box<
            dyn ComponentInstanceApi<
                Moniker = Self::Moniker,
                Environment = Self::Environment,
                Component = Self::Component,
                ComponentInstanceCapability = Self::ComponentInstanceCapability,
            >,
        > {
            Box::new(Self::default())
        }

        fn children(
            &self,
        ) -> Box<
            dyn Iterator<
                Item = Box<
                    dyn ComponentInstanceApi<
                        Moniker = Self::Moniker,
                        Environment = Self::Environment,
                        Component = Self::Component,
                        ComponentInstanceCapability = Self::ComponentInstanceCapability,
                    >,
                >,
            >,
        > {
            Box::new(iter::empty())
        }

        fn descendants(
            &self,
        ) -> Box<
            dyn Iterator<
                Item = Box<
                    dyn ComponentInstanceApi<
                        Moniker = Self::Moniker,
                        Environment = Self::Environment,
                        Component = Self::Component,
                        ComponentInstanceCapability = Self::ComponentInstanceCapability,
                    >,
                >,
            >,
        > {
            Box::new(iter::empty())
        }

        fn ancestors(
            &self,
        ) -> Box<
            dyn Iterator<
                Item = Box<
                    dyn ComponentInstanceApi<
                        Moniker = Self::Moniker,
                        Environment = Self::Environment,
                        Component = Self::Component,
                        ComponentInstanceCapability = Self::ComponentInstanceCapability,
                    >,
                >,
            >,
        > {
            Box::new(iter::empty())
        }

        fn uses(&self) -> Box<dyn Iterator<Item = Self::ComponentInstanceCapability>> {
            Box::new(iter::empty())
        }

        fn exposes(&self) -> Box<dyn Iterator<Item = Self::ComponentInstanceCapability>> {
            Box::new(iter::empty())
        }

        fn offers(&self) -> Box<dyn Iterator<Item = Self::ComponentInstanceCapability>> {
            Box::new(iter::empty())
        }

        fn capabilities(&self) -> Box<dyn Iterator<Item = Self::ComponentInstanceCapability>> {
            Box::new(iter::empty())
        }
    }

    /// TODO(fxbug.dev/111245): Implement for production component instance API.
    #[derive(Default)]
    pub(crate) struct Moniker;

    impl MonikerApi for Moniker {}

    /// TODO(fxbug.dev/111245): Implement for production component instance API.
    #[derive(Default)]
    pub(crate) struct Environment;

    impl EnvironmentApi for Environment {}
}
