// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_developer_ffx as ffx,
    fidl_fuchsia_sl4f_ffx::{
        Sl4fBridgeExecuteResponder, Sl4fBridgeRequest, Sl4fBridgeRequestStream,
    },
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    hyper::{Body, Request},
};

// Compose the request containing the given value.
async fn compose(value: String) -> Result<Request<Body>, Error> {
    // Use port 80 because that's the port used by the SL4F server.
    let req = Request::post("http://[::1]:80")
        .header("content-type", "application/json")
        .body(Body::from(value))?;
    Ok(req)
}

// Convert the given Body into a String.
async fn body_to_string(body: Body) -> Result<String, Error> {
    let bytes = body
        .try_fold(Vec::new(), |mut vec, b| async move {
            vec.extend(b);
            Ok(vec)
        })
        .await
        .unwrap();
    Ok(String::from_utf8(bytes)?)
}

async fn execute(
    _target_query: ffx::TargetQuery,
    value: String,
    responder: Sl4fBridgeExecuteResponder,
) -> Result<(), Error> {
    let http_client = fuchsia_hyper::new_client();
    let req = compose(value).await?;
    let response = http_client.request(req).await?;
    assert_eq!(response.status(), hyper::StatusCode::OK);
    let resp_to_send = body_to_string(response.into_body()).await?;
    responder.send(&resp_to_send).context("error sending response")
}

// This handler runs on the device and sends requests to an unmodified SL4F component.
async fn bridge_handler(stream: Sl4fBridgeRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            let Sl4fBridgeRequest::Execute { target_query, value, responder } = request;
            println!("  bridge_handler() on {:?}", &target_query.string_matcher);
            execute(target_query, value, responder).await?;
            Ok(())
        })
        .await
}

enum IncomingService {
    Sl4fBridge(Sl4fBridgeRequestStream),
}

#[fuchsia::main(logging_tags = ["sl4f_server"])]
async fn main() -> Result<(), Error> {
    println!("Starting the host-side server");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Sl4fBridge);
    fs.take_and_serve_directory_handle()?;

    // Listen for incoming requests to connect to Sl4fBridge, and send to bridge_handler.
    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Sl4fBridge(stream)| {
        bridge_handler(stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        crate::body_to_string,
        crate::compose,
        anyhow::{self, Error},
        fidl_fuchsia_developer_ffx as ffx,
        fidl_fuchsia_sl4f_ffx::{Sl4fBridgeMarker, Sl4fBridgeRequest, Sl4fBridgeRequestStream},
        fuchsia_async::{self as fasync},
        fuchsia_component::server as fserver,
        fuchsia_component_test::{
            Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
        },
        futures::{StreamExt, TryStreamExt},
        hyper::{Body, Method},
    };

    async fn server_mock(handles: LocalComponentHandles) -> Result<(), Error> {
        // Create a new ServiceFs to host FIDL protocols from.
        let mut fs = fserver::ServiceFs::new();
        let mut tasks = vec![];

        // Add the echo protocol to the ServiceFs
        fs.dir("svc").add_fidl_service(move |mut stream: Sl4fBridgeRequestStream| {
            tasks.push(fasync::Task::local(async move {
                while let Some(req) =
                    stream.try_next().await.expect("failed to serve Sl4fBridge service")
                {
                    let Sl4fBridgeRequest::Execute { target_query: _, value, responder } = req;
                    responder.send(&value).expect("failed to send response");
                }
            }));
        });

        // Run the ServiceFs on the outgoing directory handle from the mock handles
        fs.serve_connection(handles.outgoing_dir.into_channel())?;
        fs.collect::<()>().await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn compose_test() {
        let req = compose("foo".to_string()).await.unwrap();
        assert_eq!(req.uri(), "http://[::1]:80/");
        assert_eq!(req.method(), Method::POST);
        assert_eq!(req.headers().len(), 1);
        assert_eq!(req.headers()["content-type"], "application/json");
        assert_eq!(&body_to_string(req.into_body()).await.unwrap(), "foo");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn body_to_string_test() {
        assert_eq!(&body_to_string(Body::from("")).await.unwrap(), "");
        assert_eq!(&body_to_string(Body::from("foo")).await.unwrap(), "foo");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn server_test() {
        let builder = RealmBuilder::new().await.unwrap();
        let echo_server = builder
            .add_local_child(
                "echo_server",
                move |handles: LocalComponentHandles| Box::pin(server_mock(handles)),
                ChildOptions::new(),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.sl4f.ffx.Sl4fBridge"))
                    .from(&echo_server)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        let realm = builder.build().await.unwrap();
        let server = realm.root.connect_to_protocol_at_exposed_dir::<Sl4fBridgeMarker>().unwrap();
        assert_eq!(
            server.execute(ffx::TargetQuery::EMPTY, "foo").await.unwrap(),
            "foo".to_string()
        );
    }
}
