// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::{
        self as zx,
        sys::{zx_handle_t, zx_status_t},
        Duration, DurationNum,
    },
    libc::c_char,
    std::ffi::CStr,
};

use self::uart::{serial_impl_async_protocol_t, Serial};
use crate::{control_plane::Message, transport::Uart, worker::WorkerHandle};

pub mod uart;

/// Send a control plane message to the worker using the provided `WorkerHandle` pointer.
///
/// Memory Safety: In order to safely call this function, `ptr` must point to a valid `WorkerHandle`
/// object or null. A null `ptr` results in a panic or abort but is memory safe. Passing in null
/// should be avoided, but the check is made as another line of defense against dereferencing a null
/// ptr.
unsafe fn send_worker_control_message(
    ptr: *const WorkerHandle,
    timeout_ms: u64,
    message: Message,
) -> zx_status_t {
    let worker = {
        assert!(!ptr.is_null());
        &*ptr
    };
    let timeout = Duration::from_millis(timeout_ms as i64);
    worker.control_plane.lock().send(message, timeout).into_raw()
}

/// Create a new worker object and return a thread-safe handle to communicate with it.
#[no_mangle]
pub unsafe extern "C" fn bt_hci_transport_start(
    driver_name: *const c_char,
    worker: *mut *mut WorkerHandle,
) -> zx_status_t {
    assert!(!driver_name.is_null());
    let name = { CStr::from_ptr(driver_name) }.to_string_lossy();
    let thread_name = format!("bt_{}_read_thread", name);
    match WorkerHandle::new(thread_name) {
        Ok(d) => {
            *worker = Box::into_raw(Box::new(d));
            zx::sys::ZX_OK
        }
        Err(e) => e.into_raw(),
    }
}

/// Perform any operations necessary for unbinding. This does not stop the worker thread or free
/// `ptr`.
#[no_mangle]
pub unsafe extern "C" fn bt_hci_transport_unbind(
    ptr: *const WorkerHandle,
    timeout_ms: u64,
) -> zx_status_t {
    send_worker_control_message(ptr, timeout_ms, Message::Unbind)
}

/// Shutdown the `Worker` associated with this `WorkerHandle`, freeing all owned memory,
/// and finally freeing the `WorkerHandle` when finished.
///
/// After a `*mut WorkerHandle` is passed to this function, it no longer points to valid memory.
#[no_mangle]
pub unsafe extern "C" fn bt_hci_transport_shutdown(ptr: *mut WorkerHandle, timeout_ms: u64) {
    if ptr.is_null() {
        return;
    }
    { Box::from_raw(ptr) }.shutdown((timeout_ms as i64).millis());
}

/// bt_hci_transport_open_uart takes ownership of serial.
#[no_mangle]
pub unsafe extern "C" fn bt_hci_transport_open_uart(
    ptr: *const WorkerHandle,
    serial: *mut serial_impl_async_protocol_t,
    timeout_ms: u64,
) -> zx_status_t {
    let serial = { Serial::new(serial) };
    let message = Message::OpenTransport(Box::new(Uart::builder(serial)));
    send_worker_control_message(ptr, timeout_ms, message)
}

#[no_mangle]
pub unsafe extern "C" fn bt_hci_transport_open_command_channel(
    ptr: *const WorkerHandle,
    in_handle: zx_handle_t,
    timeout_ms: u64,
) -> zx_status_t {
    let chan = { zx::Handle::from_raw(in_handle) }.into();
    send_worker_control_message(ptr, timeout_ms, Message::OpenCmd(chan))
}

#[no_mangle]
pub unsafe extern "C" fn bt_hci_transport_open_acl_data_channel(
    ptr: *const WorkerHandle,
    in_handle: zx_handle_t,
    timeout_ms: u64,
) -> zx_status_t {
    let chan = { zx::Handle::from_raw(in_handle) }.into();
    send_worker_control_message(ptr, timeout_ms, Message::OpenAcl(chan))
}

