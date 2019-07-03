// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::{DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased},
    io_util,
    std::os::raw,
    std::ptr,
    std::sync::atomic::AtomicPtr,
    std::thread,
};

/// This will create a new thread on which it will run the C++ implementation of Memfs. Once
/// running, the `clone_root_hadle` function can be used to get new handles to the root of this
/// memfs. The Memfs thread will be destroyed when this struct is dropped. This memfs wrapper will
/// panic on errors.
pub struct Memfs {
    root_handle: DirectoryProxy,
    fs_handle: *mut MemfsFilesystem,
    async_loop: *mut AsyncLoop,
}

impl Memfs {
    /// Starts up a new memfs on a new thread
    pub fn new() -> Memfs {
        // Create async loop
        let config = AsyncLoopConfig {
            make_default_for_current_thread: true,
            prologue: ptr::null_mut(),
            epilogue: ptr::null_mut(),
            data: ptr::null_mut(),
        };
        let mut async_loop: *mut AsyncLoop = ptr::null_mut();
        let status = unsafe {
            async_loop_create(
                &config as *const AsyncLoopConfig,
                &mut async_loop as *mut *mut AsyncLoop,
            )
        };
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK, "failed to create async loop");
        let async_dispatcher = unsafe { async_loop_get_dispatcher(async_loop) };

        // Create memfs with new async loop
        let mut memfs_filesystem: *mut MemfsFilesystem = ptr::null_mut();
        let mut root_handle: zx::sys::zx_handle_t = zx::sys::ZX_HANDLE_INVALID;
        let status = unsafe {
            memfs_create_filesystem(
                async_dispatcher,
                &mut memfs_filesystem as *mut *mut MemfsFilesystem,
                &mut root_handle as *mut zx::sys::zx_handle_t,
            )
        };
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK, "failed to create memfs");

        // Put it in a mutex so Rust is willing to move it between threads
        let mut async_loop_ptr = AtomicPtr::new(async_loop);
        thread::spawn(move || {
            unsafe { async_loop_run(*async_loop_ptr.get_mut(), zx::sys::ZX_TIME_INFINITE, false) };
        });
        Memfs {
            root_handle: DirectoryProxy::new(
                fasync::Channel::from_channel(zx::Channel::from_handle(unsafe {
                    zx::Handle::from_raw(root_handle)
                }))
                .unwrap(),
            ),
            fs_handle: memfs_filesystem,
            async_loop,
        }
    }

    /// Returns a new handle to the root directory of this memfs
    pub fn clone_root_handle(&self) -> DirectoryProxy {
        io_util::clone_directory(&self.root_handle, CLONE_FLAG_SAME_RIGHTS)
            .expect("failed to clone root handle")
    }
}

impl Drop for Memfs {
    /// Tears down memfs, waits for memfs to be torn down, and then tears down the async loop that
    /// was running memfs
    fn drop(&mut self) {
        let mut sync = SyncCompletion::new();
        unsafe {
            memfs_free_filesystem(self.fs_handle, &mut sync as *mut SyncCompletion);
            let status =
                sync_completion_wait(&mut sync as *mut SyncCompletion, zx::sys::ZX_TIME_INFINITE);
            assert_eq!(
                zx::Status::from_raw(status),
                zx::Status::OK,
                "sync_completion_wait returned with non-ok status"
            );
            async_loop_destroy(self.async_loop);
        }
    }
}

type MemfsFilesystem = raw::c_void;

#[link(name = "memfs")]
extern "C" {
    fn memfs_create_filesystem(
        dispatcher: *mut AsyncDispatcher,
        out_fs: *mut *mut MemfsFilesystem,
        out_root: *mut zx::sys::zx_handle_t,
    ) -> zx::sys::zx_status_t;
    fn memfs_free_filesystem(fs: *mut MemfsFilesystem, unmounted: *mut SyncCompletion);
}

#[repr(C)]
#[derive(Debug, Clone)]
struct AsyncLoopConfig {
    make_default_for_current_thread: bool,
    prologue: *mut raw::c_void,
    epilogue: *mut raw::c_void,
    data: *mut raw::c_void,
}

type AsyncLoop = raw::c_void;

#[link(name = "async-loop")]
extern "C" {
    fn async_loop_create(
        config: *const AsyncLoopConfig,
        out_loop: *mut *mut AsyncLoop,
    ) -> zx::sys::zx_status_t;
    fn async_loop_get_dispatcher(loop_: *mut AsyncLoop) -> *mut AsyncDispatcher;
    fn async_loop_destroy(loop_: *mut AsyncLoop);
    fn async_loop_run(
        loop_: *mut AsyncLoop,
        deadline: zx::sys::zx_time_t,
        once: bool,
    ) -> zx::sys::zx_status_t;
}

type AsyncDispatcher = raw::c_void;

// This `#[allow(dead_code)]` is load bearing because without it the program will fail to link
// against the following symbols, which are needed by the async-loop library.
#[allow(dead_code)]
#[link(name = "async-default")]
#[link(name = "async")]
extern "C" {
    fn async_get_default_dispatcher() -> *mut AsyncDispatcher;
    fn async_set_default_dispatcher(dispatcher: *mut AsyncDispatcher);
}

#[repr(C)]
#[derive(Debug, Clone)]
struct SyncCompletion {
    futex: zx::sys::zx_futex_t,
}

impl SyncCompletion {
    fn new() -> SyncCompletion {
        SyncCompletion { futex: 0 }
    }
}

#[link(name = "sync")]
extern "C" {
    fn sync_completion_wait(
        completion: *mut SyncCompletion,
        timeout: zx::sys::zx_duration_t,
    ) -> zx::sys::zx_status_t;
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_io::{OPEN_FLAG_CREATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        std::path::PathBuf,
    };

    #[fasync::run_singlethreaded(test)]
    async fn use_a_file() {
        let memfs = Memfs::new();
        let root_of_memfs = memfs.clone_root_handle();

        let file_name = PathBuf::from("myfile");
        let file_contents = "hippos are just really neat".to_string();

        // Create a file and write some contents
        let file_proxy =
            io_util::open_file(&root_of_memfs, &file_name, OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE)
                .expect("failed to open file");
        let (s, _) =
            await!(file_proxy.write(&mut file_contents.clone().as_bytes().to_vec().drain(..)))
                .expect("failed to write to file");
        assert_eq!(zx::Status::OK, zx::Status::from_raw(s));
        let s = await!(file_proxy.close()).expect("failed to close file");
        assert_eq!(zx::Status::OK, zx::Status::from_raw(s));

        // Open the same file and read the contents back
        let file_proxy =
            io_util::open_file(&root_of_memfs, &file_name, OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE)
                .expect("failed to open file");
        let read_contents = await!(io_util::read_file(&file_proxy)).expect("failed to read file");

        assert_eq!(file_contents, read_contents);

        //drop(memfs);
    }
}
