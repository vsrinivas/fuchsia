// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::*,
        model::{self, error::ModelError, hooks::*, Model, Realm},
    },
    cm_rust::CapabilityPath,
    failure::{Error, ResultExt},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2::*,
    fuchsia_async::{self as fasync},
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, prelude::*},
    lazy_static::lazy_static,
    log::warn,
    std::{
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref SYSTEM_CONTROLLER_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.SystemController".try_into().unwrap();
}

#[derive(Clone)]
pub struct SystemController {
    inner: Arc<SystemControllerInner>,
}

impl SystemController {
    pub fn new(model: Arc<Model>) -> Self {
        Self { inner: Arc::new(SystemControllerInner::new(model)) }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::RouteBuiltinCapability],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }
}

struct SystemControllerInner {
    model: Arc<Model>,
}

impl SystemControllerInner {
    pub fn new(model: Arc<Model>) -> Self {
        Self { model }
    }

    async fn on_route_builtin_capability_async<'a>(
        self: Arc<Self>,
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match capability {
            ComponentManagerCapability::LegacyService(capability_path)
                if *capability_path == *SYSTEM_CONTROLLER_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(SystemControllerCapabilityProvider::new(self.model.clone()))
                    as Box<dyn ComponentManagerCapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

impl Hook for SystemControllerInner {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            match &event.payload {
                EventPayload::RouteBuiltinCapability { capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_builtin_capability_async(&capability, capability_provider.take())
                        .await?;
                }
                _ => {}
            };
            Ok(())
        })
    }
}

pub struct SystemControllerCapabilityProvider {
    model: Arc<Model>,
}

impl SystemControllerCapabilityProvider {
    pub fn new(model: Arc<Model>) -> Self {
        Self { model }
    }

    async fn open_async(
        mut stream: SystemControllerRequestStream,
        model: Arc<Model>,
    ) -> Result<(), Error> {
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
                    Realm::register_action(
                        model.root_realm.clone(),
                        model.clone(),
                        model::Action::Shutdown,
                    )
                    .await
                    .context("failed to register shutdown action on root realm")?
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

impl ComponentManagerCapabilityProvider for SystemControllerCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        let server_end = ServerEnd::<SystemControllerMarker>::new(server_end);
        let stream: SystemControllerRequestStream = server_end.into_stream().unwrap();
        let model_copy = self.model.clone();
        fasync::spawn(async move {
            let result = Self::open_async(stream, model_copy).await;
            if let Err(e) = result {
                warn!("SystemController.open failed: {}", e);
            }
        });

        Box::pin(async { Ok(()) })
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::capability::ComponentManagerCapabilityProvider,
        crate::model::{actions, testing::test_helpers::*, Binder},
        crate::system_controller::SystemControllerCapabilityProvider,
        cm_rust::{ChildDecl, ComponentDecl},
        fidl::endpoints,
        fidl_fuchsia_sys2 as fsys,
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
            (
                "root",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "a",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Eager,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "b",
                ComponentDecl {
                    children: vec![
                        ChildDecl {
                            name: "c".to_string(),
                            url: "test:///c".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                        ChildDecl {
                            name: "d".to_string(),
                            url: "test:///d".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                    ],
                    ..default_component_decl()
                },
            ),
            ("c", ComponentDecl { ..default_component_decl() }),
            ("d", ComponentDecl { ..default_component_decl() }),
        ];
        let test = actions::tests::ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");

        // Wire up connections to SystemController
        let sys_controller = SystemControllerCapabilityProvider::new(test.model.clone());
        let (client_channel, server_channel) =
            endpoints::create_endpoints::<fsys::SystemControllerMarker>()
                .expect("failed creating channel endpoints");
        sys_controller.open(0, 0, "".to_string(), server_channel.into_channel());
        let controller_proxy =
            client_channel.into_proxy().expect("failed converting endpoint into proxy");

        // Attach hooks so we can listen for stop or root realm
        let notifier_hooks = {
            let notifier = test.model.notifier.lock().await;
            let notifier = notifier.as_ref();
            notifier.expect("Notifier must exist. Model is not created!").hooks()
        };
        test.model.root_realm.hooks.install(notifier_hooks).await;

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
        let completion = test.model.wait_for_root_realm_stop();
        controller_proxy.shutdown().await.expect("shutdown request failed");
        completion.await;

        // Check state bits to confirm root realm looks shut down
        root_realm_info.check_is_shut_down(&test.runner).await;
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
    }
}
