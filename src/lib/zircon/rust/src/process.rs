// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon processes.

use crate::ok;
use crate::{object_get_info, object_get_property, object_set_property, ObjectQuery, Topic};
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Status, Task, Thread, Vmar};
use crate::{Property, PropertyQuery, PropertyQueryGet, PropertyQuerySet};
use bitflags::bitflags;
use fuchsia_zircon_sys::{
    self as sys, zx_info_maps_type_t, zx_koid_t, zx_time_t, zx_vaddr_t, zx_vm_option_t,
    InfoMapsTypeUnion, PadByte, ZX_MAX_NAME_LEN, ZX_OBJ_TYPE_UPPER_BOUND,
};

bitflags! {
    /// Options that may be used when creating a `Process`.
    #[repr(transparent)]
    pub struct ProcessOptions: u32 {
        const SHARED = sys::ZX_PROCESS_SHARED;
    }
}

impl Default for ProcessOptions {
    fn default() -> Self {
        ProcessOptions::empty()
    }
}

bitflags! {
    #[repr(transparent)]
    pub struct ProcessInfoFlags: u32 {
        const STARTED = sys::ZX_INFO_PROCESS_FLAG_STARTED;
        const EXITED = sys::ZX_INFO_PROCESS_FLAG_EXITED;
        const DEBUGGER_ATTACHED = sys::ZX_INFO_PROCESS_FLAG_DEBUGGER_ATTACHED;
    }
}

/// An object representing a Zircon process.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Process(Handle);
impl_handle_based!(Process);
unsafe_handle_properties!(object: Process,
    props: [
        {query_ty: PROCESS_DEBUG_ADDR, tag: ProcessDebugAddrTag, prop_ty: u64, get:get_debug_addr, set:set_debug_addr},
    ]
);

sys::zx_info_process_t!(ProcessInfo);

impl From<sys::zx_info_process_t> for ProcessInfo {
    fn from(info: sys::zx_info_process_t) -> ProcessInfo {
        let sys::zx_info_process_t { return_code, start_time, flags } = info;
        ProcessInfo { return_code, start_time, flags }
    }
}

// ProcessInfo is able to be safely replaced with a byte representation and is a PoD type.
unsafe impl ObjectQuery for ProcessInfo {
    const TOPIC: Topic = Topic::PROCESS;
    type InfoTy = ProcessInfo;
}

sys::zx_info_task_stats_t!(TaskStatsInfo);

impl From<sys::zx_info_task_stats_t> for TaskStatsInfo {
    fn from(
        sys::zx_info_task_stats_t {
            mem_mapped_bytes,
            mem_private_bytes,
            mem_shared_bytes,
            mem_scaled_shared_bytes,
        }: sys::zx_info_task_stats_t,
    ) -> TaskStatsInfo {
        TaskStatsInfo {
            mem_mapped_bytes,
            mem_private_bytes,
            mem_shared_bytes,
            mem_scaled_shared_bytes,
        }
    }
}

// TaskStatsInfo is able to be safely replaced with a byte representation and is a PoD type.
unsafe impl ObjectQuery for TaskStatsInfo {
    const TOPIC: Topic = Topic::TASK_STATS;
    type InfoTy = TaskStatsInfo;
}

sys::zx_info_maps_mapping_t!(MapsMappingInfo);

impl From<sys::zx_info_maps_mapping_t> for MapsMappingInfo {
    fn from(
        sys::zx_info_maps_mapping_t {
            mmu_flags,
            padding1,
            vmo_koid,
            vmo_offset,
            committed_pages,
        }: sys::zx_info_maps_mapping_t,
    ) -> Self {
        Self { mmu_flags, padding1, vmo_koid, vmo_offset, committed_pages }
    }
}

sys::zx_info_maps_t!(ProcessMapsInfo);

impl ProcessMapsInfo {
    pub fn into_mapping_info(&self) -> Option<MapsMappingInfo> {
        if self.r#type != sys::ZX_INFO_MAPS_TYPE_MAPPING {
            return None;
        }
        // All the fields of u.mapping are objects that are well defined for any bit
        // representation, hence it is always safe to read it.
        Some(unsafe { self.u.mapping }.into())
    }
}

