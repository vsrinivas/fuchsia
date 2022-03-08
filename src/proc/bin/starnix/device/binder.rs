// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::WithStaticDeviceId;
use crate::fs::{
    fileops_impl_nonblocking, FileObject, FileOps, FsNode, FsNodeOps, NamespaceNode, SeekOrigin,
};
use crate::logging::not_implemented;
use crate::mm::{DesiredAddress, MappedVmo, MappingOptions};
use crate::syscalls::{SyscallResult, SUCCESS};
use crate::task::CurrentTask;
use crate::types::*;
use fuchsia_zircon as zx;
use parking_lot::{Mutex, RwLock};
use std::collections::BTreeMap;
use std::sync::{Arc, Weak};
use zerocopy::FromBytes;

/// The largest mapping of shared memory allowed by the binder driver.
const MAX_MMAP_SIZE: usize = 4 * 1024 * 1024;

/// Android's binder kernel driver implementation.
#[derive(Clone)]
pub struct DevBinder(Arc<BinderDriver>);

impl DevBinder {
    /// The binder's static device ID. Use a MISC major number, which is the category used for
    /// non-specific hardware drivers.
    /// This could be dynamically assigned by the kernel but we don't have enough drivers nor the
    /// ability to dynamically load drivers to need this yet.
    pub const DEVICE_ID: DeviceType = DeviceType::new(10, 0);

    pub fn new() -> Self {
        Self(Arc::new(BinderDriver::new()))
    }
}

impl WithStaticDeviceId for DevBinder {
    const ID: DeviceType = Self::DEVICE_ID;
}

impl FsNodeOps for DevBinder {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(self.clone()))
    }
}

impl FileOps for DevBinder {
    fileops_impl_nonblocking!();

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.0.ioctl(current_task, request, in_addr)
    }

    fn get_vmo(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        length: Option<usize>,
        _prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        self.0.get_vmo(length.unwrap_or(MAX_MMAP_SIZE))
    }

    fn mmap(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        addr: DesiredAddress,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        mapping_options: MappingOptions,
        filename: NamespaceNode,
    ) -> Result<MappedVmo, Errno> {
        self.0.mmap(file, current_task, addr, vmo_offset, length, flags, mapping_options, filename)
    }

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }
    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn seek(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: off_t,
        _whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        error!(EOPNOTSUPP)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }
}

#[derive(Debug)]
struct BinderProcess {
    #[allow(unused)]
    pid: pid_t,
    shared_memory: Mutex<Option<SharedMemory>>,
}

impl BinderProcess {
    fn new(pid: pid_t) -> Self {
        Self { pid, shared_memory: Mutex::new(None) }
    }
}

/// The mapped VMO shared between userspace and the binder driver.
///
/// The binder driver copies messages from one process to another, which essentially amounts to
/// a copy between VMOs. It is not possible to copy directly between VMOs without an intermediate
/// copy, and the binder driver must only perform one copy for performance reasons.
///
/// The memory allocated to a binder process is shared with the binder driver, and mapped into
/// the kernel's address space so that a VMO read operation can copy directly into the mapped VMO.
#[derive(Debug)]
struct SharedMemory {
    /// The address in kernel address space where the VMO is mapped.
    #[allow(unused)]
    kernel_address: *mut u8,
    /// The address in user address space where the VMO is mapped.
    #[allow(unused)]
    user_address: UserAddress,
    /// The length of the shared memory mapping.
    #[allow(unused)]
    length: usize,
}

impl Drop for SharedMemory {
    fn drop(&mut self) {
        let kernel_root_vmar = fuchsia_runtime::vmar_root_self();

        // SAFETY: This object hands out references to the mapped memory, but the borrow checker
        // ensures correct lifetimes.
        let res = unsafe { kernel_root_vmar.unmap(self.kernel_address as usize, self.length) };
        match res {
            Ok(()) => {}
            Err(status) => {
                log::error!("failed to unmap shared binder region from kernel: {:?}", status);
            }
        }
    }
}

// SAFETY: SharedMemory has exclusive ownership of the `kernel_address` pointer, so it is safe to
// send across threads.
unsafe impl Send for SharedMemory {}

impl SharedMemory {
    fn map(vmo: &zx::Vmo, user_address: UserAddress, length: usize) -> Result<Self, Errno> {
        // Map the VMO into the kernel's address space.
        let kernel_root_vmar = fuchsia_runtime::vmar_root_self();
        let kernel_address = kernel_root_vmar
            .map(0, vmo, 0, length, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE)
            .map_err(|status| {
                log::error!("failed to map shared binder region in kernel: {:?}", status);
                errno!(ENOMEM)
            })?;
        Ok(Self { kernel_address: kernel_address as *mut u8, user_address, length })
    }
}

/// The ioctl character for all binder ioctls.
const BINDER_IOCTL_CHAR: u8 = b'b';

/// The ioctl for initiating a read-write transaction.
const BINDER_IOCTL_WRITE_READ: u32 =
    encode_ioctl_write_read::<binder_write_read>(BINDER_IOCTL_CHAR, 1);

/// The ioctl for setting the maximum number of threads the process wants to create for binder work.
const BINDER_IOCTL_SET_MAX_THREADS: u32 = encode_ioctl_write::<u32>(BINDER_IOCTL_CHAR, 5);

/// The ioctl for requests to become context manager.
const BINDER_IOCTL_SET_CONTEXT_MGR: u32 = encode_ioctl_write::<u32>(BINDER_IOCTL_CHAR, 7);

