// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines how TCP sockets interacts with incoming ICMP messages.

use log::error;
use net_types::SpecifiedAddr;

use crate::{
    ip::{icmp::IcmpIpExt, IpDeviceIdContext, IpTransportContext},
    transport::tcp::socket::TcpIpTransportContext,
};

impl<I: IcmpIpExt, C, SC: IpDeviceIdContext<I> + ?Sized> IpTransportContext<I, C, SC>
    for TcpIpTransportContext
{
    fn receive_icmp_error(
        _sync_ctx: &mut SC,
        _ctx: &mut C,
        _device: &SC::DeviceId,
        _original_src_ip: Option<SpecifiedAddr<I::Addr>>,
        _original_dst_ip: SpecifiedAddr<I::Addr>,
        _original_body: &[u8],
        err: I::ErrorCode,
    ) {
        // TODO(https://fxbug.dev/101806): Process incoming ICMP error message.
        error!(
            "TcpIpTransportContext::receive_icmp_error: Received ICMP error message ({:?}) for TCP",
            err
        );
    }
}
