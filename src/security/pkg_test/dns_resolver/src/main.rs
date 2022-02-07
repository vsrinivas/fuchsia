// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    fidl_fuchsia_net::{IpAddress, Ipv4Address},
    fidl_fuchsia_net_name::{LookupRequest, LookupRequestStream, LookupResult},
    fuchsia_async::{futures::StreamExt, Task},
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_info, init},
    io_util::directory::open_in_namespace,
    security_pkg_test_util::hostname_from_pkg_resolver_directory,
    std::net::Ipv4Addr,
};

const PKG_RESOLVER_REPOSITORIES_CONFIG_PATH: &str = "/pkg-resolver-repositories";

fn localhost() -> IpAddress {
    IpAddress::Ipv4(Ipv4Address { addr: Ipv4Addr::LOCALHOST.octets() })
}

async fn get_pkg_server_hostname() -> String {
    let pkg_resolver_repositories_dir =
        open_in_namespace(PKG_RESOLVER_REPOSITORIES_CONFIG_PATH, OPEN_RIGHT_READABLE).unwrap();
    hostname_from_pkg_resolver_directory(&pkg_resolver_repositories_dir).await
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    init().unwrap();

    fx_log_info!("Starting fake DNS component");

    let pkg_server_host = get_pkg_server_hostname().await;

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: LookupRequestStream| {
        let pkg_server_host = pkg_server_host.clone();
        fx_log_info!("New connection to fuchsia.net.name.Lookup");
        Task::spawn(async move {
            fx_log_info!("Received fuchsia.net.name.Lookup request");
            match stream.next().await.unwrap().unwrap() {
                LookupRequest::LookupIp { hostname, options: _, responder } => {
                    assert_eq!(pkg_server_host, hostname);
                    fx_log_info!("LooupIp: {} => {:#?}", hostname, localhost());
                    responder
                        .send(&mut Ok(LookupResult {
                            addresses: Some(vec![localhost()]),
                            ..LookupResult::EMPTY
                        }))
                        .unwrap();
                }
                LookupRequest::LookupHostname { addr, responder } => {
                    assert_eq!(addr, localhost());
                    fx_log_info!("LookupHostname: {:#?} => {}", addr, pkg_server_host);
                    responder.send(&mut Ok(pkg_server_host.clone())).unwrap();
                }
            }
        })
        .detach()
    });
    fs.take_and_serve_directory_handle().unwrap();
    fs.collect::<()>().await;
}
