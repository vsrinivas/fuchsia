// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file was generated with bindgen, then modified to consume already bound
// types and remove various bindgen-isms that we don't want.

use zircon::sys::*;
use std::os::raw;

#[repr(C)]
#[derive(Default)]
pub struct __IncompleteArrayField<T>(::std::marker::PhantomData<T>);
impl<T> __IncompleteArrayField<T> {
    #[inline]
    pub fn new() -> Self {
        __IncompleteArrayField(::std::marker::PhantomData)
    }
    #[inline]
    pub unsafe fn as_ptr(&self) -> *const T {
        ::std::mem::transmute(self)
    }
    #[inline]
    pub unsafe fn as_mut_ptr(&mut self) -> *mut T {
        ::std::mem::transmute(self)
    }
    #[inline]
    pub unsafe fn as_slice(&self, len: usize) -> &[T] {
        ::std::slice::from_raw_parts(self.as_ptr(), len)
    }
    #[inline]
    pub unsafe fn as_mut_slice(&mut self, len: usize) -> &mut [T] {
        ::std::slice::from_raw_parts_mut(self.as_mut_ptr(), len)
    }
}
impl<T> ::std::fmt::Debug for __IncompleteArrayField<T> {
    fn fmt(&self, fmt: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        fmt.write_str("__IncompleteArrayField")
    }
}
impl<T> ::std::clone::Clone for __IncompleteArrayField<T> {
    #[inline]
    fn clone(&self) -> Self {
        Self::new()
    }
}
impl<T> ::std::marker::Copy for __IncompleteArrayField<T> {}

// TODO(raggi): this should be able to come from libc instead.
pub const O_DIRECTORY: raw::c_int = 0x00080000;
pub const O_NOREMOTE: raw::c_int = 0x00200000;
pub const O_ADMIN: raw::c_int = 0x00000004;
pub const ZX_FS_FLAG_DESCRIBE: raw::c_uint = 0x00800000;

pub const ZXRIO_HDR_SZ: usize = 56;
pub const ZXRIO_MSG_SZ: usize = ZXRIO_HDR_SZ + FDIO_CHUNK_SIZE as usize;

pub const ZXRIO_OBJECT_MINSIZE: usize = 8;
pub const ZXRIO_OBJECT_MAXSIZE: usize = ZXRIO_OBJECT_MINSIZE + ZXRIO_OBJECT_EXTRA as usize;

#[test]
fn zxrio_msg_size() {
    assert_eq!(::std::mem::size_of::<zxrio_msg_t>(), ZXRIO_MSG_SZ);
    assert_eq!(
        ::std::mem::size_of::<zxrio_msg_t>() - FDIO_CHUNK_SIZE as usize,
        ZXRIO_HDR_SZ
    );
}

