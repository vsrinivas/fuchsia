// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        channel,
        model::{
            actions::{Action, ActionSet},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
        },
    },
    anyhow::{Context as _, Error},
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2::*,
    fuchsia_async::{self as fasync},
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    log::warn,
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
        time::Duration,
    },
};

lazy_static! {
    pub static ref SYSTEM_CONTROLLER_CAPABILITY_NAME: CapabilityName =
        "fuchsia.sys2.SystemController".into();
}

#[derive(Clone)]
pub struct SystemController {
    model: Arc<Model>,
    shutdown_timeout: Duration,
}

impl SystemController {
    pub fn new(model: Arc<Model>, shutdown_timeout: Duration) -> Self {
        Self { model, shutdown_timeout }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "SystemController",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        if capability.matches_protocol(&SYSTEM_CONTROLLER_CAPABILITY_NAME) {
            Ok(Some(Box::new(SystemControllerCapabilityProvider::new(
                self.model.clone(),
                self.shutdown_timeout.clone(),
            )) as Box<dyn CapabilityProvider>))
        } else {
            Ok(capability_provider)
        }
    }
}

#[async_trait]
impl Hook for SystemController {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::AboveRoot { capability },
            capability_provider,
        }) = &event.result
        {
            let mut capability_provider = capability_provider.lock().await;
            *capability_provider = self
                .on_framework_capability_routed_async(&capability, capability_provider.take())
                .await?;
        };
        Ok(())
    }
}

pub struct SystemControllerCapabilityProvider {
    model: Arc<Model>,
    request_timeout: Duration,
}

impl SystemControllerCapabilityProvider {
    // TODO (jmatt) allow timeout to be supplied in the constructor
    pub fn new(model: Arc<Model>, request_timeout: Duration) -> Self {
        Self { model, request_timeout }
    }

