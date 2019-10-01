// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{framework::FrameworkCapability, model::*},
    async_trait::*,
    by_addr::ByAddr,
    cm_rust::FrameworkCapabilityDecl,
    futures::{future::BoxFuture, lock::Mutex},
    std::{collections::HashMap, sync::Arc},
};

#[async_trait]
pub trait Hook {
    async fn on(&self, event: &Event<'_>) -> Result<(), ModelError>;
}

#[derive(Debug, Eq, PartialEq, Hash)]
pub enum EventType {
    AddDynamicChild,
    RemoveDynamicChild,
    BindInstance,
    RouteFrameworkCapability,
    StopInstance,
    DestroyInstance,
}

pub struct HookRegistration {
    pub event_type: EventType,
    pub callback: Arc<dyn Hook + Send + Sync>,
}

pub enum Event<'a> {
    AddDynamicChild {
        realm: Arc<Realm>,
    },
    RemoveDynamicChild {
        realm: Arc<Realm>,
    },
    BindInstance {
        realm: Arc<Realm>,
        realm_state: &'a RealmState,
        routing_facade: RoutingFacade,
    },
    RouteFrameworkCapability {
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        // Events are passed to hooks as immutable borrows. In order to mutate,
        // a field within an Event, interior mutability is employed here with
        // a Mutex.
        capability: Mutex<Option<Box<dyn FrameworkCapability>>>,
    },
    StopInstance {
        realm: Arc<Realm>,
    },
    DestroyInstance {
        realm: Arc<Realm>,
    },
}

impl Event<'_> {
    pub fn type_(&self) -> EventType {
        match self {
            Event::AddDynamicChild { .. } => EventType::AddDynamicChild,
            Event::RemoveDynamicChild { .. } => EventType::RemoveDynamicChild,
            Event::BindInstance { .. } => EventType::BindInstance,
            Event::RouteFrameworkCapability { .. } => EventType::RouteFrameworkCapability,
            Event::StopInstance { .. } => EventType::StopInstance,
            Event::DestroyInstance { .. } => EventType::DestroyInstance,
        }
    }
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

    pub async fn install(&self, hooks: Vec<HookRegistration>) {
        let mut inner = self.inner.lock().await;
        for hook in hooks {
            install_hook(
                &mut inner.hooks.entry(hook.event_type).or_insert(vec![]),
                ByAddr::new(hook.callback),
            );
        }
    }

    pub fn dispatch<'a>(&'a self, event: &'a Event<'a>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            let hooks = {
                let inner = self.inner.lock().await;
                inner.hooks.get(&event.type_()).cloned()
            };
            if let Some(hooks) = hooks {
                for hook in hooks.iter() {
                    hook.0.on(event).await?;
                }
            }
            if let Some(parent) = &self.parent {
                parent.dispatch(event).await?;
            }
            Ok(())
        })
    }
}

pub struct HooksInner {
    hooks: HashMap<EventType, Vec<ByAddr<dyn Hook + Send + Sync>>>,
}

impl HooksInner {
    pub fn new() -> Self {
        Self { hooks: HashMap::new() }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::model::testing::mocks, std::sync::Arc};

    #[derive(Clone)]
    struct EventLog {
        log: Arc<Mutex<Vec<String>>>,
    }

    impl EventLog {
        pub fn new() -> Self {
            Self { log: Arc::new(Mutex::new(Vec::new())) }
        }

        pub async fn append(&self, item: String) {
            let mut log = self.log.lock().await;
            log.push(item);
        }

        pub async fn get(&self) -> Vec<String> {
            self.log.lock().await.clone()
        }
    }

    struct CallCounter {
        name: String,
        logger: Option<EventLog>,
        call_count: Mutex<i32>,
    }

    impl CallCounter {
        pub fn new(name: &str, logger: Option<EventLog>) -> Arc<Self> {
            Arc::new(Self { name: name.to_string(), logger, call_count: Mutex::new(0) })
        }

        pub async fn count(&self) -> i32 {
            *self.call_count.lock().await
        }

        async fn on_add_dynamic_child_async(&self) -> Result<(), ModelError> {
            let mut call_count = self.call_count.lock().await;
            *call_count += 1;
            if let Some(logger) = &self.logger {
                logger.append(format!("{}::OnAddDynamicChild", self.name)).await;
            }
            Ok(())
        }
    }

    #[async_trait]
    impl Hook for CallCounter {
        async fn on(&self, event: &Event<'_>) -> Result<(), ModelError> {
            if let Event::AddDynamicChild { .. } = event {
                self.on_add_dynamic_child_async().await?;
            }
            Ok(())
        }
    }

    fn log(v: Vec<&str>) -> Vec<String> {
        v.iter().map(|s| s.to_string()).collect()
    }

    // This test verifies that a hook cannot be installed twice.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn install_hook_twice() {
        // CallCounter counts the number of AddDynamicChild events it receives.
        // It should only ever receive one.
        let call_counter = CallCounter::new("CallCounter", None);
        let hooks = Hooks::new(None);

        // Attempt to install CallCounter twice.
        hooks
            .install(vec![HookRegistration {
                event_type: EventType::AddDynamicChild,
                callback: call_counter.clone(),
            }])
            .await;
        hooks
            .install(vec![HookRegistration {
                event_type: EventType::AddDynamicChild,
                callback: call_counter.clone(),
            }])
            .await;

        let realm = {
            let resolver = ResolverRegistry::new();
            let runner = Arc::new(mocks::MockRunner::new());
            let root_component_url = "test:///root".to_string();
            Arc::new(Realm::new_root_realm(resolver, runner, root_component_url))
        };
        let event = Event::AddDynamicChild { realm: realm.clone() };
        hooks.dispatch(&event).await.expect("Unable to call hooks.");
        assert_eq!(1, call_counter.count().await);
    }

    // This test verifies that events propagate from child_hooks to parent_hooks.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_propagation() {
        let parent_hooks = Hooks::new(None);
        let child_hooks = Hooks::new(Some(&parent_hooks));

        let event_log = EventLog::new();
        let parent_call_counter = CallCounter::new("ParentCallCounter", Some(event_log.clone()));
        parent_hooks
            .install(vec![HookRegistration {
                event_type: EventType::AddDynamicChild,
                callback: parent_call_counter.clone(),
            }])
            .await;

        let child_call_counter = CallCounter::new("ChildCallCounter", Some(event_log.clone()));
        child_hooks
            .install(vec![HookRegistration {
                event_type: EventType::AddDynamicChild,
                callback: child_call_counter.clone(),
            }])
            .await;

        let realm = {
            let resolver = ResolverRegistry::new();
            let runner = Arc::new(mocks::MockRunner::new());
            let root_component_url = "test:///root".to_string();
            Arc::new(Realm::new_root_realm(resolver, runner, root_component_url))
        };
        let event = Event::AddDynamicChild { realm: realm.clone() };
        child_hooks.dispatch(&event).await.expect("Unable to call hooks.");
        // parent_call_counter gets informed of the event on child_hooks even though it has
        // been installed on parent_hooks.
        assert_eq!(1, parent_call_counter.count().await);
        // child_call_counter should be called only once.
        assert_eq!(1, child_call_counter.count().await);

        // ChildCallCounter should be called before ParentCallCounter.
        assert_eq!(
            log(vec![
                "ChildCallCounter::OnAddDynamicChild",
                "ParentCallCounter::OnAddDynamicChild"
            ]),
            event_log.get().await
        );
    }
}
