// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Error};
use fidl::{AsHandleRef, HandleRef};
use fidl_fuchsia_overnet_protocol::SocketType;

#[cfg(target_os = "fuchsia")]
pub(crate) type HandleKey = fuchsia_zircon::Koid;

#[cfg(not(target_os = "fuchsia"))]
pub(crate) type HandleKey = u32;

/// When sending a datagram on a channel, contains information needed to establish streams
/// for any handles being sent.
#[derive(Copy, Clone, Debug)]
pub(crate) enum HandleType {
    /// A handle of type channel is being sent.
    Channel,
    Socket(SocketType),
}

#[derive(Copy, Clone, Debug)]
pub(crate) struct HandleInfo {
    pub(crate) handle_type: HandleType,
    pub(crate) this_handle_key: HandleKey,
    pub(crate) pair_handle_key: HandleKey,
}

#[cfg(not(target_os = "fuchsia"))]
pub(crate) fn handle_info(hdl: HandleRef<'_>) -> Result<HandleInfo, Error> {
    let handle_type = match hdl.handle_type() {
        fidl::FidlHdlType::Channel => HandleType::Channel,
        fidl::FidlHdlType::Socket => HandleType::Socket(SocketType::Stream),
        fidl::FidlHdlType::Invalid => bail!("Unsupported handle type"),
    };
    Ok(HandleInfo {
        handle_type,
        this_handle_key: hdl.raw_handle(),
        pair_handle_key: hdl.related().raw_handle(),
    })
}

#[cfg(target_os = "fuchsia")]
pub(crate) fn handle_info(handle: HandleRef<'_>) -> Result<HandleInfo, Error> {
    use fuchsia_zircon as zx;

    // zx_info_socket_t is able to be safely replaced with a byte representation and is a PoD type.
    struct SocketInfoQuery;
    unsafe impl zx::ObjectQuery for SocketInfoQuery {
        const TOPIC: zx::Topic = zx::Topic::SOCKET;
        type InfoTy = zx::sys::zx_info_socket_t;
    }

    let basic_info = handle.basic_info()?;

    let handle_type = match basic_info.object_type {
        zx::ObjectType::CHANNEL => HandleType::Channel,
        zx::ObjectType::SOCKET => {
            let mut info = zx::sys::zx_info_socket_t::default();
            let info = zx::object_get_info::<SocketInfoQuery>(
                handle.as_handle_ref(),
                std::slice::from_mut(&mut info),
            )
            .map(|_| zx::SocketInfo::from(info))?;
            let socket_type = match info.options {
                zx::SocketOpts::STREAM => SocketType::Stream,
                zx::SocketOpts::DATAGRAM => SocketType::Datagram,
                _ => bail!("Unhandled socket options"),
            };
            HandleType::Socket(socket_type)
        }
        _ => bail!("Handle type not proxyable {:?}", handle.basic_info()?.object_type),
    };

    Ok(HandleInfo {
        handle_type,
        this_handle_key: basic_info.koid,
        pair_handle_key: basic_info.related_koid,
    })
}