pub const FDIO_MAX_FD: raw::c_uint = 256;
pub const FDIO_MAX_HANDLES: raw::c_uint = 3;
pub const FDIO_CHUNK_SIZE: raw::c_uint = 8192;
pub const FDIO_IOCTL_MAX_INPUT: raw::c_uint = 1024;
pub const FDIO_MAX_FILENAME: raw::c_uint = 255;
pub const ZXRIO_ONE_HANDLE: raw::c_uint = 256;
pub const ZXRIO_STATUS: raw::c_uint = 0;
pub const ZXRIO_CLOSE: raw::c_uint = 1;
pub const ZXRIO_CLONE: raw::c_uint = 258;
pub const ZXRIO_OPEN: raw::c_uint = 259;
pub const ZXRIO_MISC: raw::c_uint = 4;
pub const ZXRIO_READ: raw::c_uint = 5;
pub const ZXRIO_WRITE: raw::c_uint = 6;
pub const ZXRIO_SEEK: raw::c_uint = 7;
pub const ZXRIO_STAT: raw::c_uint = 8;
pub const ZXRIO_READDIR: raw::c_uint = 9;
pub const ZXRIO_IOCTL: raw::c_uint = 10;
pub const ZXRIO_IOCTL_1H: raw::c_uint = 266;
pub const ZXRIO_UNLINK: raw::c_uint = 11;
pub const ZXRIO_READ_AT: raw::c_uint = 12;
pub const ZXRIO_WRITE_AT: raw::c_uint = 13;
pub const ZXRIO_TRUNCATE: raw::c_uint = 14;
pub const ZXRIO_RENAME: raw::c_uint = 271;
pub const ZXRIO_CONNECT: raw::c_uint = 16;
pub const ZXRIO_BIND: raw::c_uint = 17;
pub const ZXRIO_LISTEN: raw::c_uint = 18;
pub const ZXRIO_GETSOCKNAME: raw::c_uint = 19;
pub const ZXRIO_GETPEERNAME: raw::c_uint = 20;
pub const ZXRIO_GETSOCKOPT: raw::c_uint = 21;
pub const ZXRIO_SETSOCKOPT: raw::c_uint = 22;
pub const ZXRIO_GETADDRINFO: raw::c_uint = 23;
pub const ZXRIO_SETATTR: raw::c_uint = 24;
pub const ZXRIO_SYNC: raw::c_uint = 25;
pub const ZXRIO_LINK: raw::c_uint = 282;
pub const ZXRIO_MMAP: raw::c_uint = 27;
pub const ZXRIO_FCNTL: raw::c_uint = 28;
pub const ZXRIO_NUM_OPS: raw::c_uint = 29;
pub const ZXRIO_OBJECT_EXTRA: raw::c_uint = 32;
pub const FDIO_MMAP_FLAG_READ: raw::c_uint = 1;
pub const FDIO_MMAP_FLAG_WRITE: raw::c_uint = 2;
pub const FDIO_MMAP_FLAG_EXEC: raw::c_uint = 4;
pub const FDIO_MMAP_FLAG_PRIVATE: raw::c_uint = 65536;
pub const FDIO_FLAG_USE_FOR_STDIO: raw::c_uint = 32768;
pub const FDIO_NONBLOCKING: raw::c_uint = 1;
pub const FDIO_PROTOCOL_SERVICE: raw::c_uint = 0;
pub const FDIO_PROTOCOL_FILE: raw::c_uint = 1;
pub const FDIO_PROTOCOL_DIRECTORY: raw::c_uint = 2;
pub const FDIO_PROTOCOL_PIPE: raw::c_uint = 3;
pub const FDIO_PROTOCOL_VMOFILE: raw::c_uint = 4;
pub const FDIO_PROTOCOL_DEVICE: raw::c_uint = 5;
pub const FDIO_PROTOCOL_SOCKET: raw::c_uint = 6;
pub const FDIO_PROTOCOL_SOCKET_CONNECTED: raw::c_uint = 7;
pub const FDIO_EVT_READABLE: raw::c_uint = 1;
pub const FDIO_EVT_WRITABLE: raw::c_uint = 4;
pub const FDIO_EVT_ERROR: raw::c_uint = 8;
pub const FDIO_EVT_PEER_CLOSED: raw::c_uint = 8192;
pub const FDIO_EVT_ALL: raw::c_uint = 8205;
pub const IOCTL_KIND_DEFAULT: raw::c_int = 0;
pub const IOCTL_KIND_GET_HANDLE: raw::c_int = 1;
pub const IOCTL_KIND_GET_TWO_HANDLES: raw::c_int = 2;
pub const IOCTL_KIND_GET_THREE_HANDLES: raw::c_int = 4;
pub const IOCTL_KIND_SET_HANDLE: raw::c_int = 3;
pub const IOCTL_KIND_SET_TWO_HANDLES: raw::c_int = 5;
pub const IOCTL_FAMILY_RESERVED: raw::c_int = 0;
pub const IOCTL_FAMILY_DEVICE: raw::c_int = 1;
pub const IOCTL_FAMILY_VFS: raw::c_int = 2;
pub const IOCTL_FAMILY_DMCTL: raw::c_int = 3;
pub const IOCTL_FAMILY_TEST: raw::c_int = 4;
pub const IOCTL_FAMILY_CONSOLE: raw::c_int = 16;
pub const IOCTL_FAMILY_INPUT: raw::c_int = 17;
pub const IOCTL_FAMILY_DISPLAY: raw::c_int = 18;
pub const IOCTL_FAMILY_BLOCK: raw::c_int = 19;
pub const IOCTL_FAMILY_I2C: raw::c_int = 20;
pub const IOCTL_FAMILY_TPM: raw::c_int = 21;
pub const IOCTL_FAMILY_USB: raw::c_int = 22;
pub const IOCTL_FAMILY_HID: raw::c_int = 23;
pub const IOCTL_FAMILY_AUDIO: raw::c_int = 25;
pub const IOCTL_FAMILY_MIDI: raw::c_int = 26;
pub const IOCTL_FAMILY_KTRACE: raw::c_int = 27;
pub const IOCTL_FAMILY_BT_HCI: raw::c_int = 28;
pub const IOCTL_FAMILY_SYSINFO: raw::c_int = 29;
pub const IOCTL_FAMILY_GPU: raw::c_int = 30;
pub const IOCTL_FAMILY_RTC: raw::c_int = 31;
pub const IOCTL_FAMILY_ETH: raw::c_int = 32;
pub const IOCTL_FAMILY_IPT: raw::c_int = 33;
pub const IOCTL_FAMILY_RAMDISK: raw::c_int = 34;
pub const IOCTL_FAMILY_SDMMC: raw::c_int = 35;
pub const IOCTL_FAMILY_WLAN: raw::c_int = 36;
pub const IOCTL_FAMILY_PTY: raw::c_int = 37;
pub const IOCTL_FAMILY_NETCONFIG: raw::c_int = 38;
pub const IOCTL_FAMILY_ETHERTAP: raw::c_int = 39;
pub const IOCTL_FAMILY_USB_DEVICE: raw::c_int = 40;
pub const IOCTL_FAMILY_USB_VIRT_BUS: raw::c_int = 41;
pub const IOCTL_FAMILY_POWER: raw::c_int = 48;
pub const IOCTL_FAMILY_THERMAL: raw::c_int = 49;
pub const IOCTL_FAMILY_CAMERA: raw::c_int = 50;
pub const IOCTL_FAMILY_BT_HOST: raw::c_int = 51;
pub const IOCTL_FAMILY_WLANPHY: raw::c_int = 52;
pub const IOCTL_FAMILY_WLANTAP: raw::c_int = 0x36;
pub const IOCTL_FAMILY_DISPLAY_CONTROLLER: raw::c_int = 0x37;
pub const ZXRIO_SOCKET_DIR_NONE: &'static [u8; 5usize] = b"none\x00";
pub const ZXRIO_SOCKET_DIR_SOCKET: &'static [u8; 7usize] = b"socket\x00";
pub const ZXRIO_SOCKET_DIR_ACCEPT: &'static [u8; 7usize] = b"accept\x00";
pub const ZXRIO_GAI_REQ_NODE_MAXLEN: raw::c_uint = 256;
pub const ZXRIO_GAI_REQ_SERVICE_MAXLEN: raw::c_uint = 256;
pub const ZXRIO_GAI_REPLY_MAX: raw::c_uint = 4;
pub const VFS_MAX_HANDLES: raw::c_uint = 2;
pub const VNATTR_BLKSIZE: raw::c_uint = 512;
pub const ATTR_CTIME: raw::c_uint = 1;
pub const ATTR_MTIME: raw::c_uint = 2;
pub const ATTR_ATIME: raw::c_uint = 4;
pub const V_TYPE_MASK: raw::c_uint = 61440;
pub const V_TYPE_SOCK: raw::c_uint = 49152;
pub const V_TYPE_LINK: raw::c_uint = 40960;
pub const V_TYPE_FILE: raw::c_uint = 32768;
pub const V_TYPE_BDEV: raw::c_uint = 24576;
pub const V_TYPE_DIR: raw::c_uint = 16384;
pub const V_TYPE_CDEV: raw::c_uint = 8192;
pub const V_TYPE_PIPE: raw::c_uint = 4096;
pub const V_ISUID: raw::c_uint = 2048;
pub const V_ISGID: raw::c_uint = 1024;
pub const V_ISVTX: raw::c_uint = 512;
pub const V_IRWXU: raw::c_uint = 448;
pub const V_IRUSR: raw::c_uint = 256;
pub const V_IWUSR: raw::c_uint = 128;
pub const V_IXUSR: raw::c_uint = 64;
pub const V_IRWXG: raw::c_uint = 56;
pub const V_IRGRP: raw::c_uint = 32;
pub const V_IWGRP: raw::c_uint = 16;
pub const V_IXGRP: raw::c_uint = 8;
pub const V_IRWXO: raw::c_uint = 7;
pub const V_IROTH: raw::c_uint = 4;
pub const V_IWOTH: raw::c_uint = 2;
pub const V_IXOTH: raw::c_uint = 1;
pub const WATCH_EVENT_ADD_FILE: raw::c_int = 1;
pub const WATCH_EVENT_REMOVE_FILE: raw::c_int = 2;
pub const WATCH_EVENT_IDLE: raw::c_int = 3;

