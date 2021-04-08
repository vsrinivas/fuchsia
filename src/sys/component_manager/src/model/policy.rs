// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/71901): remove aliases once the routing lib has a stable API.
pub type PolicyError = ::routing::policy::PolicyError;
pub type GlobalPolicyChecker = ::routing::policy::GlobalPolicyChecker;
pub type ScopedPolicyChecker = ::routing::policy::ScopedPolicyChecker;

#[cfg(test)]
mod tests {
    use {
        crate::model::{
            component::{
                ComponentInstance, ComponentManagerInstance, WeakComponentInstance,
                WeakExtendedInstance,
            },
            context::WeakModelContext,
            environment::{DebugRegistry, Environment, RunnerRegistry},
            hooks::Hooks,
            resolver::ResolverRegistry,
        },
        anyhow::Error,
        fidl_fuchsia_sys2 as fsys,
        moniker::AbsoluteMoniker,
        routing_test_helpers::policy::GlobalPolicyCheckerTest,
        std::sync::Arc,
    };

    // Tests `GlobalPolicyChecker` methods for `ComponentInstance`s.
    #[derive(Default)]
    struct GlobalPolicyCheckerTestForCm {}

    impl GlobalPolicyCheckerTest<ComponentInstance> for GlobalPolicyCheckerTestForCm {
        fn make_component(&self, abs_moniker: AbsoluteMoniker) -> Arc<ComponentInstance> {
            let top_instance = Arc::new(ComponentManagerInstance::new(vec![]));
            ComponentInstance::new(
                Arc::new(Environment::new_root(
                    &top_instance,
                    RunnerRegistry::default(),
                    ResolverRegistry::new(),
                    DebugRegistry::default(),
                )),
                abs_moniker,
                "test:///bar".into(),
                fsys::StartupMode::Lazy,
                WeakModelContext::default(),
                WeakExtendedInstance::Component(WeakComponentInstance::default()),
                Arc::new(Hooks::new(None)),
            )
        }
    }

    fn new_test() -> GlobalPolicyCheckerTestForCm {
        GlobalPolicyCheckerTestForCm::default()
    }

    #[test]
    fn global_policy_checker_can_route_capability_framework_cap_for_cm() -> Result<(), Error> {
        new_test().global_policy_checker_can_route_capability_framework_cap()
    }

    #[test]
    fn global_policy_checker_can_route_capability_namespace_cap_for_cm() -> Result<(), Error> {
        new_test().global_policy_checker_can_route_capability_namespace_cap()
    }

    #[test]
    fn global_policy_checker_can_route_capability_component_cap_for_cm() -> Result<(), Error> {
        new_test().global_policy_checker_can_route_capability_component_cap()
    }

    #[test]
    fn global_policy_checker_can_route_capability_capability_cap_for_cm() -> Result<(), Error> {
        new_test().global_policy_checker_can_route_capability_capability_cap()
    }

    #[test]
    fn global_policy_checker_can_route_debug_capability_capability_cap_for_cm() -> Result<(), Error>
    {
        new_test().global_policy_checker_can_route_debug_capability_capability_cap()
    }

    #[test]
    fn global_policy_checker_can_route_capability_builtin_cap_for_cm() -> Result<(), Error> {
        new_test().global_policy_checker_can_route_capability_builtin_cap()
    }

    #[test]
    fn global_policy_checker_can_route_capability_with_instance_ids_cap_for_cm() -> Result<(), Error>
    {
        new_test().global_policy_checker_can_route_capability_with_instance_ids_cap()
    }
}
