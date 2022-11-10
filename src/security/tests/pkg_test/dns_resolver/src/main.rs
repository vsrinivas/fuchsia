// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::{from_env, FromArgs},
    fidl_fuchsia_net::{IpAddress, Ipv4Address},
    fidl_fuchsia_net_name::{LookupRequest, LookupRequestStream, LookupResult},
    fuchsia_async::Task,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    security_pkg_test_util::load_config,
    std::net::Ipv4Addr,
    tracing::info,
};

/// Flags for dns_resolver.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to shared test configuration file understood by
    /// security_pkg_test_util::load_config().
    #[argh(option)]
    test_config_path: String,
}

fn localhost() -> IpAddress {
    IpAddress::Ipv4(Ipv4Address { addr: Ipv4Addr::LOCALHOST.octets() })
}

#[fuchsia::main]
async fn main() {
    info!("Starting fake DNS component");
    let args @ Args { test_config_path } = &from_env();
    info!(?args, "Initalizing fake DNS component");

    let pkg_server_host = load_config(test_config_path).update_domain;

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: LookupRequestStream| {
        let pkg_server_host = pkg_server_host.clone();
        info!("New connection to fuchsia.net.name.Lookup");
        Task::spawn(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    LookupRequest::LookupIp { hostname, options: _, responder } => {
                        assert_eq!(pkg_server_host, hostname);
                        info!(%hostname, localhost = ?localhost(), "LooupIp");
                        responder
                            .send(&mut Ok(LookupResult {
                                addresses: Some(vec![localhost()]),
                                ..LookupResult::EMPTY
                            }))
                            .unwrap();
                    }
                    LookupRequest::LookupHostname { addr, responder } => {
                        assert_eq!(addr, localhost());
                        info!(?addr, %pkg_server_host, "LookupHostname");
                        responder.send(&mut Ok(pkg_server_host.clone())).unwrap();
                    }
                }
            }
        })
        .detach()
    });
    fs.take_and_serve_directory_handle().unwrap();
    fs.collect::<()>().await;
}
