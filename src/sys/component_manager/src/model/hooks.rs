// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{framework::FrameworkCapability, model::*},
    cm_rust::FrameworkCapabilityDecl,
    futures::{future::BoxFuture, lock::Mutex},
    std::sync::Arc,
};

// ByAddr allows two Arcs to be compared by address instead of using its contained
// type's comparator.
#[derive(Debug)]
pub struct ByAddr<T: ?Sized>(Arc<T>);

impl<T: ?Sized> ByAddr<T> {
    fn new(value: Arc<T>) -> Self {
        Self(value)
    }
}

impl<T: ?Sized> Clone for ByAddr<T> {
    fn clone(&self) -> Self {
        ByAddr::new(self.0.clone())
    }
}

impl<T: ?Sized> PartialEq<ByAddr<T>> for ByAddr<T> {
    fn eq(&self, other: &ByAddr<T>) -> bool {
        self.0.as_ref() as *const T == other.0.as_ref() as *const T
    }
}

impl<T: ?Sized> Eq for ByAddr<T> {}

pub trait AddDynamicChildHook {
    // Called when a dynamic instance is added with `realm`.
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type AddDynamicChildHookRef = Arc<dyn AddDynamicChildHook + Send + Sync>;
pub type AddDynamicChildHookInternalRef = ByAddr<dyn AddDynamicChildHook + Send + Sync>;

pub trait RemoveDynamicChildHook {
    // Called when a dynamic instance is removed from `realm`.
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type RemoveDynamicChildHookRef = Arc<dyn RemoveDynamicChildHook + Send + Sync>;
pub type RemoveDynamicChildHookInternalRef = ByAddr<dyn RemoveDynamicChildHook + Send + Sync>;

pub trait BindInstanceHook {
    // Called when a component instance is bound to the given `realm`.
    fn on<'a>(
        &'a self,
        realm: Arc<Realm>,
        realm_state: &'a RealmState,
        routing_facade: RoutingFacade,
    ) -> BoxFuture<Result<(), ModelError>>;
}
pub type BindInstanceHookRef = Arc<dyn BindInstanceHook + Send + Sync>;
pub type BindInstanceHookInternalRef = ByAddr<dyn BindInstanceHook + Send + Sync>;

pub trait RouteFrameworkCapabilityHook {
    // Called when the component specified by |abs_moniker| requests a capability provided
    // by the framework.
    fn on<'a>(
        &'a self,
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> BoxFuture<Result<Option<Box<dyn FrameworkCapability>>, ModelError>>;
}
pub type RouteFrameworkCapabilityHookRef = Arc<dyn RouteFrameworkCapabilityHook + Send + Sync>;
pub type RouteFrameworkCapabilityHookInternalRef =
    ByAddr<dyn RouteFrameworkCapabilityHook + Send + Sync>;

pub trait StopInstanceHook {
    // Called when a component instance is stopped.
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type StopInstanceHookRef = Arc<dyn StopInstanceHook + Send + Sync>;
pub type StopInstanceHookInternalRef = ByAddr<dyn StopInstanceHook + Send + Sync>;

pub trait DestroyInstanceHook {
    // Called when component manager destroys an instance (including cleanup).
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type DestroyInstanceHookRef = Arc<dyn DestroyInstanceHook + Send + Sync>;
pub type DestroyInstanceHookInternalRef = ByAddr<dyn DestroyInstanceHook + Send + Sync>;

#[derive(Clone)]
pub struct Hooks {
    add_dynamic_child_hooks: Arc<Mutex<Vec<AddDynamicChildHookInternalRef>>>,
    remove_dynamic_child_hooks: Arc<Mutex<Vec<RemoveDynamicChildHookInternalRef>>>,
    bind_instance_hooks: Arc<Mutex<Vec<BindInstanceHookInternalRef>>>,
    capability_routing_hooks: Arc<Mutex<Vec<RouteFrameworkCapabilityHookInternalRef>>>,
    stop_instance_hooks: Arc<Mutex<Vec<StopInstanceHookInternalRef>>>,
    destroy_instance_hooks: Arc<Mutex<Vec<DestroyInstanceHookInternalRef>>>,
}

pub enum Hook {
    AddDynamicChild(AddDynamicChildHookRef),
    RemoveDynamicChild(RemoveDynamicChildHookRef),
    BindInstance(BindInstanceHookRef),
    RouteFrameworkCapability(RouteFrameworkCapabilityHookRef),
    StopInstance(StopInstanceHookRef),
    DestroyInstance(DestroyInstanceHookRef),
}

impl Hooks {
    pub fn new() -> Self {
        Hooks {
            add_dynamic_child_hooks: Arc::new(Mutex::new(Vec::new())),
            remove_dynamic_child_hooks: Arc::new(Mutex::new(Vec::new())),
            bind_instance_hooks: Arc::new(Mutex::new(Vec::new())),
            capability_routing_hooks: Arc::new(Mutex::new(Vec::new())),
            stop_instance_hooks: Arc::new(Mutex::new(Vec::new())),
            destroy_instance_hooks: Arc::new(Mutex::new(Vec::new())),
        }
    }

