// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    async_utils::stream::{StreamItem, WithEpitaph},
    fidl::endpoints::{create_proxy, ControlHandle, RequestStream},
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_protocol, connect_to_protocol_at_dir_svc},
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

/// Proxy a ComponentController stream to a nested runner.
/// Returns true iff Stop or Kill was called before the proxy terminated.
async fn run_component_controller(
    nested_runner: &ScopedInstance,
    start_info: fcrunner::ComponentStartInfo,
    request_stream: fcrunner::ComponentControllerRequestStream,
) -> Result<(), Error> {
    let is_test = component_is_test(&start_info);

    let relative_moniker = format!("./{}:{}", RUNNER_COLLECTION, nested_runner.child_name());

    let controller = connect_to_protocol::<fsys::LifecycleControllerMarker>()?;
    controller.start(&relative_moniker).await?.expect("nested runer could not be started");

    let realm_query = connect_to_protocol::<fsys::RealmQueryMarker>()?;
    let resolved_dirs = realm_query
        .get_instance_directories(&relative_moniker)
        .await?
        .unwrap()
        .expect("nested runner was not resolved");
    let started_dirs = resolved_dirs.started_dirs.expect("nested runner was not started");
    let out_dir = started_dirs.out_dir.expect("nested runner does not have out dir");
    let out_dir = out_dir.into_proxy().unwrap();

    let component_runner =
        connect_to_protocol_at_dir_svc::<fcrunner::ComponentRunnerMarker>(&out_dir)?;
    let (nested_controller, server_end) = create_proxy::<fcrunner::ComponentControllerMarker>()?;
    component_runner.start(start_info, server_end)?;
    proxy_component_controller(nested_controller, request_stream, is_test).await
}

async fn proxy_component_controller(
    nested_controller: fcrunner::ComponentControllerProxy,
    request_stream: fcrunner::ComponentControllerRequestStream,
    is_test: bool,
) -> Result<(), Error> {
    let nested_controller_events = nested_controller.take_event_stream();
    let request_stream_control = request_stream.control_handle();

    enum ProxyEvent<T, U> {
        ClientRequest(T),
        ServerEvent(U),
    }

    let mut combined_stream = futures::stream::select(
        request_stream.with_epitaph(()).map(ProxyEvent::ClientRequest),
        nested_controller_events.map(ProxyEvent::ServerEvent),
    );

    let mut stop_or_kill_called = false;

    while let Some(event) = combined_stream.next().await {
        match event {
            ProxyEvent::ClientRequest(StreamItem::Epitaph(()))
            | ProxyEvent::ClientRequest(StreamItem::Item(Err(_))) => {
                // channel to client is broken - no need to do any more work.
                break;
            }
            ProxyEvent::ClientRequest(StreamItem::Item(Ok(request))) => match request {
                fcrunner::ComponentControllerRequest::Stop { control_handle } => {
                    nested_controller
                        .stop()
                        .unwrap_or_else(|e| shutdown_controller(&control_handle, e));
                    stop_or_kill_called = true;
                }
                fcrunner::ComponentControllerRequest::Kill { control_handle } => {
                    nested_controller
                        .kill()
                        .unwrap_or_else(|e| shutdown_controller(&control_handle, e));
                    stop_or_kill_called = true;
                }
            },

            ProxyEvent::ServerEvent(Ok(
                fcrunner::ComponentControllerEvent::OnPublishDiagnostics { payload },
            )) => {
                let _ = request_stream_control.send_on_publish_diagnostics(payload);
            }
            ProxyEvent::ServerEvent(Err(e)) => {
                // The real Dart runner appears to close the controller with an epitaph
                // before fuchsia.test.Suite completes running. Because of this, in the
                // case of tests, killing the runner immediately after the proxy terminates
                // can kill fuchsia.test.Suite, preventing test_manager from retrieving
                // test results.
                // To handle this case, for tests only we ignore the signal and wait for
                // stop or kill to be called instead. Note that this could cause timeouts
                // during destruction if a test crashes, but this will likely be reported
                // over fuchsia.test.Suite.
                if !is_test || stop_or_kill_called {
                    shutdown_controller(&request_stream_control, e);
                }
            }
        }
    }
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

fn component_is_test(start_info: &fcrunner::ComponentStartInfo) -> bool {
    // The dart runner identifies tests using an --is_test=true argument passed in via the program
    // definition.
    const TEST_ARG: &str = "--is_test=true";
    let args = match runner::get_program_args(start_info) {
        Ok(args) => args,
        // allow the underlying runner to handle any errors.
        Err(_) => return false,
    };
    args.iter().any(|arg| arg == TEST_ARG)
}

#[cfg(test)]
mod test {
    use {
        super::*, assert_matches::assert_matches, fidl::endpoints::create_proxy_and_stream,
        futures::task::Poll,
    };

    #[fuchsia::test]
    async fn closing_client_closes_server() {
        let (client_proxy, client_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .expect("create stream");
        let (server_proxy, mut server_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .expect("create stream");

        let task = fasync::Task::spawn(proxy_component_controller(
            server_proxy,
            client_request_stream,
            false,
        ));

        drop(client_proxy);
        assert!(server_request_stream.next().await.is_none());
        task.await.expect("proxy future failed");
    }

    #[fuchsia::test]
    async fn closing_server_closes_client_if_not_test() {
        let (client_proxy, client_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .expect("create stream");
        let (server_proxy, mut server_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .expect("create stream");

        let task = fasync::Task::spawn(proxy_component_controller(
            server_proxy,
            client_request_stream,
            false,
        ));

        server_request_stream.control_handle().shutdown_with_epitaph(fidl::Status::OK);
        assert!(server_request_stream.next().await.is_none());

        assert_matches!(
            client_proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::OK, .. })
        );
        task.await.expect("proxy future failed");
    }

    #[fuchsia::test]
    fn closing_server_does_not_close_client_if_test() {
        let mut executor = fasync::TestExecutor::new().unwrap();

        let (client_proxy, client_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .expect("create stream");
        let (server_proxy, mut server_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .expect("create stream");

        let mut proxy_fut =
            proxy_component_controller(server_proxy, client_request_stream, true).boxed();

        server_request_stream.control_handle().shutdown_with_epitaph(fidl::Status::OK);
        assert_matches!(
            executor.run_until_stalled(&mut server_request_stream.next()),
            Poll::Ready(None)
        );
        drop(server_request_stream);

        assert_matches!(executor.run_until_stalled(&mut proxy_fut), Poll::Pending);

        let mut event_stream = client_proxy.take_event_stream();
        assert_matches!(executor.run_until_stalled(&mut event_stream.next()), Poll::Pending);

        // After calling stop or kill, the client channel should close.
        client_proxy.stop().expect("stop should succeed");
        assert_matches!(executor.run_until_stalled(&mut proxy_fut), Poll::Ready(Ok(())));
        assert_matches!(
            executor.run_until_stalled(&mut event_stream.next()),
            Poll::Ready(Some(Err(fidl::Error::ClientChannelClosed {
                status: fidl::Status::OK,
                ..
            })))
        );
    }
}
