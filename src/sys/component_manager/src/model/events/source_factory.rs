// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        model::{
            error::ModelError,
            events::{
                event::SyncMode,
                registry::{EventRegistry, ExecutionMode, SubscriptionOptions, SubscriptionType},
                source::EventSource,
            },
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
            moniker::AbsoluteMoniker,
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityNameOrPath, CapabilityPath, ComponentDecl},
    futures::lock::Mutex,
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref EVENT_SOURCE_SERVICE_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.EventSource".try_into().unwrap();
    pub static ref EVENT_SOURCE_SYNC_SERVICE_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap();
}

/// Allows to create `EventSource`s and tracks all the created ones.
pub struct EventSourceFactory {
    /// The component model, needed to route events.
    model: Weak<Model>,

    /// The event registry. It subscribes to all events happening in the system and
    /// routes them to subscribers.
    // TODO(fxb/48512): instead of using a global registry integrate more with the hooks model.
    event_registry: Weak<EventRegistry>,

    /// Tracks the event source used by each component identified with the given `moniker`.
    event_source_registry: Mutex<HashMap<AbsoluteMoniker, EventSource>>,

    execution_mode: ExecutionMode,
}

impl EventSourceFactory {
    pub fn new(
        model: Weak<Model>,
        event_registry: Weak<EventRegistry>,
        execution_mode: ExecutionMode,
    ) -> Self {
        Self {
            model,
            event_registry,
            event_source_registry: Mutex::new(HashMap::new()),
            execution_mode,
        }
    }

    /// Creates the subscription to the required events.
    /// `CapabilityReady` used to track events and associate them with the component that needs them
    /// as well as the scoped that will be allowed. Also the EventSource protocol capability.
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![
            // This hook provides the EventSource capability to components in the tree
            HooksRegistration::new(
                "EventSourceFactory",
                vec![EventType::CapabilityRouted, EventType::Destroyed, EventType::Resolved],
                Arc::downgrade(self) as Weak<dyn Hook>,
            ),
        ]
    }

    /// Creates a debug event source.
    pub async fn create_for_debug(&self, sync_mode: SyncMode) -> Result<EventSource, ModelError> {
        EventSource::new_for_debug(self.model.clone(), self.event_registry.clone(), sync_mode).await
    }

    /// Creates a `EventSource` for the given `target_moniker`.
    pub async fn create(
        &self,
        target_moniker: AbsoluteMoniker,
        sync_mode: SyncMode,
    ) -> Result<EventSource, ModelError> {
        EventSource::new(
            self.model.clone(),
            SubscriptionOptions::new(
                SubscriptionType::Component(target_moniker),
                sync_mode,
                self.execution_mode.clone(),
            ),
            self.event_registry.clone(),
        )
        .await
    }

    /// Returns an EventSource. An EventSource holds an AbsoluteMoniker that
    /// corresponds to the realm in which it will receive events.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        capability_decl: &InternalCapability,
        target_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, InternalCapability::Protocol(CapabilityNameOrPath::Path(source_path)))
                if *source_path == *EVENT_SOURCE_SERVICE_PATH
                    || *source_path == *EVENT_SOURCE_SYNC_SERVICE_PATH =>
            {
                let event_source_registry = self.event_source_registry.lock().await;
                if let Some(event_source) = event_source_registry.get(&target_moniker) {
                    Ok(Some(Box::new(event_source.clone()) as Box<dyn CapabilityProvider>))
                } else {
                    // Evidently the component was destroyed.
                    Err(ModelError::instance_not_found(target_moniker.clone()))
                }
            }
            (c, _) => Ok(c),
        }
    }

    async fn on_destroyed_async(self: &Arc<Self>, target_moniker: &AbsoluteMoniker) {
        let mut event_source_registry = self.event_source_registry.lock().await;
        event_source_registry.remove(&target_moniker);
    }

    async fn on_resolved_async(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) -> Result<(), ModelError> {
        // TODO(miguelfrde): we have a problem here. The protocol is now routed and not always used
        // from framework. This now needs to be done on CapabilityRouted as the protocol name that
        // we have in the component decl might not match the source name after all the routing and
        // potential renames.
        let sync_mode = if decl.uses_protocol(&EVENT_SOURCE_SERVICE_PATH) {
            SyncMode::Async
        } else if decl.uses_protocol(&EVENT_SOURCE_SYNC_SERVICE_PATH) {
            SyncMode::Sync
        } else {
            return Ok(());
        };
        let key = target_moniker.clone();
        let mut event_source_registry = self.event_source_registry.lock().await;
        // It is currently assumed that a component instance's declaration
        // is resolved only once. Someday, this may no longer be true if individual
        // components can be updated.
        assert!(!event_source_registry.contains_key(&key));
        // An EventSource is created on resolution in order to ensure that discovery
        // and resolution of children is not missed.
        let event_source = self.create(key.clone(), sync_mode).await?;
        event_source_registry.insert(key, event_source);
        Ok(())
    }

    #[cfg(test)]
    async fn has_event_source(&self, abs_moniker: &AbsoluteMoniker) -> bool {
        let event_source_registry = self.event_source_registry.lock().await;
        event_source_registry.contains_key(abs_moniker)
    }
}