#[no_mangle]
pub unsafe extern "C" fn bt_hci_transport_open_snoop_channel(
    ptr: *const WorkerHandle,
    in_handle: zx_handle_t,
    timeout_ms: u64,
) -> zx_status_t {
    let chan = { zx::Handle::from_raw(in_handle) }.into();
    send_worker_control_message(ptr, timeout_ms, Message::OpenSnoop(chan))
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fuchsia_zircon::{
            sys::{ZX_ERR_ALREADY_BOUND, ZX_OK},
            AsHandleRef,
        },
        std::ffi::CString,
    };

    const TIMEOUT_MS: u64 = 30 * 1000; // 30 seconds

    #[test]
    fn start_worker() {
        let name = CString::new("test").unwrap();
        let mut worker = std::ptr::null_mut();
        let status = unsafe { bt_hci_transport_start(name.as_ptr(), &mut worker) };
        assert!(!worker.is_null());
        assert_eq!(status, ZX_OK);
    }

    #[test]
    #[should_panic]
    fn start_bt_hci_transport_null_name() {
        let mut worker = std::ptr::null_mut();
        let status = unsafe { bt_hci_transport_start(std::ptr::null(), &mut worker) };
        assert_eq!(status, ZX_OK);
        assert!(!worker.is_null());
    }

    #[test]
    fn shutdown_worker() {
        let name = CString::new("test").unwrap();
        let mut worker = std::ptr::null_mut();
        let status = unsafe { bt_hci_transport_start(name.as_ptr(), &mut worker) };
        assert_eq!(status, zx::sys::ZX_OK);
        unsafe { bt_hci_transport_shutdown(worker, TIMEOUT_MS) };

        // freeing null ptr is ok
        unsafe { bt_hci_transport_shutdown(std::ptr::null_mut(), TIMEOUT_MS) };
    }

    #[test]
    fn open_transport_uart() {
        let name = CString::new("test").unwrap();
        let mut worker = std::ptr::null_mut();
        let status = unsafe { bt_hci_transport_start(name.as_ptr(), &mut worker) };
        assert_eq!(status, ZX_OK);
        let serial = Box::new(serial_impl_async_protocol_t::new());
        let status =
            unsafe { bt_hci_transport_open_uart(worker, Box::into_raw(serial), TIMEOUT_MS) };
        assert_eq!(status, ZX_OK);

        // test that errors are mapped through ffi function.
        let serial = Box::new(serial_impl_async_protocol_t::new());
        let status =
            unsafe { bt_hci_transport_open_uart(worker, Box::into_raw(serial), TIMEOUT_MS) };
        assert_eq!(status, ZX_ERR_ALREADY_BOUND);
    }

    #[test]
    #[should_panic]
    fn open_transport_uart_with_null_worker() {
        let serial = Box::new(serial_impl_async_protocol_t::new());
        unsafe { bt_hci_transport_open_uart(std::ptr::null(), Box::into_raw(serial), TIMEOUT_MS) };
    }

    #[test]
    fn open_command_channel() {
        let name = CString::new("test").unwrap();
        let mut worker = std::ptr::null_mut();
        let status = unsafe { bt_hci_transport_start(name.as_ptr(), &mut worker) };
        assert_eq!(status, ZX_OK);
        let (cmd, _cmd) = zx::Channel::create().unwrap();
        let status =
            unsafe { bt_hci_transport_open_command_channel(worker, cmd.raw_handle(), TIMEOUT_MS) };
        assert_eq!(status, ZX_OK);

        // test that errors are mapped through ffi function.
        let (cmd, _cmd) = zx::Channel::create().unwrap();
        let status =
            unsafe { bt_hci_transport_open_command_channel(worker, cmd.raw_handle(), TIMEOUT_MS) };
        assert_eq!(status, ZX_ERR_ALREADY_BOUND);
    }

    #[test]
    #[should_panic]
    fn open_command_with_null_worker() {
        let (cmd, _cmd) = zx::Channel::create().unwrap();
        unsafe {
            bt_hci_transport_open_command_channel(std::ptr::null(), cmd.raw_handle(), TIMEOUT_MS)
        };
    }

    #[test]
    fn open_acl_channel() {
        let name = CString::new("test").unwrap();
        let mut worker = std::ptr::null_mut();
        let status = unsafe { bt_hci_transport_start(name.as_ptr(), &mut worker) };
        assert_eq!(status, ZX_OK);
        let (acl, _acl) = zx::Channel::create().unwrap();
        let status =
            unsafe { bt_hci_transport_open_acl_data_channel(worker, acl.raw_handle(), TIMEOUT_MS) };
        assert_eq!(status, ZX_OK);

        // test that errors are mapped through ffi function.
        let (acl, _acl) = zx::Channel::create().unwrap();
        let status =
            unsafe { bt_hci_transport_open_acl_data_channel(worker, acl.raw_handle(), TIMEOUT_MS) };
        assert_eq!(status, ZX_ERR_ALREADY_BOUND);
    }

    #[test]
    #[should_panic]
    fn open_acl_data_with_null_worker() {
        let (acl, _acl) = zx::Channel::create().unwrap();
        unsafe {
            bt_hci_transport_open_acl_data_channel(std::ptr::null(), acl.raw_handle(), TIMEOUT_MS)
        };
    }

    #[test]
    fn open_snoop_channel() {
        let name = CString::new("test").unwrap();
        let mut worker = std::ptr::null_mut();
        let status = unsafe { bt_hci_transport_start(name.as_ptr(), &mut worker) };
        assert_eq!(status, ZX_OK);
        let (snoop, _snoop) = zx::Channel::create().unwrap();
        let status =
            unsafe { bt_hci_transport_open_snoop_channel(worker, snoop.raw_handle(), TIMEOUT_MS) };
        assert_eq!(status, ZX_OK);

        // test that errors are mapped through ffi function.
        let (snoop, _snoop) = zx::Channel::create().unwrap();
        let status =
            unsafe { bt_hci_transport_open_snoop_channel(worker, snoop.raw_handle(), TIMEOUT_MS) };
        assert_eq!(status, ZX_ERR_ALREADY_BOUND);
    }

    #[test]
    #[should_panic]
    fn open_snoop_with_null_worker() {
        let (snoop, _snoop) = zx::Channel::create().unwrap();
        unsafe {
            bt_hci_transport_open_snoop_channel(std::ptr::null(), snoop.raw_handle(), TIMEOUT_MS)
        };
    }
}