pub type zx_txid_t = u32;
#[repr(C)]
#[derive(Copy)]
pub struct zxrio_msg {
    pub txid: zx_txid_t,
    pub reserved0: u32,
    pub flags: u32,
    pub op: u32,
    pub datalen: u32,
    pub arg: i32,
    pub arg2: zxrio_msg__bindgen_ty_1,
    pub reserved1: i32,
    pub hcount: u32,
    pub handle: [zx_handle_t; 4usize],
    pub data: [u8; 8192usize],
}
#[repr(C)]
#[derive(Copy)]
pub union zxrio_msg__bindgen_ty_1 {
    pub off: i64,
    pub mode: u32,
    pub protocol: u32,
    pub op: u32,
    _bindgen_union_align: u64,
}
#[test]
fn bindgen_test_layout_zxrio_msg__bindgen_ty_1() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_msg__bindgen_ty_1>(),
        8usize,
        concat!("Size of: ", stringify!(zxrio_msg__bindgen_ty_1))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_msg__bindgen_ty_1>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_msg__bindgen_ty_1))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg__bindgen_ty_1)).off as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg__bindgen_ty_1),
            "::",
            stringify!(off)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg__bindgen_ty_1)).mode as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg__bindgen_ty_1),
            "::",
            stringify!(mode)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg__bindgen_ty_1)).protocol as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg__bindgen_ty_1),
            "::",
            stringify!(protocol)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg__bindgen_ty_1)).op as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg__bindgen_ty_1),
            "::",
            stringify!(op)
        )
    );
}
impl Clone for zxrio_msg__bindgen_ty_1 {
    fn clone(&self) -> Self {
        *self
    }
}
#[test]
fn bindgen_test_layout_zxrio_msg() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_msg>(),
        8248usize,
        concat!("Size of: ", stringify!(zxrio_msg))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_msg>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_msg))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).txid as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(txid)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).reserved0 as *const _ as usize },
        4usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(reserved0)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).flags as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(flags)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).op as *const _ as usize },
        12usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(op)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).datalen as *const _ as usize },
        16usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(datalen)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).arg as *const _ as usize },
        20usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(arg)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).arg2 as *const _ as usize },
        24usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(arg2)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).reserved1 as *const _ as usize },
        32usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(reserved)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).hcount as *const _ as usize },
        36usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(hcount)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).handle as *const _ as usize },
        40usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(handle)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_msg)).data as *const _ as usize },
        56usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_msg),
            "::",
            stringify!(data)
        )
    );
}
impl Clone for zxrio_msg {
    fn clone(&self) -> Self {
        *self
    }
}
pub type zxrio_msg_t = zxrio_msg;
pub type zxrio_cb_t = ::std::option::Option<
    unsafe extern "C" fn(msg: *mut zxrio_msg_t,
                         cookie: *mut raw::c_void)
                         -> zx_status_t,
