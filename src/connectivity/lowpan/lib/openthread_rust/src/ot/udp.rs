// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;
use std::marker::PhantomData;
use std::ptr::{null_mut, NonNull};

/// Functional equivalent of [`otsys::otUdpSocket`](crate::otsys::otUdpSocket).
#[repr(transparent)]
pub struct UdpSocket<'a>(pub otUdpSocket, PhantomData<*mut otUdpSocket>, PhantomData<&'a ()>);

impl_ot_castable!(opaque lifetime UdpSocket<'_>, otUdpSocket, Default::default(), Default::default());

impl std::fmt::Debug for UdpSocket<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("otUdpSocket")
            .field("peer_name", &self.peer_name())
            .field("sock_name", &self.sock_name())
            .field("handle", &self.get_handle())
            .finish()
    }
}

impl<'a> UdpSocket<'a> {
    /// The peer IPv6 socket address.
    pub fn peer_name(&self) -> SockAddr {
        self.0.mPeerName.into()
    }

    /// The local IPv6 socket address.
    pub fn sock_name(&self) -> SockAddr {
        self.0.mSockName.into()
    }

    /// A handle to platform's UDP.
    pub fn get_handle(&self) -> Option<NonNull<::std::os::raw::c_void>> {
        NonNull::new(self.0.mHandle)
    }

    /// Change the UDP handle.
    pub fn set_handle(&mut self, handle: Option<NonNull<::std::os::raw::c_void>>) {
        self.0.mHandle = handle.map(NonNull::as_ptr).unwrap_or(null_mut());
    }

    /// Platform callback for handling received data.
    pub fn handle_receive(&self, msg: OtMessageBox<'a>, info: &ot::message::Info) {
        if let Some(handler) = self.0.mHandler {
            unsafe {
                handler(self.0.mContext, msg.take_ot_ptr(), info.as_ot_ptr());
            }
        }
    }
}

/// Iterates over the available UDP sockets. See [`Udp::udp_get_sockets`].
#[derive(Debug, Clone)]
#[repr(transparent)]
pub struct UdpSocketIterator<'a>(*mut otUdpSocket, PhantomData<&'a ()>);

impl<'a> Iterator for UdpSocketIterator<'a> {
    type Item = &'a UdpSocket<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        let ret = unsafe { UdpSocket::ref_from_ot_ptr(self.0) };
        if ret.is_some() {
            self.0 = unsafe { (*self.0).mNext };
        }
        ret
    }
}

/// Methods from the [OpenThread "UDP" Module](https://openthread.io/reference/group/api-udp).
pub trait Udp {
    /// Functional equivalent of [`otsys::otUdpGetSockets`](crate::otsys::otUdpGetSockets).
    fn udp_get_sockets(&self) -> UdpSocketIterator<'_>;
}

impl<T: Udp + ot::Boxable> Udp for ot::Box<T> {
    fn udp_get_sockets(&self) -> UdpSocketIterator<'_> {
        self.as_ref().udp_get_sockets()
    }
}

impl Udp for Instance {
    fn udp_get_sockets(&self) -> UdpSocketIterator<'_> {
        UdpSocketIterator(unsafe { otUdpGetSockets(self.as_ot_ptr()) }, PhantomData::default())
    }
}
