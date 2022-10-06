// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use crate::fs::buffers::*;
use crate::fs::*;
use crate::logging::not_implemented;
use crate::task::*;
use crate::types::*;

// This is a stubbed version of AF_NETLINK
pub struct NetlinkSocket {}

impl NetlinkSocket {
    pub fn new(_socket_type: SocketType) -> NetlinkSocket {
        NetlinkSocket {}
    }
}

impl SocketOps for NetlinkSocket {
    fn connect(
        &self,
        _socket: &SocketHandle,
        _peer: SocketPeer,
        _credentials: ucred,
    ) -> Result<(), Errno> {
        error!(ENOSYS)
    }

    fn listen(&self, _socket: &Socket, _backlog: i32, _credentials: ucred) -> Result<(), Errno> {
        not_implemented!("?", "NetlinkSocket::listen is stubbed");
        Ok(())
    }

    fn accept(&self, _socket: &Socket) -> Result<SocketHandle, Errno> {
        not_implemented!("?", "NetlinkSocket::accept is stubbed");
        error!(EAGAIN)
    }

    fn remote_connection(&self, _socket: &Socket, _file: FileHandle) -> Result<(), Errno> {
        not_implemented!("?", "NetlinkSocket::remote_connection is stubbed");
        Ok(())
    }

    fn bind(&self, _socket: &Socket, _socket_address: SocketAddress) -> Result<(), Errno> {
        not_implemented!("?", "NetlinkSocket::bind is stubbed");
        Ok(())
    }

    fn read(
        &self,
        _socket: &Socket,
        _current_task: &CurrentTask,
        _user_buffers: &mut UserBufferIterator<'_>,
        _flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno> {
        not_implemented!("?", "NetlinkSocket::read is unsupported");
        error!(ENOSYS)
    }

    fn write(
        &self,
        _socket: &Socket,
        _current_task: &CurrentTask,
        _user_buffers: &mut UserBufferIterator<'_>,
        _dest_address: &mut Option<SocketAddress>,
        _ancillary_data: &mut Vec<AncillaryData>,
    ) -> Result<usize, Errno> {
        not_implemented!("?", "NetlinkSocket::write is unsupported");
        error!(ENOSYS)
    }

    fn wait_async(
        &self,
        _socket: &Socket,
        _current_task: &CurrentTask,
        _waiter: &Waiter,
        _events: FdEvents,
        _handler: EventHandler,
        _options: WaitAsyncOptions,
    ) -> WaitKey {
        not_implemented!("?", "NetlinkSocket::wait_async is stubbed");
        WaitKey::empty()
    }

    fn cancel_wait(
        &self,
        _socket: &Socket,
        _current_task: &CurrentTask,
        _waiter: &Waiter,
        _key: WaitKey,
    ) {
    }

    fn query_events(&self, _socket: &Socket, _current_task: &CurrentTask) -> FdEvents {
        not_implemented!("?", "NetlinkSocket::query_events is stubbed");
        FdEvents::empty()
    }

    fn shutdown(&self, _socket: &Socket, _how: SocketShutdownFlags) -> Result<(), Errno> {
        not_implemented!("?", "NetlinkSocket::shutdown is stubbed");
        Ok(())
    }

    fn close(&self, _socket: &Socket) {
        not_implemented!("?", "NetlinkSocket::close is stubbed");
    }

    fn getsockname(&self, _socket: &Socket) -> Vec<u8> {
        not_implemented!("?", "NetlinkSocket::getsockname is stubbed");
        vec![]
    }

    fn getpeername(&self, _socket: &Socket) -> Result<Vec<u8>, Errno> {
        not_implemented!("?", "NetlinkSocket::getpeername is stubbed");
        error!(ENOTCONN)
    }

    fn setsockopt(
        &self,
        _socket: &Socket,
        _task: &Task,
        _level: u32,
        _optname: u32,
        _user_opt: UserBuffer,
    ) -> Result<(), Errno> {
        not_implemented!("?", "NetlinkSocket::setsockopt is stubbed");
        Ok(())
    }
}
