// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::{
            component::{StartReason, WeakComponentInstance},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
            routing::report_routing_failure,
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, CapabilityPath, ProtocolDecl},
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, ExtendedMoniker},
    routing::capability_source::{ComponentCapability, InternalCapability},
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
    tracing::warn,
};

lazy_static! {
    pub static ref BINDER_SERVICE: CapabilityName = "fuchsia.component.Binder".into();
    pub static ref BINDER_CAPABILITY: ComponentCapability =
        ComponentCapability::Protocol(ProtocolDecl {
            name: BINDER_SERVICE.clone(),
            source_path: Some(CapabilityPath {
                basename: "fuchsia.component.Binder".into(),
                dirname: "svc".into()
            }),
        });
}

/// Implementation of `fuchsia.component.Binder` FIDL protocol.
pub struct BinderCapabilityProvider {
    source: WeakComponentInstance,
    target: WeakComponentInstance,
}

impl BinderCapabilityProvider {
    pub fn new(source: WeakComponentInstance, target: WeakComponentInstance) -> Self {
        Self { source, target }
    }
}

#[async_trait]
impl CapabilityProvider for BinderCapabilityProvider {
    async fn open(
        self: Box<Self>,
        task_scope: TaskScope,
        _flags: fio::OpenFlags,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let target = self.target.clone();
        let source = self.source.clone();
        let server_end = channel::take_channel(server_end);

        task_scope
            .add_task(async move {
                let source = match source.upgrade().map_err(|e| ModelError::from(e)) {
                    Ok(source) => source,
                    Err(err) => {
                        report_routing_failure_to_target(target, err, server_end).await;
                        return;
                    }
                };

                let start_reason = StartReason::AccessCapability {
                    target: target.abs_moniker.clone(),
                    name: BINDER_SERVICE.clone(),
                };
                match source.start(&start_reason).await {
                    Ok(_) => {
                        source.scope_to_runtime(server_end).await;
                    }
                    Err(err) => {
                        report_routing_failure_to_target(target, err, server_end).await;
                    }
                }
            })
            .await;
        Ok(())
    }
}

// A `Hook` that serves the `fuchsia.component.Binder` FIDL protocol.
#[derive(Clone)]
pub struct BinderCapabilityHost {
    model: Weak<Model>,
}

impl BinderCapabilityHost {
    pub fn new(model: Weak<Model>) -> Self {
        Self { model }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "BinderCapabilityHost",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_scoped_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        source: WeakComponentInstance,
        target_moniker: AbsoluteMoniker,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        // If some other capability has already been installed, then there's nothing to
        // do here.
        if capability_provider.is_none() && capability.matches_protocol(&BINDER_SERVICE) {
            let model = self.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
            let target = WeakComponentInstance::new(&model.look_up(&target_moniker).await?);
            Ok(Some(Box::new(BinderCapabilityProvider::new(source, target))
                as Box<dyn CapabilityProvider>))
        } else {
            Ok(capability_provider)
        }
    }
}

#[async_trait]
impl Hook for BinderCapabilityHost {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::Framework { capability, component },
            capability_provider,
        }) = &event.result
        {
            let target_moniker = match &event.target_moniker {
                ExtendedMoniker::ComponentManager => {
                    Err(ModelError::UnexpectedComponentManagerMoniker)
                }
                ExtendedMoniker::ComponentInstance(moniker) => Ok(moniker),
            }?;
            let mut capability_provider = capability_provider.lock().await;
            *capability_provider = self
                .on_scoped_framework_capability_routed_async(
                    component.clone(),
                    target_moniker.clone(),
                    &capability,
                    capability_provider.take(),
                )
                .await?;
        }
        Ok(())
    }
}

