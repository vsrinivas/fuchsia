// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file was generated with bindgen, then modified to consume already bound
// types and remove various bindgen-isms that we don't want.

use {fuchsia_zircon::sys::*, std::os::raw};

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
    fn fmt(&self, fmt: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
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

pub const FDIO_MAX_FD: raw::c_uint = 1024;
pub const FDIO_CHUNK_SIZE: raw::c_uint = 8192;
pub const FDIO_MAX_FILENAME: raw::c_uint = 255;
pub const FDIO_SPAWN_ACTION_CLONE_FD: u32 = 1;
pub const FDIO_SPAWN_ACTION_TRANSFER_FD: u32 = 2;
pub const FDIO_SPAWN_ACTION_ADD_NS_ENTRY: u32 = 3;
pub const FDIO_SPAWN_ACTION_ADD_HANDLE: u32 = 4;
pub const FDIO_SPAWN_ACTION_SET_NAME: u32 = 5;
pub const FDIO_SPAWN_CLONE_JOB: u32 = 0x1;
pub const FDIO_SPAWN_DEFAULT_LDSVC: u32 = 0x2;
pub const FDIO_SPAWN_CLONE_NAMESPACE: u32 = 0x4;
pub const FDIO_SPAWN_CLONE_STDIO: u32 = 0x8;
pub const FDIO_SPAWN_CLONE_ENVIRON: u32 = 0x10;
pub const FDIO_SPAWN_CLONE_ALL: u32 = 0xFFFF;
pub const FDIO_SPAWN_ERR_MSG_MAX_LENGTH: u32 = 1024;
pub const FDIO_MMAP_FLAG_READ: raw::c_uint = 1;
pub const FDIO_MMAP_FLAG_WRITE: raw::c_uint = 2;
pub const FDIO_MMAP_FLAG_EXEC: raw::c_uint = 4;
pub const FDIO_MMAP_FLAG_PRIVATE: raw::c_uint = 65536;
pub const FDIO_FLAG_USE_FOR_STDIO: raw::c_uint = 32768;
pub const FDIO_NONBLOCKING: raw::c_uint = 1;
pub const FDIO_EVT_READABLE: raw::c_uint = 1;
pub const FDIO_EVT_WRITABLE: raw::c_uint = 4;
pub const FDIO_EVT_ERROR: raw::c_uint = 8;
pub const FDIO_EVT_PEER_CLOSED: raw::c_uint = 8192;
pub const FDIO_EVT_ALL: raw::c_uint = 8205;
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
        concat!("Alignment of field: ", stringify!(fdio_flat_namespace), "::", stringify!(count))
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_flat_namespace)).handle as *const _ as usize },
        8usize,
        concat!("Alignment of field: ", stringify!(fdio_flat_namespace), "::", stringify!(handle))
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_flat_namespace)).type_ as *const _ as usize },
        16usize,
        concat!("Alignment of field: ", stringify!(fdio_flat_namespace), "::", stringify!(type_))
    );
    assert_eq!(
        unsafe { &(*(0 as *const fdio_flat_namespace)).path as *const _ as usize },
        24usize,
        concat!("Alignment of field: ", stringify!(fdio_flat_namespace), "::", stringify!(path))
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
        concat!("Alignment of field: ", stringify!(sockaddr), "::", stringify!(sa_family))
    );
    assert_eq!(
        unsafe { &(*(0 as *const sockaddr)).sa_data as *const _ as usize },
        2usize,
        concat!("Alignment of field: ", stringify!(sockaddr), "::", stringify!(sa_data))
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
        concat!("Alignment of field: ", stringify!(sockaddr_storage), "::", stringify!(ss_family))
    );
    assert_eq!(
        unsafe { &(*(0 as *const sockaddr_storage)).__ss_align as *const _ as usize },
        8usize,
        concat!("Alignment of field: ", stringify!(sockaddr_storage), "::", stringify!(__ss_align))
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
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_flags))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_family as *const _ as usize },
        4usize,
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_family))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_socktype as *const _ as usize },
        8usize,
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_socktype))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_protocol as *const _ as usize },
        12usize,
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_protocol))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_addrlen as *const _ as usize },
        16usize,
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_addrlen))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_addr as *const _ as usize },
        24usize,
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_addr))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_canonname as *const _ as usize },
        32usize,
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_canonname))
    );
    assert_eq!(
        unsafe { &(*(0 as *const addrinfo)).ai_next as *const _ as usize },
        40usize,
        concat!("Alignment of field: ", stringify!(addrinfo), "::", stringify!(ai_next))
    );
}
impl Clone for addrinfo {
    fn clone(&self) -> Self {
        *self
    }
}
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
    assert_eq!(::std::mem::size_of::<vnattr>(), 64usize, concat!("Size of: ", stringify!(vnattr)));
    assert_eq!(
        ::std::mem::align_of::<vnattr>(),
        8usize,
        concat!("Alignment of ", stringify!(vnattr))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).valid as *const _ as usize },
        0usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(valid))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).mode as *const _ as usize },
        4usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(mode))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).inode as *const _ as usize },
        8usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(inode))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).size as *const _ as usize },
        16usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(size))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).blksize as *const _ as usize },
        24usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(blksize))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).blkcount as *const _ as usize },
        32usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(blkcount))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).nlink as *const _ as usize },
        40usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(nlink))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).create_time as *const _ as usize },
        48usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(create_time))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vnattr)).modify_time as *const _ as usize },
        56usize,
        concat!("Alignment of field: ", stringify!(vnattr), "::", stringify!(modify_time))
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
    assert_eq!(::std::mem::size_of::<vdirent>(), 8usize, concat!("Size of: ", stringify!(vdirent)));
    assert_eq!(
        ::std::mem::align_of::<vdirent>(),
        4usize,
        concat!("Alignment of ", stringify!(vdirent))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vdirent)).size as *const _ as usize },
        0usize,
        concat!("Alignment of field: ", stringify!(vdirent), "::", stringify!(size))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vdirent)).type_ as *const _ as usize },
        4usize,
        concat!("Alignment of field: ", stringify!(vdirent), "::", stringify!(type_))
    );
    assert_eq!(
        unsafe { &(*(0 as *const vdirent)).name as *const _ as usize },
        8usize,
        concat!("Alignment of field: ", stringify!(vdirent), "::", stringify!(name))
    );
}
pub type vdirent_t = vdirent;
pub type watchdir_func_t = ::std::option::Option<
    unsafe extern "C" fn(
        dirfd: raw::c_int,
        event: raw::c_int,
        fn_: *const raw::c_char,
        cookie: *mut raw::c_void,
    ) -> zx_status_t,
