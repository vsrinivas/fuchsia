// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test;

use {
    anyhow::*,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_io as fio, fidl_fuchsia_test as ftest,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    log::{error, info},
    test::StressTest,
};

#[fuchsia::component]
async fn main() -> Result<()> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(serve_runner(stream).map(|r| info!("Serving runner: {:?}", r)))
            .detach();
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

async fn serve_runner(mut stream: fcrunner::ComponentRunnerRequestStream) -> Result<()> {
    while let Some(fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. }) =
        stream.try_next().await?
    {
        let controller_stream = controller.into_stream()?;
        fasync::Task::spawn(
            serve_controller(controller_stream).map(|r| info!("Serving controller: {:?}", r)),
        )
        .detach();

        let dictionary = start_info.program.ok_or(anyhow!("No program dictionary"))?;
        let dictionary = dictionary.entries.ok_or(anyhow!("No entries in program dictionary"))?;
        let namespace = start_info.ns.ok_or(anyhow!("No incoming namespace"))?;
        let test = StressTest::new(dictionary, namespace)?;
        let out_dir = start_info.outgoing_dir.ok_or(anyhow!("No outgoing directory"))?;
        fasync::Task::spawn(
            serve_out_dir(out_dir, test).map(|r| info!("Serving out dir: {:?}", r)),
        )
        .detach();
    }
    Ok(())
}

/// TODO(xbhatnag): All futures should be aborted when a Stop/Kill request is received.
async fn serve_controller(mut stream: fcrunner::ComponentControllerRequestStream) -> Result<()> {
    if let Some(request) = stream.try_next().await? {
        info!("Received controller request: {:?}", request);
    }
    Ok(())
}

async fn serve_out_dir(out_dir: ServerEnd<fio::DirectoryMarker>, test: StressTest) -> Result<()> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(
            serve_test_suite(stream, test.clone()).map(|r| info!("Serving test suite: {:?}", r)),
        )
        .detach();
    });
    fs.serve_connection(out_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

/// Implements `fuchsia.test.Suite` service and runs test.
async fn serve_test_suite(mut stream: ftest::SuiteRequestStream, test: StressTest) -> Result<()> {
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::SuiteRequest::GetTests { iterator, control_handle: _ } => {
                fasync::Task::spawn(
                    serve_case_iterator(iterator).map(|r| info!("Serving case iterator: {:?}", r)),
                )
                .detach();
            }
            ftest::SuiteRequest::Run { tests, listener, .. } => {
                let listener = listener.into_proxy()?;

                for invocation in tests {
                    let (case_listener, server_end) = create_proxy::<ftest::CaseListenerMarker>()?;

                    // TODO(84852): Use stderr to print status of actors
                    let (stderr_client, stderr_server) =
                        zx::Socket::create(zx::SocketOpts::DATAGRAM)?;
                    let std_handles = ftest::StdHandles {
                        out: None,
                        err: Some(stderr_server),
                        ..ftest::StdHandles::EMPTY
                    };
                    listener.on_test_case_started(invocation, std_handles, server_end)?;

                    let result = if let Err(e) = test.clone().start().await {
                        if let Err(status) = stderr_client.write(e.to_string().as_bytes()) {
                            error!(
                                "Failed to write error to stderr socket [write status={}][error={:?}]",
                                status, e
                            )
                        }
                        ftest::Result_ {
                            status: Some(ftest::Status::Failed),
                            ..ftest::Result_::EMPTY
                        }
                    } else {
                        ftest::Result_ {
                            status: Some(ftest::Status::Passed),
                            ..ftest::Result_::EMPTY
                        }
                    };

                    case_listener.finished(result)?;
                }
                listener.on_finished()?;
            }
        }
    }
    Ok(())
}

async fn serve_case_iterator(iterator: ServerEnd<ftest::CaseIteratorMarker>) -> Result<()> {
    let mut stream = iterator.into_stream()?;
    let cases = vec![ftest::Case {
        name: Some("stress_test".to_string()),
        enabled: Some(true),
        ..ftest::Case::EMPTY
    }];
    let mut iter = cases.into_iter();

    // Send the `stress_test` case in the first call
    if let Some(ftest::CaseIteratorRequest::GetNext { responder }) = stream.try_next().await? {
        responder.send(&mut iter)?;
    }

    // Send an empty response in the second call. This instructs the test_manager that there are
    // no more cases in this test suite.
    if let Some(ftest::CaseIteratorRequest::GetNext { responder }) = stream.try_next().await? {
        responder.send(&mut vec![].into_iter())?;
    }
    Ok(())
}