/// The ioctl for retrieving the kernel binder version.
const BINDER_IOCTL_VERSION: u32 = encode_ioctl_write_read::<binder_version>(BINDER_IOCTL_CHAR, 9);

/// The ioctl for requests to become context manager, v2.
const BINDER_IOCTL_SET_CONTEXT_MGR_EXT: u32 =
    encode_ioctl_write::<flat_binder_object>(BINDER_IOCTL_CHAR, 13);

// The ioctl for enabling one-way transaction spam detection.
const BINDER_IOCTL_ENABLE_ONEWAY_SPAM_DETECTION: u32 =
    encode_ioctl_write::<u32>(BINDER_IOCTL_CHAR, 16);

struct BinderDriver {
    /// The "name server" process that is addressed via the special handle 0 and is responsible
    /// for implementing the binder protocol `IServiceManager`.
    context_manager: RwLock<Option<Weak<BinderProcess>>>,

    /// Manages the internal state of each process interacting with the binder driver.
    procs: RwLock<BTreeMap<pid_t, Arc<BinderProcess>>>,
}

impl BinderDriver {
    fn new() -> Self {
        Self { context_manager: RwLock::new(None), procs: RwLock::new(BTreeMap::new()) }
    }

    fn find_or_register_process(&self, pid: pid_t) -> Arc<BinderProcess> {
        self.procs.write().entry(pid).or_insert_with(|| Arc::new(BinderProcess::new(pid))).clone()
    }

    fn ioctl(
        &self,
        current_task: &CurrentTask,
        request: u32,
        user_arg: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            BINDER_IOCTL_VERSION => {
                // A thread is requesting the version of this binder driver.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }
                let response =
                    binder_version { protocol_version: BINDER_CURRENT_PROTOCOL_VERSION as i32 };
                current_task.mm.write_object(UserRef::new(user_arg), &response)?;
                Ok(SUCCESS)
            }
            BINDER_IOCTL_SET_CONTEXT_MGR | BINDER_IOCTL_SET_CONTEXT_MGR_EXT => {
                // A process is registering itself as the context manager.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }

                // TODO: Read the flat_binder_object when ioctl is BINDER_IOCTL_SET_CONTEXT_MGR_EXT.

                let binder_proc = self.find_or_register_process(current_task.get_pid());
                *self.context_manager.write() = Some(Arc::downgrade(&binder_proc));
                Ok(SUCCESS)
            }
            BINDER_IOCTL_WRITE_READ => {
                // A thread is requesting to exchange data with the binder driver.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }

                let user_ref = UserRef::new(user_arg);
                let mut input = binder_write_read::new_zeroed();
                current_task.mm.read_object(user_ref, &mut input)?;

                // We will be writing this back to userspace, don't trust what the client gave us.
                input.write_consumed = 0;
                input.read_consumed = 0;

                if input.write_size > 0 {
                    // The calling thread wants to write some data to the binder driver.
                    not_implemented!("handling a binder thread's write");
                    return error!(EOPNOTSUPP);
                }

                if input.read_size > 0 {
                    // The calling thread wants to read some data from the binder driver, blocking
                    // if there is nothing immediately available.
                    not_implemented!("handling a binder thread's read");
                    return error!(EOPNOTSUPP);
                }

                // Write back to the calling thread how much data was read/written.
                current_task.mm.write_object(user_ref, &input)?;
                Ok(SUCCESS)
            }
            BINDER_IOCTL_SET_MAX_THREADS => {
                not_implemented!("binder ignoring SET_MAX_THREADS ioctl");
                Ok(SUCCESS)
            }
            BINDER_IOCTL_ENABLE_ONEWAY_SPAM_DETECTION => {
                not_implemented!("binder ignoring ENABLE_ONEWAY_SPAM_DETECTION ioctl");
                Ok(SUCCESS)
            }
            _ => {
                log::error!("binder received unknown ioctl request 0x{:08x}", request);
                error!(EINVAL)
            }
        }
    }

    fn get_vmo(&self, length: usize) -> Result<zx::Vmo, Errno> {
        zx::Vmo::create(length as u64).map_err(|_| errno!(ENOMEM))
    }

    fn mmap(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        addr: DesiredAddress,
        _vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        mapping_options: MappingOptions,
        filename: NamespaceNode,
    ) -> Result<MappedVmo, Errno> {
        let binder_proc = self.find_or_register_process(current_task.get_pid());

        // Do not support mapping shared memory more than once.
        let mut shared_memory = binder_proc.shared_memory.lock();
        if shared_memory.is_some() {
            return error!(EINVAL);
        }

        // Create a VMO that will be shared between the driver and the client process.
        let vmo = Arc::new(self.get_vmo(length)?);

        // Map the VMO into the binder process' address space.
        let user_address = current_task.mm.map(
            addr,
            vmo.clone(),
            0,
            length,
            flags,
            mapping_options,
            Some(filename),
        )?;

        // Map the VMO into the driver's address space.
        match SharedMemory::map(&*vmo, user_address, length) {
            Ok(mem) => {
                *shared_memory = Some(mem);
                Ok(MappedVmo::new(vmo, user_address))
            }
            Err(err) => {
                // Try to cleanup by unmapping from userspace, but ignore any errors. We
                // can't really recover from them.
                let _ = current_task.mm.unmap(user_address, length);
                Err(err)
            }
        }
    }
}
