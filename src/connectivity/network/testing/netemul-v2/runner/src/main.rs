// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use async_utils::stream::FlattenUnorderedExt as _;
use fidl::endpoints::ControlHandle as _;
use fidl_fuchsia_component_runner as frunner;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_zircon as zx;
use futures_util::{FutureExt as _, StreamExt as _, TryStreamExt as _};
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

            // Retrieve the '/svc' and '/pkg' directories from the test root's namespace, so we can
            // access the fuchsia.test/Suite protocol from the test driver, the network
            // configuration file, and any netstacks that need to be configured.
            let (svc_dir, pkg_dir) = namespace.into_iter().fold(
                (None, None),
                |(svc_dir, pkg_dir), frunner::ComponentNamespaceEntry { path, directory, .. }| {
                    match path.as_ref().map(|s| s.as_str()) {
                        Some("/svc") => {
                            assert_eq!(svc_dir, None);
                            (Some(directory), pkg_dir)
                        }
                        Some("/pkg") => {
                            assert_eq!(pkg_dir, None);
                            (svc_dir, Some(directory))
                        }
                        _ => (svc_dir, pkg_dir),
                    }
                },
            );
            let svc_dir = svc_dir
                .context("/svc directory not in namespace")?
                .context("directory field not set for /svc namespace entry")?;
            let pkg_dir = pkg_dir
                .context("/pkg directory not in namespace")?
                .context("directory field not set for /pkg namespace entry")?
                .into_proxy()
                .context("client end into proxy")?;

            let config::Config { networks, netstacks } =
                config::load_from_program(program, &pkg_dir)
                    .await
                    .context("retrieving and parsing network configuration")?;
            info!(
                "configuring environment with networks: {:#?} and netstacks: {:#?}",
                networks, netstacks
            );

            // Proxy the `fuchsia.test/Suite` protocol from the test root's namespace (where it was
            // routed from the test driver) to its outgoing directory, where the Test Runner
            // Framework can access it.
            let svc_dir = std::sync::Arc::new(svc_dir.into_channel());
            let mut fs = ServiceFs::new_local();
            let _: &mut ServiceFsDir<'_, _> =
                fs.dir("svc").add_proxy_service_to::<fidl_fuchsia_test::SuiteMarker, ()>(svc_dir);
            let serve_test_suite = fs
                .serve_connection(outgoing_dir.into_channel())
                .context("serve connection on test component's outgoing dir")?
                .collect::<()>();

            let (mut request_stream, control_handle) = controller
                .into_stream_and_control_handle()
                .context("server end into request stream")?;

            let request = futures_util::select! {
                () = serve_test_suite.fuse() => panic!("service fs closed unexpectedly"),
                request = request_stream.try_next() => request,
            };

            // If the component manager sent a stop or kill request, or if the client end of the
            // component controller channel was dropped, we clean up any resources and signal that
            // execution has finished by closing the channel with an epitaph.
            if let Some(request) = request.context("receive request")? {
                match request {
                    frunner::ComponentControllerRequest::Stop { control_handle: _ } => {
                        info!("received stop request for component {}", resolved_url);
                    }
                    frunner::ComponentControllerRequest::Kill { control_handle: _ } => {
                        info!("received kill request for component {}", resolved_url);
                    }
                }
            } else {
                warn!("client end of component controller was dropped");
            }
            control_handle.shutdown_with_epitaph(zx::Status::OK);
        }
    }
    Ok(())
}