impl Default for ProcessMapsInfo {
    fn default() -> Self {
        let mapping = sys::zx_info_maps_mapping_t::default();
        Self {
            u: sys::InfoMapsTypeUnion { mapping },
            name: Default::default(),
            base: Default::default(),
            size: Default::default(),
            depth: Default::default(),
            r#type: Default::default(),
        }
    }
}

unsafe impl ObjectQuery for ProcessMapsInfo {
    const TOPIC: Topic = Topic::PROCESS_MAPS;
    type InfoTy = ProcessMapsInfo;
}

sys::zx_info_process_handle_stats_t!(ProcessHandleStats);

impl Default for ProcessHandleStats {
    fn default() -> Self {
        Self { handle_count: [0; ZX_OBJ_TYPE_UPPER_BOUND] }
    }
}

unsafe impl ObjectQuery for ProcessHandleStats {
    const TOPIC: Topic = Topic::PROCESS_HANDLE_STATS;
    type InfoTy = ProcessHandleStats;
}

impl Process {
    /// Similar to `Thread::start`, but is used to start the first thread in a process.
    ///
    /// Wraps the
    /// [zx_process_start](https://fuchsia.dev/fuchsia-src/reference/syscalls/process_start.md)
    /// syscall.
    pub fn start(
        &self,
        thread: &Thread,
        entry: usize,
        stack: usize,
        arg1: Handle,
        arg2: usize,
    ) -> Result<(), Status> {
        let process_raw = self.raw_handle();
        let thread_raw = thread.raw_handle();
        let arg1 = arg1.into_raw();
        ok(unsafe { sys::zx_process_start(process_raw, thread_raw, entry, stack, arg1, arg2) })
    }

    /// Create a thread inside a process.
    ///
    /// Wraps the
    /// [zx_thread_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/thread_create.md)
    /// syscall.
    pub fn create_thread(&self, name: &[u8]) -> Result<Thread, Status> {
        let process_raw = self.raw_handle();
        let name_ptr = name.as_ptr();
        let name_len = name.len();
        let options = 0;
        let mut thread_out = 0;
        let status = unsafe {
            sys::zx_thread_create(process_raw, name_ptr, name_len, options, &mut thread_out)
        };
        ok(status)?;
        unsafe { Ok(Thread::from(Handle::from_raw(thread_out))) }
    }

    /// Creates a process that shares half its address space with this process.
    ///
    /// The created process will also share its handle table and futex context with `self`.
    ///
    /// Returns the created process and a handle to the created process' restricted address space.
    ///
    /// Wraps the
    /// [zx_process_create_shared](https://fuchsia.dev/fuchsia-src/reference/syscalls/process_create_shared.md)
    /// syscall.
    pub fn create_shared(
        &self,
        options: ProcessOptions,
        name: &[u8],
    ) -> Result<(Process, Vmar), Status> {
        let self_raw = self.raw_handle();
        let name_ptr = name.as_ptr();
        let name_len = name.len();
        let mut process_out = 0;
        let mut restricted_vmar_out = 0;
        let status = unsafe {
            sys::zx_process_create_shared(
                self_raw,
                options.bits(),
                name_ptr,
                name_len,
                &mut process_out,
                &mut restricted_vmar_out,
            )
        };
        ok(status)?;
        unsafe {
            Ok((
                Process::from(Handle::from_raw(process_out)),
                Vmar::from(Handle::from_raw(restricted_vmar_out)),
            ))
        }
    }

