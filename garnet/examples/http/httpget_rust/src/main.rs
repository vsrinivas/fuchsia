// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_net_http as http, fuchsia_async as fasync,
    fuchsia_component as component, fuchsia_zircon as zx, futures::io::AllowStdIo,
};

fn print_headers(resp: &http::Response) {
    println!(">>> Headers <<<");
    if let Some(status) = &resp.status_line {
        println!("  {:?}", status);
    }
    if let Some(hdrs) = &resp.headers {
        for hdr in hdrs {
            println!("  {:?}={:?}", hdr.name, hdr.value);
        }
    }
}

fn main() -> Result<(), Error> {
    let url = match std::env::args().nth(1) {
        Some(url) => {
            if url.find("://").is_none() {
                ["http://", &url].concat()
            } else {
                url
            }
        }
        None => {
            println!("usage: {} <url>", std::env::args().nth(0).unwrap());
            return Ok(());
        }
    };

    // Set up async executor
    let mut exec = fasync::Executor::new()?;

    //// Run the future to completion
    exec.run_singlethreaded(http_get(url))
}

/// Connects to the http service, sends a url request, and prints the response.
async fn http_get(url: String) -> Result<(), Error> {
    // Connect to the service.
    let loader_proxy = component::client::connect_to_service::<http::LoaderMarker>()?;

    // Send the request.
    let req = http::Request {
        url: Some(url),
        method: Some(String::from("GET")),
        headers: None,
        body: None,
        deadline: None,
        ..http::Request::empty()
    };

    let resp = loader_proxy.fetch(req).await?;
    if let Some(e) = resp.error {
        println!("Got error: {:?}", e);
        return Ok(());
    }
    print_headers(&resp);

    let socket = match resp.body {
        Some(s) => fasync::Socket::from_socket(s)?,
        None => return Err(Error::from(zx::Status::BAD_STATE)),
    };

    // stdout is blocking, but we'll pretend it's okay
    println!(">>> Body <<<");

    // Copy the bytes from the socket to stdout
    let mut stdio = AllowStdIo::new(std::io::stdout());
    futures::io::copy(socket, &mut stdio).await?;
    println!("\n>>> EOF <<<");

    Ok(())
}
