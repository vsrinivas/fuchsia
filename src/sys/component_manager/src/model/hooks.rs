// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{framework::FrameworkCapability, model::*},
    cm_rust::FrameworkCapabilityDecl,
    futures::{future::BoxFuture, lock::Mutex, prelude::*},
    std::{pin::Pin, sync::Arc},
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

pub enum Hook {
    AddDynamicChild(AddDynamicChildHookRef),
    RemoveDynamicChild(RemoveDynamicChildHookRef),
    BindInstance(BindInstanceHookRef),
    RouteFrameworkCapability(RouteFrameworkCapabilityHookRef),
    StopInstance(StopInstanceHookRef),
    DestroyInstance(DestroyInstanceHookRef),
}

/// This is a collection of hooks to component manager events.
/// Note: Cloning Hooks results in a shallow copy.
#[derive(Clone)]
pub struct Hooks {
    parent: Option<Box<Hooks>>,
    inner: Arc<Mutex<HooksInner>>,
}

fn install_hook<T>(hooks: &mut Vec<T>, hook: T)
where
    T: Sized + PartialEq,
{
    if !hooks.contains(&hook) {
        hooks.push(hook);
    }
}

impl Hooks {
    pub fn new(parent: Option<&Hooks>) -> Self {
        Self {
            parent: parent.map(|hooks| Box::new(hooks.clone())),
            inner: Arc::new(Mutex::new(HooksInner::new())),
        }
    }

    pub async fn install(&self, hooks: Vec<Hook>) {
        for hook in hooks.into_iter() {
            let mut inner = self.inner.lock().await;
            match hook {
                Hook::AddDynamicChild(hook) => {
                    install_hook(&mut inner.add_dynamic_child_hooks, ByAddr::new(hook));
                }
                Hook::RemoveDynamicChild(hook) => {
                    install_hook(&mut inner.remove_dynamic_child_hooks, ByAddr::new(hook));
                }
                Hook::BindInstance(hook) => {
                    install_hook(&mut inner.bind_instance_hooks, ByAddr::new(hook));
                }
                Hook::RouteFrameworkCapability(hook) => {
                    install_hook(&mut inner.capability_routing_hooks, ByAddr::new(hook));
                }
                Hook::StopInstance(hook) => {
                    install_hook(&mut inner.stop_instance_hooks, ByAddr::new(hook));
                }
                Hook::DestroyInstance(hook) => {
                    install_hook(&mut inner.destroy_instance_hooks, ByAddr::new(hook));
                }
            }
        }
    }

    pub fn on_route_framework_capability<'a>(
        &'a self,
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        mut capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Pin<
        Box<
            dyn Future<Output = Result<Option<Box<dyn FrameworkCapability>>, ModelError>>
                + Send
                + 'a,
        >,
    > {
        Box::pin(async move {
            let hooks = { self.inner.lock().await.capability_routing_hooks.clone() };
            for hook in hooks.iter() {
                capability = hook.0.on(realm.clone(), &capability_decl, capability).await?;
            }
            if let Some(parent) = &self.parent {
                capability = parent
                    .on_route_framework_capability(realm, capability_decl, capability)
                    .await?;
            }
            Ok(capability)
        })
    }

    pub fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
        realm_state: &'a RealmState,
        routing_facade: RoutingFacade,
    ) -> Pin<Box<dyn Future<Output = Result<(), ModelError>> + Send + 'a>> {
        Box::pin(async move {
            let hooks = { self.inner.lock().await.bind_instance_hooks.clone() };
            for hook in hooks.iter() {
                hook.0.on(realm.clone(), &realm_state, routing_facade.clone()).await?;
            }
            if let Some(parent) = &self.parent {
                parent.on_bind_instance(realm, realm_state, routing_facade).await?;
            }
            Ok(())
        })
    }

    pub fn on_add_dynamic_child<'a>(
        &'a self,
        realm: Arc<Realm>,
    ) -> Pin<Box<dyn Future<Output = Result<(), ModelError>> + Send + 'a>> {
        Box::pin(async move {
            let hooks = { self.inner.lock().await.add_dynamic_child_hooks.clone() };
            for hook in hooks.iter() {
                hook.0.on(realm.clone()).await?;
            }
            if let Some(parent) = &self.parent {
                parent.on_add_dynamic_child(realm).await?;
            }
            Ok(())
        })
    }

    pub fn on_remove_dynamic_child<'a>(
        &'a self,
        realm: Arc<Realm>,
    ) -> Pin<Box<dyn Future<Output = Result<(), ModelError>> + Send + 'a>> {
        Box::pin(async move {
            let hooks = { self.inner.lock().await.remove_dynamic_child_hooks.clone() };
            for hook in hooks.iter() {
                hook.0.on(realm.clone()).await?;
            }
            if let Some(parent) = &self.parent {
                parent.on_remove_dynamic_child(realm).await?;
            }
            Ok(())
        })
    }

    pub fn on_stop_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
    ) -> Pin<Box<dyn Future<Output = Result<(), ModelError>> + Send + 'a>> {
        Box::pin(async move {
            let hooks = { self.inner.lock().await.stop_instance_hooks.clone() };
            for hook in hooks.iter() {
                hook.0.on(realm.clone()).await?;
            }
            if let Some(parent) = &self.parent {
                parent.on_stop_instance(realm).await?;
            }
            Ok(())
        })
    }

    pub fn on_destroy_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
    ) -> Pin<Box<dyn Future<Output = Result<(), ModelError>> + Send + 'a>> {
        Box::pin(async move {
            let hooks = { self.inner.lock().await.destroy_instance_hooks.clone() };
            for hook in hooks.iter() {
                hook.0.on(realm.clone()).await?;
            }
            if let Some(parent) = &self.parent {
                parent.on_destroy_instance(realm).await?;
            }
            Ok(())
        })
    }
}

pub struct HooksInner {
    add_dynamic_child_hooks: Vec<AddDynamicChildHookInternalRef>,
    remove_dynamic_child_hooks: Vec<RemoveDynamicChildHookInternalRef>,
    bind_instance_hooks: Vec<BindInstanceHookInternalRef>,
    capability_routing_hooks: Vec<RouteFrameworkCapabilityHookInternalRef>,
    stop_instance_hooks: Vec<StopInstanceHookInternalRef>,
    destroy_instance_hooks: Vec<DestroyInstanceHookInternalRef>,
}

impl HooksInner {
    pub fn new() -> Self {
        Self {
            add_dynamic_child_hooks: vec![],
            remove_dynamic_child_hooks: vec![],
            bind_instance_hooks: vec![],
            capability_routing_hooks: vec![],
            stop_instance_hooks: vec![],
            destroy_instance_hooks: vec![],
        }
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
        let hooks = Hooks::new(None);

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

    // This test verifies that events propagate from child_hooks to parent_hooks.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_propagation() {
        let parent_hooks = Hooks::new(None);
        let child_hooks = Hooks::new(Some(&parent_hooks));

        let call_counter = CallCounter::new();
        parent_hooks.install(vec![Hook::AddDynamicChild(call_counter.clone())]).await;

        let realm = {
            let resolver = ResolverRegistry::new();
            let runner = Arc::new(mocks::MockRunner::new());
            let root_component_url = "test:///root".to_string();
            Arc::new(Realm::new_root_realm(resolver, runner, root_component_url))
        };
        child_hooks.on_add_dynamic_child(realm.clone()).await.expect("Unable to call hooks.");
        // call_counter gets informed of the event on child_hooks even though it has
        // been installed on parent_hooks.
        assert_eq!(1, call_counter.count().await);
    }
}
