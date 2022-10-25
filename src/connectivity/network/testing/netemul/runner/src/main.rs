// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _};
use async_utils::stream::FlattenUnorderedExt as _;
use component_events::events::{self};
use fidl::endpoints::Proxy as _;
use fidl::endpoints::{ControlHandle as _, RequestStream as _};
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_runner as frunner;
use fidl_fuchsia_data as fdata;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_netemul as fnetemul;
use fidl_fuchsia_sys2 as fsys2;
use fidl_fuchsia_test as ftest;
use fuchsia_component::client::{
    connect_to_named_protocol_at_dir_root, connect_to_protocol_at_dir_root,
};
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryStreamExt as _};
use log::{error, info, warn};

mod config;

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    diagnostics_log::init!();
    info!("started");

    let mut fs = ServiceFs::new_local();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(|s: frunner::ComponentRunnerRequestStream| s);
    let _: &mut ServiceFs<_> = fs.take_and_serve_directory_handle()?;
    fs.fuse()
        .flatten_unordered()
        .map(|r| r.context("error reading request stream"))
        .and_then(handle_runner_request)
        .for_each_concurrent(None, |r| async {
            r.unwrap_or_else(|e| error!("error handling component runner requests: {:?}", e));
        })
        .await;

    Ok(())
}

// Performs any necessary test setup, such as reading the specified virtual
// network configuration and configuring it, and, if successful, returns a
// handle to its network environment, along with the '/svc' directory from the
// test root's namespace.
async fn test_setup(
    program: fdata::Dictionary,
    namespace: Vec<frunner::ComponentNamespaceEntry>,
) -> Result<(config::NetworkEnvironment, fio::DirectoryProxy), anyhow::Error> {
    // Retrieve the '/svc' directory from the test root's namespace, so that we
    // can:
    // - access the `fuchsia.test/Suite` protocol from the test driver
    // - access any netstacks that need to be configured
    // - use the `fuchsia.sys2/LifecycleController` for the test root to start
    //   non-test components once test setup is complete
    let svc_dir = namespace
        .into_iter()
        .find_map(|frunner::ComponentNamespaceEntry { path, directory, .. }| {
            (path.map(|path| path == "/svc").unwrap_or(false)).then(|| directory)
        })
        .context("/svc directory not in namespace")?
        .context("directory field not set for /svc namespace entry")?
        .into_proxy()
        .context("client end into proxy")?;

    let lifecycle_controller =
        connect_to_protocol_at_dir_root::<fsys2::LifecycleControllerMarker>(&svc_dir)
            .context("connect to LifecycleController protocol")?;

    let network_environment = config::Config::load_from_program(program)
        .context("retrieving and parsing network configuration")?
        .apply(
            |name| {
                connect_to_named_protocol_at_dir_root::<fnetemul::ConfigurableNetstackMarker>(
                    &svc_dir, &name,
                )
                .context("connect to protocol")
            },
            lifecycle_controller,
        )
        .await
        .context("configuring networking environment")?;

    Ok((network_environment, svc_dir))
}

