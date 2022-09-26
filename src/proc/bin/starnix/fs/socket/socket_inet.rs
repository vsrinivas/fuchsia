// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use crate::fs::buffers::*;
use crate::fs::*;
use crate::logging::not_implemented;
use crate::task::*;
use crate::types::*;
use fidl_fuchsia_posix_socket as fposix_socket;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_zircon as zx;
use fuchsia_zircon::sys::{ZX_ERR_INTERNAL, ZX_ERR_NO_MEMORY, ZX_OK};
use fuchsia_zircon::HandleBased;
use std::sync::Arc;
use syncio::zxio::{zx_handle_t, zx_status_t, zxio_object_type_t, zxio_socket, zxio_storage_t};
use syncio::Zxio;

/// Connects to `fuchsia_posix_socket::Provider`.
///
/// This function is intended to be passed to zxio_socket().
///
/// On success, `provider_handle` will contain a handle to the protocol.
///
/// SAFETY: Dereferences the raw pointer `provider_handle`
unsafe extern "C" fn socket_provider(
    _service_name: *const c_char,
    provider_handle: *mut zx_handle_t,
) -> zx_status_t {
    let (client_end, server_end) = match zx::Channel::create() {
        Err(e) => return e.into_raw(),
        Ok((c, s)) => (c, s),
    };
    if let Err(e) = connect_channel_to_protocol::<fposix_socket::ProviderMarker>(server_end) {
        if let Some(err) = e.downcast_ref::<zx::Status>() {
            return err.into_raw();
        }
        return ZX_ERR_INTERNAL;
    };

    *provider_handle = client_end.into_handle().into_raw();
    ZX_OK
}

/// Sets `out_storage` as the zxio_storage of `out_context`.
///
/// This function is intended to be passed to zxio_socket().
///
/// SAFETY: Dereferences the raw pointer `out_storage`.
unsafe extern "C" fn storage_allocator(
    _type: zxio_object_type_t,
    out_storage: *mut *mut zxio_storage_t,
    out_context: *mut *mut ::std::os::raw::c_void,
) -> zx_status_t {
    let zxio_ptr_ptr = out_context as *mut *mut Zxio;
    if let Some(zxio_ptr) = zxio_ptr_ptr.as_ref() {
        if let Some(zxio) = zxio_ptr.as_ref() {
            *out_storage = zxio.as_storage_ptr();
            return ZX_OK;
        }
    }
    ZX_ERR_NO_MEMORY
}

#[allow(dead_code)]
pub struct InetSocket {
    zxio: Arc<syncio::Zxio>,
}

impl InetSocket {
    pub fn new(
        domain: SocketDomain,
        socket_type: SocketType,
        protocol: SocketProtocol,
    ) -> Result<InetSocket, Errno> {
        let mut zxio = Zxio::default();
        let mut out_context = &mut zxio as *mut _ as *mut c_void;
        let mut out_code = 0;
        unsafe {
            let status = zxio_socket(
                Some(socket_provider),
                domain.as_raw() as c_int,
                socket_type.as_raw() as c_int,
                protocol.as_raw() as c_int,
                Some(storage_allocator),
                &mut out_context as *mut *mut c_void,
                &mut out_code,
            );
            zx::ok(status).map_err(|status| from_status_like_fdio!(status))?;
        }

        if out_code != 0 {
            return error_from_code!(out_code);
        }

        Ok(InetSocket { zxio: Arc::new(zxio) })
    }
}

impl SocketOps for InetSocket {
    fn connect(
        &self,
        _socket: &SocketHandle,
        _peer: &SocketHandle,
        _credentials: ucred,
    ) -> Result<(), Errno> {
        error!(ENOSYS)
    }

    fn listen(&self, _socket: &Socket, _backlog: i32, _credentials: ucred) -> Result<(), Errno> {
        not_implemented!("?", "InetSocket::listen is stubbed");
        Ok(())
    }

    fn accept(&self, _socket: &Socket) -> Result<SocketHandle, Errno> {
        not_implemented!("?", "InetSocket::accept is stubbed");
        error!(EAGAIN)
    }

    fn remote_connection(&self, _socket: &Socket, _file: FileHandle) -> Result<(), Errno> {
        not_implemented!("?", "InetSocket::remote_connection is stubbed");
        Ok(())
    }

    fn bind(&self, _socket: &Socket, _socket_address: SocketAddress) -> Result<(), Errno> {
        not_implemented!("?", "InetSocket::bind is stubbed");
        Ok(())
    }

    fn read(
        &self,
        _socket: &Socket,
        _current_task: &CurrentTask,
        _user_buffers: &mut UserBufferIterator<'_>,
        _flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno> {
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
        not_implemented!("?", "InetSocket::wait_async is stubbed");
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
        not_implemented!("?", "InetSocket::query_events is stubbed");
        FdEvents::empty()
    }

    fn shutdown(&self, _socket: &Socket, _how: SocketShutdownFlags) -> Result<(), Errno> {
        Ok(())
    }

    fn close(&self, _socket: &Socket) {}

    fn getsockname(&self, _socket: &Socket) -> Vec<u8> {
        vec![]
    }

    fn getpeername(&self, _socket: &Socket) -> Result<Vec<u8>, Errno> {
        error!(ENOTCONN)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[::fuchsia::test]
    fn test_storage_allocator() {
        let mut out_storage = zxio_storage_t::default();
        let mut out_storage_ptr = &mut out_storage as *mut zxio_storage_t;

        let mut out_context = Zxio::default();
        let mut out_context_ptr = &mut out_context as *mut Zxio;

        let out = unsafe {
            storage_allocator(
                0 as zxio_object_type_t,
                &mut out_storage_ptr as *mut *mut zxio_storage_t,
                &mut out_context_ptr as *mut *mut Zxio as *mut *mut c_void,
            )
        };
        assert_eq!(out, ZX_OK);
    }

    #[::fuchsia::test]
    fn test_storage_allocator_bad_context() {
        let mut out_storage = zxio_storage_t::default();
        let mut out_storage_ptr = &mut out_storage as *mut zxio_storage_t;

        let out_context = std::ptr::null_mut();

        let out = unsafe {
            storage_allocator(
                0 as zxio_object_type_t,
                &mut out_storage_ptr as *mut *mut zxio_storage_t,
                out_context,
            )
        };
        assert_eq!(out, ZX_ERR_NO_MEMORY);
    }
}
