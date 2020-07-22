// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Fuchsia Inspect format for structured metrics trees.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_examples_inspect::{FizzBuzzMarker, ReverserMarker, ReverserProxy},
    fidl_fuchsia_sys::{
        ComponentControllerEvent, ComponentControllerMarker, ComponentControllerProxy, LaunchInfo,
        ServiceList, TerminationReason,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::NestedEnvironment,
    fuchsia_syslog::macros::*,
    fuchsia_zircon as zx,
    futures::{TryFutureExt, TryStreamExt},
};

pub struct CodelabEnvironment {
    env: NestedEnvironment,
    fizzbuzz_svc: Option<zx::Channel>,
    package: String,
    part: usize,
}

impl CodelabEnvironment {
    /// Creates a new codelab environmnent.
    pub fn new(env: NestedEnvironment, package: impl Into<String>, part: usize) -> Self {
        Self { env, part, package: package.into(), fizzbuzz_svc: None }
    }

    /// Launches a FizzBuzz component.
    pub fn launch_fizzbuzz(&mut self) -> Result<(), Error> {
        let (fizzbuzz_directory_client, fizzbuzz_directory_server) = zx::Channel::create()?;
        let url = format!(
            "fuchsia-pkg://fuchsia.com/{}#meta/inspect_rust_codelab_fizzbuzz.cmx",
            self.package
        );
        let mut launch_info_fizzbuzz = LaunchInfo {
            url,
            arguments: None,
            out: None,
            err: None,
            flat_namespace: None,
            additional_services: None,
            directory_request: Some(fizzbuzz_directory_server),
        };
        let (ctrl_fizzbuzz, server_end) =
            fidl::endpoints::create_proxy::<ComponentControllerMarker>().unwrap();
        self.env.launcher().create_component(&mut launch_info_fizzbuzz, Some(server_end))?;
        self.spawn_component_on_terminated_waiter(ctrl_fizzbuzz);
        self.fizzbuzz_svc = Some(fizzbuzz_directory_client);
        Ok(())
    }

    /// Launch a Reverser component for the associated Codelab part and connect to the Reverser
    /// service it exposes. If FizzBuzz was launched beforehand, connects to it.
    pub fn launch_reverser(&mut self) -> Result<ReverserProxy, Error> {
        let mut additional_services = None;
        if let Some(fizzbuzz_directory_client) = self.fizzbuzz_svc.take() {
            additional_services = Some(Box::new(ServiceList {
                names: vec![FizzBuzzMarker::SERVICE_NAME.to_string()],
                host_directory: Some(fizzbuzz_directory_client),
                provider: None,
            }));
        }
        let (reverser_directory_client, reverser_directory_server) = zx::Channel::create()?;
        let url = format!(
            "fuchsia-pkg://fuchsia.com/{}#meta/inspect_rust_codelab_part_{}.cmx",
            self.package, self.part
        );
        let mut launch_info_reverser = LaunchInfo {
            url,
            arguments: None,
            out: None,
            err: None,
            flat_namespace: None,
            additional_services,
            directory_request: Some(reverser_directory_server),
        };
        let (ctrl_reverser, server_end) =
            fidl::endpoints::create_proxy::<ComponentControllerMarker>().unwrap();
        self.env.launcher().create_component(&mut launch_info_reverser, Some(server_end))?;

        // Connect to the reverser service and call it with the input.
        let (reverser, server_end) = fidl::endpoints::create_proxy::<ReverserMarker>()?;
        fdio::service_connect_at(
            &reverser_directory_client,
            ReverserMarker::SERVICE_NAME,
            server_end.into_channel(),
        )?;

        self.spawn_component_on_terminated_waiter(ctrl_reverser);

        Ok(reverser)
    }

    fn spawn_component_on_terminated_waiter(&self, controller: ComponentControllerProxy) {
        fasync::Task::spawn(
            async move {
                let mut component_events = controller.take_event_stream();
                while let Some(event) = component_events.try_next().await? {
                    match event {
                        ComponentControllerEvent::OnTerminated {
                            return_code,
                            termination_reason,
                        } => {
                            if return_code != 0 || termination_reason != TerminationReason::Exited {
                                return Err(format_err!(
                                    "Component exited with code {}, reason {}",
                                    return_code,
                                    termination_reason as u32
                                ));
                            }
                            return Ok(());
                        }
                        _ => {}
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e| fx_log_err!("Error waiting for component: {:?}", e)),
        )
        .detach()
    }
}
