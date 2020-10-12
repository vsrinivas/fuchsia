// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Fuchsia-specific `libc` functionality.
//!
//! This crate is a minimal extension on top of the `fuchsia-zircon` crate,
//! which provides bindings to the Zircon kernel's syscalls, but does not
//! depend on functionality from `libc`.

// AKA `libc`-granted ambient-authority crate ;)

#![deny(missing_docs)]

use {
    fuchsia_zircon::{
        sys::{zx_handle_t, ZX_HANDLE_INVALID}, // handle type (primitive, non-owning)
        Clock,
        Handle,
        Job,
        Process,
        Rights,
        Status,
        Thread,
        Unowned,
        Vmar,
    },
    num_derive::FromPrimitive,
    num_traits::cast::FromPrimitive,
    std::convert::TryFrom,
    thiserror::Error,
};

// TODO(fxbug.dev/61174): Document these.
#[allow(missing_docs)]
extern "C" {
    pub fn zx_take_startup_handle(hnd_info: u32) -> zx_handle_t;
    pub fn zx_thread_self() -> zx_handle_t;
    pub fn zx_process_self() -> zx_handle_t;
    pub fn zx_vmar_root_self() -> zx_handle_t;
    pub fn zx_job_default() -> zx_handle_t;
    pub fn zx_utc_reference_get() -> zx_handle_t;
}

/// Handle types as defined by the processargs protocol.
///
/// See [//zircon/system/public/zircon/processargs.h][processargs.h] for canonical definitions.
///
/// Short descriptions of each handle type are given, but more complete documentation may be found
/// in the [processargs.h] header.
///
/// [processargs.h]: https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/public/zircon/processargs.h
#[repr(u8)]
#[derive(FromPrimitive, Copy, Clone, Debug, Eq, PartialEq)]
pub enum HandleType {
    /// Handle to our own process.
    ///
    /// Equivalent to PA_PROC_SELF.
    ProcessSelf = 0x01,

    /// Handle to the initial thread of our own process.
    ///
    /// Equivalent to PA_THREAD_SELF.
    ThreadSelf = 0x02,

    /// Handle to a job object which can be used to make child processes.
    ///
    /// The job can be the same as the one used to create this process or it can
    /// be different.
    ///
    /// Equivalent to PA_JOB_DEFAULT.
    DefaultJob = 0x03,

    /// Handle to the root of our address space.
    ///
    /// Equivalent to PA_VMAR_ROOT.
    RootVmar = 0x04,

    /// Handle to the VMAR used to load the initial program image.
    ///
    /// Equivalent to PA_VMAR_LOADED.
    LoadedVmar = 0x05,

    /// Service for loading shared libraries.
    ///
    /// See `fuchsia.ldsvc.Loader` for the interface definition.
    ///
    /// Equivalent to PA_LDSVC_LOADER.
    LdsvcLoader = 0x10,

    /// Handle to the VMO containing the vDSO ELF image.
    ///
    /// Equivalent to PA_VMO_VDSO.
    VdsoVmo = 0x11,

    /// Handle to the VMO used to map the initial thread's stack.
    ///
    /// Equivalent to PA_VMO_STACK.
    StackVmo = 0x13,

    /// Handle to the VMO for the main executable file.
    ///
    /// Equivalent to PA_VMO_EXECUTABLE.
    ExecutableVmo = 0x14,

    /// Used by kernel and userboot during startup.
    ///
    /// Equivalent to PA_VMO_BOOTDATA.
    BootdataVmo = 0x1A,

    /// Used by kernel and userboot during startup.
    ///
    /// Equivalent to PA_VMO_BOOTFS.
    BootfsVmo = 0x1B,

    /// Used by the kernel to export debug information as a file in bootfs.
    ///
    /// Equivalent to PA_VMO_KERNEL_FILE.
    KernelFileVmo = 0x1C,

    /// A handle to a fuchsia.io.Directory service to be used as a directory in the process's
    /// namespace. Corresponds to a path in the processargs bootstrap message's namespace table
    /// based on the argument of a HandleInfo of this type.
    ///
    /// Equivalent to PA_NS_DIR.
    NamespaceDirectory = 0x20,

