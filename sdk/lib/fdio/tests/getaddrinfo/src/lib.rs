// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_net as fnet, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_component::server::ServiceFs,
    futures::{FutureExt as _, StreamExt as _, TryStreamExt as _},
};

#[fasync::run_singlethreaded(test)]
async fn test_getaddrinfo() {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFs<_> = fs.add_fidl_service(|s: fnet::NameLookupRequestStream| s);

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
            fnet::NameLookupRequest::LookupIp { hostname, options, responder } => {
                futures::future::ready(responder.send(&mut if hostname == "example.com" {
                    Ok(fnet::IpAddressInfo {
                        ipv4_addrs: options
                            .contains(fnet::LookupIpOptions::V4Addrs)
                            .then(|| net_declare::fidl_ip_v4!("192.0.2.1"))
                            .into_iter()
                            .collect(),
                        ipv6_addrs: options
                            .contains(fnet::LookupIpOptions::V6Addrs)
                            .then(|| net_declare::fidl_ip_v6!("2001:db8::1"))
                            .into_iter()
                            .collect(),
                        canonical_name: None,
                    })
                } else {
                    Err(fnet::LookupError::NotFound)
                }))
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
