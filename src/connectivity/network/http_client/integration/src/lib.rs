// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_net_oldhttp as oldhttp, fuchsia_async as fasync,
    fuchsia_component::client::{launch, launcher},
    fuchsia_component::fuchsia_single_component_package_url,
    rouille::{self, router, Request, Response},
    std::thread,
};

const SERVER_IP: &str = "[::]";
const ROOT_DOCUMENT: &str = "Root document\n";

pub fn serve_request(request: &Request) -> Response {
    router!(request,
        (GET) (/) => {
            rouille::Response::text(ROOT_DOCUMENT)
        },
        _ => {
            rouille::Response::text("File not found\n").with_status_code(404)
        }
    )
}

pub fn start_test_server() -> Result<u16, Error> {
    let address = format!("{}:0", SERVER_IP);
    let server =
        rouille::Server::new(address, serve_request).expect("failed to create rouille server");
    let port = server.server_addr().port();
    thread::Builder::new().name("test-server".into()).spawn(move || {
        server.run();
    })?;

    Ok(port)
}

#[fasync::run_singlethreaded(test)]
async fn test_new_client() -> Result<(), Error> {
    test_oldhttp(fuchsia_single_component_package_url!("http_client"), false).await
}

#[fasync::run_singlethreaded(test)]
async fn test_old_client() -> Result<(), Error> {
    test_oldhttp(fuchsia_single_component_package_url!("http"), true).await
}

async fn test_oldhttp(package_path: &str, check_body: bool) -> Result<(), Error> {
    let server_port = start_test_server()?;

    let launcher = launcher().context("Failed to open launcher service")?;
    let http_client = launch(&launcher, package_path.to_string(), None)
        .context("Failed to launch http_client")?;

    let (loader, loader_server) = create_proxy::<oldhttp::UrlLoaderMarker>()?;
    let service = http_client.connect_to_service::<oldhttp::HttpServiceMarker>()?;
    service.create_url_loader(loader_server)?;

    let mut request = oldhttp::UrlRequest {
        url: format!("http://127.0.0.1:{}/", server_port).to_string(),
        method: "GET".to_string(),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        cache_mode: oldhttp::CacheMode::Default,
        response_body_mode: oldhttp::ResponseBodyMode::Buffer,
        auto_follow_redirects: false,
    };
    let response = loader.start(&mut request).await?;

    assert_eq!(response.status_code, 200);
    let expected_header_names = ["server", "date", "content-type", "content-length"];
    // If the webserver started above ever returns different headers, or changes the order, this
    // assertion will fail. Note that case doesn't matter, and can vary across HTTP client
    // implementations, so we lowercase all the header keys before checking.
    let response_headers: Vec<String> =
        response.headers.unwrap().iter().map(|h| h.name.to_lowercase()).collect();
    assert_eq!(&response_headers, &expected_header_names);

    if check_body {
        match *response.body.unwrap() {
            oldhttp::UrlBody::Buffer(b) => {
                // Temporary check until we can use const fn when defining buf below.
                assert_eq!(ROOT_DOCUMENT.len(), 14);
                assert_eq!(b.size, ROOT_DOCUMENT.len() as u64);
                let mut buf = [0; 14];
                b.vmo.read(&mut buf, 0).unwrap();
                assert_eq!(&buf, ROOT_DOCUMENT.as_bytes());
            }
            oldhttp::UrlBody::Stream(_) => panic!("Unexpected stream for response body"),
        }
    }

    Ok(())
}
