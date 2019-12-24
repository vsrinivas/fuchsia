// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    dhcp_validity_lib::{configure_dhcp_server, verify_addr_present},
    fuchsia_async as fasync,
    std::net::{IpAddr, Ipv4Addr},
    std::time::Duration,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let ip_addr = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 10));
    configure_dhcp_server("debian_guest", "/bin/sh -c /root/input/dhcp_setup.sh").await?;
    verify_addr_present(ip_addr, Duration::from_secs(30)).await
}
