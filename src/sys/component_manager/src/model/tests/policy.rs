// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{
            ComponentInstance, ComponentManagerInstance, WeakComponentInstance,
            WeakExtendedInstance,
        },
        context::WeakModelContext,
        environment::Environment,
        hooks::Hooks,
        resolver::ResolverRegistry,
    },
    anyhow::Error,
    cm_moniker::InstancedAbsoluteMoniker,
    fidl_fuchsia_component_decl as fdecl,
    routing::environment::{DebugRegistry, RunnerRegistry},
    routing_test_helpers::{
        instantiate_global_policy_checker_tests, policy::GlobalPolicyCheckerTest,
    },
    std::sync::Arc,
};

// Tests `GlobalPolicyChecker` methods for `ComponentInstance`s.
#[derive(Default)]
struct GlobalPolicyCheckerTestForCm {}

impl GlobalPolicyCheckerTest<ComponentInstance> for GlobalPolicyCheckerTestForCm {
    fn make_component(
        &self,
        instanced_moniker: InstancedAbsoluteMoniker,
    ) -> Arc<ComponentInstance> {
        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        ComponentInstance::new(
            Arc::new(Environment::new_root(
                &top_instance,
                RunnerRegistry::default(),
                ResolverRegistry::new(),
                DebugRegistry::default(),
            )),
            instanced_moniker,
            "test:///bar".into(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::default(),
            WeakExtendedInstance::Component(WeakComponentInstance::default()),
            Arc::new(Hooks::new(None)),
            None,
        )
    }
}

instantiate_global_policy_checker_tests!(GlobalPolicyCheckerTestForCm);