>;
#[repr(C)]
#[derive(Debug, Copy)]
pub struct zxrio_object_t {
    pub status: zx_status_t,
    pub type_: u32,
    pub extra: [u8; 32usize],
    pub esize: u32,
    pub hcount: u32,
    pub handle: [zx_handle_t; 3usize],
}
#[test]
fn bindgen_test_layout_zxrio_object_t() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_object_t>(),
        60usize,
        concat!("Size of: ", stringify!(zxrio_object_t))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_object_t>(),
        4usize,
        concat!("Alignment of ", stringify!(zxrio_object_t))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_object_t)).status as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_object_t),
            "::",
            stringify!(status)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_object_t)).type_ as *const _ as usize },
        4usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_object_t),
            "::",
            stringify!(type_)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_object_t)).extra as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_object_t),
            "::",
            stringify!(extra)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_object_t)).esize as *const _ as usize },
        40usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_object_t),
            "::",
            stringify!(esize)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_object_t)).hcount as *const _ as usize },
        44usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_object_t),
            "::",
            stringify!(hcount)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_object_t)).handle as *const _ as usize },
        48usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_object_t),
            "::",
            stringify!(handle)
        )
    );
}
impl Clone for zxrio_object_t {
    fn clone(&self) -> Self {
        *self
    }
}
#[repr(C)]
#[derive(Debug, Copy)]
pub struct zxrio_mmap_data {
    pub offset: usize,
    pub length: u64,
    pub flags: i32,
}
#[test]
fn bindgen_test_layout_zxrio_mmap_data() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_mmap_data>(),
        24usize,
        concat!("Size of: ", stringify!(zxrio_mmap_data))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_mmap_data>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_mmap_data))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_mmap_data)).offset as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_mmap_data),
            "::",
            stringify!(offset)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_mmap_data)).length as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_mmap_data),
            "::",
            stringify!(length)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_mmap_data)).flags as *const _ as usize },
        16usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_mmap_data),
            "::",
            stringify!(flags)
        )
    );
}
impl Clone for zxrio_mmap_data {
    fn clone(&self) -> Self {
        *self
    }
}
pub type zxrio_mmap_data_t = zxrio_mmap_data;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct fdio_dispatcher {
    _unused: [u8; 0],
}
pub type fdio_dispatcher_t = fdio_dispatcher;
pub type fdio_dispatcher_cb_t =
    ::std::option::Option<
        unsafe extern "C" fn(h: zx_handle_t,
                             func: *mut raw::c_void,
                             cookie: *mut raw::c_void)
                             -> zx_status_t,
    >;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct fdio_namespace {
    _unused: [u8; 0],
}
pub type fdio_ns_t = fdio_namespace;
#[repr(C)]
#[derive(Debug, Copy)]
pub struct fdio_flat_namespace {
    pub count: usize,
    pub handle: *mut zx_handle_t,
    pub type_: *mut u32,
    pub path: *const *const raw::c_char,
}
#[test]
fn bindgen_test_layout_fdio_flat_namespace() {
    assert_eq!(
        ::std::mem::size_of::<fdio_flat_namespace>(),
        32usize,
        concat!("Size of: ", stringify!(fdio_flat_namespace))
    );
    assert_eq!(
        ::std::mem::align_of::<fdio_flat_namespace>(),
        8usize,
        concat!("Alignment of ", stringify!(fdio_flat_namespace))
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_flat_namespace)).count as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_flat_namespace),
            "::",
            stringify!(count)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_flat_namespace)).handle as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_flat_namespace),
            "::",
            stringify!(handle)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_flat_namespace)).type_ as *const _ as usize },
        16usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_flat_namespace),
            "::",
            stringify!(type_)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_flat_namespace)).path as *const _ as usize },
        24usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_flat_namespace),
            "::",
            stringify!(path)
        )
    );
}
impl Clone for fdio_flat_namespace {
    fn clone(&self) -> Self {
        *self
    }
}
pub type fdio_flat_namespace_t = fdio_flat_namespace;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct fdio {
    _unused: [u8; 0],
}
pub type fdio_t = fdio;
pub type socklen_t = raw::c_uint;
pub type sa_family_t = raw::c_ushort;
#[repr(C)]
#[derive(Debug, Copy)]
pub struct sockaddr {
    pub sa_family: sa_family_t,
    pub sa_data: [raw::c_char; 14usize],
}
#[test]
fn bindgen_test_layout_sockaddr() {
    assert_eq!(
        ::std::mem::size_of::<sockaddr>(),
        16usize,
        concat!("Size of: ", stringify!(sockaddr))
    );
    assert_eq!(
        ::std::mem::align_of::<sockaddr>(),
        2usize,
        concat!("Alignment of ", stringify!(sockaddr))
    );
    assert_eq!(
        unsafe { &(*(0 as *const sockaddr)).sa_family as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(sockaddr),
            "::",
            stringify!(sa_family)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const sockaddr)).sa_data as *const _ as usize },
        2usize,
        concat!(
            "Alignment of field: ",
            stringify!(sockaddr),
            "::",
            stringify!(sa_data)
        )
    );
}
impl Clone for sockaddr {
    fn clone(&self) -> Self {
        *self
    }
}
#[repr(C)]
#[derive(Copy)]
pub struct sockaddr_storage {
    pub ss_family: sa_family_t,
    pub __ss_align: raw::c_ulong,
    pub __ss_padding: [raw::c_char; 112usize],
}
#[test]
fn bindgen_test_layout_sockaddr_storage() {
    assert_eq!(
        ::std::mem::size_of::<sockaddr_storage>(),
        128usize,
        concat!("Size of: ", stringify!(sockaddr_storage))
    );
    assert_eq!(
        ::std::mem::align_of::<sockaddr_storage>(),
        8usize,
        concat!("Alignment of ", stringify!(sockaddr_storage))
    );
    assert_eq!(
        unsafe { &(*(0 as *const sockaddr_storage)).ss_family as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(sockaddr_storage),
            "::",
            stringify!(ss_family)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const sockaddr_storage)).__ss_align as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(sockaddr_storage),
            "::",
            stringify!(__ss_align)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const sockaddr_storage)).__ss_padding as *const _ as usize },
        16usize,
        concat!(
            "Alignment of field: ",
            stringify!(sockaddr_storage),
            "::",
            stringify!(__ss_padding)
        )
    );
}
impl Clone for sockaddr_storage {
    fn clone(&self) -> Self {
        *self
    }
}
#[repr(C)]
#[derive(Debug, Copy)]
pub struct addrinfo {
    pub ai_flags: raw::c_int,
    pub ai_family: raw::c_int,
    pub ai_socktype: raw::c_int,
    pub ai_protocol: raw::c_int,
    pub ai_addrlen: socklen_t,
    pub ai_addr: *mut sockaddr,
    pub ai_canonname: *mut raw::c_char,
    pub ai_next: *mut addrinfo,
}
#[test]
fn bindgen_test_layout_addrinfo() {
    assert_eq!(
        ::std::mem::size_of::<addrinfo>(),
        48usize,
        concat!("Size of: ", stringify!(addrinfo))
    );
    assert_eq!(
        ::std::mem::align_of::<addrinfo>(),
        8usize,
        concat!("Alignment of ", stringify!(addrinfo))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_flags as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_flags)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_family as *const _ as usize },
        4usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_family)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_socktype as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_socktype)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_protocol as *const _ as usize },
        12usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_protocol)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_addrlen as *const _ as usize },
        16usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_addrlen)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_addr as *const _ as usize },
        24usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_addr)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_canonname as *const _ as usize },
        32usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_canonname)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_next as *const _ as usize },
        40usize,
        concat!(
            "Alignment of field: ",
            stringify!(addrinfo),
            "::",
            stringify!(ai_next)
        )
    );
}
impl Clone for addrinfo {
    fn clone(&self) -> Self {
        *self
    }
}
#[repr(C)]
#[derive(Copy)]
pub struct zxrio_gai_req {
    pub node_is_null: u8,
    pub service_is_null: u8,
    pub hints_is_null: u8,
    pub reserved: u8,
    pub reserved2: u32,
    pub node: [raw::c_char; 256usize],
    pub service: [raw::c_char; 256usize],
    pub hints: addrinfo,
}
#[test]
fn bindgen_test_layout_zxrio_gai_req() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_gai_req>(),
        568usize,
        concat!("Size of: ", stringify!(zxrio_gai_req))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_gai_req>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_gai_req))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).node_is_null as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(node_is_null)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).service_is_null as *const _ as usize },
        1usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(service_is_null)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).hints_is_null as *const _ as usize },
        2usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(hints_is_null)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).reserved as *const _ as usize },
        3usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(reserved)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).reserved2 as *const _ as usize },
        4usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(reserved2)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).node as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(node)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).service as *const _ as usize },
        264usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(service)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req)).hints as *const _ as usize },
        520usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req),
            "::",
            stringify!(hints)
        )
    );
}
impl Clone for zxrio_gai_req {
    fn clone(&self) -> Self {
        *self
    }
}
pub type zxrio_gai_req_t = zxrio_gai_req;
#[repr(C)]
#[derive(Copy)]
pub struct zxrio_gai_reply {
    pub res: [zxrio_gai_reply__bindgen_ty_1; 4usize],
    pub nres: i32,
    pub retval: i32,
}
#[repr(C)]
#[derive(Copy)]
pub struct zxrio_gai_reply__bindgen_ty_1 {
    pub ai: addrinfo,
    pub addr: sockaddr_storage,
}
#[test]
fn bindgen_test_layout_zxrio_gai_reply__bindgen_ty_1() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_gai_reply__bindgen_ty_1>(),
        176usize,
        concat!("Size of: ", stringify!(zxrio_gai_reply__bindgen_ty_1))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_gai_reply__bindgen_ty_1>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_gai_reply__bindgen_ty_1))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_reply__bindgen_ty_1)).ai as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_reply__bindgen_ty_1),
            "::",
            stringify!(ai)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_reply__bindgen_ty_1)).addr as *const _ as usize },
        48usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_reply__bindgen_ty_1),
            "::",
            stringify!(addr)
        )
    );
}
impl Clone for zxrio_gai_reply__bindgen_ty_1 {
    fn clone(&self) -> Self {
        *self
    }
}
#[test]
fn bindgen_test_layout_zxrio_gai_reply() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_gai_reply>(),
        712usize,
        concat!("Size of: ", stringify!(zxrio_gai_reply))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_gai_reply>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_gai_reply))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_reply)).res as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_reply),
            "::",
            stringify!(res)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_reply)).nres as *const _ as usize },
        704usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_reply),
            "::",
            stringify!(nres)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_reply)).retval as *const _ as usize },
        708usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_reply),
            "::",
            stringify!(retval)
        )
    );
}
impl Clone for zxrio_gai_reply {
    fn clone(&self) -> Self {
        *self
    }
}
pub type zxrio_gai_reply_t = zxrio_gai_reply;
#[repr(C)]
#[derive(Copy)]
pub union zxrio_gai_req_reply_t {
    pub req: zxrio_gai_req_t,
    pub reply: zxrio_gai_reply_t,
    _bindgen_union_align: [u64; 89usize],
}
#[test]
fn bindgen_test_layout_zxrio_gai_req_reply_t() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_gai_req_reply_t>(),
        712usize,
        concat!("Size of: ", stringify!(zxrio_gai_req_reply_t))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_gai_req_reply_t>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_gai_req_reply_t))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req_reply_t)).req as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req_reply_t),
            "::",
            stringify!(req)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_gai_req_reply_t)).reply as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_gai_req_reply_t),
            "::",
            stringify!(reply)
        )
    );
}
impl Clone for zxrio_gai_req_reply_t {
    fn clone(&self) -> Self {
        *self
    }
}
#[repr(C)]
#[derive(Copy)]
pub struct zxrio_sockaddr_reply {
    pub addr: sockaddr_storage,
    pub len: socklen_t,
}
#[test]
fn bindgen_test_layout_zxrio_sockaddr_reply() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_sockaddr_reply>(),
        136usize,
        concat!("Size of: ", stringify!(zxrio_sockaddr_reply))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_sockaddr_reply>(),
        8usize,
        concat!("Alignment of ", stringify!(zxrio_sockaddr_reply))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_sockaddr_reply)).addr as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_sockaddr_reply),
            "::",
            stringify!(addr)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_sockaddr_reply)).len as *const _ as usize },
        128usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_sockaddr_reply),
            "::",
            stringify!(len)
        )
    );
}
impl Clone for zxrio_sockaddr_reply {
    fn clone(&self) -> Self {
        *self
    }
}
pub type zxrio_sockaddr_reply_t = zxrio_sockaddr_reply;
#[repr(C)]
#[derive(Copy)]
pub struct zxrio_sockopt_req_reply {
    pub level: i32,
    pub optname: i32,
    pub optval: [raw::c_char; 128usize],
    pub optlen: socklen_t,
}
#[test]
fn bindgen_test_layout_zxrio_sockopt_req_reply() {
    assert_eq!(
        ::std::mem::size_of::<zxrio_sockopt_req_reply>(),
        140usize,
        concat!("Size of: ", stringify!(zxrio_sockopt_req_reply))
    );
    assert_eq!(
        ::std::mem::align_of::<zxrio_sockopt_req_reply>(),
        4usize,
        concat!("Alignment of ", stringify!(zxrio_sockopt_req_reply))
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_sockopt_req_reply)).level as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_sockopt_req_reply),
            "::",
            stringify!(level)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_sockopt_req_reply)).optname as *const _ as usize },
        4usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_sockopt_req_reply),
            "::",
            stringify!(optname)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_sockopt_req_reply)).optval as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_sockopt_req_reply),
            "::",
            stringify!(optval)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const zxrio_sockopt_req_reply)).optlen as *const _ as usize },
        136usize,
        concat!(
            "Alignment of field: ",
            stringify!(zxrio_sockopt_req_reply),
            "::",
            stringify!(optlen)
        )
    );
}
impl Clone for zxrio_sockopt_req_reply {
    fn clone(&self) -> Self {
        *self
    }
}
pub type zxrio_sockopt_req_reply_t = zxrio_sockopt_req_reply;
#[repr(C)]
#[derive(Copy)]
pub struct fdio_socket_msg {
    pub addr: sockaddr_storage,
    pub addrlen: socklen_t,
    pub flags: i32,
    pub data: [raw::c_char; 1usize],
}
#[test]
fn bindgen_test_layout_fdio_socket_msg() {
    assert_eq!(
        ::std::mem::size_of::<fdio_socket_msg>(),
        144usize,
        concat!("Size of: ", stringify!(fdio_socket_msg))
    );
    assert_eq!(
        ::std::mem::align_of::<fdio_socket_msg>(),
        8usize,
        concat!("Alignment of ", stringify!(fdio_socket_msg))
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_socket_msg)).addr as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_socket_msg),
            "::",
            stringify!(addr)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_socket_msg)).addrlen as *const _ as usize },
        128usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_socket_msg),
            "::",
            stringify!(addrlen)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_socket_msg)).flags as *const _ as usize },
        132usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_socket_msg),
            "::",
            stringify!(flags)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_socket_msg)).data as *const _ as usize },
        136usize,
        concat!(
            "Alignment of field: ",
            stringify!(fdio_socket_msg),
            "::",
            stringify!(data)
        )
    );
}
impl Clone for fdio_socket_msg {
    fn clone(&self) -> Self {
        *self
    }
}
pub type fdio_socket_msg_t = fdio_socket_msg;
#[repr(C)]
#[derive(Debug, Copy)]
pub struct vnattr {
    pub valid: u32,
    pub mode: u32,
    pub inode: u64,
    pub size: u64,
    pub blksize: u64,
    pub blkcount: u64,
    pub nlink: u64,
    pub create_time: u64,
    pub modify_time: u64,
}
#[test]
fn bindgen_test_layout_vnattr() {
    assert_eq!(
        ::std::mem::size_of::<vnattr>(),
        64usize,
        concat!("Size of: ", stringify!(vnattr))
    );
    assert_eq!(
        ::std::mem::align_of::<vnattr>(),
        8usize,
        concat!("Alignment of ", stringify!(vnattr))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).valid as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(valid)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).mode as *const _ as usize },
        4usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(mode)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).inode as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(inode)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).size as *const _ as usize },
        16usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(size)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).blksize as *const _ as usize },
        24usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(blksize)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).blkcount as *const _ as usize },
        32usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(blkcount)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).nlink as *const _ as usize },
        40usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(nlink)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).create_time as *const _ as usize },
        48usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(create_time)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).modify_time as *const _ as usize },
        56usize,
        concat!(
            "Alignment of field: ",
            stringify!(vnattr),
            "::",
            stringify!(modify_time)
        )
    );
}
impl Clone for vnattr {
    fn clone(&self) -> Self {
        *self
    }
}
pub type vnattr_t = vnattr;
#[repr(C)]
#[derive(Debug)]
pub struct vdirent {
    pub size: u32,
    pub type_: u32,
    pub name: __IncompleteArrayField<raw::c_char>,
}
#[test]
fn bindgen_test_layout_vdirent() {
    assert_eq!(
        ::std::mem::size_of::<vdirent>(),
        8usize,
        concat!("Size of: ", stringify!(vdirent))
    );
    assert_eq!(
        ::std::mem::align_of::<vdirent>(),
        4usize,
        concat!("Alignment of ", stringify!(vdirent))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vdirent)).size as *const _ as usize },
        0usize,
        concat!(
            "Alignment of field: ",
            stringify!(vdirent),
            "::",
            stringify!(size)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vdirent)).type_ as *const _ as usize },
        4usize,
        concat!(
            "Alignment of field: ",
            stringify!(vdirent),
            "::",
            stringify!(type_)
        )
    );
    assert_eq!(
        unsafe { &(*(0 as *const vdirent)).name as *const _ as usize },
        8usize,
        concat!(
            "Alignment of field: ",
            stringify!(vdirent),
            "::",
            stringify!(name)
        )
    );
}
pub type vdirent_t = vdirent;
pub type watchdir_func_t = ::std::option::Option<
    unsafe extern "C" fn(dirfd: raw::c_int,
                         event: raw::c_int,
                         fn_: *const raw::c_char,
                         cookie: *mut raw::c_void)
                         -> zx_status_t,
