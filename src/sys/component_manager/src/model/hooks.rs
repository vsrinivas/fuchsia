// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{framework::FrameworkCapability, model::*},
    cm_rust::FrameworkCapabilityDecl,
    futures::{future::BoxFuture, lock::Mutex},
    std::sync::Arc,
};

pub trait AddDynamicChildHook {
    // Called when a dynamic instance is added with `realm`.
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type AddDynamicChildHookRef = Arc<dyn AddDynamicChildHook + Send + Sync>;

pub trait RemoveDynamicChildHook {
    // Called when a dynamic instance is removed from `realm`.
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type RemoveDynamicChildHookRef = Arc<dyn RemoveDynamicChildHook + Send + Sync>;

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

pub trait StopInstanceHook {
    // Called when a component instance is stopped.
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type StopInstanceHookRef = Arc<dyn StopInstanceHook + Send + Sync>;

pub trait DestroyInstanceHook {
    // Called when component manager destroys an instance (including cleanup).
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}
pub type DestroyInstanceHookRef = Arc<dyn DestroyInstanceHook + Send + Sync>;

#[derive(Clone)]
pub struct Hooks {
    add_dynamic_child_hooks: Arc<Mutex<Vec<AddDynamicChildHookRef>>>,
    remove_dynamic_child_hooks: Arc<Mutex<Vec<RemoveDynamicChildHookRef>>>,
    bind_instance_hooks: Arc<Mutex<Vec<BindInstanceHookRef>>>,
    capability_routing_hooks: Arc<Mutex<Vec<RouteFrameworkCapabilityHookRef>>>,
    stop_instance_hooks: Arc<Mutex<Vec<StopInstanceHookRef>>>,
    destroy_instance_hooks: Arc<Mutex<Vec<DestroyInstanceHookRef>>>,
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
            add_dynamic_child_hooks: Arc::new(Mutex::new(vec![])),
            remove_dynamic_child_hooks: Arc::new(Mutex::new(vec![])),
            bind_instance_hooks: Arc::new(Mutex::new(vec![])),
            capability_routing_hooks: Arc::new(Mutex::new(vec![])),
            stop_instance_hooks: Arc::new(Mutex::new(vec![])),
            destroy_instance_hooks: Arc::new(Mutex::new(vec![])),
        }
    }

    pub async fn install(&self, hooks: Vec<Hook>) {
        for hook in hooks.into_iter() {
            match hook {
                Hook::AddDynamicChild(hook) => {
                    let mut add_dynamic_child_hooks = self.add_dynamic_child_hooks.lock().await;
                    add_dynamic_child_hooks.push(hook);
                }
                Hook::RemoveDynamicChild(hook) => {
                    let mut remove_dynamic_child_hooks =
                        self.remove_dynamic_child_hooks.lock().await;
                    remove_dynamic_child_hooks.push(hook);
                }
                Hook::BindInstance(hook) => {
                    let mut bind_instance_hooks = self.bind_instance_hooks.lock().await;
                    bind_instance_hooks.push(hook);
                }
                Hook::RouteFrameworkCapability(hook) => {
                    let mut capability_routing_hooks = self.capability_routing_hooks.lock().await;
                    capability_routing_hooks.push(hook);
                }
                Hook::StopInstance(hook) => {
                    let mut stop_instance_hooks = self.stop_instance_hooks.lock().await;
                    stop_instance_hooks.push(hook);
                }
                Hook::DestroyInstance(hook) => {
                    let mut destroy_instance_hooks = self.destroy_instance_hooks.lock().await;
                    destroy_instance_hooks.push(hook);
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
            capability = hook.on(realm.clone(), &capability_decl, capability).await?;
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
            hook.on(realm.clone(), &realm_state, routing_facade.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_add_dynamic_child(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.add_dynamic_child_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.on(realm.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_remove_dynamic_child(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.remove_dynamic_child_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.on(realm.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_stop_instance(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.stop_instance_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.on(realm.clone()).await?;
        }
        Ok(())
    }

    pub async fn on_destroy_instance(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let hooks = { self.destroy_instance_hooks.lock().await.clone() };
        for hook in hooks.iter() {
            hook.on(realm.clone()).await?;
        }
        Ok(())
    }
}
