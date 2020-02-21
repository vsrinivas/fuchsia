// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_io as io, fidl_fuchsia_io_test as io_test, fuchsia_async as fasync,
    futures::StreamExt,
    io_conformance::{
        io1_harness_receiver::Io1HarnessReceiver,
        io1_request_logger_factory::Io1RequestLoggerFactory,
    },
    test_utils_lib::test_utils::BlackBoxTest,
};

// Creates a BlackBoxTest component from the |url|, listens for the child component to connect
// to a HarnessReceiver injector, and returns the connected HarnessProxy.
async fn setup_harness_connection(
    url: String,
) -> Result<(io_test::Io1HarnessProxy, BlackBoxTest), Error> {
    let test = BlackBoxTest::default(&url)
        .await
        .context(format!("Cannot create BlackBoxTest with url {}", &url))?;
    let event_source =
        test.connect_to_event_source().await.context("Cannot connect to event source.")?;

    // Install HarnessReceiver injector for the child component to connect and to send
    // the harness to run the test through.
    let (capability, mut rx) = Io1HarnessReceiver::new();
    let injector =
        event_source.install_injector(capability).await.context("Cannot install injector.")?;

    event_source.start_component_tree().await?;

    // Wait for the injector to receive the TestHarness connection from the child component
    // before continuing.
    let harness = rx.next().await.unwrap();
    let harness = harness.into_proxy()?;

    injector.abort();

    Ok((harness, test))
}

// Example test to start up a v2 component harness to test when opening a path that goes through a
// remote mount point, the server forwards the request to the remote correctly.
#[fasync::run_singlethreaded(test)]
async fn open_remote_directory_test() {
    let (harness, _test) = setup_harness_connection(
        "fuchsia-pkg://fuchsia.com/io_fidl_conformance_tests#meta/io_conformance_harness_ulibfs.cm"
            .to_string(),
    )
    .await
    .expect("Could not setup harness connection.");

    let (remote_dir_client, remote_dir_server) =
        fidl::endpoints::create_endpoints::<io::DirectoryMarker>()
            .expect("Cannot create endpoints.");

    let remote_name = "remote_directory";

    // Request an extra directory connection from the harness to use as the remote,
    // and interpose the requests from the server under test to this remote.
    let (logger, mut rx) = Io1RequestLoggerFactory::new();
    let remote_dir_server =
        logger.get_logged_directory(remote_name.to_string(), remote_dir_server).await;
    harness
        .get_empty_directory(io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE, remote_dir_server)
        .expect("Cannot get empty remote directory.");

    let (test_dir_proxy, test_dir_server) =
        fidl::endpoints::create_proxy::<io::DirectoryMarker>().expect("Cannot create proxy.");
    harness
        .get_directory_with_remote_directory(
            remote_dir_client,
            remote_name,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
            test_dir_server,
        )
        .expect("Cannot get test harness directory.");

    let (_remote_dir_proxy, remote_dir_server) =
        fidl::endpoints::create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
    test_dir_proxy
        .open(io::OPEN_RIGHT_READABLE, io::MODE_TYPE_DIRECTORY, remote_name, remote_dir_server)
        .expect("Cannot open remote directory.");

    // Wait on an open call to the interposed remote directory.
    let open_request_string = rx.next().await.expect("Local tx/rx channel was closed");

    // TODO(fxb/45613):: Bare-metal testing against returned request string. We need
    // to find a more ergonomic return format.
    assert_eq!(open_request_string, "remote_directory flags:1, mode:16384, path:.");
}
