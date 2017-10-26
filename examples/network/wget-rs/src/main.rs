// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate fidl;
extern crate fuchsia_app;
extern crate futures;
extern crate garnet_public_lib_network_fidl;
extern crate tokio_core;
extern crate tokio_fuchsia;

use fidl::FidlService;
use fuchsia_app::client::ApplicationContext;
use futures::{Future};
use futures::stream::{self, Stream};
use garnet_public_lib_network_fidl as netsvc;
use std::io;
use tokio_core::reactor;

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

    let fut = loader_proxy.start(req);

    //// Run the future to completion
    let resp = core.run(fut)?;
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
    if let Some(netsvc::URLBody::Stream(s)) = resp.body.map(|x| *x) {
        let sock = tokio_fuchsia::Socket::from_socket(s, &handle).unwrap();
        let buf = vec![0; 4096];
        println!(">>> Body <<<");
        let fut = stream::unfold((sock, buf, 1), |(sock, buf, num_read)| {
            if num_read == 0 {
                None
            } else {
                Some(sock.read(buf).map(|(s, b, n)| {
                    ((b,n), (s, vec![0; 4096], n))
                }))
            }
        }).for_each(|(buf, n)| {
            print!("{}", String::from_utf8_lossy(&buf[..n]));
            Ok(())
        });

        core.run(fut).or_else(|e| {
            if e.kind() == io::ErrorKind::ConnectionAborted {
                println!("\n>>> EOF <<<");
                Ok(())
            } else {
                Err(fidl::Error::IoError(e))
            }
        })
    } else {
        Ok(())
    }
}