>;

#[link(name = "fdio")]
extern "C" {
    pub fn zxrio_handler(
        h: zx_handle_t,
        cb: *mut raw::c_void,
        cookie: *mut raw::c_void,
    ) -> zx_status_t;
    pub fn zxrio_handle_rpc(
        h: zx_handle_t,
        msg: *mut zxrio_msg_t,
        cb: zxrio_cb_t,
        cookie: *mut raw::c_void,
    ) -> zx_status_t;
    pub fn zxrio_handle_close(cb: zxrio_cb_t, cookie: *mut raw::c_void) -> zx_status_t;
    pub fn zxrio_txn_handoff(
        server: zx_handle_t,
        reply: zx_handle_t,
        msg: *mut zxrio_msg_t,
    ) -> zx_status_t;
    pub fn fdio_dispatcher_create(
        out: *mut *mut fdio_dispatcher_t,
        cb: fdio_dispatcher_cb_t,
    ) -> zx_status_t;
    pub fn fdio_dispatcher_start(
        md: *mut fdio_dispatcher_t,
        name: *const raw::c_char,
    ) -> zx_status_t;
    pub fn fdio_dispatcher_run(md: *mut fdio_dispatcher_t);
    pub fn fdio_dispatcher_add(
        md: *mut fdio_dispatcher_t,
        h: zx_handle_t,
        func: *mut raw::c_void,
        cookie: *mut raw::c_void,
    ) -> zx_status_t;
    pub fn fdio_dispatcher_add_etc(
        md: *mut fdio_dispatcher_t,
        h: zx_handle_t,
        callback: fdio_dispatcher_cb_t,
        func: *mut raw::c_void,
        cookie: *mut raw::c_void,
    ) -> zx_status_t;
    pub fn fdio_wait_fd(
        fd: raw::c_int,
        events: u32,
        pending: *mut u32,
        deadline: zx_time_t,
    ) -> zx_status_t;
    pub fn fdio_handle_fd(
        h: zx_handle_t,
        signals_in: zx_signals_t,
        signals_out: zx_signals_t,
        shared_handle: bool,
    ) -> raw::c_int;
    pub fn fdio_ioctl(
        fd: raw::c_int,
        op: raw::c_int,
        in_buf: *const raw::c_void,
        in_len: usize,
        out_buf: *mut raw::c_void,
        out_len: usize,
    ) -> isize;
    pub fn fdio_pipe_half(handle: *mut zx_handle_t, type_: *mut u32) -> zx_status_t;
    pub fn fdio_get_vmo(fd: raw::c_int, out_vmo: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_get_exact_vmo(fd: raw::c_int, out_vmo: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_vmo_fd(vmo: zx_handle_t, offset: u64, length: u64) -> raw::c_int;
    pub fn fdio_ns_create(out: *mut *mut fdio_ns_t) -> zx_status_t;
    pub fn fdio_ns_destroy(ns: *mut fdio_ns_t) -> zx_status_t;
    pub fn fdio_ns_bind(
        ns: *mut fdio_ns_t,
        path: *const raw::c_char,
        h: zx_handle_t,
    ) -> zx_status_t;
    pub fn fdio_ns_bind_fd(
        ns: *mut fdio_ns_t,
        path: *const raw::c_char,
        fd: raw::c_int,
    ) -> zx_status_t;
    pub fn fdio_ns_opendir(ns: *mut fdio_ns_t) -> raw::c_int;
    pub fn fdio_ns_chdir(ns: *mut fdio_ns_t) -> zx_status_t;
    pub fn fdio_ns_install(ns: *mut fdio_ns_t) -> zx_status_t;
    pub fn fdio_ns_export(ns: *mut fdio_ns_t, out: *mut *mut fdio_flat_namespace_t) -> zx_status_t;
    pub fn fdio_ns_export_root(out: *mut *mut fdio_flat_namespace_t) -> zx_status_t;
    pub fn fdio_ns_connect(
        ns: *mut fdio_ns_t,
        path: *const raw::c_char,
        h: zx_handle_t,
    ) -> zx_status_t;
    pub fn __fdio_cleanpath(
        in_: *const raw::c_char,
        out: *mut raw::c_char,
        outlen: *mut usize,
        is_dir: *mut bool,
    ) -> zx_status_t;
    pub fn __fdio_fd_to_io(fd: raw::c_int) -> *mut fdio_t;
    pub fn __fdio_release(io: *mut fdio_t);
    pub fn __fdio_wait_begin(
        io: *mut fdio_t,
        events: u32,
        handle_out: *mut zx_handle_t,
        signals_out: *mut zx_signals_t,
    );
    pub fn __fdio_wait_end(io: *mut fdio_t, signals: zx_signals_t, events_out: *mut u32);
    pub fn fdio_clone_cwd(handles: *mut zx_handle_t, types: *mut u32) -> zx_status_t;
    pub fn fdio_clone_fd(
        fd: raw::c_int,
        newfd: raw::c_int,
        handles: *mut zx_handle_t,
        types: *mut u32,
    ) -> zx_status_t;
    pub fn fdio_pipe_pair_raw(handles: *mut zx_handle_t, types: *mut u32) -> zx_status_t;
    pub fn fdio_transfer_fd(
        fd: raw::c_int,
        newfd: raw::c_int,
        handles: *mut zx_handle_t,
        types: *mut u32,
    ) -> zx_status_t;
    pub fn fdio_create_fd(
        handles: *mut zx_handle_t,
        types: *mut u32,
        hcount: usize,
        fd_out: *mut raw::c_int,
    ) -> zx_status_t;
    pub fn fdio_bind_to_fd(io: *mut fdio_t, fd: raw::c_int, starting_fd: raw::c_int) -> raw::c_int;
    pub fn fdio_unbind_from_fd(fd: raw::c_int, io_out: *mut *mut fdio_t) -> zx_status_t;
    pub fn fdio_get_service_handle(fd: raw::c_int, out: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_null_create() -> *mut fdio_t;
    pub fn fdio_remote_create(h: zx_handle_t, e: zx_handle_t) -> *mut fdio_t;
    pub fn fdio_service_create(arg1: zx_handle_t) -> *mut fdio_t;
    pub fn fdio_logger_create(arg1: zx_handle_t) -> *mut fdio_t;
    pub fn fdio_output_create(
        func: ::std::option::Option<
            unsafe extern "C" fn(cookie: *mut raw::c_void,
                                 data: *const raw::c_void,
                                 len: usize)
                                 -> isize,
        >,
        cookie: *mut raw::c_void,
    ) -> *mut fdio_t;
    pub fn fdio_service_connect(svcpath: *const raw::c_char, h: zx_handle_t) -> zx_status_t;
    pub fn fdio_service_connect_at(
        dir: zx_handle_t,
        path: *const raw::c_char,
        h: zx_handle_t,
    ) -> zx_status_t;
    pub fn fdio_service_clone(h: zx_handle_t) -> zx_handle_t;
    pub fn fdio_watch_directory(
        dirfd: raw::c_int,
        cb: watchdir_func_t,
        deadline: zx_time_t,
        cookie: *mut raw::c_void,
    ) -> zx_status_t;
}
