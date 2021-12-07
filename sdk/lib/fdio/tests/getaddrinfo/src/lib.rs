// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_net_name as fnet_name, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_component::server::ServiceFs,
    futures::{FutureExt as _, StreamExt as _, TryStreamExt as _},
};

// The maximum number of addresses that fdio can handle.
const MAXADDRS: usize = 1024;

#[fasync::run_singlethreaded(test)]
async fn test_getaddrinfo() {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFs<_> = fs.add_fidl_service(|s: fnet_name::LookupRequestStream| s);

    let env = fs
        .create_salted_nested_environment("test_getaddrinfo")
        .expect("failed to create environment");
    let app = client::AppBuilder::new(
        "fuchsia-pkg://fuchsia.com/getaddrinfo_tests#meta/getaddrinfo_test_client.cmx",
    )
    .output(env.launcher())
    .expect("failed to launch test client");

    let mut fs = fs.map(Ok).try_for_each_concurrent(None, |stream| {
        stream.try_for_each_concurrent(None, |request| match request {
            fnet_name::LookupRequest::LookupIp { hostname, options, responder } => {
                futures::future::ready(responder.send(&mut (|| {
                    let size = match hostname.as_str() {
                        "example.com" => 1,
                        "lotsofrecords.com" => MAXADDRS,
                        "google.com" => return Err(fnet_name::LookupError::NotFound),
                        hostname => panic!("unexpected hostname {}", hostname),
                    };
                    let fnet_name::LookupIpOptions {
                        ipv4_lookup,
                        ipv6_lookup,
                        sort_addresses: _,
                        ..
                    } = options;
                    let ipv4_addresses = ipv4_lookup
                        .unwrap_or(false)
                        .then(|| std::iter::repeat(net_declare::fidl_ip!("192.0.2.1")).take(size))
                        .into_iter()
                        .flatten();
                    let ipv6_addresses = ipv6_lookup
                        .unwrap_or(false)
                        .then(|| std::iter::repeat(net_declare::fidl_ip!("2001:db8::1")).take(size))
                        .into_iter()
                        .flatten();
                    let addresses = Some(ipv4_addresses.chain(ipv6_addresses).collect());
                    Ok(fnet_name::LookupResult { addresses, ..fnet_name::LookupResult::EMPTY })
                })()))
            }
            request => panic!("unexpected request: {:?}", request),
        })
    });

    futures::select! {
        res = app.fuse() => {
            let output = res.expect("failed to wait for test client exit");
            output.ok().expect("test client exited with nonzero status code")
        },
        res = fs => panic!("request stream terminated: {:?}", res),
    }
}
