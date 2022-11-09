// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for the Zircon fdio library

mod fdio_sys;

mod spawn_builder;

pub use spawn_builder::{Error as SpawnBuilderError, SpawnBuilder};

use {
    bitflags::bitflags,
    fidl_fuchsia_device as fdevice, fidl_fuchsia_io as fio,
    fuchsia_runtime::{HandleInfo, HandleInfoError},
    fuchsia_zircon::{self as zx, AsHandleRef as _, HandleBased as _},
    std::{
        convert::TryInto as _,
        ffi::{CStr, CString, NulError},
        fs::File,
        marker::PhantomData,
        mem::{self, MaybeUninit},
        num::TryFromIntError,
        os::{
            raw,
            unix::io::{AsRawFd, FromRawFd, IntoRawFd, RawFd},
        },
        str::Utf8Error,
    },
};

/// Connects a channel to a named service.
pub fn service_connect(service_path: &str, channel: zx::Channel) -> Result<(), zx::Status> {
    let service_path =
        CString::new(service_path).map_err(|NulError { .. }| zx::Status::INVALID_ARGS)?;
    let service_path = service_path.as_ptr();

    // The channel is always consumed.
    let channel = channel.into_raw();
    let status = unsafe { fdio_sys::fdio_service_connect(service_path, channel) };
    zx::Status::ok(status)
}

/// Connects a channel to a named service relative to a directory `dir`.
/// `dir` must be a directory protocol channel.
pub fn service_connect_at(
    dir: &zx::Channel,
    service_path: &str,
    channel: zx::Channel,
) -> Result<(), zx::Status> {
    let dir = dir.raw_handle();
    let service_path =
        CString::new(service_path).map_err(|NulError { .. }| zx::Status::INVALID_ARGS)?;
    let service_path = service_path.as_ptr();

    // The channel is always consumed.
    let channel = channel.into_raw();
    let status = unsafe { fdio_sys::fdio_service_connect_at(dir, service_path, channel) };
    zx::Status::ok(status)
}

/// Opens the remote object at the given `path` with the given `flags` asynchronously.
/// ('asynchronous' here is refering to fuchsia.io.Directory.Open not having a return value).
///
/// Wraps fdio_open.
pub fn open(path: &str, flags: fio::OpenFlags, channel: zx::Channel) -> Result<(), zx::Status> {
    let path = CString::new(path).map_err(|NulError { .. }| zx::Status::INVALID_ARGS)?;
    let path = path.as_ptr();
    let flags = flags.bits();

    // The channel is always consumed.
    let channel = channel.into_raw();
    let status = unsafe { fdio_sys::fdio_open(path, flags, channel) };
    zx::Status::ok(status)
}

/// Opens the remote object at the given `path` relative to the given `dir` with the given `flags`
/// asynchronously. ('asynchronous' here is refering to fuchsia.io.Directory.Open not having a
/// return value).
///
/// `dir` must be a directory protocol channel.
///
/// Wraps fdio_open_at.
pub fn open_at(
    dir: &zx::Channel,
    path: &str,
    flags: fio::OpenFlags,
    channel: zx::Channel,
) -> Result<(), zx::Status> {
    let dir = dir.raw_handle();
    let path = CString::new(path).map_err(|NulError { .. }| zx::Status::INVALID_ARGS)?;
    let path = path.as_ptr();
    let flags = flags.bits();

    // The channel is always consumed.
    let channel = channel.into_raw();
    let status = unsafe { fdio_sys::fdio_open_at(dir, path, flags, channel) };
    zx::Status::ok(status)
}

/// Opens the remote object at the given `path` with the given `flags` synchronously, and on
/// success, binds that channel to a file descriptor and returns it.
///
/// Wraps fdio_open_fd.
pub fn open_fd(path: &str, flags: fio::OpenFlags) -> Result<File, zx::Status> {
    let path = CString::new(path).map_err(|NulError { .. }| zx::Status::INVALID_ARGS)?;
    let path = path.as_ptr();
    let flags = flags.bits();

    // file descriptors are always positive; we expect fdio to initialize this to a legal value.
    let mut fd = MaybeUninit::new(-1);
    let status = {
        let fd = fd.as_mut_ptr();
        unsafe { fdio_sys::fdio_open_fd(path, flags, fd) }
    };
    let () = zx::Status::ok(status)?;
    let fd = unsafe { fd.assume_init() };
    debug_assert!(fd >= 0, "{} >= 0", fd);
    let f = unsafe { File::from_raw_fd(fd) };
    Ok(f)
}