async fn report_routing_failure_to_target(
    target: WeakComponentInstance,
    err: ModelError,
    server_end: zx::Channel,
) {
    match target.upgrade().map_err(|e| ModelError::from(e)) {
        Ok(target) => {
            report_routing_failure(&target, &*BINDER_CAPABILITY, &err, server_end).await;
        }
        Err(err) => {
            warn!(moniker=%target.abs_moniker, error=%err, "failed to upgrade reference");
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            builtin_environment::BuiltinEnvironment,
            capability::CapabilityProvider,
            model::{
                events::{source::EventSource, stream::EventStream},
                testing::test_helpers::*,
            },
        },
        assert_matches::assert_matches,
        cm_rust::{self, CapabilityName, ComponentDecl, EventMode},
        cm_rust_testing::*,
        cm_task_scope::TaskScope,
        fidl::{client::Client, handle::AsyncChannel},
        fuchsia_zircon as zx,
        futures::{lock::Mutex, StreamExt},
        moniker::AbsoluteMoniker,
        std::path::PathBuf,
    };

    struct BinderCapabilityTestFixture {
        builtin_environment: Arc<Mutex<BuiltinEnvironment>>,
    }

    impl BinderCapabilityTestFixture {
        async fn new(components: Vec<(&'static str, ComponentDecl)>) -> Self {
            let TestModelResult { builtin_environment, .. } =
                TestEnvironmentBuilder::new().set_components(components).build().await;

            BinderCapabilityTestFixture { builtin_environment }
        }

        async fn new_event_stream(
            &self,
            events: Vec<CapabilityName>,
            mode: EventMode,
        ) -> (EventSource, EventStream) {
            new_event_stream(self.builtin_environment.clone(), events, mode).await
        }

        async fn provider(
            &self,
            source: AbsoluteMoniker,
            target: AbsoluteMoniker,
        ) -> Box<BinderCapabilityProvider> {
            let builtin_environment = self.builtin_environment.lock().await;
            let source = builtin_environment
                .model
                .look_up(&source)
                .await
                .expect("failed to look up source moniker");
            let target = builtin_environment
                .model
                .look_up(&target)
                .await
                .expect("failed to look up target moniker");

            Box::new(BinderCapabilityProvider::new(
                WeakComponentInstance::new(&source),
                WeakComponentInstance::new(&target),
            ))
        }
    }

    #[fuchsia::test]
    async fn component_starts_on_open() {
        let fixture = BinderCapabilityTestFixture::new(vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("source")
                    .add_lazy_child("target")
                    .build(),
            ),
            ("source", component_decl_with_test_runner()),
            ("target", component_decl_with_test_runner()),
        ])
        .await;
        let (_event_source, mut event_stream) = fixture
            .new_event_stream(
                vec![EventType::Resolved.into(), EventType::Started.into()],
                EventMode::Async,
            )
            .await;
        let (_client_end, mut server_end) =
            zx::Channel::create().expect("failed to create channels");
        let moniker: AbsoluteMoniker = vec!["source"].into();

        let task_scope = TaskScope::new();
        fixture
            .provider(moniker.clone(), vec!["target"].into())
            .await
            .open(task_scope.clone(), fio::OpenFlags::empty(), 0, PathBuf::new(), &mut server_end)
            .await
            .expect("failed to call open()");
        task_scope.shutdown().await;

        assert!(event_stream.wait_until(EventType::Resolved, moniker.clone()).await.is_some());
        assert!(event_stream.wait_until(EventType::Started, moniker.clone()).await.is_some());
    }

    // TODO(yaneury): Figure out a way to test this behavior.
    #[ignore]
    #[fuchsia::test]
    async fn channel_is_closed_if_component_does_not_exist() {
        let fixture = BinderCapabilityTestFixture::new(vec![(
            "root",
            ComponentDeclBuilder::new()
                .add_lazy_child("target")
                .add_lazy_child("unresolvable")
                .build(),
        )])
        .await;
        let (client_end, mut server_end) =
            zx::Channel::create().expect("failed to create channels");
        let moniker: AbsoluteMoniker = vec!["foo"].into();

        let task_scope = TaskScope::new();
        fixture
            .provider(moniker, vec![].into())
            .await
            .open(task_scope.clone(), fio::OpenFlags::empty(), 0, PathBuf::new(), &mut server_end)
            .await
            .expect("failed to call open()");
        task_scope.shutdown().await;

        let client_end =
            AsyncChannel::from_channel(client_end).expect("failed to create AsyncChanel");
        let client = Client::new(client_end, "binder_service");
        let mut event_receiver = client.take_event_receiver();
        assert_matches!(
            event_receiver.next().await,
            Some(Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_FOUND,
                protocol_name: "binder_service"
            }))
        );
        assert_matches!(event_receiver.next().await, None);
    }
}
