// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::lib::{EchoClientTest, Mocks};
use anyhow::Error;
use async_trait::async_trait;
use fidl_fidl_examples_routing_echo::*;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::LocalComponentHandles;
use futures::{StreamExt, TryStreamExt};

mod lib;

#[async_trait]
impl Mocks for EchoClientTest {
    async fn echo_impl(handles: LocalComponentHandles) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let mut tasks = vec![];

        fs.dir("svc").add_fidl_service(move |mut stream: EchoRequestStream| {
            tasks.push(fasync::Task::local(async move {
                while let Some(EchoRequest::EchoString { value, responder }) =
                    stream.try_next().await.expect("failed to serve echo service")
                {
                    responder
                        .send(value.as_ref().map(|s| &**s))
                        .expect("failed to send echo response");
                }
            }));
        });
        fs.serve_connection(handles.outgoing_dir).unwrap();
        fs.collect::<()>().await;
        Ok(())
    }
}

#[fuchsia::test]
async fn test_echomarker() {
    let instance = EchoClientTest::create_realm().await.expect("setting up test realm");
    let proxy = instance
        .root
        .connect_to_protocol_at_exposed_dir::<EchoMarker>()
        .expect("connecting to Echo");
    assert_eq!(
        Some("Hello Fuchsia".to_string()),
        proxy.echo_string(Some("Hello Fuchsia")).await.unwrap()
    );
}

#[fuchsia::test]
async fn test_echomarker2() {
    let instance = EchoClientTest::create_realm().await.expect("setting up test realm");
    let proxy = instance
        .root
        .connect_to_protocol_at_exposed_dir::<EchoMarker>()
        .expect("connecting to Echo");
    assert_eq!(
        Some("Hello Fuchsia2".to_string()),
        proxy.echo_string(Some("Hello Fuchsia2")).await.unwrap()
    );
}
