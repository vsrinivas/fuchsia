// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Error};
use fidl::HandleRef;
use fidl_fuchsia_overnet_protocol::{ChannelRights, SocketRights, SocketType};

#[cfg(target_os = "fuchsia")]
use fidl::AsHandleRef;

#[cfg(not(target_os = "fuchsia"))]
use fidl::EmulatedHandleRef;

#[cfg(target_os = "fuchsia")]
pub(crate) type HandleKey = fuchsia_zircon::Koid;

#[cfg(not(target_os = "fuchsia"))]
pub(crate) type HandleKey = u64;

/// When sending a datagram on a channel, contains information needed to establish streams
/// for any handles being sent.
#[derive(Copy, Clone, Debug)]
pub(crate) enum HandleType {
    /// A handle of type channel is being sent.
    Channel(ChannelRights),
    Socket(SocketType, SocketRights),
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
        fidl::HandleType::Channel => {
            HandleType::Channel(ChannelRights::Read | ChannelRights::Write)
        }
        fidl::HandleType::Socket => {
            HandleType::Socket(SocketType::Stream, SocketRights::Read | SocketRights::Write)
        }
        fidl::HandleType::Invalid => bail!("Unsupported handle type"),
    };
    let (this_handle_key, pair_handle_key) = hdl.koid_pair();
    Ok(HandleInfo { handle_type, this_handle_key, pair_handle_key })
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
        zx::ObjectType::CHANNEL => {
            let mut rights = ChannelRights::empty();
            rights.set(ChannelRights::Read, basic_info.rights.contains(zx::Rights::READ));
            rights.set(ChannelRights::Write, basic_info.rights.contains(zx::Rights::WRITE));
            HandleType::Channel(rights)
        }
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
            let mut rights = SocketRights::empty();
            rights.set(SocketRights::Read, basic_info.rights.contains(zx::Rights::READ));
            rights.set(SocketRights::Write, basic_info.rights.contains(zx::Rights::WRITE));
            HandleType::Socket(socket_type, rights)
        }
        _ => bail!("Handle type not proxyable {:?}", handle.basic_info()?.object_type),
    };

    Ok(HandleInfo {
        handle_type,
        this_handle_key: basic_info.koid,
        pair_handle_key: basic_info.related_koid,
    })
}

pub(crate) trait WithRights {
    type Rights;
    fn with_rights(self, rights: Self::Rights) -> Result<Self, Error>
    where
        Self: Sized;
}

#[cfg(target_os = "fuchsia")]
impl WithRights for fidl::Channel {
    type Rights = ChannelRights;
    fn with_rights(self, rights: ChannelRights) -> Result<Self, Error> {
        use fuchsia_zircon as zx;
        use zx::HandleBased;
        let mut zx_rights = self.basic_info()?.rights;
        zx_rights.set(zx::Rights::READ, rights.contains(ChannelRights::Read));
        zx_rights.set(zx::Rights::WRITE, rights.contains(ChannelRights::Write));
        zx_rights.insert(zx::Rights::TRANSFER);
        Ok(self.replace_handle(zx_rights)?)
    }
}

#[cfg(target_os = "fuchsia")]
impl WithRights for fidl::Socket {
    type Rights = SocketRights;
    fn with_rights(self, rights: SocketRights) -> Result<Self, Error> {
        use fuchsia_zircon as zx;
        use zx::HandleBased;
        let mut zx_rights = self.basic_info()?.rights;
        zx_rights.set(zx::Rights::READ, rights.contains(SocketRights::Read));
        zx_rights.set(zx::Rights::WRITE, rights.contains(SocketRights::Write));
        zx_rights.insert(zx::Rights::TRANSFER);
        Ok(self.replace_handle(zx_rights)?)
    }
}

#[cfg(not(target_os = "fuchsia"))]
impl WithRights for fidl::Channel {
    type Rights = ChannelRights;
    fn with_rights(self, rights: ChannelRights) -> Result<Self, Error> {
        if rights != ChannelRights::Read | ChannelRights::Write {
            bail!("Restricted rights not supported on non-Fuchsia platforms");
        }
        Ok(self)
    }
}

#[cfg(not(target_os = "fuchsia"))]
impl WithRights for fidl::Socket {
    type Rights = SocketRights;
    fn with_rights(self, rights: SocketRights) -> Result<Self, Error> {
        if rights != SocketRights::Read | SocketRights::Write {
            bail!("Restricted rights not supported on non-Fuchsia platforms");
        }
        Ok(self)
    }
}
