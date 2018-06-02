// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate fidl_network as netsvc;

use failure::{Error, ResultExt};
use futures::prelude::*;
use futures::io::AllowStdIo;

fn print_headers(resp: &netsvc::UrlResponse) {
    println!(">>> Headers <<<");
    if let Some(status) = &resp.status_line {
        println!("  {}", status);
    }
    if let Some(hdrs) = &resp.headers {
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
fn main_res() -> Result<(), Error> {
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
    let mut exec = async::Executor::new()?;

    // Connect to the network service
    let net = component::client::connect_to_service::<netsvc::NetworkServiceMarker>()?;

    // Create a UrlLoader instance
    let (s, p) = zx::Channel::create().context("failed to create zx channel")?;
    let proxy = async::Channel::from_channel(p).context("failed to make async channel")?;

    let loader_server = fidl::endpoints2::ServerEnd::<netsvc::UrlLoaderMarker>::new(s);
    net.create_url_loader(loader_server)?;

    // Send the UrlRequest to fetch the webpage
    let mut req = netsvc::UrlRequest {
        url: url,
        method: String::from("GET"),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        auto_follow_redirects: true,
        cache_mode: netsvc::CacheMode::Default,
        response_body_mode: netsvc::ResponseBodyMode::Stream,
    };

	let loader_proxy = netsvc::UrlLoaderProxy::new(proxy);
    let fut = loader_proxy.start(&mut req).err_into().and_then(|resp| {
        if let Some(e) = resp.error {
            let code = e.code;
            println!("Got error: {} ({})",
                    code,
                    e.description.unwrap_or("".into()));
            return None;
        }
        print_headers(&resp);

        match resp.body.map(|x| *x) {
            Some(netsvc::UrlBody::Stream(s)) => {
                Some(async::Socket::from_socket(s)
                        .into_future()
                        .err_into())
            }
            Some(netsvc::UrlBody::Buffer(_)) |
            Some(netsvc::UrlBody::SizedBuffer(_)) |
            None =>  None,
        }
    }).and_then(|socket_opt| {
        socket_opt.map(|socket| {
            // stdout is blocking, but we'll pretend it's okay
            println!(">>> Body <<<");

            // Copy the bytes from the socket to stdout
            socket.copy_into(AllowStdIo::new(::std::io::stdout()))
                .map(|_| println!("\n>>> EOF <<<"))
                .err_into()
        })
    }).map(|_| ());

    //// Run the future to completion
    exec.run_singlethreaded(fut)
}