    async fn open_async(self, mut stream: SystemControllerRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            // TODO(jmatt) There is the potential for a race here. If
            // the thing that called SystemController.Shutdown is a
            // component that component_manager controls, it should
            // be gone by now. Sending a response doesn't make a lot
            // of sense in this case. However, the caller might live
            // outside component_manager, in which case a response
            // does make sense. Figure out if our behavior should be
            // different and/or whether we should drop the response
            // from this API.
            match request {
                // Shutting down the root component causes component_manager to
                // exit. main.rs waits on the model to observe the root realm
                // disappear.
                SystemControllerRequest::Shutdown { responder } => {
                    let timeout = zx::Duration::from(self.request_timeout);
                    fasync::Task::spawn(async move {
                        fasync::Timer::new(fasync::Time::after(timeout)).await;
                        panic!("Component manager did not complete shutdown in allowed time.");
                    })
                    .detach();
                    ActionSet::register(self.model.root_realm.clone(), Action::Shutdown)
                        .await
                        .await
                        .context("got error waiting for shutdown action to complete")?;
                    match responder.send() {
                        Ok(()) => {}
                        Err(e) => {
                            println!(
                                "error sending response to shutdown requester:\
                                 {}\n shut down proceeding",
                                e
                            );
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

#[async_trait]
impl CapabilityProvider for SystemControllerCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<SystemControllerMarker>::new(server_end);
        let stream: SystemControllerRequestStream =
            server_end.into_stream().map_err(ModelError::stream_creation_error)?;
        fasync::Task::spawn(async move {
            let result = self.open_async(stream).await;
            if let Err(e) = result {
                warn!("SystemController.open failed: {}", e);
            }
        })
        .detach();

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            binding::Binder,
            hooks::{EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
            realm::BindReason,
            testing::test_helpers::{
                component_decl_with_test_runner, ActionsTest, ComponentDeclBuilder, ComponentInfo,
            },
        },
        async_trait::async_trait,
        fidl::endpoints,
        fidl_fuchsia_sys2 as fsys,
        std::{boxed::Box, convert::TryFrom, sync::Arc, time::Duration},
    };

    /// Use SystemController to shut down a system whose root has the child `a`
    /// and `a` has descendents as shown in the diagram below.
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c   d
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_system_controller() {
        // Configure and start realm
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::BindChild { parent: AbsoluteMoniker::root() })
            .await
            .expect("could not bind to a");

        // Wire up connections to SystemController
        let sys_controller = Box::new(SystemControllerCapabilityProvider::new(
            test.model.clone(),
            // allow simulated shutdown to take up to 30 days
            Duration::from_secs(60 * 60 * 24 * 30),
        ));
        let (client_channel, server_channel) =
            endpoints::create_endpoints::<fsys::SystemControllerMarker>()
                .expect("failed creating channel endpoints");
        let mut server_channel = server_channel.into_channel();
        sys_controller
            .open(0, 0, PathBuf::new(), &mut server_channel)
            .await
            .expect("failed to open capability");
        let controller_proxy =
            client_channel.into_proxy().expect("failed converting endpoint into proxy");

        let root_realm_info = ComponentInfo::new(test.model.root_realm.clone()).await;
        let realm_a_info = ComponentInfo::new(realm_a.clone()).await;
        let realm_b_info = ComponentInfo::new(realm_b.clone()).await;
        let realm_c_info = ComponentInfo::new(realm_c.clone()).await;
        let realm_d_info = ComponentInfo::new(realm_d.clone()).await;

        // Check that the root realm is still here
        root_realm_info.check_not_shut_down(&test.runner).await;
        realm_a_info.check_not_shut_down(&test.runner).await;
        realm_b_info.check_not_shut_down(&test.runner).await;
        realm_c_info.check_not_shut_down(&test.runner).await;
        realm_d_info.check_not_shut_down(&test.runner).await;

        // Ask the SystemController to shut down the system and wait to be
        // notified that the room realm stopped.
        let completion = test.builtin_environment.wait_for_root_realm_stop();
        controller_proxy.shutdown().await.expect("shutdown request failed");
        completion.await;

        // Check state bits to confirm root realm looks shut down
        root_realm_info.check_is_shut_down(&test.runner).await;
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
    }

    #[test]
    #[should_panic(expected = "Component manager did not complete shutdown in allowed time.")]
    fn test_timeout() {
        const TIMEOUT_SECONDS: i64 = 6;
        const EVENT_PAUSE_SECONDS: i64 = TIMEOUT_SECONDS + 1;
        struct StopHook;
        #[async_trait]
        impl Hook for StopHook {
            async fn on(self: Arc<Self>, _event: &Event) -> Result<(), ModelError> {
                fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(
                    EVENT_PAUSE_SECONDS.into(),
                )))
                .await;
                Ok(())
            }
        }

        let mut exec = fasync::Executor::new_with_fake_time().unwrap();
        let mut test_logic = Box::pin(async {
            // Configure and start realm
            let components = vec![
                ("root", ComponentDeclBuilder::new().add_eager_child("a").build()),
                ("a", ComponentDeclBuilder::new().build()),
            ];

            let s = StopHook {};
            let s_hook: Arc<dyn Hook> = Arc::new(s);
            let hooks_reg = HooksRegistration::new(
                "stop hook",
                vec![EventType::Stopped],
                Arc::downgrade(&s_hook),
            );

            let test = ActionsTest::new_with_hooks("root", components, None, vec![hooks_reg]).await;
            let realm_a = test.look_up(vec!["a:0"].into()).await;
            test.model
                .bind(
                    &realm_a.abs_moniker,
                    &BindReason::BindChild { parent: AbsoluteMoniker::root() },
                )
                .await
                .expect("could not bind to a");

            // Wire up connections to SystemController
            let sys_controller = Box::new(SystemControllerCapabilityProvider::new(
                test.model.clone(),
                // require shutdown in a second
                Duration::from_secs(u64::try_from(TIMEOUT_SECONDS).unwrap()),
            ));
            let (client_channel, server_channel) =
                endpoints::create_endpoints::<fsys::SystemControllerMarker>()
                    .expect("failed creating channel endpoints");
            let mut server_channel = server_channel.into_channel();
            sys_controller
                .open(0, 0, PathBuf::new(), &mut server_channel)
                .await
                .expect("failed to open capability");
            let controller_proxy =
                client_channel.into_proxy().expect("failed converting endpoint into proxy");

            let root_realm_info = ComponentInfo::new(test.model.root_realm.clone()).await;
            let realm_a_info = ComponentInfo::new(realm_a.clone()).await;

            // Check that the root realm is still here
            root_realm_info.check_not_shut_down(&test.runner).await;
            realm_a_info.check_not_shut_down(&test.runner).await;

            // Ask the SystemController to shut down the system and wait to be
            // notified that the room realm stopped.
            let _completion = test.builtin_environment.wait_for_root_realm_stop();
            controller_proxy.shutdown().await.expect("shutdown request failed");
        });

        assert_eq!(std::task::Poll::Pending, exec.run_until_stalled(&mut test_logic));

        let new_time = fasync::Time::from_nanos(
            exec.now().into_nanos() + zx::Duration::from_seconds(TIMEOUT_SECONDS).into_nanos(),
        );

        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        assert_eq!(std::task::Poll::Pending, exec.run_until_stalled(&mut test_logic));
    }
}