    /// Write memory inside a process.
    ///
    /// Wraps the
    /// [zx_process_write_memory](https://fuchsia.dev/fuchsia-src/reference/syscalls/process_write_memory.md)
    /// syscall.
    pub fn write_memory(&self, vaddr: sys::zx_vaddr_t, bytes: &[u8]) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::zx_process_write_memory(
                self.raw_handle(),
                vaddr,
                bytes.as_ptr(),
                bytes.len(),
                &mut actual,
            )
        };
        ok(status).map(|()| actual)
    }

    /// Read memory from inside a process.
    ///
    /// Wraps the
    /// [zx_process_read_memory](https://fuchsia.dev/fuchsia-src/reference/syscalls/process_read_memory.md)
    /// syscall.
    pub fn read_memory(&self, vaddr: sys::zx_vaddr_t, bytes: &mut [u8]) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::zx_process_read_memory(
                self.raw_handle(),
                vaddr,
                bytes.as_mut_ptr(),
                bytes.len(),
                &mut actual,
            )
        };
        ok(status).map(|()| actual)
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_PROCESS topic.
    pub fn info(&self) -> Result<ProcessInfo, Status> {
        let mut info = ProcessInfo::default();
        object_get_info::<ProcessInfo>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_TASK_STATS topic.
    pub fn task_stats(&self) -> Result<TaskStatsInfo, Status> {
        let mut info = TaskStatsInfo::default();
        object_get_info::<TaskStatsInfo>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_PROCESS_MAPS topic.
    ///
    /// Returns `(num_returned, num_remaining)` on success and writes `num_returned`, entries to
    /// `info_out`.
    pub fn info_maps(&self, info_out: &mut [ProcessMapsInfo]) -> Result<(usize, usize), Status> {
        object_get_info::<ProcessMapsInfo>(self.as_handle_ref(), info_out)
    }

    /// Exit the current process with the given return code.
    ///
    /// Wraps the
    /// [zx_process_exit](https://fuchsia.dev/fuchsia-src/reference/syscalls/process_exit.md)
    /// syscall.
    pub fn exit(retcode: i64) -> ! {
        unsafe {
            sys::zx_process_exit(retcode);
            // kazoo generates the syscall returning a unit value. We know it will not proceed
            // past this point however.
            std::hint::unreachable_unchecked()
        }
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_PROCESS_HANDLE_STATS topic.
    pub fn handle_stats(&self) -> Result<ProcessHandleStats, Status> {
        let mut info = ProcessHandleStats::default();
        object_get_info::<ProcessHandleStats>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }
}

impl Task for Process {}

#[cfg(test)]
mod tests {
    use crate::cprng_draw;
    // The unit tests are built with a different crate name, but fdio and fuchsia_runtime return a
    // "real" fuchsia_zircon::Process that we need to use.
    use assert_matches::assert_matches;
    use fuchsia_zircon::{
        sys, system_get_page_size, AsHandleRef, ProcessInfo, ProcessInfoFlags, ProcessMapsInfo,
        Signals, Task, TaskStatsInfo, Time, VmarFlags, Vmo,
    };
    use std::ffi::CString;

    #[test]
    fn info_self() {
        let process = fuchsia_runtime::process_self();
        let info = process.info().unwrap();
        const STARTED: u32 = ProcessInfoFlags::STARTED.bits();
        assert_matches!(
            info,
            ProcessInfo {
                return_code: 0,
                start_time,
                flags: STARTED,
            } if start_time > 0
        );
    }

    #[test]
    fn stats_self() {
        let process = fuchsia_runtime::process_self();
        let task_stats = process.task_stats().unwrap();

        // Values greater than zero should be reported back for all memory usage
        // types.
        assert!(matches!(task_stats,
            TaskStatsInfo {
                mem_mapped_bytes,
                mem_private_bytes,
                mem_shared_bytes,
                mem_scaled_shared_bytes
            }
            if mem_mapped_bytes > 0
                && mem_private_bytes > 0
                && mem_shared_bytes > 0
                && mem_scaled_shared_bytes > 0));
    }

    #[test]
    fn exit_and_info() {
        let mut randbuf = [0; 8];
        cprng_draw(&mut randbuf);
        let expected_code = i64::from_le_bytes(randbuf);
        let arg = CString::new(format!("{}", expected_code)).unwrap();

        // This test utility will exercise zx::Process::exit, using the provided argument as the
        // return code.
        let binpath = CString::new("/pkg/bin/exit_with_code_util").unwrap();
        let process = fdio::spawn(
            &fuchsia_runtime::job_default(),
            fdio::SpawnOptions::DEFAULT_LOADER,
            &binpath,
            &[&arg],
        )
        .expect("Failed to spawn process");

        process
            .wait_handle(Signals::PROCESS_TERMINATED, Time::INFINITE)
            .expect("Wait for process termination failed");
        let info = process.info().unwrap();
        const STARTED_AND_EXITED: u32 =
            ProcessInfoFlags::STARTED.bits() | ProcessInfoFlags::EXITED.bits();
        assert_matches!(
            info,
            ProcessInfo {
                return_code,
                start_time,
                flags: STARTED_AND_EXITED,
            } if return_code == expected_code && start_time > 0
        );
    }

    #[test]
    fn kill_and_info() {
        // This test utility will sleep "forever" without exiting, so that we can kill it..
        let binpath = CString::new("/pkg/bin/sleep_forever_util").unwrap();
        let process = fdio::spawn(
            &fuchsia_runtime::job_default(),
            // Careful not to clone stdio here, or the test runner can hang.
            fdio::SpawnOptions::DEFAULT_LOADER,
            &binpath,
            &[&binpath],
        )
        .expect("Failed to spawn process");

        let info = process.info().unwrap();
        const STARTED: u32 = ProcessInfoFlags::STARTED.bits();
        assert_matches!(
            info,
            ProcessInfo {
                return_code: 0,
                start_time,
                flags: STARTED,
             } if start_time > 0
        );

        process.kill().expect("Failed to kill process");
        process
            .wait_handle(Signals::PROCESS_TERMINATED, Time::INFINITE)
            .expect("Wait for process termination failed");

        let info = process.info().unwrap();
        const STARTED_AND_EXITED: u32 =
            ProcessInfoFlags::STARTED.bits() | ProcessInfoFlags::EXITED.bits();
        assert_matches!(
            info,
            ProcessInfo {
                return_code: sys::ZX_TASK_RETCODE_SYSCALL_KILL,
                start_time,
                flags: STARTED_AND_EXITED,
            } if start_time > 0
        );
    }

    #[test]
    fn maps_info() {
        let root_vmar = fuchsia_runtime::vmar_root_self();
        let process = fuchsia_runtime::process_self();

        // Create two mappings so we know what to expect from our test calls.
        let vmo = Vmo::create(system_get_page_size() as u64).unwrap();
        let vmo_koid = vmo.get_koid().unwrap();

        let map1 = root_vmar
            .map(0, &vmo, 0, system_get_page_size() as usize, VmarFlags::PERM_READ)
            .unwrap();
        let map2 = root_vmar
            .map(0, &vmo, 0, system_get_page_size() as usize, VmarFlags::PERM_READ)
            .unwrap();

        // Querying a single info. As we know there are at least two mappings this is guaranteed to
        // not return all of them.
        let mut info = ProcessMapsInfo::default();
        let (returned, remaining) = process.info_maps(std::slice::from_mut(&mut info)).unwrap();
        assert_eq!(returned, 1);
        assert!(remaining > 0);

        // Add some slack to the total to account for mappings created as a result of the heap
        // allocation in Vec.
        let total = returned + remaining + 10;

        // Allocate and retrieve all of the mappings.
        let mut info = Vec::with_capacity(total);
        info.resize(total, ProcessMapsInfo::default());

        let (_, remaining) = process.info_maps(info.as_mut_slice()).unwrap();
        // Don't know exactly how many were returned, but since we are going to search for our
        // mappings we do need to know that none are remaining.
        assert_eq!(remaining, 0);

        // We should find our two mappings in the info.
        let count = info
            .iter()
            .filter_map(ProcessMapsInfo::into_mapping_info)
            .filter(|map| map.vmo_koid == vmo_koid.raw_koid())
            .count();
        assert_eq!(count, 2);

        // We created these mappings and are not letting any references to them escape so unmapping
        // is safe to do.
        unsafe {
            root_vmar.unmap(map1, system_get_page_size() as usize).unwrap();
            root_vmar.unmap(map2, system_get_page_size() as usize).unwrap();
        }
    }

    #[test]
    fn handle_stats() {
        let process = fuchsia_runtime::process_self();
        let handle_stats = process.handle_stats().unwrap();

        // We don't have an opinion about how many handles a typical process has, or what types
        // they will be (the function returns counts for up to 64 different types),
        // but a reasonable total should be between 1 and a million.
        let sum: u32 = handle_stats.handle_count.iter().sum();

        assert!(sum > 0);
        assert!(sum < 1_000_000);
    }
}