    fn install_hook<T>(hooks: &mut Vec<T>, hook: T)
    where
        T: Sized + PartialEq,
    {
        if !hooks.contains(&hook) {
            hooks.push(hook);
        }
    }

    pub async fn install(&self, hooks: Vec<Hook>) {
        for hook in hooks.into_iter() {
            match hook {
                Hook::AddDynamicChild(hook) => {
                    let mut hooks = self.add_dynamic_child_hooks.lock().await;
                    Self::install_hook(&mut hooks, ByAddr::new(hook));
                }
                Hook::RemoveDynamicChild(hook) => {
                    let mut hooks = self.remove_dynamic_child_hooks.lock().await;
                    Self::install_hook(&mut hooks, ByAddr::new(hook));
                }
                Hook::BindInstance(hook) => {
                    let mut hooks = self.bind_instance_hooks.lock().await;
                    Self::install_hook(&mut hooks, ByAddr::new(hook));
                }
                Hook::RouteFrameworkCapability(hook) => {
                    let mut hooks = self.capability_routing_hooks.lock().await;
                    Self::install_hook(&mut hooks, ByAddr::new(hook));
                }
                Hook::StopInstance(hook) => {
                    let mut hooks = self.stop_instance_hooks.lock().await;
                    Self::install_hook(&mut hooks, ByAddr::new(hook));
                }
                Hook::DestroyInstance(hook) => {
                    let mut hooks = self.destroy_instance_hooks.lock().await;
                    Self::install_hook(&mut hooks, ByAddr::new(hook));
                }
            }
        }
    }

    pub async fn on_route_framework_capability<'a>(
        &'a self,
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        mut capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Result<Option<Box<dyn FrameworkCapability>>, ModelError> {
        let capability_routing_hooks = { self.capability_routing_hooks.lock().await.clone() };
        for hook in capability_routing_hooks.iter() {
            capability = hook.0.on(realm.clone(), &capability_decl, capability).await?;
        }
        Ok(capability)
    }

    pub async fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
        realm_state: &'a RealmState,
        routing_facade: RoutingFacade,
    ) -> Result<(), ModelError> {
        let hooks = { self.bind_instance_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.0.on(realm.clone(), &realm_state, routing_facade.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_add_dynamic_child(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.add_dynamic_child_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.0.on(realm.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_remove_dynamic_child(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.remove_dynamic_child_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.0.on(realm.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_stop_instance(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.stop_instance_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.0.on(realm.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_destroy_instance(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.destroy_instance_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.0.on(realm.clone()).await?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::model::testing::mocks, std::sync::Arc};

    struct CallCounter {
        call_count: Mutex<i32>,
    }

    impl CallCounter {
        pub fn new() -> Arc<Self> {
            Arc::new(Self { call_count: Mutex::new(0) })
        }

        pub async fn count(&self) -> i32 {
            *self.call_count.lock().await
        }

        async fn on_add_dynamic_child_async(&self) -> Result<(), ModelError> {
            let mut call_count = self.call_count.lock().await;
            *call_count += 1;
            Ok(())
        }
    }

    impl AddDynamicChildHook for CallCounter {
        fn on(&self, _realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
            Box::pin(self.on_add_dynamic_child_async())
        }
    }

    // This test verifies that a hook cannot be installed twice.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn install_hook_twice() {
        // CallCounter counts the number of AddDynamicChild events it receives.
        // It should only ever receive one.
        let call_counter = CallCounter::new();
        let hooks = Hooks::new();

        // Attempt to install CallCounter twice.
        hooks.install(vec![Hook::AddDynamicChild(call_counter.clone())]).await;
        hooks.install(vec![Hook::AddDynamicChild(call_counter.clone())]).await;

        let realm = {
            let resolver = ResolverRegistry::new();
            let runner = Arc::new(mocks::MockRunner::new());
            let root_component_url = "test:///root".to_string();
            Arc::new(Realm::new_root_realm(resolver, runner, root_component_url))
        };
        hooks.on_add_dynamic_child(realm.clone()).await.expect("Unable to call hooks.");
        assert_eq!(1, call_counter.count().await);
    }
}