/// Opens the remote object at the given `path` relative to the given `dir` with the given `flags`
/// synchronously, and on success, binds that channel to a file descriptor and returns it.
///
/// `dir` must be backed by a directory protocol channel (even though it is
/// wrapped in a std::fs::File).
///
/// Wraps fdio_open_fd_at.
pub fn open_fd_at(dir: &File, path: &str, flags: fio::OpenFlags) -> Result<File, zx::Status> {
    let dir = dir.as_raw_fd();
    let path = CString::new(path).map_err(|NulError { .. }| zx::Status::INVALID_ARGS)?;
    let path = path.as_ptr();
    let flags = flags.bits();

    // file descriptors are always positive; we expect fdio to initialize this to a legal value.
    let mut fd = MaybeUninit::new(-1);
    let status = {
        let fd = fd.as_mut_ptr();
        unsafe { fdio_sys::fdio_open_fd_at(dir, path, flags, fd) }
    };
    let () = zx::Status::ok(status)?;
    let fd = unsafe { fd.assume_init() };
    debug_assert!(fd >= 0, "{} >= 0", fd);
    let f = unsafe { File::from_raw_fd(fd) };
    Ok(f)
}

/// Clones an object's underlying handle.
pub fn clone_fd(f: &impl AsRawFd) -> Result<zx::Handle, zx::Status> {
    let fd = f.as_raw_fd();
    // we expect fdio to initialize this to a legal value.
    let mut handle = MaybeUninit::new(zx::Handle::invalid().raw_handle());
    let status = {
        let handle = handle.as_mut_ptr();
        unsafe { fdio_sys::fdio_fd_clone(fd, handle) }
    };
    let () = zx::Status::ok(status)?;
    let handle = unsafe { handle.assume_init() };
    let handle = unsafe { zx::Handle::from_raw(handle) };
    debug_assert!(!handle.is_invalid(), "({:?}).is_invalid()", handle);
    Ok(handle)
}

/// Removes an object from the file descriptor table and returns its underlying handle.
pub fn transfer_fd(f: impl AsRawFd) -> Result<zx::Handle, zx::Status> {
    let fd = f.as_raw_fd();
    // we expect fdio to initialize this to a legal value.
    let mut handle = MaybeUninit::new(zx::Handle::invalid().raw_handle());
    let status = {
        let handle = handle.as_mut_ptr();
        unsafe { fdio_sys::fdio_fd_transfer(fd, handle) }
    };
    let () = zx::Status::ok(status)?;
    let handle = unsafe { handle.assume_init() };
    let handle = unsafe { zx::Handle::from_raw(handle) };
    debug_assert!(!handle.is_invalid(), "({:?}).is_invalid()", handle);
    Ok(handle)
}

/// Create an object from a handle.
///
/// Afterward, the handle is owned by fdio, and will close with `F`.
/// See `transfer_fd` for a way to get it back.
pub fn create_fd<F: FromRawFd>(handle: zx::Handle) -> Result<F, zx::Status> {
    let handle = handle.into_raw();
    // file descriptors are always positive; we expect fdio to initialize this to a legal value.
    let mut fd = MaybeUninit::new(-1);
    let status = {
        let fd = fd.as_mut_ptr();
        unsafe { fdio_sys::fdio_fd_create(handle, fd) }
    };
    let () = zx::Status::ok(status)?;
    let fd = unsafe { fd.assume_init() };
    debug_assert!(fd >= 0, "{} >= 0", fd);
    let f = unsafe { F::from_raw_fd(fd) };
    Ok(f)
}

/// Bind a handle to a specific file descriptor.
///
/// Afterward, the handle is owned by fdio, and will close when the file descriptor is closed.
/// See `transfer_fd` for a way to get it back.
pub fn bind_to_fd(handle: zx::Handle, fd: RawFd) -> Result<(), zx::Status> {
    if fd < 0 {
        // fdio_bind_to_fd supports finding the next available fd when provided with a negative
        // number, but due to lack of use-cases for this in Rust this is currently unsupported by
        // this function.
        return Err(zx::Status::INVALID_ARGS);
    }

    // The handle is always consumed.
    let handle = handle.into_raw();
    // we expect fdio to initialize this to a legal value.
    let mut fdio = MaybeUninit::new(std::ptr::null_mut());

    let status = unsafe { fdio_sys::fdio_create(handle, fdio.as_mut_ptr()) };
    let () = zx::Status::ok(status)?;
    let fdio = unsafe { fdio.assume_init() };
    debug_assert_ne!(fdio, std::ptr::null_mut());
    // The fdio object is always consumed.
    let bound_fd = unsafe { fdio_sys::fdio_bind_to_fd(fdio, fd, 0) };
    if bound_fd < 0 {
        return Err(zx::Status::BAD_STATE);
    }
    // We requested a specific fd, we expect to have gotten it, or failed.
    assert_eq!(bound_fd, fd);
    Ok(())
}

/// Clones an object's underlying handle and checks that it is a channel.
pub fn clone_channel(f: &impl AsRawFd) -> Result<zx::Channel, zx::Status> {
    let handle = clone_fd(f)?;
    let zx::HandleBasicInfo { object_type, .. } = handle.basic_info()?;
    if object_type == zx::ObjectType::CHANNEL {
        Ok(handle.into())
    } else {
        Err(zx::Status::WRONG_TYPE)
    }
}

/// Retrieves the topological path for a device node.
pub fn device_get_topo_path(dev: &File) -> Result<String, zx::Status> {
    let channel = clone_channel(dev)?;
    let interface = fdevice::ControllerSynchronousProxy::new(channel);
    interface
        .get_topological_path(fuchsia_zircon::Time::INFINITE)
        .map_err(|_| zx::Status::IO)?
        .map_err(zx::Status::from_raw)
}

