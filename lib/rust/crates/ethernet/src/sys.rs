// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fasync::FifoEntry;
use fdio::{fdio_sys, ioctl_raw};
use std::fmt;
use std::fs::File;
use std::mem;
use std::os::raw;
use std::os::unix::io::AsRawFd;
use std::ptr;
use zx::sys::zx_status_t;
use zx::{self, AsHandleRef};

#[repr(C)]
#[derive(Default)]
pub struct eth_info_t {
    pub features: u32,
    pub mtu: u32,
    pub mac: [u8; 6],
    _pad: [u8; 2],
    _reserved: [u32; 12],
}

pub const ETH_FEATURE_WLAN: u32 = 1;
pub const ETH_FEATURE_SYNTH: u32 = 2;
pub const ETH_FEATURE_LOOPBACK: u32 = 4;

pub const ETH_STATUS_ONLINE: u32 = 1;

pub const ETH_FIFO_RX_OK: u16 = 1;
pub const ETH_FIFO_TX_OK: u16 = 1;
pub const ETH_FIFO_INVALID: u16 = 2;
pub const ETH_FIFO_RX_TX: u16 = 4;

#[repr(C)]
#[derive(Debug)]
pub struct eth_fifos_t {
    pub tx_fifo: zx::sys::zx_handle_t,
    pub rx_fifo: zx::sys::zx_handle_t,
    pub tx_depth: u32,
    pub rx_depth: u32,
}

impl eth_fifos_t {
    fn new() -> Self {
        eth_fifos_t {
            tx_fifo: zx::sys::ZX_HANDLE_INVALID,
            rx_fifo: zx::sys::ZX_HANDLE_INVALID,
            tx_depth: 0,
            rx_depth: 0,
        }
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct eth_fifo_entry {
    pub offset: u32,
    pub length: u16,
    pub flags: u16,
    pub cookie: usize,
}
unsafe impl FifoEntry for eth_fifo_entry {}

pub fn get_info(device: &File) -> Result<eth_info_t, zx::Status> {
    let mut info = eth_info_t::default();
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_GET_INFO,
            ptr::null(),
            0,
            &mut info as *mut _ as *mut raw::c_void,
            mem::size_of::<eth_info_t>(),
        )
    } as zx_status_t)
        .map(|_| info)
}

pub(crate) fn get_fifos(device: &File) -> Result<eth_fifos_t, zx::Status> {
    let mut fifos = eth_fifos_t::new();
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_GET_FIFOS,
            ptr::null(),
            0,
            &mut fifos as *mut _ as *mut raw::c_void,
            mem::size_of::<eth_fifos_t>(),
        )
    } as zx_status_t)
        .map(|_| fifos)
}

pub fn set_iobuf(device: &File, buf: zx::Vmo) -> Result<(), zx::Status> {
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_SET_IOBUF,
            &buf.raw_handle() as *const _ as *const raw::c_void,
            mem::size_of::<zx::sys::zx_handle_t>(),
            ptr::null_mut(),
            0,
        )
    } as zx_status_t)
        .map(|_| ())
}

pub fn start(device: &File) -> Result<(), zx::Status> {
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_START,
            ptr::null(),
            0,
            ptr::null_mut(),
            0,
        )
    } as zx_status_t)
        .map(|_| ())
}

pub fn stop(device: &File) -> Result<(), zx::Status> {
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_STOP,
            ptr::null(),
            0,
            ptr::null_mut(),
            0,
        )
    } as zx_status_t)
        .map(|_| ())
}

pub fn tx_listen_start(device: &File) -> Result<(), zx::Status> {
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_TX_LISTEN_START,
            ptr::null(),
            0,
            ptr::null_mut(),
            0,
        )
    } as zx_status_t)
        .map(|_| ())
}

pub fn tx_listen_stop(device: &File) -> Result<(), zx::Status> {
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_TX_LISTEN_STOP,
            ptr::null(),
            0,
            ptr::null_mut(),
            0,
        )
    } as zx_status_t)
        .map(|_| ())
}

pub fn set_client_name(device: &File, name: &str) -> Result<(), zx::Status> {
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_SET_CLIENT_NAME,
            name.as_bytes() as *const _ as *const raw::c_void,
            name.len(),
            ptr::null_mut(),
            0,
        )
    } as zx_status_t)
        .map(|_| ())
}

pub fn get_status(device: &File) -> Result<u32, zx::Status> {
    let mut status = 0;
    zx::ioctl_ok(unsafe {
        ioctl_raw(
            device.as_raw_fd(),
            IOCTL_ETHERNET_GET_STATUS,
            ptr::null(),
            0,
            &mut status as *mut _ as *mut raw::c_void,
            mem::size_of::<u32>(),
        )
    } as zx_status_t)
        .map(|_| status)
}

impl fmt::Debug for eth_info_t {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("eth_info_t")
            .field("features", &self.features)
            .field("mtu", &self.mtu)
            .field(
                "mac",
                &format!(
                    "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                    self.mac[0], self.mac[1], self.mac[2], self.mac[3], self.mac[4], self.mac[5]
                ),
            )
            .finish()
    }
}

macro_rules! eth_ioctls {
    ( $({$name:ident, $kind:ident, $id:expr}),* $(,)*) => {
        $(
        const $name: raw::c_int = make_ioctl!(
            fdio_sys::$kind, fdio_sys::IOCTL_FAMILY_ETH, $id
        );
        )*
    };
}

eth_ioctls! {
    {IOCTL_ETHERNET_GET_INFO,  IOCTL_KIND_DEFAULT,         0},
    {IOCTL_ETHERNET_GET_FIFOS, IOCTL_KIND_GET_TWO_HANDLES, 1},
    {IOCTL_ETHERNET_SET_IOBUF, IOCTL_KIND_SET_HANDLE,      2},
    {IOCTL_ETHERNET_START,     IOCTL_KIND_DEFAULT,         3},
    {IOCTL_ETHERNET_STOP,      IOCTL_KIND_DEFAULT,         4},
    {IOCTL_ETHERNET_TX_LISTEN_START, IOCTL_KIND_DEFAULT,   5},
    {IOCTL_ETHERNET_TX_LISTEN_STOP,  IOCTL_KIND_DEFAULT,   6},
    {IOCTL_ETHERNET_SET_CLIENT_NAME, IOCTL_KIND_DEFAULT,   7},
    {IOCTL_ETHERNET_GET_STATUS, IOCTL_KIND_DEFAULT,        8},
    // TODO(tkilbourn): promiscuous mode and multicast filtering
}
