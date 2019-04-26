// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![cfg(test)]

use failure::{Error, ResultExt};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_net_oldhttp as oldhttp;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};
use rouille::{self, router, Request, Response};
use std::thread;

const SERVER_IP: &str = "[::]";
const SERVER_PORT: &str = "5646";

pub fn serve_request(request: &Request) -> Response {
    router!(request,
        (GET) (/) => {
            rouille::Response::text("Root document\n")
        },
        _ => {
            rouille::Response::text("File not found\n").with_status_code(404)
        }
    )
}

pub fn start_test_server() -> Result<(), Error> {
    let address = format!("{}:{}", SERVER_IP, SERVER_PORT);
    thread::Builder::new().name("test-server".into()).spawn(move || {
        rouille::start_server(address, serve_request);
    })?;

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn test_oldhttp() -> Result<(), Error> {
    start_test_server()?;

    let launcher = launcher().context("Failed to open launcher service")?;
    let http_client = launch(
        &launcher,
        "fuchsia-pkg://fuchsia.com/http_client#meta/http_client.cmx".to_string(),
        None,
    )
    .context("Failed to launch http_client")?;

    let (loader, loader_server) = create_proxy::<oldhttp::UrlLoaderMarker>()?;
    let service = http_client.connect_to_service::<oldhttp::HttpServiceMarker>()?;
    service.create_url_loader(loader_server)?;

    let mut request = oldhttp::UrlRequest {
        url: "http://127.0.0.1:5646/".to_string(),
        method: "GET".to_string(),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        cache_mode: oldhttp::CacheMode::Default,
        response_body_mode: oldhttp::ResponseBodyMode::Buffer,
        auto_follow_redirects: false,
    };
    let response = await!(loader.start(&mut request))?;

    assert_eq!(response.status_code, 200);
    // We can't check the body yet because the service doesn't return it.

    Ok(())
}
