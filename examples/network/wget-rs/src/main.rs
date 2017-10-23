// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(box_patterns)]

extern crate fidl;
extern crate fuchsia_app;
extern crate fuchsia_zircon as zircon;
extern crate garnet_public_lib_network_fidl;
extern crate tokio_core;

use fidl::FidlService;
use fuchsia_app::client::ApplicationContext;
use garnet_public_lib_network_fidl as netsvc;
use tokio_core::reactor;
use zircon::{AsHandleRef, ZX_SOCKET_READABLE, ZX_SOCKET_PEER_CLOSED, ZX_TIME_INFINITE};

fn print_response(resp: netsvc::URLResponse) -> Result<(), zircon::Status> {
    if let Some(e) = resp.error {
        let code = e.code;
        println!(
            "Got error: {} ({})",
            code,
            e.description.unwrap_or("".into())
        );
        return Ok(());
    }

    print_headers(&resp);

    match resp.body.map(|x| *x) {
        Some(netsvc::URLBody::Stream(ref s)) => print_body(s)?,
        Some(_) => println!("Unexpected URLBody type!"),
        None => (),
    }
    Ok(())
}

fn print_headers(resp: &netsvc::URLResponse) {
    println!(">>> Headers <<<");
    if let Some(ref status) = resp.status_line {
        println!("  {}", status);
    }
    if let Some(ref hdrs) = resp.headers {
        for hdr in hdrs {
            println!("  {}={}", hdr.name, hdr.value);
        }
    }
}

fn print_body(sock: &zircon::Socket) -> Result<(), zircon::Status> {
    println!(">>> Body <<<");

    // TODO(tkilbourn): this is doing blocking work, rather than using the tokio reactor. Once
    // tokio-fuchsia supports zircon sockets, rewrite this method to use futures instead.
    const BUFSIZE: usize = 4096;
    let wait_sigs = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED;
    let mut buf: [u8; BUFSIZE] = [0; BUFSIZE];
    loop {
        match sock.read(zircon::SocketReadOpts::Default, &mut buf) {
            Ok(num_read) => print!("{}", String::from_utf8_lossy(&buf[..num_read])),
            Err(e) => {
                match e {
                    zircon::Status::ErrShouldWait => {
                        let _ = sock.as_handle_ref().wait(wait_sigs, ZX_TIME_INFINITE)?;
                    }
                    zircon::Status::ErrPeerClosed => break,  // not an error
                    _ => {
                        println!("\nUnexpected error reading response {:?}", e);
                        break;
                    }
                }
            }
        }
    }

    println!("\n>>> EOF <<<");
    Ok(())
}

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

/// Connects to the network service, sends a url request, and prints the response.
fn main_res() -> Result<(), fidl::Error> {
    let url = match std::env::args().nth(1) {
        Some(url) => url,
        None => {
            println!("usage: {} <url>", std::env::args().nth(0).unwrap());
            return Ok(());
        }
    };

    // Set up tokio reactor
    let mut core = reactor::Core::new().unwrap();
    let handle = core.handle();

    // Connect to the network service
    let app_context = ApplicationContext::new(&handle)?;
    let net = app_context
        .connect_to_service::<netsvc::NetworkService::Service>(&handle)?;

    // Create a URLLoader instance
    let (loader_proxy, loader_server) = netsvc::URLLoader::Service::new_pair(&handle)?;
    net.create_url_loader(loader_server)?;

    // Send the URLRequest to fetch the webpage
    let req = netsvc::URLRequest {
        url: url,
        method: String::from("GET"),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        auto_follow_redirects: true,
        cache_mode: netsvc::URLRequestCacheMode::Default,
        response_body_mode: netsvc::URLRequestResponseBodyMode::Stream,
    };
    let response_fut = loader_proxy.start(req);

    //// Run `response_fut` to completion and print the response
    let response = core.run(response_fut)?;
    print_response(response)?;

    Ok(())
}
