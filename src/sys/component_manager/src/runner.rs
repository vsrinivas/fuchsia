// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        capability::{ComponentManagerCapability, ComponentManagerCapabilityProvider},
        model::{
            self,
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            Runner,
        },
    },
    cm_rust::CapabilityName,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::BoxFuture,
    futures::stream::TryStreamExt,
    log::warn,
    std::sync::{Arc, Weak},
};

/// Provides a hook for routing built-in runners to realms.
#[derive(Clone)]
pub struct BuiltinRunner {
    inner: Arc<BuiltinRunnerInner>,
}

struct BuiltinRunnerInner {
    name: CapabilityName,
    runner: Arc<dyn Runner + Sync + Send>,
}

impl BuiltinRunner {
    pub fn new(name: CapabilityName, runner: Arc<dyn Runner + Sync + Send>) -> Self {
        BuiltinRunner { inner: Arc::new(BuiltinRunnerInner { name, runner }) }
    }

    /// Construct a `HooksRegistration` that will route our runner as a builtin capability.
    pub fn hook(&self) -> HooksRegistration {
        HooksRegistration {
            events: vec![EventType::RouteBuiltinCapability],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }
    }
}

impl model::Hook for BuiltinRunnerInner {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            if let EventPayload::RouteBuiltinCapability { capability, capability_provider } =
                &event.payload
            {
                // If we are being asked about the runner capability we own, pass a
                // copy back to the caller.
                let mut capability_provider = capability_provider.lock().await;
                if let ComponentManagerCapability::Runner(runner_name) = capability {
                    if self.name == *runner_name {
                        *capability_provider =
                            Some(Box::new(RunnerCapabilityProvider::new(self.runner.clone())));
                    }
                }
            }
            Ok(())
        })
    }
}

/// Allows a Rust `Runner` object to be treated as a generic capability,
/// as is required by the capability routing code.
#[derive(Clone)]
struct RunnerCapabilityProvider {
    runner: Arc<dyn Runner + Sync + Send>,
}

impl RunnerCapabilityProvider {
    pub fn new(runner: Arc<dyn Runner + Sync + Send>) -> Self {
        RunnerCapabilityProvider { runner }
    }

    async fn open_async(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let runner = Arc::clone(&self.runner);
        let mut stream = ServerEnd::<fsys::ComponentRunnerMarker>::new(server_chan)
            .into_stream()
            .expect("could not convert channel into stream");
        fasync::spawn(async move {
            // Keep handling requests until the stream closes or we receive a runner error.
            while let Ok(Some(request)) = stream.try_next().await {
                let fsys::ComponentRunnerRequest::Start { start_info, controller, .. } = request;
                let component_url = start_info.resolved_url.clone();
                if let Err(error) = runner.start(start_info, controller).await {
                    warn!(
                        "Runner returned an error attempting to start '{}': {}",
                        component_url.as_ref().map(|x| x as &str).unwrap_or("<unknown>"),
                        error
                    );
                    break;
                }
            }
        });
        Ok(())
    }
}

