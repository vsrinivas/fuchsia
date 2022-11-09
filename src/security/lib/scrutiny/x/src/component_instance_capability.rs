// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/111246): Implement production component instance capability API.

#[cfg(test)]
pub mod fake {
    use crate::api::ComponentInstanceCapability as ComponentInstanceCapabilityApi;
    use crate::component_capability::fake::ComponentCapability;
    use crate::component_instance::fake::ComponentInstance;
    use std::iter;

    #[derive(Default)]
    pub(crate) struct ComponentInstanceCapability;

    impl ComponentInstanceCapabilityApi for ComponentInstanceCapability {
        type ComponentCapability = ComponentCapability;
        type ComponentInstance = ComponentInstance;

        fn component_capability(&self) -> Self::ComponentCapability {
            ComponentCapability::default()
        }

        fn component_instance(&self) -> Self::ComponentInstance {
            ComponentInstance::default()
        }

        fn source(
            &self,
        ) -> Box<
            dyn ComponentInstanceCapabilityApi<
                ComponentCapability = Self::ComponentCapability,
                ComponentInstance = Self::ComponentInstance,
            >,
        > {
            Box::new(ComponentInstanceCapability::default())
        }

        fn source_path(
            &self,
        ) -> Box<
            dyn Iterator<
                Item = Box<
                    dyn ComponentInstanceCapabilityApi<
                        ComponentCapability = Self::ComponentCapability,
                        ComponentInstance = Self::ComponentInstance,
                    >,
                >,
            >,
        > {
            Box::new(iter::empty())
        }

        fn destination_paths(
            &self,
        ) -> Box<
            dyn Iterator<
                Item = Box<
                    dyn Iterator<
                        Item = Box<
                            dyn ComponentInstanceCapabilityApi<
                                ComponentCapability = Self::ComponentCapability,
                                ComponentInstance = Self::ComponentInstance,
                            >,
                        >,
                    >,
                >,
            >,
        > {
            Box::new(iter::empty())
        }

        fn all_paths(
            &self,
        ) -> Box<
            dyn Iterator<
                Item = Box<
                    dyn Iterator<
                        Item = Box<
                            dyn ComponentInstanceCapabilityApi<
                                ComponentCapability = Self::ComponentCapability,
                                ComponentInstance = Self::ComponentInstance,
                            >,
                        >,
                    >,
                >,
            >,
        > {
            Box::new(iter::empty())
        }
    }
}
