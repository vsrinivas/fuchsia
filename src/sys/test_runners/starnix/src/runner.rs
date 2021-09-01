// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test_suite::handle_suite_requests,
    anyhow::{anyhow, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_data as fdata,
    fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    runner::component::ComponentNamespace,
    std::convert::TryFrom,
};

/// The services exposed in the outgoing directory for the test component.
enum TestComponentExposedServices {
    Suite(ftest::SuiteRequestStream),
}

/// Handles a `fcrunner::ComponentRunnerRequestStream`.
///
/// When a run request arrives, the test suite protocol is served in the test component's outgoing
/// namespace, and then the component is run in response to `ftest::SuiteRequest::Run` requests.
///
/// See `test_suite` for more on how the test suite requests are handled.
pub async fn handle_runner_requests(
    mut request_stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                let outgoing_dir_channel = start_info
                    .outgoing_dir
                    .ok_or(anyhow!("Missing outgoing directory."))?
                    .into_channel();
                let component_url =
                    start_info.resolved_url.ok_or(anyhow!("Missing resolved URL."))?;
                let program = start_info.program.ok_or(anyhow!("Missing program information."))?;
                let namespace = ComponentNamespace::try_from(
                    start_info.ns.ok_or(anyhow!("Missing namespace."))?,
                )?;

                fasync::Task::local(async move {
                    match serve_test_suite(
                        &component_url,
                        program,
                        outgoing_dir_channel,
                        namespace,
                        controller,
                    )
                    .await
                    {
                        Ok(_) => fuchsia_syslog::fx_log_info!(
                            "Finished serving test suite for component."
                        ),
                        Err(e) => fuchsia_syslog::fx_log_err!("Error serving test suite: {:?}", e),
                    }
                })
                .detach();
            }
        }
    }

    Ok(())
}

/// Serves a `ftest::SuiteRequestStream` from `directory_channel`.
///
/// This function is used to serve a `ftest::SuiteRequestStream` in the outgoing directory of a test
/// component. This is what the test framework connects to to run test cases.
///
/// When the returned future completes, the outgoing directory has finished serving requests.
///
/// # Parameters
/// - `test_url`: The URL of the test component.
/// - `program`: The program data associated with the run request for the test component.
/// - `outgoing_dir_channel`: The channel for the directory to serve the test suite protocol from.
/// - `namespace`: The incoming namespace to provide to the test component.
/// - `controller`: The component controller associated with the test component.
async fn serve_test_suite(
    test_url: &str,
    program: fdata::Dictionary,
    outgoing_dir_channel: fidl::Channel,
    namespace: ComponentNamespace,
    controller: ServerEnd<fcrunner::ComponentControllerMarker>,
) -> Result<(), Error> {
    let test_url = test_url.to_string();

    let mut outgoing_dir = ServiceFs::new_local();
    outgoing_dir.dir("svc").add_fidl_service(TestComponentExposedServices::Suite);
    outgoing_dir.serve_connection(outgoing_dir_channel)?;

    if let Some(service_request) = outgoing_dir.next().await {
        match service_request {
            TestComponentExposedServices::Suite(stream) => {
                let test_url = test_url.to_string();
                let program = program.clone();
                match handle_suite_requests(&test_url, Some(program), namespace.clone(), stream)
                    .await
                {
                    Ok(_) => fuchsia_syslog::fx_log_info!("Finished serving test suite requests."),
                    Err(e) => {
                        fuchsia_syslog::fx_log_err!("Error serving test suite requsets: {:?}", e)
                    }
                }
                let _ = controller.close_with_epitaph(zx::Status::OK);
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy, DiscoverableProtocolMarker},
        fidl_fuchsia_component_runner::ComponentControllerMarker,
        fidl_fuchsia_io as fio, fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    };

    async fn list_directory(root_proxy: &fio::DirectoryProxy) -> Vec<String> {
        let entries =
            files_async::readdir(&root_proxy).await.expect("Couldn't read listed directory.");
        let items = entries.into_iter().map(|entry| entry.name).collect::<Vec<String>>();
        items
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_serving_test_suite_in_svc() {
        let (directory_proxy, directory_server) =
            create_proxy::<fio::DirectoryMarker>().expect("Couldn't create case iterator.");
        let (_, controller) = create_proxy::<ComponentControllerMarker>().unwrap();
        fasync::Task::local(async move {
            let _ = serve_test_suite(
                "test",
                fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY },
                directory_server.into_channel(),
                ComponentNamespace::try_from(vec![]).unwrap(),
                controller,
            )
            .await;
        })
        .detach();

        let svc_path = std::path::Path::new("svc");
        let svc_dir = io_util::open_directory(&directory_proxy, svc_path, fio::OPEN_RIGHT_READABLE)
            .expect("Couldn't open svc.");
        let svc_contents = list_directory(&svc_dir).await;
        assert_eq!(svc_contents, vec![ftest::SuiteMarker::PROTOCOL_NAME]);
    }
}
