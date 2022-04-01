// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use async_utils::stream::FlattenUnorderedExt as _;
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
    // Retrieve the '/svc' and '/hub' directories from the test root's namespace, so
    // that we can:
    // - access the `fuchsia.test/Suite` protocol from the test driver
    // - access any netstacks that need to be configured
    // - use the `fuchsia.sys2/LifecycleController` for the test root to start
    //   non-test components once test setup is complete
    let (svc_dir, hub_dir) = namespace.into_iter().fold(
        (None, None),
        |(svc_dir, hub_dir), frunner::ComponentNamespaceEntry { path, directory, .. }| match path
            .as_ref()
            .map(|s| s.as_str())
        {
            Some("/svc") => {
                assert_eq!(svc_dir, None);
                (Some(directory), hub_dir)
            }
            Some("/hub") => {
                assert_eq!(hub_dir, None);
                (svc_dir, Some(directory))
            }
            _ => (svc_dir, hub_dir),
        },
    );

    let svc_dir = svc_dir
        .context("/svc directory not in namespace")?
        .context("directory field not set for /svc namespace entry")?
        .into_proxy()
        .context("client end into proxy")?;

    let lifecycle_controller = {
        let hub_dir = hub_dir
            .context("/hub directory not in namespace")?
            .context("directory field not set for /hub namespace entry")?
            .into_proxy()
            .context("client end into proxy")?;

        // TODO(https://fxbug.dev/96639): pipeline the `open` call rather than calling
        // with the `fuchsia.io/OPEN_FLAG_DESCRIBE` flag and waiting for the describe
        // event signaling that the directory is open.
        //
        // We cannot do this currently because there is a race between component manager
        // opening /hub/debug and sending the start request to this runner, so it's
        // possible to see a `PEER_CLOSED` error when attempting to interact with the
        // `fuchsia.sys2/LifecycleController` protocol exposed in that directory.
        let debug = io_util::directory::open_directory(
            &hub_dir,
            "debug",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .context("open /hub/debug")?;

        connect_to_protocol_at_dir_root::<fsys2::LifecycleControllerMarker>(&debug)
            .context("connect to protocol")?
    };

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
                component_epitaph,
            ) = match test_setup(program, namespace).await {
                Ok((env, svc_dir)) => {
                    // Proxy `fuchsia.test/Suite` requests at the test root's outgoing directory,
                    // where the test manager will expect to be able to access it, to the '/svc'
                    // directory in the test root's namespace, where the protocol was routed from
                    // the test driver.
                    let svc_dir = std::sync::Arc::new(
                        svc_dir.into_channel().expect("proxy into channel").into(),
                    );
                    let _: &mut ServiceFsDir<'_, _> =
                        fs.dir("svc").add_proxy_service_to::<ftest::SuiteMarker, ()>(svc_dir);
                    (Some(env), zx::Status::OK)
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
                    (None, zx::Status::from_raw(fcomponent::Error::InstanceCannotStart as i32))
                }
            };
            let serve_test_suite = fs
                .serve_connection(outgoing_dir.into_channel())
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
            } else {
                warn!("component manager dropped client end of component controller channel");
            }
        }
    }
    Ok(())
}