    /// A handle which will be used as a file descriptor.
    ///
    /// Equivalent to PA_FD.
    FileDescriptor = 0x30,

    /// A Handle to a channel on which the process may serve the
    /// the |fuchsia.process.Lifecycle| protocol.
    ///
    /// Equivalent to PA_LIFECYCLE.
    Lifecycle = 0x3A,

    /// Server endpoint for handling connections to appmgr services.
    ///
    /// Equivalent to PA_DIRECTORY_REQUEST.
    DirectoryRequest = 0x3B,

    /// A Handle to a resource object. Used by devcoordinator and devhosts.
    ///
    /// Equivalent to PA_RESOURCE.
    Resource = 0x3F,

    /// A Handle to a clock object representing UTC.  Used by runtimes to gain
    /// access to UTC time.
    ///
    /// Equivalent to PA_CLOCK_UTC.
    ClockUtc = 0x40,

    /// A Handle to an MMIO resource object.
    ///
    /// Equivalent to PA_MMIO_RESOURCE.
    MmioResource = 0x50,

    /// A handle type with user-defined meaning.
    ///
    /// Equivalent to PA_USER0.
    User0 = 0xF0,

    /// A handle type with user-defined meaning.
    ///
    /// Equivalent to PA_USER1.
    User1 = 0xF1,

    /// A handle type with user-defined meaning.
    ///
    /// Equivalent to PA_USER2.
    User2 = 0xF2,

    #[doc(hidden)]
    __Nonexhaustive,
}

/// Metadata information for a handle in a processargs message. Contains a handle type and an
/// unsigned 16-bit value, whose meaning is handle type dependent.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct HandleInfo {
    htype: HandleType,
    arg: u16,
}

impl HandleInfo {
    /// Create a handle info struct from a handle type and an argument.
    ///
    /// For example, a `HandleInfo::new(HandleType::FileDescriptor, 32)` identifies
    /// the respective handle as file descriptor 32.
    ///
    /// Corresponds to PA_HND in processargs.h.
    pub fn new(htype: HandleType, arg: u16) -> Self {
        HandleInfo { htype, arg }
    }

    /// Returns the handle type for this handle info struct.
    pub fn handle_type(&self) -> HandleType {
        self.htype
    }

    /// Returns the argument for this handle info struct.
    pub fn arg(&self) -> u16 {
        self.arg
    }

    /// Convert the handle info into a raw u32 value for FFI purposes.
    pub fn as_raw(&self) -> u32 {
        ((self.htype as u32) & 0xFF) | (self.arg as u32) << 16
    }
}

/// An implementation of the From trait to create a [HandleInfo] from a [HandleType] with an argument
/// of 0.
impl From<HandleType> for HandleInfo {
    fn from(ty: HandleType) -> Self {
        Self::new(ty, 0)
    }
}

/// Possible errors when converting a raw u32 to a HandleInfo with the  TryFrom<u32> impl on
/// HandleInfo.
#[derive(Error, Debug)]
pub enum HandleInfoError {
    /// Unknown handle type.
    #[error("Unknown handle type for HandleInfo: {:#x?}", _0)]
    UnknownHandleType(u32),

    /// Otherwise invalid raw value, like reserved bytes being non-zero.
    #[error("Invalid value for HandleInfo: {:#x?}", _0)]
    InvalidHandleInfo(u32),
}

impl TryFrom<u32> for HandleInfo {
    type Error = HandleInfoError;

    /// Attempt to convert a u32 to a handle ID value. Can fail if the value represents an
    /// unknown handle type or is otherwise invalid.
    ///
    /// Useful to convert existing handle info values received through FIDL APIs, e.g. from a
    /// client that creates them using the PA_HND macro in processargs.h from C/C++.
    fn try_from(value: u32) -> Result<HandleInfo, HandleInfoError> {
        // 2nd byte should be zero, it is currently unused.
        if value & 0xFF00 != 0 {
            return Err(HandleInfoError::InvalidHandleInfo(value));
        }

        let htype = HandleType::from_u8((value & 0xFF) as u8)
            .ok_or(HandleInfoError::UnknownHandleType(value))?;
        Ok(HandleInfo::new(htype, (value >> 16) as u16))
    }
}

