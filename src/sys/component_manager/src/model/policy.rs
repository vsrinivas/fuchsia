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
        routing_test_helpers::{
            instantiate_global_policy_checker_tests, policy::GlobalPolicyCheckerTest,
        },
        std::sync::Arc,
    };

    // Tests `GlobalPolicyChecker` methods for `ComponentInstance`s.
    #[derive(Default)]
    struct GlobalPolicyCheckerTestForCm {}

    impl GlobalPolicyCheckerTest<ComponentInstance> for GlobalPolicyCheckerTestForCm {
        fn make_component(&self, abs_moniker: AbsoluteMoniker) -> Arc<ComponentInstance> {
            let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
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
                fsys::OnTerminate::None,
                WeakModelContext::default(),
                WeakExtendedInstance::Component(WeakComponentInstance::default()),
                Arc::new(Hooks::new(None)),
                None,
            )
        }
    }

    instantiate_global_policy_checker_tests!(GlobalPolicyCheckerTestForCm);
}