/// Creates a named pipe and returns one end as a zx::Socket.
pub fn pipe_half() -> Result<(File, zx::Socket), zx::Status> {
    // file descriptors are always positive; we expect fdio to initialize this to a legal value.
    let mut fd = MaybeUninit::new(-1);
    // we expect fdio to initialize this to a legal value.
    let mut handle = MaybeUninit::new(zx::Handle::invalid().raw_handle());
    let status = {
        let fd = fd.as_mut_ptr();
        let handle = handle.as_mut_ptr();
        unsafe { fdio_sys::fdio_pipe_half(fd, handle) }
    };
    let () = zx::Status::ok(status)?;
    let fd = unsafe { fd.assume_init() };
    debug_assert!(fd >= 0, "{} >= 0", fd);
    let f = unsafe { File::from_raw_fd(fd) };
    let handle = unsafe { handle.assume_init() };
    let handle = unsafe { zx::Handle::from_raw(handle) };
    debug_assert!(!handle.is_invalid(), "({:?}).is_invalid()", handle);
    Ok((f, zx::Socket::from(handle)))
}

bitflags! {
    /// Options to allow some or all of the environment of the running process
    /// to be shared with the process being spawned.
    pub struct SpawnOptions: u32 {
        /// Provide the spawned process with the job in which the process was created.
        ///
        /// The job will be available to the new process as the PA_JOB_DEFAULT argument
        /// (exposed in Rust as `fuchsia_runtim::job_default()`).
        const CLONE_JOB = fdio_sys::FDIO_SPAWN_CLONE_JOB;

        /// Provide the spawned process with the shared library loader via the
        /// PA_LDSVC_LOADER argument.
        const DEFAULT_LOADER = fdio_sys::FDIO_SPAWN_DEFAULT_LDSVC;

        /// Clones the filesystem namespace into the spawned process.
        const CLONE_NAMESPACE = fdio_sys::FDIO_SPAWN_CLONE_NAMESPACE;

        /// Clones file descriptors 0, 1, and 2 into the spawned process.
        ///
        /// Skips any of these file descriptors that are closed without
        /// generating an error.
        const CLONE_STDIO = fdio_sys::FDIO_SPAWN_CLONE_STDIO;

        /// Clones the environment into the spawned process.
        const CLONE_ENVIRONMENT = fdio_sys::FDIO_SPAWN_CLONE_ENVIRON;

        /// Clones the namespace, stdio, and environment into the spawned process.
        const CLONE_ALL = fdio_sys::FDIO_SPAWN_CLONE_ALL;
    }
}

// TODO: someday we'll have custom DSTs which will make this unnecessary.
fn nul_term_from_slice(argv: &[&CStr]) -> Vec<*const raw::c_char> {
    argv.iter().map(|cstr| cstr.as_ptr()).chain(std::iter::once(std::ptr::null())).collect()
}

/// Spawn a process in the given `job`.
pub fn spawn(
    job: &zx::Job,
    options: SpawnOptions,
    path: &CStr,
    argv: &[&CStr],
) -> Result<zx::Process, zx::Status> {
    let job = job.raw_handle();
    let flags = options.bits();
    let path = path.as_ptr();
    let argv = nul_term_from_slice(argv);
    // we expect fdio to initialize this to a legal value.
    let mut process = MaybeUninit::new(zx::Handle::invalid().raw_handle());

    // Safety: spawn consumes no handles and frees no pointers, and only
    // produces a valid process upon success.
    let status = {
        let argv = argv.as_ptr();
        let process = process.as_mut_ptr();
        unsafe { fdio_sys::fdio_spawn(job, flags, path, argv, process) }
    };
    let () = zx::Status::ok(status)?;
    let process = unsafe { process.assume_init() };
    let process = unsafe { zx::Handle::from_raw(process) };
    debug_assert!(!process.is_invalid(), "({:?}).is_invalid()", process);
    Ok(zx::Process::from(process))
}

/// An action to take in `spawn_etc`.
#[repr(transparent)]
pub struct SpawnAction<'a>(fdio_sys::fdio_spawn_action_t, PhantomData<&'a ()>);

// TODO(https://github.com/rust-lang/rust-bindgen/issues/2000): bindgen
// generates really bad names for these.
mod fdio_spawn_action {
    #![allow(dead_code)]
    #![allow(non_camel_case_types)]

    pub(super) type action_t = super::fdio_sys::fdio_spawn_action__bindgen_ty_1;
    pub(super) type fd_t = super::fdio_sys::fdio_spawn_action__bindgen_ty_1__bindgen_ty_1;
    pub(super) type ns_t = super::fdio_sys::fdio_spawn_action__bindgen_ty_1__bindgen_ty_2;
    pub(super) type h_t = super::fdio_sys::fdio_spawn_action__bindgen_ty_1__bindgen_ty_3;
    pub(super) type name_t = super::fdio_sys::fdio_spawn_action__bindgen_ty_1__bindgen_ty_4;
    pub(super) type dir_t = super::fdio_sys::fdio_spawn_action__bindgen_ty_1__bindgen_ty_5;
}

