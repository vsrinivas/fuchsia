// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        channel,
        config::{RuntimeConfig, ScopedPolicyChecker},
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            runner::Runner,
        },
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::stream::TryStreamExt,
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

/// Trait for built-in runner services. Wraps the generic Runner trait to provide a
/// ScopedPolicyChecker for the realm of the component being started, so that runners can enforce
/// security policy.
pub trait BuiltinRunnerFactory: Send + Sync {
    fn get_scoped_runner(self: Arc<Self>, checker: ScopedPolicyChecker) -> Arc<dyn Runner>;
}

/// Provides a hook for routing built-in runners to realms.
pub struct BuiltinRunner {
    name: CapabilityName,
    runner: Arc<dyn BuiltinRunnerFactory>,
    config: Weak<RuntimeConfig>,
}

impl BuiltinRunner {
    pub fn new(
        name: CapabilityName,
        runner: Arc<dyn BuiltinRunnerFactory>,
        config: Weak<RuntimeConfig>,
    ) -> Self {
        Self { name, runner, config }
    }

    /// Construct a `HooksRegistration` that will route our runner as a framework capability.
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "BuiltinRunner",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }
}

#[async_trait]
impl Hook for BuiltinRunner {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::Builtin { capability },
            capability_provider,
        }) = &event.result
        {
            // If we are being asked about the runner capability we own, pass a copy back to the
            // caller.
            if let InternalCapability::Runner(runner_name) = capability {
                if *runner_name == self.name {
                    let checker =
                        ScopedPolicyChecker::new(self.config.clone(), event.target_moniker.clone());
                    let runner = self.runner.clone().get_scoped_runner(checker);
                    *capability_provider.lock().await =
                        Some(Box::new(RunnerCapabilityProvider::new(runner)));
                }
            }
        }
        Ok(())
    }
}

/// Allows a Rust `Runner` object to be treated as a generic capability,
/// as is required by the capability routing code.
#[derive(Clone)]
struct RunnerCapabilityProvider {
    runner: Arc<dyn Runner>,
}

impl RunnerCapabilityProvider {
    pub fn new(runner: Arc<dyn Runner>) -> Self {
        RunnerCapabilityProvider { runner }
    }
}

