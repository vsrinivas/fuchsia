// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fidl::endpoints::{create_proxy, ControlHandle, Proxy, RequestStream},
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_protocol, connect_to_protocol_at},
        server::ServiceFs,
    },
    fuchsia_component_test::{ScopedInstance, ScopedInstanceFactory},
    fuchsia_zircon as zx,
    futures::prelude::*,
    tracing::{info, warn},
};

const RUNNER_COLLECTION: &str = "runners";

#[derive(FromArgs)]
/// Serve a test runner using a nested test runner that is restarted for each test.
struct Args {
    #[argh(positional)]
    /// the nested test runner to launch.
    nested_runner_url: String,
}

#[fuchsia::main(logging_tags=["dart_wrapper_runner"])]
async fn main() -> Result<(), Error> {
    info!("started");
    let Args { nested_runner_url } = argh::from_env();

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        info!("Got stream");
        let nested_runner_url_clone = nested_runner_url.clone();
        fasync::Task::local(async move {
            start_runner(stream, &nested_runner_url_clone)
                .await
                .unwrap_or_else(|e| warn!(?e, "failed to run runner."));
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

async fn start_runner(
    request_stream: fcrunner::ComponentRunnerRequestStream,
    runner_url: &str,
) -> Result<(), Error> {
    request_stream
        .map_err(Error::from)
        .try_for_each_concurrent(
            None,
            |fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. }| async move {
                info!("Starting nested runner");
                let mut nested =
                    ScopedInstanceFactory::new(RUNNER_COLLECTION).new_instance(runner_url).await?;
                if let Err(e) =
                    run_component_controller(&nested, start_info, controller.into_stream()?).await
                {
                    warn!("Failed to run component: {:?}", e);
                }
                info!("Tearing down nested runner");
                let destroy_awaiter = nested.take_destroy_waiter();
                drop(nested);
                if let Err(e) = destroy_awaiter.await {
                    warn!("Failed to destroy nested runner: {:?}", e);
                }
                Ok(())
            },
        )
        .await
}

async fn run_component_controller(
    nested_runner: &ScopedInstance,
    start_info: fcrunner::ComponentStartInfo,
    request_stream: fcrunner::ComponentControllerRequestStream,
) -> Result<(), Error> {
    let hub_location =
        format!("/hub/children/{}:{}/exec/out/svc", RUNNER_COLLECTION, nested_runner.child_name());

    let controller = connect_to_protocol::<fsys::LifecycleControllerMarker>()?;
    controller
        .start(&format!("./{}:{}", RUNNER_COLLECTION, nested_runner.child_name()))
        .await?
        .unwrap();

    let component_runner =
        connect_to_protocol_at::<fcrunner::ComponentRunnerMarker>(&hub_location)?;
    let (nested_controller, server_end) = create_proxy::<fcrunner::ComponentControllerMarker>()?;
    component_runner.start(start_info, server_end)?;
    let nested_controller_events = nested_controller.take_event_stream();
    let request_stream_control = request_stream.control_handle();

    enum ProxyEvent<T, U> {
        ClientRequest(T),
        ServerEvent(U),
    }

    let mut combined_stream = futures::stream::select(
        request_stream.map(ProxyEvent::ClientRequest),
        nested_controller_events.map(ProxyEvent::ServerEvent),
    );

    while let Some(event) = combined_stream.next().await {
        match event {
            ProxyEvent::ClientRequest(Ok(request)) => match request {
                fcrunner::ComponentControllerRequest::Stop { control_handle } => {
                    nested_controller
                        .stop()
                        .unwrap_or_else(|e| shutdown_controller(&control_handle, e));
                }
                fcrunner::ComponentControllerRequest::Kill { control_handle } => {
                    nested_controller
                        .kill()
                        .unwrap_or_else(|e| shutdown_controller(&control_handle, e));
                }
            },
            ProxyEvent::ClientRequest(Err(_)) => {
                // channel to client is broken - no need to do any more work.
                break;
            }
            ProxyEvent::ServerEvent(Ok(
                fcrunner::ComponentControllerEvent::OnPublishDiagnostics { payload },
            )) => {
                let _ = request_stream_control.send_on_publish_diagnostics(payload);
            }
            ProxyEvent::ServerEvent(Err(e)) => {
                // Dart runner appears to close this channel with an epitaph while
                // fuchsia.test.Suite is still running. To allow this work to complete,
                // we'll ignore the error here and instead report the epitaph when
                // component_manager calls Stop or Kill before tearing down the nested
                // runner.
                info!("server closed channel: {:?}", e);
            }
        }
    }
    let _ = nested_controller.on_closed().await;
    Ok(())
}

fn shutdown_controller<T: ControlHandle>(controller: &T, err: fidl::Error) {
    match err {
        fidl::Error::ClientChannelClosed { status, .. } if status != zx::Status::PEER_CLOSED => {
            controller.shutdown_with_epitaph(status);
        }
        _ => {
            controller.shutdown();
        }
    }
}