impl<'a> SpawnAction<'a> {
    pub const USE_FOR_STDIO: i32 = fdio_sys::FDIO_FLAG_USE_FOR_STDIO as i32;

    /// Clone a file descriptor into the new process.
    ///
    /// `local_fd`: File descriptor within the current process.
    /// `target_fd`: File descriptor within the new process that will receive the clone.
    pub fn clone_fd(local_fd: &'a impl AsRawFd, target_fd: i32) -> Self {
        // Safety: `local_fd` is a valid file descriptor so long as we're inside the
        // 'a lifetime.
        Self(
            fdio_sys::fdio_spawn_action_t {
                action: fdio_sys::FDIO_SPAWN_ACTION_CLONE_FD,
                __bindgen_anon_1: fdio_spawn_action::action_t {
                    fd: fdio_spawn_action::fd_t { local_fd: local_fd.as_raw_fd(), target_fd },
                },
            },
            PhantomData,
        )
    }

    /// Transfer a file descriptor into the new process.
    ///
    /// `local_fd`: File descriptor within the current process.
    /// `target_fd`: File descriptor within the new process that will receive the transfer.
    pub fn transfer_fd(local_fd: impl IntoRawFd, target_fd: i32) -> Self {
        // Safety: ownership of `local_fd` is consumed, so `Self` can live arbitrarily long.
        // When the action is executed, the fd will be transferred.
        Self(
            fdio_sys::fdio_spawn_action_t {
                action: fdio_sys::FDIO_SPAWN_ACTION_TRANSFER_FD,
                __bindgen_anon_1: fdio_spawn_action::action_t {
                    fd: fdio_spawn_action::fd_t { local_fd: local_fd.into_raw_fd(), target_fd },
                },
            },
            PhantomData,
        )
    }

    /// Add the given entry to the namespace of the spawned process.
    ///
    /// If `SpawnOptions::CLONE_NAMESPACE` is set, the namespace entry is added
    /// to the cloned namespace from the calling process.
    pub fn add_namespace_entry(prefix: &'a CStr, handle: zx::Handle) -> Self {
        // Safety: ownership of the `handle` is consumed.
        // The prefix string must stay valid through the 'a lifetime.
        Self(
            fdio_sys::fdio_spawn_action_t {
                action: fdio_sys::FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                __bindgen_anon_1: fdio_spawn_action::action_t {
                    ns: fdio_spawn_action::ns_t {
                        prefix: prefix.as_ptr(),
                        handle: handle.into_raw(),
                    },
                },
            },
            PhantomData,
        )
    }

    /// Add the given handle to the process arguments of the spawned process.
    pub fn add_handle(kind: fuchsia_runtime::HandleInfo, handle: zx::Handle) -> Self {
        // Safety: ownership of the `handle` is consumed.
        // The prefix string must stay valid through the 'a lifetime.
        Self(
            fdio_sys::fdio_spawn_action_t {
                action: fdio_sys::FDIO_SPAWN_ACTION_ADD_HANDLE,
                __bindgen_anon_1: fdio_spawn_action::action_t {
                    h: fdio_spawn_action::h_t { id: kind.as_raw(), handle: handle.into_raw() },
                },
            },
            PhantomData,
        )
    }

    /// Sets the name of the spawned process to the given name.
    pub fn set_name(name: &'a CStr) -> Self {
        // Safety: the `name` pointer must be valid at least as long as `Self`.
        Self(
            fdio_sys::fdio_spawn_action_t {
                action: fdio_sys::FDIO_SPAWN_ACTION_SET_NAME,
                __bindgen_anon_1: fdio_spawn_action::action_t {
                    name: fdio_spawn_action::name_t { data: name.as_ptr() },
                },
            },
            PhantomData,
        )
    }

    fn is_null(&self) -> bool {
        let Self(fdio_sys::fdio_spawn_action_t { action, __bindgen_anon_1: _ }, PhantomData) = self;
        *action == 0
    }

    /// Nullifies the action to prevent the inner contents from being dropped.
    fn nullify(&mut self) {
        // Assert that our null value doesn't conflict with any "real" actions.
        debug_assert!(
            (fdio_sys::FDIO_SPAWN_ACTION_CLONE_FD != 0)
                && (fdio_sys::FDIO_SPAWN_ACTION_TRANSFER_FD != 0)
                && (fdio_sys::FDIO_SPAWN_ACTION_ADD_NS_ENTRY != 0)
                && (fdio_sys::FDIO_SPAWN_ACTION_ADD_HANDLE != 0)
                && (fdio_sys::FDIO_SPAWN_ACTION_SET_NAME != 0)
        );
        let Self(fdio_sys::fdio_spawn_action_t { action, __bindgen_anon_1: _ }, PhantomData) = self;
        *action = 0;
    }
}