>;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct fdio_spawn_action_t {
    pub action_tag: u32,
    pub action_value: fdio_spawn_action_union_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union fdio_spawn_action_union_t {
    pub fd: fdio_spawn_action_fd_t,
    pub ns: fdio_spawn_action_ns_t,
    pub h: fdio_spawn_action_h_t,
    pub name: fdio_spawn_action_name_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct fdio_spawn_action_fd_t {
    pub local_fd: raw::c_int,
    pub target_fd: raw::c_int,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct fdio_spawn_action_ns_t {
    pub prefix: *const raw::c_char,
    pub handle: zx_handle_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct fdio_spawn_action_h_t {
    pub id: u32,
    pub handle: zx_handle_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct fdio_spawn_action_name_t {
    pub data: *const raw::c_char,
}

#[link(name = "fdio")]
extern "C" {
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
    pub fn fdio_pipe_half(fd: *mut i32, handle: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_get_vmo_copy(fd: raw::c_int, out_vmo: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_get_vmo_clone(fd: raw::c_int, out_vmo: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_get_exact_vmo(fd: raw::c_int, out_vmo: *mut zx_handle_t) -> zx_status_t;
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
    pub fn fdio_ns_unbind(ns: *mut fdio_ns_t, path: *const raw::c_char) -> zx_status_t;
    pub fn fdio_ns_opendir(ns: *mut fdio_ns_t) -> raw::c_int;
    pub fn fdio_ns_chdir(ns: *mut fdio_ns_t) -> zx_status_t;
    pub fn fdio_ns_get_installed(ns: *mut *mut fdio_ns_t) -> zx_status_t;
    pub fn fdio_ns_export(ns: *mut fdio_ns_t, out: *mut *mut fdio_flat_namespace_t) -> zx_status_t;
    pub fn fdio_ns_export_root(out: *mut *mut fdio_flat_namespace_t) -> zx_status_t;
    pub fn fdio_ns_connect(
        ns: *mut fdio_ns_t,
        path: *const raw::c_char,
        zxflags: u32,
        h: zx_handle_t,
    ) -> zx_status_t;
    pub fn __fdio_cleanpath(
        in_: *const raw::c_char,
        out: *mut raw::c_char,
        outlen: *mut usize,
        is_dir: *mut bool,
    ) -> zx_status_t;
    pub fn fdio_unsafe_fd_to_io(fd: raw::c_int) -> *mut fdio_t;
    pub fn fdio_unsafe_borrow_channel(io: *mut fdio) -> zx_handle_t;
    pub fn fdio_unsafe_release(io: *mut fdio_t);
    pub fn fdio_unsafe_wait_begin(
        io: *mut fdio_t,
        events: u32,
        handle_out: *mut zx_handle_t,
        signals_out: *mut zx_signals_t,
    );
    pub fn fdio_unsafe_wait_end(io: *mut fdio_t, signals: zx_signals_t, events_out: *mut u32);
    pub fn fdio_fd_clone(fd: raw::c_int, handle: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_pipe_pair_raw(handles: *mut zx_handle_t, types: *mut u32) -> zx_status_t;
    pub fn fdio_fd_transfer(fd: raw::c_int, handle: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_fd_create(handle: zx_handle_t, fd_out: *mut raw::c_int) -> zx_status_t;
    pub fn fdio_bind_to_fd(io: *mut fdio_t, fd: raw::c_int, starting_fd: raw::c_int) -> raw::c_int;
    pub fn fdio_unbind_from_fd(fd: raw::c_int, io_out: *mut *mut fdio_t) -> zx_status_t;
    pub fn fdio_get_service_handle(fd: raw::c_int, out: *mut zx_handle_t) -> zx_status_t;
    pub fn fdio_null_create() -> *mut fdio_t;
    pub fn fdio_service_connect(svcpath: *const raw::c_char, h: zx_handle_t) -> zx_status_t;
    pub fn fdio_service_connect_at(
        dir: zx_handle_t,
        path: *const raw::c_char,
        h: zx_handle_t,
    ) -> zx_status_t;
    pub fn fdio_service_clone(h: zx_handle_t) -> zx_handle_t;
    pub fn fdio_open(path: *const raw::c_char, flags: u32, h: zx_handle_t) -> zx_status_t;
    pub fn fdio_open_at(
        dir: zx_handle_t,
        path: *const raw::c_char,
        flags: u32,
        h: zx_handle_t,
    ) -> zx_status_t;
    pub fn fdio_open_fd(
        path: *const raw::c_char,
        flags: u32,
        fd_out: *mut raw::c_int,
    ) -> zx_status_t;
    pub fn fdio_open_fd_at(
        dir_fd: raw::c_int,
        path: *const raw::c_char,
        flags: u32,
        fd_out: *mut raw::c_int,
    ) -> zx_status_t;
    pub fn fdio_spawn(
        job: zx_handle_t,
        flags: u32,
        path: *const raw::c_char,
        argv: *const *const raw::c_char,
        process_out: *mut zx_handle_t,
    ) -> zx_status_t;
    pub fn fdio_spawn_etc(
        job: zx_handle_t,
        flags: u32,
        path: *const raw::c_char,
        argv: *const *const raw::c_char,
        environ: *const *const raw::c_char,
        action_count: usize,
        actions: *const fdio_spawn_action_t,
        process_out: *mut zx_handle_t,
        err_msg_out: *mut [raw::c_char; FDIO_SPAWN_ERR_MSG_MAX_LENGTH as usize],
    ) -> zx_status_t;
    pub fn fdio_spawn_vmo(
        job: zx_handle_t,
        flags: u32,
        executable_vmo: zx_handle_t,
        argv: *const *const raw::c_char,
        environ: *const *const raw::c_char,
        action_count: usize,
        actions: *const fdio_spawn_action_t,
        process_out: *mut zx_handle_t,
        err_msg_out: *mut [raw::c_char; FDIO_SPAWN_ERR_MSG_MAX_LENGTH as usize],
    ) -> zx_status_t;
    pub fn fdio_watch_directory(
        dirfd: raw::c_int,
        cb: watchdir_func_t,
        deadline: zx_time_t,
        cookie: *mut raw::c_void,
    ) -> zx_status_t;
}