/// Removes the handle of type `HandleType` from the list of handles received at startup.
///
/// This function will return `Some` at-most once per handle type.
/// This function will return `None` if the requested type was not received at
/// startup or if the handle with the provided type was already taken.
pub fn take_startup_handle(info: HandleInfo) -> Option<Handle> {
    unsafe {
        let raw = zx_take_startup_handle(info.as_raw());
        if raw == ZX_HANDLE_INVALID {
            None
        } else {
            Some(Handle::from_raw(raw))
        }
    }
}

/// Get a reference to the handle of the current thread.
pub fn thread_self() -> Unowned<'static, Thread> {
    unsafe {
        let handle = zx_thread_self();
        Unowned::from_raw_handle(handle)
    }
}

/// Get a reference to the handle of the current process.
pub fn process_self() -> Unowned<'static, Process> {
    unsafe {
        let handle = zx_process_self();
        Unowned::from_raw_handle(handle)
    }
}

/// Get a reference to the handle of the current address space.
pub fn vmar_root_self() -> Unowned<'static, Vmar> {
    unsafe {
        let handle = zx_vmar_root_self();
        Unowned::from_raw_handle(handle)
    }
}

/// Get a reference to the default `Job` provided to the process on startup.
///
/// This typically refers to the `Job` that is the immediate parent of the current
/// process.
///
/// If the current process was launched as a Fuchsia Component, this `Job`
/// will begin with no child processes other than the current process.
pub fn job_default() -> Unowned<'static, Job> {
    unsafe {
        let handle = zx_job_default();
        Unowned::from_raw_handle(handle)
    }
}

/// Duplicate the UTC `Clock` registered with the runtime.
pub fn duplicate_utc_clock_handle(rights: Rights) -> Result<Clock, Status> {
    let handle: Unowned<'static, Clock> = unsafe {
        let handle = zx_utc_reference_get();
        Unowned::from_raw_handle(handle)
    };
    handle.duplicate(rights)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handle_id() {
        let mut randbuf = [0; 2];
        for type_val in 0..0xFF {
            if let Some(htype) = HandleType::from_u8(type_val as u8) {
                zx::cprng_draw(&mut randbuf)?;
                let arg = u16::from_le_bytes(randbuf);

                let info = HandleInfo::new(htype, arg);
                assert_eq!(info.handle_type(), htype);
                assert_eq!(info.arg(), arg);

                let info = HandleInfo::from(htype);
                assert_eq!(info.handle_type(), htype);
                assert_eq!(info.arg(), 0);
            }
        }
    }

    #[test]
    fn handle_id_raw() {
        assert_eq!(HandleInfo::new(HandleType::ProcessSelf, 0).as_raw(), 0x00000001);
        assert_eq!(HandleInfo::new(HandleType::DirectoryRequest, 0).as_raw(), 0x0000003B);
        assert_eq!(HandleInfo::new(HandleType::LdsvcLoader, 0xABCD).as_raw(), 0xABCD0010);
        assert_eq!(HandleInfo::new(HandleType::User0, 0x1).as_raw(), 0x000100F0);
        assert_eq!(HandleInfo::new(HandleType::User1, 0xABCD).as_raw(), 0xABCD00F1);
        assert_eq!(HandleInfo::new(HandleType::User2, 0xFFFF).as_raw(), 0xFFFF00F2);
    }

    #[test]
    fn handle_id_from_u32() {
        assert_eq!(
            HandleInfo::try_from(0x00000002).unwrap(),
            HandleInfo::new(HandleType::ThreadSelf, 0)
        );
        assert_eq!(
            HandleInfo::try_from(0x00040030).unwrap(),
            HandleInfo::new(HandleType::FileDescriptor, 4)
        );
        assert_eq!(
            HandleInfo::try_from(0x501C00F2).unwrap(),
            HandleInfo::new(HandleType::User2, 0x501C)
        );

        // Non-zero unused byte
        assert!(HandleInfo::try_from(0x00001100).is_err());
        assert!(HandleInfo::try_from(0x00010101).is_err());
        // Unknown handle type
        assert!(HandleInfo::try_from(0x00000000).is_err());
        assert!(HandleInfo::try_from(0x00000006).is_err());
        assert!(HandleInfo::try_from(0x000000F3).is_err());
    }
}