const ERR_MSG_MAX_LENGTH: usize = fdio_sys::FDIO_SPAWN_ERR_MSG_MAX_LENGTH as usize;

fn spawn_with_actions(
    job: &zx::Job,
    options: SpawnOptions,
    argv: &[&CStr],
    environ: Option<&[&CStr]>,
    actions: &mut [SpawnAction<'_>],
    spawn_fn: impl FnOnce(
        zx::sys::zx_handle_t,                 // job
        u32,                                  // flags
        *const *const raw::c_char,            // argv
        *const *const raw::c_char,            // environ
        u64,                                  // action_count
        *const fdio_sys::fdio_spawn_action_t, // actions
        *mut zx::sys::zx_handle_t,            // process_out,
        *mut raw::c_char,                     // err_msg_out
    ) -> zx::sys::zx_status_t,
) -> Result<zx::Process, (zx::Status, String)> {
    let job = job.raw_handle();
    let flags = options.bits();
    let argv = nul_term_from_slice(argv);
    let environ = environ.map(nul_term_from_slice);

    if actions.iter().any(SpawnAction::is_null) {
        return Err((zx::Status::INVALID_ARGS, "null SpawnAction".to_string()));
    }

    // we expect fdio to initialize this to a legal value.
    let mut process = MaybeUninit::new(zx::Handle::invalid().raw_handle());
    let mut err_msg = MaybeUninit::new([0; ERR_MSG_MAX_LENGTH]);

    let status = {
        let environ = environ.as_ref().map_or_else(std::ptr::null, Vec::as_ptr);
        match actions.len().try_into() {
            Ok(action_count) => spawn_fn(
                job,
                flags,
                argv.as_ptr(),
                environ,
                action_count,
                actions.as_ptr() as _,
                process.as_mut_ptr(),
                err_msg.as_mut_ptr() as _,
            ),
            Err(std::num::TryFromIntError { .. }) => zx::Status::INVALID_ARGS.into_raw(),
        }
    };

    // Statically verify this hasn't been moved out of during the call above;
    // raw pointers escape the borrow checker.
    std::mem::drop(environ);

    zx::Status::ok(status).map_err(|status| {
        let err_msg = unsafe { err_msg.assume_init() };
        let err_msg = unsafe { CStr::from_ptr(err_msg.as_ptr()) };
        let err_msg = err_msg.to_string_lossy().into_owned();
        (status, err_msg)
    })?;

    // Clear out the actions so we can't unsafely re-use them in a future call.
    actions.iter_mut().for_each(SpawnAction::nullify);

    let process = unsafe { process.assume_init() };
    let process = unsafe { zx::Handle::from_raw(process) };
    debug_assert!(!process.is_invalid(), "({:?}).is_invalid()", process);
    Ok(zx::Process::from(process))
}

/// Spawn a process in the given `job` using a series of `SpawnAction`s.
/// All `SpawnAction`s are nullified after their use in this function.
pub fn spawn_etc(
    job: &zx::Job,
    options: SpawnOptions,
    path: &CStr,
    argv: &[&CStr],
    environ: Option<&[&CStr]>,
    actions: &mut [SpawnAction<'_>],
) -> Result<zx::Process, (zx::Status, String)> {
    let path = path.as_ptr();
    spawn_with_actions(
        job,
        options,
        argv,
        environ,
        actions,
        |job, flags, argv, environ, action_count, actions_ptr, process_out, err_msg_out| unsafe {
            fdio_sys::fdio_spawn_etc(
                job,
                flags,
                path,
                argv,
                environ,
                action_count,
                actions_ptr,
                process_out,
                err_msg_out,
            )
        },
    )
}

/// Spawn a process in the given job using an executable VMO.
pub fn spawn_vmo(
    job: &zx::Job,
    options: SpawnOptions,
    executable_vmo: zx::Vmo,
    argv: &[&CStr],
    environ: Option<&[&CStr]>,
    actions: &mut [SpawnAction<'_>],
) -> Result<zx::Process, (zx::Status, String)> {
    let executable_vmo = executable_vmo.into_raw();
    spawn_with_actions(
        job,
        options,
        argv,
        environ,
        actions,
        |job, flags, argv, environ, action_count, actions_ptr, process_out, err_msg_out| unsafe {
            fdio_sys::fdio_spawn_vmo(
                job,
                flags,
                executable_vmo,
                argv,
                environ,
                action_count,
                actions_ptr,
                process_out,
                err_msg_out,
            )
        },
    )
}

/// Gets a read-only VMO containing the whole contents of the file. This function
/// creates a clone of the underlying VMO when possible, falling back to eagerly
/// reading the contents into a freshly-created VMO.
pub fn get_vmo_copy_from_file(f: &File) -> Result<zx::Vmo, zx::Status> {
    let fd = f.as_raw_fd();
    // we expect fdio to initialize this to a legal value.
    let mut vmo = MaybeUninit::new(zx::Handle::invalid().raw_handle());
    let status = {
        let vmo = vmo.as_mut_ptr();
        unsafe { fdio_sys::fdio_get_vmo_copy(fd, vmo) }
    };
    let () = zx::Status::ok(status)?;
    let vmo = unsafe { vmo.assume_init() };
    let vmo = unsafe { zx::Handle::from_raw(vmo) };
    debug_assert!(!vmo.is_invalid(), "({:?}).is_invalid()", vmo);
    Ok(zx::Vmo::from(vmo))
}

/// Gets a read-exec VMO containing the whole contents of the file.
pub fn get_vmo_exec_from_file(f: &File) -> Result<zx::Vmo, zx::Status> {
    let fd = f.as_raw_fd();
    // we expect fdio to initialize this to a legal value.
    let mut vmo = MaybeUninit::new(zx::Handle::invalid().raw_handle());
    let status = {
        let vmo = vmo.as_mut_ptr();
        unsafe { fdio_sys::fdio_get_vmo_exec(fd, vmo) }
    };
    let () = zx::Status::ok(status)?;
    let vmo = unsafe { vmo.assume_init() };
    let vmo = unsafe { zx::Handle::from_raw(vmo) };
    debug_assert!(!vmo.is_invalid(), "({:?}).is_invalid()", vmo);
    Ok(zx::Vmo::from(vmo))
}

pub struct Namespace {
    ns: *mut fdio_sys::fdio_ns_t,
}

impl Namespace {
    /// Get the currently installed namespace.
    pub fn installed() -> Result<Self, zx::Status> {
        // we expect fdio to initialize this to a legal value.
        let mut ns = std::ptr::null_mut();
        let status = { unsafe { fdio_sys::fdio_ns_get_installed(&mut ns) } };
        zx::Status::ok(status)?;
        debug_assert_ne!(ns, std::ptr::null_mut());
        Ok(Namespace { ns })
    }

    /// Open an object at |path| relative to the root of this namespace with |flags|.
    ///
    /// |path| must be absolute.
    ///
    /// This corresponds with fdio_ns_open in C.
    pub fn open(
        &self,
        path: &str,
        flags: fio::OpenFlags,
        channel: zx::Channel,
    ) -> Result<(), zx::Status> {
        let &Self { ns } = self;
        let path = CString::new(path)?;
        let path = path.as_ptr();
        let flags = flags.bits();

        // The channel is always consumed.
        let channel = channel.into_raw();
        let status = unsafe { fdio_sys::fdio_ns_open(ns, path, flags, channel) };
        zx::Status::ok(status)
    }

    /// Create a new directory within the namespace, bound to the provided
    /// directory-protocol-compatible channel. The path must be an absolute path, like "/x/y/z",
    /// containing no "." nor ".." entries. It is relative to the root of the namespace.
    ///
    /// This corresponds with fdio_ns_bind in C.
    pub fn bind(
        &self,
        path: &str,
        channel: fidl::endpoints::ClientEnd<fio::DirectoryMarker>,
    ) -> Result<(), zx::Status> {
        let &Self { ns } = self;
        let path = CString::new(path)?;
        let path = path.as_ptr();

        // The channel is always consumed.
        let channel = channel.into_raw();
        let status = unsafe { fdio_sys::fdio_ns_bind(ns, path, channel) };
        zx::Status::ok(status)
    }

    /// Unbind the channel at path, closing the associated handle when all references to the node go
    /// out of scope. The path must be an absolute path, like "/x/y/z", containing no "." nor ".."
    /// entries. It is relative to the root of the namespace.
    ///
    /// This corresponds with fdio_ns_unbind in C.
    pub fn unbind(&self, path: &str) -> Result<(), zx::Status> {
        let &Self { ns } = self;
        let path = CString::new(path)?;
        let path = path.as_ptr();
        let status = unsafe { fdio_sys::fdio_ns_unbind(ns, path) };
        zx::Status::ok(status)
    }

    // Export this namespace, by returning a flat representation of it. The
    // handles returned are clones of the handles within the namespace.
    pub fn export(&self) -> Result<Vec<NamespaceEntry>, zx::Status> {
        let mut flat: *mut fdio_sys::fdio_flat_namespace_t = std::ptr::null_mut();

        unsafe {
            zx::Status::ok(fdio_sys::fdio_ns_export(self.ns, &mut flat))?;
            let entries = Self::export_entries(flat);
            fdio_sys::fdio_ns_free_flat_ns(flat);
            entries
        }
    }

    unsafe fn export_entries(
        flat: *mut fdio_sys::fdio_flat_namespace_t,
    ) -> Result<Vec<NamespaceEntry>, zx::Status> {
        let fdio_sys::fdio_flat_namespace_t { count, handle, type_, path } = *flat;
        let capacity: usize =
            count.try_into().map_err(|_: TryFromIntError| zx::Status::INVALID_ARGS)?;
        let len: isize =
            capacity.try_into().map_err(|_: TryFromIntError| zx::Status::INVALID_ARGS)?;
        let mut entries = Vec::with_capacity(capacity);
        for i in 0..len {
            // Explicitly take ownership of the handle, and invalidate the source.
            let handle = zx::Handle::from_raw(mem::replace(
                &mut *handle.offset(i),
                zx::sys::ZX_HANDLE_INVALID,
            ));
            entries.push(NamespaceEntry {
                handle,
                info: (*type_.offset(i))
                    .try_into()
                    .map_err(|_: HandleInfoError| zx::Status::INVALID_ARGS)?,
                path: CStr::from_ptr(*path.offset(i))
                    .to_str()
                    .map_err(|_: Utf8Error| zx::Status::INVALID_ARGS)?
                    .to_owned(),
            });
        }
        Ok(entries)
    }

    pub fn into_raw(self) -> *mut fdio_sys::fdio_ns_t {
        let Self { ns } = self;
        ns
    }
}

/// Entry in a flat representation of a namespace.
pub struct NamespaceEntry {
    pub handle: zx::Handle,
    pub info: HandleInfo,
    pub path: String,
}

/// Returns the handle for the given FIDL service, or a Zircon Error Status. This function takes
/// ownership of 'service' because the File is released when acquiring the service handle.
///
/// This corresponds with fdio_get_service_handle in C.
pub fn get_service_handle(service: File) -> Result<zx::Channel, zx::Status> {
    let fd = service.into_raw_fd();
    // we expect fdio to initialize this to a legal value.
    let mut handle = MaybeUninit::new(zx::Handle::invalid().raw_handle());
    // Safety: fdio_get_service_handle() is an unsafe function because its 'out' parameter is a raw
    // pointer and because it closes the given file descriptor. This invocation is safe because:
    // 1) the supplied 'out' value, "&mut handle", is guaranteed to be valid,
    // 2) fdio_get_service_handle() does not retain a copy of `out`, and
    // 3) 'fd' is not used following this invocation.
    let status = {
        let handle = handle.as_mut_ptr();
        unsafe { fdio_sys::fdio_get_service_handle(fd, handle) }
    };
    let () = zx::Status::ok(status)?;
    // Safety: from_raw() is an unsafe function because it assumes the given raw handle is valid.
    // Above, fdio_get_service_handle()'s return value was ok, so 'handle' must be valid.
    let handle = unsafe { handle.assume_init() };
    let handle = unsafe { zx::Handle::from_raw(handle) };
    debug_assert!(!handle.is_invalid(), "({:?}).is_invalid()", handle);
    Ok(zx::Channel::from(handle))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_zircon::{object_wait_many, Signals, Status, Time, WaitItem},
    };

    #[test]
    fn namespace_get_installed() {
        let namespace = Namespace::installed().expect("failed to get installed namespace");
        assert!(!namespace.into_raw().is_null());
    }

    #[test]
    fn namespace_bind_open_unbind() {
        let namespace = Namespace::installed().unwrap();
        // client => ns_server => ns_client => server
        //        ^            ^            ^-- zx channel connection
        //        |            |-- connected through namespace bind/connect
        //        |-- zx channel connection
        let (ns_client, _server) = fidl::endpoints::create_endpoints().unwrap();
        let (_client, ns_server) = zx::Channel::create().unwrap();
        let path = "/test_path1";

        assert_eq!(namespace.bind(path, ns_client), Ok(()));
        assert_eq!(namespace.open(path, fio::OpenFlags::empty(), ns_server), Ok(()));
        assert_eq!(namespace.unbind(path), Ok(()));
    }

    #[test]
    fn namespace_double_bind_error() {
        let namespace = Namespace::installed().unwrap();
        let (ns_client1, _server1) = fidl::endpoints::create_endpoints().unwrap();
        let (ns_client2, _server2) = fidl::endpoints::create_endpoints().unwrap();
        let path = "/test_path2";

        assert_eq!(namespace.bind(path, ns_client1), Ok(()));
        assert_eq!(namespace.bind(path, ns_client2), Err(zx::Status::ALREADY_EXISTS));
        assert_eq!(namespace.unbind(path), Ok(()));
    }

    #[test]
    fn namespace_open_error() {
        let namespace = Namespace::installed().unwrap();
        let (_client, ns_server) = zx::Channel::create().unwrap();
        let path = "/test_path3";

        assert_eq!(
            namespace.open(path, fio::OpenFlags::empty(), ns_server),
            Err(zx::Status::NOT_FOUND)
        );
    }

    #[test]
    fn namespace_unbind_error() {
        let namespace = Namespace::installed().unwrap();
        let path = "/test_path4";

        assert_eq!(namespace.unbind(path), Err(zx::Status::NOT_FOUND));
    }

    #[test]
    fn namespace_export() {
        let namespace = Namespace::installed().unwrap();
        let entries = namespace.export().unwrap();

        assert!(!entries.is_empty());
    }

    fn cstr(orig: &str) -> CString {
        CString::new(orig).expect("CString::new failed")
    }

    #[test]
    fn fdio_spawn_run_target_bin_no_env() {
        let job = zx::Job::from(zx::Handle::invalid());
        let cpath = cstr("/pkg/bin/spawn_test_target");
        let (stdout_file, stdout_sock) = pipe_half().expect("Failed to make pipe");
        let mut spawn_actions = [SpawnAction::clone_fd(&stdout_file, 1)];

        let cstrags: Vec<CString> = vec![cstr("test_arg")];
        let mut cargs: Vec<&CStr> = cstrags.iter().map(|x| x.as_c_str()).collect();
        cargs.insert(0, cpath.as_c_str());
        let process = spawn_etc(
            &job,
            SpawnOptions::CLONE_ALL,
            cpath.as_c_str(),
            cargs.as_slice(),
            None,
            &mut spawn_actions,
        )
        .expect("Unable to spawn process");

        let mut output = vec![];
        loop {
            let mut items = vec![
                WaitItem {
                    handle: process.as_handle_ref(),
                    waitfor: Signals::PROCESS_TERMINATED,
                    pending: Signals::NONE,
                },
                WaitItem {
                    handle: stdout_sock.as_handle_ref(),
                    waitfor: Signals::SOCKET_READABLE | Signals::SOCKET_PEER_CLOSED,
                    pending: Signals::NONE,
                },
            ];

            let signals_result =
                object_wait_many(&mut items, Time::INFINITE).expect("unable to wait");

            if items[1].pending.contains(Signals::SOCKET_READABLE) {
                let bytes_len = stdout_sock.outstanding_read_bytes().expect("Socket error");
                let mut buf: Vec<u8> = vec![0; bytes_len];
                let read_len = stdout_sock
                    .read(&mut buf[..])
                    .or_else(|status| match status {
                        Status::SHOULD_WAIT => Ok(0),
                        _ => Err(status),
                    })
                    .expect("Unable to read buff");
                output.extend_from_slice(&buf[0..read_len]);
            }

            // read stdout buffer until test process dies or the socket is closed
            if items[1].pending.contains(Signals::SOCKET_PEER_CLOSED) {
                break;
            }

            if items[0].pending.contains(Signals::PROCESS_TERMINATED) {
                break;
            }

            if signals_result {
                break;
            };
        }

        assert_eq!(String::from_utf8(output).expect("unable to decode stdout"), "hello world\n");
    }

    // Simple tests of the fdio_open and fdio_open_at wrappers. These aren't intended to
    // exhaustively test the fdio functions - there are separate tests for that - but they do
    // exercise one success and one failure case for each function.
    #[test]
    fn fdio_open_and_open_at() {
        use rand::distributions::DistString as _;

        // fdio_open requires paths to be absolute
        {
            let (_, pkg_server) = zx::Channel::create().unwrap();
            assert_eq!(
                open("pkg", fio::OpenFlags::RIGHT_READABLE, pkg_server),
                Err(zx::Status::NOT_FOUND)
            );
        }

        let (pkg_client, pkg_server) = zx::Channel::create().unwrap();
        assert_eq!(open("/pkg", fio::OpenFlags::RIGHT_READABLE, pkg_server), Ok(()));

        // fdio_open/fdio_open_at disallow paths that are too long
        {
            let path = rand::distributions::Alphanumeric
                .sample_string(&mut rand::thread_rng(), libc::PATH_MAX.try_into().unwrap());
            let (_, server) = zx::Channel::create().unwrap();
            assert_eq!(
                open_at(&pkg_client, &path, fio::OpenFlags::empty(), server),
                Err(zx::Status::INVALID_ARGS)
            );
        }

        let (_, bin_server) = zx::Channel::create().unwrap();
        assert_eq!(open_at(&pkg_client, "bin", fio::OpenFlags::RIGHT_READABLE, bin_server), Ok(()));
    }

    // Simple tests of the fdio_open_fd and fdio_open_fd_at wrappers. These aren't intended to
    // exhaustively test the fdio functions - there are separate tests for that - but they do
    // exercise one success and one failure case for each function.
    #[test]
    fn fdio_open_fd_and_open_fd_at() {
        let pkg_fd = open_fd("/pkg", fio::OpenFlags::RIGHT_READABLE)
            .expect("Failed to open /pkg using fdio_open_fd");

        // Trying to open a non-existent directory should fail.
        assert_matches!(
            open_fd_at(&pkg_fd, "blahblah", fio::OpenFlags::RIGHT_READABLE),
            Err(zx::Status::NOT_FOUND)
        );

        let _: File = open_fd_at(&pkg_fd, "bin", fio::OpenFlags::RIGHT_READABLE)
            .expect("Failed to open bin/ subdirectory using fdio_open_fd_at");
    }

    // Tests success & error handling of the get_service_handle function. This does not exhaustively
    // test the underlying FDIO function, as that is tested alongside its C implementation.
    #[test]
    fn fdio_get_service_handle() {
        // Setup two handles to the same underlying file.
        let service = File::open("/svc").expect("failed to open /svc");
        let clone = service.try_clone().expect("failed to clone /svc service");

        // 'service' keeps the file busy during 'get_service_handle(clone)'.
        assert_eq!(get_service_handle(clone), Err(zx::Status::UNAVAILABLE));
        // 'clone' released its handle to the file above; 'service' now holds the sole handle.
        let _: zx::Channel =
            get_service_handle(service).expect("failed to get fdio service handle");
    }
}