#[async_trait]
impl Hook for EventSourceFactory {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::CapabilityRouted {
                source: CapabilitySource::AboveRoot { capability },
                capability_provider,
            }) => {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_capability_routed_async(
                        &capability,
                        event.target_moniker.clone(),
                        capability_provider.take(),
                    )
                    .await?;
            }
            Ok(EventPayload::Destroyed) => {
                self.on_destroyed_async(&event.target_moniker).await;
            }
            Ok(EventPayload::Resolved { decl }) => {
                self.on_resolved_async(&event.target_moniker, decl).await?;
            }
            _ => {}
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            environment::{Environment, RunnerRegistry},
            hooks::Hooks,
            model::ModelParams,
            resolver::ResolverRegistry,
            testing::{mocks::MockResolver, test_helpers::ComponentDeclBuilder},
        },
        cm_rust::{CapabilityNameOrPath, UseDecl, UseEventDecl, UseProtocolDecl, UseSource},
        matches::assert_matches,
    };

    async fn dispatch_resolved_event(
        hooks: &Hooks,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let decl = ComponentDeclBuilder::new()
            .use_(UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Framework,
                source_path: CapabilityNameOrPath::Path(EVENT_SOURCE_SYNC_SERVICE_PATH.clone()),
                target_path: EVENT_SOURCE_SYNC_SERVICE_PATH.clone(),
            }))
            .use_(UseDecl::Event(UseEventDecl {
                source: UseSource::Framework,
                source_name: "resolved".into(),
                target_name: "resolved".into(),
                filter: None,
            }))
            .build();
        let event = Event::new_for_test(
            target_moniker.clone(),
            "fuchsia-pkg://test",
            Ok(EventPayload::Resolved { decl: decl.clone() }),
        );
        hooks.dispatch(&event).await
    }

    async fn dispatch_destroyed_event(
        hooks: &Hooks,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let event = Event::new_for_test(
            target_moniker.clone(),
            "fuchsia-pkg://test",
            Ok(EventPayload::Destroyed),
        );
        hooks.dispatch(&event).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn drop_event_source_when_component_destroyed() {
        let model = {
            let mut resolver = MockResolver::new();
            resolver.add_component(
                "root",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                    }))
                    .build(),
            );
            let resolver_registry = {
                let mut registry = ResolverRegistry::new();
                registry.register("test".to_string(), Box::new(resolver));
                registry
            };
            Arc::new(Model::new(ModelParams {
                root_component_url: "test:///root".to_string(),
                root_environment: Environment::new_root(
                    RunnerRegistry::default(),
                    resolver_registry,
                ),
            }))
        };
        let event_registry = Arc::new(EventRegistry::new(Arc::downgrade(&model)));
        let event_source_factory = Arc::new(EventSourceFactory::new(
            Arc::downgrade(&model),
            Arc::downgrade(&event_registry),
            ExecutionMode::Production,
        ));

        let hooks = Hooks::new(None);
        hooks.install(event_source_factory.hooks()).await;

        let root = AbsoluteMoniker::root();

        // Verify that there is no EventSource for the root until we dispatch the Resolved event.
        assert!(!event_source_factory.has_event_source(&root).await);
        dispatch_resolved_event(&hooks, &root).await.unwrap();
        assert!(event_source_factory.has_event_source(&root).await);
        // Verify that destroying the component destroys the EventSource.
        dispatch_destroyed_event(&hooks, &root).await.unwrap();
        assert!(!event_source_factory.has_event_source(&root).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn passes_on_capability_routed_from_framework_not_on_root() {
        let model = {
            let resolver = ResolverRegistry::new();
            Arc::new(Model::new(ModelParams {
                root_component_url: "test:///root".to_string(),
                root_environment: Environment::new_root(RunnerRegistry::default(), resolver),
            }))
        };
        let event_registry = Arc::new(EventRegistry::new(Arc::downgrade(&model)));
        let event_source_factory = Arc::new(EventSourceFactory::new(
            Arc::downgrade(&model),
            Arc::downgrade(&event_registry),
            ExecutionMode::Production,
        ));

        let target: AbsoluteMoniker = vec!["a:0"].into();
        let scope: AbsoluteMoniker = vec!["b:0"].into();
        let capability_provider = Arc::new(Mutex::new(None));
        let result = event_source_factory
            .on(&Event::new_for_test(
                target.clone(),
                "fuchsia-pkg://test",
                Ok(EventPayload::CapabilityRouted {
                    capability_provider: capability_provider.clone(),
                    source: CapabilitySource::Framework {
                        capability: InternalCapability::Protocol(CapabilityNameOrPath::Path(
                            CapabilityPath {
                                dirname: "/svc".to_string(),
                                basename: "fuchsia.sys2.EventSource".to_string(),
                            },
                        )),
                        scope_moniker: scope.clone(),
                    },
                }),
            ))
            .await;

        assert!(capability_provider.lock().await.is_none());
        assert_matches!(result, Ok(()));
    }
}