#[async_trait]
impl CapabilityProvider for RunnerCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let runner = Arc::clone(&self.runner);
        let server_end = channel::take_channel(server_end);
        let mut stream = ServerEnd::<fcrunner::ComponentRunnerMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        fasync::Task::spawn(async move {
            // Keep handling requests until the stream closes.
            while let Ok(Some(request)) = stream.try_next().await {
                let fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } =
                    request;
                runner.start(start_info, controller).await;
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
        crate::{
            config::{JobPolicyAllowlists, SecurityPolicy},
            model::{
                hooks::Hooks,
                moniker::AbsoluteMoniker,
                testing::{mocks::MockRunner, routing_test_helpers::*, test_helpers::*},
            },
        },
        anyhow::Error,
        cm_rust::{self, CapabilityName, ChildDecl, ComponentDecl, UseDecl, UseRunnerDecl},
        fidl_fuchsia_sys2 as fsys,
        futures::{lock::Mutex, prelude::*},
        matches::assert_matches,
    };

    fn sample_start_info(name: &str) -> fcrunner::ComponentStartInfo {
        fcrunner::ComponentStartInfo {
            resolved_url: Some(name.to_string()),
            program: None,
            ns: Some(vec![]),
            outgoing_dir: None,
            runtime_dir: None,
        }
    }

    async fn start_component_through_hooks(
        hooks: &Hooks,
        moniker: AbsoluteMoniker,
        url: &str,
    ) -> Result<(), Error> {
        let provider_result = Arc::new(Mutex::new(None));
        hooks
            .dispatch(&Event::new_for_test(
                moniker,
                url,
                Ok(EventPayload::CapabilityRouted {
                    source: CapabilitySource::Builtin {
                        capability: InternalCapability::Runner("elf".into()),
                    },
                    capability_provider: provider_result.clone(),
                }),
            ))
            .await?;
        let provider = provider_result.lock().await.take().expect("did not get runner cap");

        // Open a connection to the provider.
        let (client, server) = fidl::endpoints::create_proxy::<fcrunner::ComponentRunnerMarker>()?;
        let (_, server_controller) =
            fidl::endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>()?;
        let mut server = server.into_channel();
        provider.open(0, 0, PathBuf::from("."), &mut server).await?;

        // Start the component.
        client.start(sample_start_info(url), server_controller)?;

        Ok(())
    }

    // Test plumbing a `BuiltinRunner` through the hook system.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn builtin_runner_hook() -> Result<(), Error> {
        let config = Arc::new(RuntimeConfig {
            security_policy: SecurityPolicy {
                job_policy: JobPolicyAllowlists {
                    ambient_mark_vmo_exec: vec![AbsoluteMoniker::from(vec!["foo:0"])],
                    ..Default::default()
                },
            },
            ..Default::default()
        });
        let runner = Arc::new(MockRunner::new());
        let builtin_runner =
            Arc::new(BuiltinRunner::new("elf".into(), runner.clone(), Arc::downgrade(&config)));

        let hooks = Hooks::new(None);
        hooks.install(builtin_runner.hooks()).await;

        // Ensure we see the start events and that an appropriate PolicyChecker was provided to the
        // runner depending on the moniker of the component being run.

        // Case 1: The started component's moniker matches the allowlist entry above.
        let url = "xxx://test";
        start_component_through_hooks(&hooks, AbsoluteMoniker::from(vec!["foo:0"]), url).await?;
        runner.wait_for_url(url).await;
        let checker = runner.last_checker().expect("No PolicyChecker held by MockRunner");
        assert_matches!(checker.ambient_mark_vmo_exec_allowed(), Ok(()));

        // Case 2: Moniker does not match allowlist entry.
        start_component_through_hooks(&hooks, AbsoluteMoniker::root(), url).await?;
        runner.wait_for_url(url).await;
        let checker = runner.last_checker().expect("No PolicyChecker held by MockRunner");
        assert_matches!(checker.ambient_mark_vmo_exec_allowed(), Err(_));

        Ok(())
    }

    // Test sending a start command to a failing runner.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn capability_provider_error_from_runner() -> Result<(), Error> {
        // Set up a capability provider wrapping a runner that returns an error on our
        // target URL.
        let mock_runner = Arc::new(MockRunner::new());
        mock_runner.add_failing_url("xxx://failing");
        let provider = Box::new(RunnerCapabilityProvider { runner: mock_runner });

        // Open a connection to the provider.
        let (client, server) = fidl::endpoints::create_proxy::<fcrunner::ComponentRunnerMarker>()?;
        let mut server = server.into_channel();
        provider.open(0, 0, PathBuf::from("."), &mut server).await?;

        // Ensure errors are propagated back to the caller.
        //
        // We make multiple calls over the same channel to ensure that the channel remains open
        // even after errors.
        for _ in 0..3i32 {
            let (client_controller, server_controller) =
                fidl::endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>()?;
            client.start(sample_start_info("xxx://failing"), server_controller)?;
            let actual = client_controller
                .into_proxy()?
                .take_event_stream()
                .next()
                .await
                .unwrap()
                .err()
                .unwrap();
            assert_matches!(actual,
                fidl::Error::ClientChannelClosed { status: zx::Status::UNAVAILABLE, .. }
            );
        }

        Ok(())
    }

    //   (cm)
    //    |
    //    a
    //
    // a: uses runner "elf" offered from the component mananger.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_runner_from_component_manager() {
        let mock_runner = Arc::new(MockRunner::new());

        let components = vec![(
            "a",
            ComponentDecl {
                uses: vec![UseDecl::Runner(UseRunnerDecl {
                    source_name: CapabilityName("my_runner".to_string()),
                })],
                ..default_component_decl()
            },
        )];

        // Set up the system.
        let universe = RoutingTestBuilder::new("a", components)
            .add_builtin_runner("my_runner", mock_runner.clone())
            .build()
            .await;

        // Bind the root component.
        universe.bind_instance(&vec![].into()).await.expect("bind failed");

        // Ensure the instance starts up.
        mock_runner.wait_for_url("test:///a_resolved").await;
    }

    //   (cm)
    //    |
    //    a
    //    |
    //    b
    //
    // (cm): registers runner "elf".
    // b: uses runner "elf".
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_runner_from_component_manager_environment() {
        let mock_runner = Arc::new(MockRunner::new());

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
                        environment: None,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "b",
                ComponentDecl {
                    uses: vec![UseDecl::Runner(UseRunnerDecl {
                        source_name: CapabilityName("elf".to_string()),
                    })],
                    ..default_component_decl()
                },
            ),
        ];

        // Set up the system.
        let universe = RoutingTestBuilder::new("a", components)
            .add_builtin_runner("elf", mock_runner.clone())
            .build()
            .await;

        // Bind the root component.
        universe.bind_instance(&vec!["b:0"].into()).await.expect("bind failed");

        // Ensure the instances started up.
        mock_runner.wait_for_url("test:///a_resolved").await;
        mock_runner.wait_for_url("test:///b_resolved").await;
    }
}
