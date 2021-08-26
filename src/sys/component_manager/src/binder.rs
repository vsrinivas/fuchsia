// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{
            CapabilityProvider, CapabilitySource, ComponentCapability, InternalCapability,
            OptionalTask,
        },
        channel,
        model::{
            component::{BindReason, WeakComponentInstance},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
            routing::report_routing_failure,
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, CapabilityPath, ProtocolDecl},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ExtendedMoniker},
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref BINDER_SERVICE: CapabilityName = "fuchsia.component.Binder".into();
    pub static ref BINDER_CAPABILITY: ComponentCapability =
        ComponentCapability::Protocol(ProtocolDecl {
            name: BINDER_SERVICE.clone(),
            source_path: CapabilityPath {
                basename: "fuchsia.component.Binder".into(),
                dirname: "svc".into()
            },
        });
}

/// Implementation of `fuchsia.component.Binder` FIDL protocol.
pub struct BinderCapabilityProvider {
    source: WeakComponentInstance,
    target: WeakComponentInstance,
    host: Arc<BinderCapabilityHost>,
}

impl BinderCapabilityProvider {
    pub fn new(
        source: WeakComponentInstance,
        target: WeakComponentInstance,
        host: Arc<BinderCapabilityHost>,
    ) -> Self {
        Self { source, target, host }
    }
}

#[async_trait]
impl CapabilityProvider for BinderCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<OptionalTask, ModelError> {
        let host = self.host.clone();
        let target = self.target.clone();
        let source = self.source.clone();
        let server_end = channel::take_channel(server_end);
        Ok(fasync::Task::spawn(async move {
            if let Err(err) = host.bind(source).await {
                let res = target.upgrade().map_err(|e| ModelError::from(e));
                match res {
                    Ok(target) => {
                        report_routing_failure(&target, &*BINDER_CAPABILITY, &err, server_end)
                            .await;
                    }
                    Err(err) => {
                        log::warn!("failed to upgrade reference to {}: {}", target.moniker, err);
                    }
                }
            }
        })
        .into())
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

    pub async fn bind(&self, source: WeakComponentInstance) -> Result<(), ModelError> {
        let source = source.upgrade().map_err(|e| ModelError::from(e))?;
        source.bind(&BindReason::Binder).await?;
        Ok(())
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
            let target =
                WeakComponentInstance::new(&model.look_up(&target_moniker.to_partial()).await?);
            Ok(Some(Box::new(BinderCapabilityProvider::new(source, target, self.clone()))
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
        cm_rust::{self, CapabilityName, ComponentDecl, EventMode},
        cm_rust_testing::*,
        fidl::{client::Client, handle::AsyncChannel},
        fuchsia_zircon as zx,
        futures::{lock::Mutex, StreamExt},
        matches::assert_matches,
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
            let host = builtin_environment.binder_capability_host.clone();
            let source = builtin_environment
                .model
                .look_up(&source.to_partial())
                .await
                .expect("failed to look up source moniker");
            let target = builtin_environment
                .model
                .look_up(&target.to_partial())
                .await
                .expect("failed to look up target moniker");

            Box::new(BinderCapabilityProvider::new(
                WeakComponentInstance::new(&source),
                WeakComponentInstance::new(&target),
                host,
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
        let moniker: AbsoluteMoniker = vec!["source:0"].into();

        let () = fixture
            .provider(moniker.clone(), vec!["target:0"].into())
            .await
            .open(0, 0, PathBuf::new(), &mut server_end)
            .await
            .expect("failed to call open()")
            .take()
            .expect("task is empty")
            .await;

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
        let moniker: AbsoluteMoniker = AbsoluteMoniker::from(vec!["foo:0"]);

        let () = fixture
            .provider(moniker, vec![].into())
            .await
            .open(0, 0, PathBuf::new(), &mut server_end)
            .await
            .expect("failed to call open()")
            .take()
            .expect("task is empty")
            .await;

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