async fn handle_runner_request(
    request: frunner::ComponentRunnerRequest,
) -> Result<(), anyhow::Error> {
    match request {
        frunner::ComponentRunnerRequest::Start { start_info, controller, control_handle: _ } => {
            let frunner::ComponentStartInfo { resolved_url, program, ns, outgoing_dir, .. } =
                start_info;

            let resolved_url = resolved_url.context("component URL missing from start info")?;
            let program = program.context("program missing from start info")?;
            let namespace = ns.context("namespace missing from start info")?;
            let outgoing_dir =
                outgoing_dir.context("outgoing directory missing from start info")?;

            let mut fs = ServiceFs::new_local();
            let (
                // Keep around the handles to the virtual networks and endpoints we created, so
                // that they're not cleaned up before test execution is complete.
                _network_environment,
                test_stopped_fut,
                component_epitaph,
            ) = match test_setup(program, namespace).await {
                Ok((env, svc_dir)) => {
                    // Retrieve the component event stream from the test root so we can observe its
                    // `destroyed` lifecycle event. The test root will only be destroyed once all
                    // its child components have stopped.
                    let connection =
                        connect_to_protocol_at_dir_root::<fsys2::EventStream2Marker>(&svc_dir)
                            .context("connect to protocol")?;
                    connection
                        .wait_for_ready()
                        .await
                        .context("wait for event subscription to complete")?;
                    let mut event_stream = events::EventStream::new_v2(connection);
                    let test_stopped_fut = async move {
                        component_events::matcher::EventMatcher::ok()
                            .moniker(".")
                            .wait::<events::Destroyed>(&mut event_stream)
                            .await
                    };

                    // Proxy `fuchsia.test/Suite` requests at the test root's outgoing directory,
                    // where the test manager will expect to be able to access it, to the '/svc'
                    // directory in the test root's namespace, where the protocol was routed from
                    // the test driver.
                    //
                    // TODO(https://fxbug.dev/108786): Use Proxy::into_client_end when available.
                    let svc_dir = std::sync::Arc::new(fidl::endpoints::ClientEnd::new(
                        svc_dir.into_channel().expect("proxy into channel").into_zx_channel(),
                    ));
                    let _: &mut ServiceFsDir<'_, _> =
                        fs.dir("svc").add_proxy_service_to::<ftest::SuiteMarker, ()>(svc_dir);

                    (Some(env), Some(test_stopped_fut), zx::Status::OK)
                }
                Err(e) => {
                    error!("failed to set up test {}: {:?}", resolved_url, e);
                    // The runner could just bail when test setup fails, and in doing so, close both
                    // the `fuchsia.test/Suite` channel to test_manager and the component controller
                    // channel to component_manager. However, this would lead to a race between
                    // component shutdown and test failure.
                    //
                    // To synchronize these processes, continue to serve `fuchsia.test/Suite`
                    // (closing incoming request channels) until the runner receives a Stop request
                    // from component_manager, then shut down the component.
                    //
                    // TODO(https://fxbug.dev/94888): communicate the invalid component
                    // configuration to the test manager (via an epitaph, for example), rather than
                    // just closing the `fuchsia.test/Suite` protocol.
                    let _: &mut ServiceFsDir<'_, _> =
                        fs.dir("svc").add_fidl_service(|stream: ftest::SuiteRequestStream| {
                            stream.control_handle().shutdown()
                        });
                    (
                        None,
                        None,
                        zx::Status::from_raw(fcomponent::Error::InstanceCannotStart as i32),
                    )
                }
            };
            let serve_test_suite = fs
                .serve_connection(outgoing_dir)
                .context("serve connection on test component's outgoing dir")?
                .collect::<()>();

            let mut request_stream =
                controller.into_stream().context("server end into request stream")?;

            let request = futures::select! {
                () = serve_test_suite.fuse() => panic!("service fs closed unexpectedly"),
                request = request_stream.try_next() => request,
            };

            // If the component manager sent a stop or kill request (or dropped the client
            // end of the component controller channel), clean up any resources and, if the
            // client end wasn't dropped, signal that component execution has finished by
            // closing the channel with an epitaph.
            if let Some(request) = request.context("receive request")? {
                let control_handle = match request {
                    frunner::ComponentControllerRequest::Stop { control_handle } => {
                        info!("received stop request for component {}", resolved_url);
                        control_handle
                    }
                    frunner::ComponentControllerRequest::Kill { control_handle } => {
                        info!("received kill request for component {}", resolved_url);
                        control_handle
                    }
                };
                control_handle.shutdown_with_epitaph(component_epitaph);
                // TODO(https://fxbug.dev/81036): remove this once
                // `ControlHandle::shutdown_with_epitaph` actually closes the underlying
                // channel.
                drop(request_stream);
            } else {
                warn!("component manager dropped client end of component controller channel");
            }

            if let Some(fut) = test_stopped_fut {
                // Wait until we observe the test root's `destroyed` event to drop the handle to
                // the network environment, so that we are ensured the entire test realm has
                // completed orderly shutdown by the time we are removing interfaces. This
                // prevents spurious test failures from the virtual network being torn down
                // while some components in the test realm may still be running.
                let destroyed_event = fut.await.context("observe destroyed event")?;
                let events::DestroyedPayload {} = destroyed_event
                    .result()
                    .map_err(|e| anyhow!("error on component destroyed event: {:?}", e))?;
            }
        }
    }
    Ok(())
}
