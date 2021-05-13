// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    diagnostics_reader::{ArchiveReader, Logs},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client as fclient,
    futures::StreamExt,
};

#[fuchsia::test]
async fn hello_world_integration_test() -> Result<(), Error> {
    // Connect to the realm service, so that we can bind to and interact with child components
    let realm_proxy = fclient::realm()?;

    // The hello world component will connect to the /svc/fuchsia.logger.LogSink protocol provided
    // by the archivist. Let's connect to the archivist from this test to read the logs written by
    // the hello world component. We use the `ArchiveReader` library which simplifies interacting
    // with the `ArchiveAccessor` protocol for simple cases like this one. We start watching for
    // logs before starting the hello_world component although given that we are using the
    // `SNAPSHOT_THEN_SUBSCRIBE` mode, technically we could start listening after starting the
    // component as well and not miss logs.
    let reader = ArchiveReader::new();
    let log_stream = reader.snapshot_then_subscribe::<Logs>()?;

    // Now that we're connected to the log server, let's use the fuchsia.sys2.Realm protocol to
    // manually bind to our hello_world child, which will cause it to start. Once started, the
    // hello_world component will connect to the observer component to send its hello world log
    // message
    let (_hello_world_proxy, hello_world_server_end) = create_proxy::<DirectoryMarker>()?;
    realm_proxy
        .bind_child(
            &mut fsys::ChildRef { name: "hello_world".to_string(), collection: None },
            hello_world_server_end,
        )
        .await?
        .map_err(|e| anyhow!("failed to bind to hello_world: {:?}", e))?;

    // We should see two log messages, one that states that logging started and the hello world
    // message we're expecting.
    let logs = log_stream.take(1).next().await.expect("got log result")?;
    assert_eq!(logs.msg().unwrap(), "Hippo: Hello World!");

    Ok(())
}
