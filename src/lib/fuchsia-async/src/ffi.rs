// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_zircon_sys::{
    zx_handle_t, zx_packet_signal_t, zx_signals_t, zx_status_t, zx_time_t, ZX_ERR_NOT_SUPPORTED,
    ZX_OK,
};
use futures::channel::oneshot;
use parking_lot::Mutex;
use std::time::Duration;

struct EPtr(*mut Executor);
unsafe impl Send for EPtr {}
unsafe impl Sync for EPtr {}

impl EPtr {
    unsafe fn as_ref(&self) -> &Executor {
        self.0.as_ref().unwrap()
    }
}

pub struct Executor {
    executor: Mutex<fasync::Executor>,
    quit_tx: Mutex<Option<oneshot::Sender<()>>>,
    start: fasync::Time,
    cb_executor: *mut std::ffi::c_void,
}

impl Executor {
    fn new(cb_executor: *mut std::ffi::c_void) -> Box<Executor> {
        Box::new(Executor {
            executor: Mutex::new(fasync::Executor::new().unwrap()),
            quit_tx: Mutex::new(None),
            start: fasync::Time::now(),
            cb_executor,
        })
    }

    fn run_singlethreaded(&self) {
        let (tx, rx) = oneshot::channel();
        *self.quit_tx.lock() = Some(tx);
        self.executor.lock().run_singlethreaded(async move { rx.await }).unwrap();
    }

    fn quit(&self) {
        self.quit_tx.lock().take().unwrap().send(()).unwrap();
    }

    fn now(&self) -> zx_time_t {
        let dur = fasync::Time::now() - self.start;
        #[cfg(target_os = "fuchsia")]
        let r = dur.into_nanos();
        #[cfg(not(target_os = "fuchsia"))]
        let r = dur.as_nanos() as zx_time_t;
        r
    }
}

/// Duplicated from //zircon/system/ulib/async/include/lib/async/dispatcher.h
#[repr(C)]
struct async_state_t {
    _reserved: [usize; 2],
}

/// Duplicated from //zircon/system/ulib/async/include/lib/async/wait.h
#[repr(C)]
struct async_wait_t {
    _state: async_state_t,
    _handler: extern "C" fn(
        *mut std::ffi::c_void,
        *mut async_wait_t,
        zx_status_t,
        *const zx_packet_signal_t,
    ),
    _object: zx_handle_t,
    _trigger: zx_signals_t,
    _options: u32,
}

/// Duplicated from //zircon/system/ulib/async/include/lib/async/task.h
#[repr(C)]
struct async_task_t {
    _state: async_state_t,
    handler: extern "C" fn(*mut std::ffi::c_void, *mut async_task_t, zx_status_t),
    deadline: zx_time_t,
}

struct TaskPtr(*mut async_task_t);
unsafe impl Send for TaskPtr {}
unsafe impl Sync for TaskPtr {}
impl TaskPtr {
    unsafe fn as_ref(&self) -> &async_task_t {
        self.0.as_ref().unwrap()
    }
}

#[no_mangle]
pub extern "C" fn fasync_executor_create(cb_executor: *mut std::ffi::c_void) -> *mut Executor {
    Box::into_raw(Executor::new(cb_executor))
}

#[no_mangle]
pub unsafe extern "C" fn fasync_executor_run_singlethreaded(executor: *mut Executor) {
    EPtr(executor).as_ref().run_singlethreaded()
}

#[no_mangle]
pub unsafe extern "C" fn fasync_executor_quit(executor: *mut Executor) {
    EPtr(executor).as_ref().quit()
}

#[no_mangle]
pub unsafe extern "C" fn fasync_executor_destroy(executor: *mut Executor) {
    drop(Box::from_raw(executor))
}

#[no_mangle]
unsafe extern "C" fn fasync_executor_now(executor: *mut Executor) -> zx_time_t {
    EPtr(executor).as_ref().now()
}

#[no_mangle]
unsafe extern "C" fn fasync_executor_begin_wait(
    _executor: *mut Executor,
    _wait: *mut async_wait_t,
) -> zx_status_t {
    ZX_ERR_NOT_SUPPORTED
}

#[no_mangle]
unsafe extern "C" fn fasync_executor_cancel_wait(
    _executor: *mut Executor,
    _wait: *mut async_wait_t,
) -> zx_status_t {
    ZX_ERR_NOT_SUPPORTED
}

#[no_mangle]
unsafe extern "C" fn fasync_executor_post_task(
    executor: *mut Executor,
    task: *mut async_task_t,
) -> zx_status_t {
    let executor = EPtr(executor);
    let task = TaskPtr(task);
    fasync::Task::spawn(async move {
        let deadline = Duration::from_nanos(task.as_ref().deadline as u64);
        let start = executor.as_ref().start;
        fasync::Timer::new(start + deadline.into()).await;
        (task.as_ref().handler)(executor.as_ref().cb_executor, task.0, ZX_OK)
    })
    .detach();
    ZX_OK
}

#[no_mangle]
unsafe extern "C" fn fasync_executor_cancel_task(
    _executor: *mut Executor,
    _task: *mut async_task_t,
) -> zx_status_t {
    ZX_ERR_NOT_SUPPORTED
}