impl ComponentManagerCapabilityProvider for RunnerCapabilityProvider {
    fn open(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.open_async(flags, open_mode, relative_path, server_chan))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::model::{
            hooks::*,
            testing::{routing_test_helpers::*, test_helpers::*},
            Realm, RemoteRunner, ResolverRegistry,
        },
        cm_rust::{
            self, CapabilityName, ChildDecl, ComponentDecl, OfferDecl, OfferRunnerDecl,
            OfferRunnerSource, OfferTarget, UseDecl, UseRunnerDecl,
        },
        failure::{format_err, Error},
        futures::lock::Mutex,
    };

    fn create_test_realm() -> Realm {
        Realm::new_root_realm(ResolverRegistry::new(), "test:///root".to_string())
    }

    fn sample_start_info() -> fsys::ComponentStartInfo {
        fsys::ComponentStartInfo {
            resolved_url: Some("test".to_string()),
            program: None,
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
        }
    }

    // Test plumbing from a `RunnerCapabilityProvider`, through the hook system, and to
    // a `Runner` object.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_cap_from_hook() -> Result<(), Error> {
        // Set up a runner.
        let (runner_proxy, mut runner_stream) =
            fidl::endpoints::create_proxy_and_stream::<fsys::ComponentRunnerMarker>()?;
        let builtin_runner =
            BuiltinRunner::new("elf".into(), Arc::new(RemoteRunner::new(runner_proxy)));

        // Install the hook, and dispatch an event.
        let hooks = Hooks::new(None);
        hooks.install(vec![builtin_runner.hook()]).await;
        let provider_result = Arc::new(Mutex::new(None));
        hooks
            .dispatch(&Event {
                target_realm: Arc::new(create_test_realm()),
                payload: EventPayload::RouteBuiltinCapability {
                    capability: ComponentManagerCapability::Runner("elf".into()),
                    capability_provider: provider_result.clone(),
                },
            })
            .await?;
        let provider = provider_result.lock().await.take().expect("did not get runner cap");

        // Open a connection to the provider.
        let (client, server) = fidl::endpoints::create_proxy::<fsys::ComponentRunnerMarker>()?;
        let (_, server_controller) =
            fidl::endpoints::create_endpoints::<fsys::ComponentControllerMarker>()?;
        provider.open(0, 0, ".".to_string(), server.into_channel()).await?;

        // Ensure the message goes through to our channel.
        client.start(sample_start_info(), server_controller)?;
        assert!(runner_stream.try_next().await?.is_some());

        Ok(())
    }

    /// A runner that always returns an error on calls to `start`.
    struct FailingRunner();

    impl Runner for FailingRunner {
        fn start(
            &self,
            _start_info: fsys::ComponentStartInfo,
            _server_end: ServerEnd<fsys::ComponentControllerMarker>,
        ) -> BoxFuture<Result<(), model::RunnerError>> {
            Box::pin(async { Err(model::RunnerError::invalid_args("xxx", format_err!("yyy"))) })
        }
    }

    // Test sending a start command to a failing runner.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn capability_provider_error_from_runner() -> Result<(), Error> {
        // Set up a capability provider wrapping a runner that always returns an error.
        let provider = RunnerCapabilityProvider { runner: Arc::new(FailingRunner {}) };

        // Open a connection to the provider.
        let (client, server) = fidl::endpoints::create_proxy::<fsys::ComponentRunnerMarker>()?;
        let (_, server_controller) =
            fidl::endpoints::create_endpoints::<fsys::ComponentControllerMarker>()?;
        provider.open(0, 0, ".".to_string(), server.into_channel()).await?;

        // Ensure the start call succeeds, even if the runner fails.
        client.start(sample_start_info(), server_controller)?;

        Ok(())
    }

    //   (cm)
    //    |
    //    a
    //
    // a: uses runner "elf" offered from the component mananger.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_runner_from_component_manager() {
        let components = vec![(
            "a",
            ComponentDecl {
                uses: vec![UseDecl::Runner(UseRunnerDecl {
                    source_name: CapabilityName("elf".to_string()),
                })],
                ..default_component_decl()
            },
        )];

        // Set up the system.
        let universe = RoutingTest::new("a", components).await;

        // Bind the root component.
        universe.bind_instance(&vec![].into()).await.expect("bind failed");

        // Ensure the instance starts up.
        universe.wait_for_component_start(&vec![].into()).await;
    }

    //   (cm)
    //    |
    //    a
    //    |
    //    b
    //
    // a: offers runner "elf" to "b"
    // b: uses runner "elf".
    #[fuchsia_async::run_singlethreaded(test)]
    async fn offer_runner_from_component_manager() {
        let components = vec![
            (
                "a",
                ComponentDecl {
                    uses: vec![UseDecl::Runner(UseRunnerDecl {
                        source_name: CapabilityName("elf".to_string()),
                    })],
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    offers: vec![OfferDecl::Runner(OfferRunnerDecl {
                        source: OfferRunnerSource::Realm,
                        source_name: CapabilityName("elf".to_string()),
                        target: OfferTarget::Child("b".to_string()),
                        target_name: CapabilityName("dwarf".to_string()),
                    })],
                    ..default_component_decl()
                },
            ),
            (
                "b",
                ComponentDecl {
                    uses: vec![UseDecl::Runner(UseRunnerDecl {
                        source_name: CapabilityName("dwarf".to_string()),
                    })],
                    ..default_component_decl()
                },
            ),
        ];

        // Set up the system.
        let universe = RoutingTest::new("a", components).await;

        // Bind the root component.
        universe.bind_instance(&vec!["b:0"].into()).await.expect("bind failed");

        // Ensure the instance starts up.
        universe.wait_for_component_start(&vec!["b:0"].into()).await;
    }
}
