// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The guest_ethernet crate provides a safe wrapper around the guest ethernet FFI interface,
//! including appropriate memory management. See the comments in interface.rs for what each
//! FFI function does. Do not use the interface directly.

// TODO(fxbug.dev/95485): Remove.
#![allow(dead_code)]

use {
    crate::interface,
    fidl_fuchsia_hardware_ethernet::MacAddress,
    fuchsia_zircon as zx,
    futures::channel::mpsc::{self, UnboundedReceiver, UnboundedSender},
    std::{marker::PhantomPinned, pin::Pin},
};

pub struct RxPacket {
    data: *const u8,
    len: usize,
    buffer_id: u32,
}

pub struct GuestEthernet {
    raw_ptr: *mut interface::guest_ethernet_t,

    // Used by the `interface::guest_ethernet_set_status` callback.
    status_tx: UnboundedSender<zx::Status>,

    // Used by the `interface::guest_ethernet_ready_for_tx` callback.
    notify_tx: UnboundedSender<()>,

    // Used by the `interface::guest_ethernet_complete` callback.
    receive_packet_tx: UnboundedSender<RxPacket>,

    // GuestEthernet must not be unpinned or the C++ GuestEthernet object will interact with
    // invalid memory when calling back into Rust.
    _pin: PhantomPinned,
}

pub struct GuestEthernetNewResult {
    pub guest_ethernet: Pin<Box<GuestEthernet>>,
    pub status_rx: UnboundedReceiver<zx::Status>,
    pub notify_rx: UnboundedReceiver<()>,
    pub receive_packet_rx: UnboundedReceiver<RxPacket>,
}

impl GuestEthernet {
    pub fn new() -> Result<GuestEthernetNewResult, zx::Status> {
        let mut raw_ptr: *mut interface::guest_ethernet_t = std::ptr::null_mut();
        zx::Status::ok(unsafe { interface::guest_ethernet_create(&mut raw_ptr) })?;

        let (status_tx, status_rx) = mpsc::unbounded::<zx::Status>();
        let (notify_tx, notify_rx) = mpsc::unbounded::<()>();
        let (receive_packet_tx, receive_packet_rx) = mpsc::unbounded::<RxPacket>();
        let guest_ethernet = Box::pin(Self {
            raw_ptr,
            status_tx,
            notify_tx,
            receive_packet_tx,
            _pin: PhantomPinned,
        });

        Ok(GuestEthernetNewResult { guest_ethernet, status_rx, notify_rx, receive_packet_rx })
    }

    pub fn initialize(
        &self,
        mac_address: MacAddress,
        enable_bridge: bool,
    ) -> Result<(), zx::Status> {
        tracing::info!("Registering a virtio-net device with the netstack");
        zx::Status::ok(unsafe {
            interface::guest_ethernet_initialize(
                self.raw_ptr,
                self as *const _ as *const libc::c_void,
                mac_address.octets.as_ptr(),
                mac_address.octets.len(),
                enable_bridge,
            )
        })
    }

    pub fn send(&self, data: *mut u8, len: u16) -> Result<(), zx::Status> {
        zx::Status::ok(unsafe { interface::guest_ethernet_send(self.raw_ptr, data, len) })
    }

    pub fn complete(&self, buffer_id: u32, status: zx::Status) {
        unsafe { interface::guest_ethernet_complete(self.raw_ptr, buffer_id, status.into_raw()) }
    }

    // Callback from C++.
    pub fn set_guest_ethernet_status(guest_ethernet: *const libc::c_void, status: zx::zx_status_t) {
        let status = zx::Status::from_raw(status);
        let guest_ethernet = unsafe {
            (guest_ethernet as *const GuestEthernet)
                .as_ref()
                .expect("received a nullptr device from C++")
        };

        tracing::info!("C++ guest ethernet object sent status: {}", status);
        guest_ethernet
            .status_tx
            .unbounded_send(status)
            .expect("status tx end should never be closed");
    }

    // Callback from C++.
    pub fn ready_for_tx(guest_ethernet: *const libc::c_void) {
        let guest_ethernet = unsafe {
            (guest_ethernet as *const GuestEthernet)
                .as_ref()
                .expect("received a nullptr device from C++")
        };

        guest_ethernet.notify_tx.unbounded_send(()).expect("notify tx end should never be closed");
    }

    // Callback from C++.
    pub fn receive_rx(
        guest_ethernet: *const libc::c_void,
        data: *const u8,
        len: usize,
        buffer_id: u32,
    ) {
        let guest_ethernet = unsafe {
            (guest_ethernet as *const GuestEthernet)
                .as_ref()
                .expect("received a nullptr device from C++")
        };

        guest_ethernet
            .receive_packet_tx
            .unbounded_send(RxPacket { data, len, buffer_id })
            .expect("receive packet tx end should never be closed");
    }
}

impl Drop for GuestEthernet {
    fn drop(&mut self) {
        unsafe {
            tracing::info!("Disconnecting a virtio-net device from the netstack");
            interface::guest_ethernet_destroy(self.raw_ptr);
        }
    }
}
