use fdio::{fdio_sys, ioctl_raw};
use std::os::raw;
use failure::Error;

const PTY_EVENT_HANGUP: u8 = 1;
const PTY_EVENT_INTERRUPT: u8 = 2;
const PTY_EVENT_SUSPEND: u8 = 4;
const PTY_EVENT_MASK: u8 = 7;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct pty_clr_set_t {
    pub clr: u32,
    pub set: u32,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct pty_window_size_t {
    pub width: u32,
    pub height: u32,
}

pub fn get_window_size() -> Result<pty_window_size_t, Error> {
    let window = pty_window_size_t {
        width: 0,
        height: 0,
    };

    let success = unsafe {
        ioctl_raw(0,
              IOCTL_PTY_GET_WINDOW_SIZE,
              ::std::ptr::null_mut() as *mut raw::c_void,
              0,
              &window as *const _ as *mut raw::c_void,
              ::std::mem::size_of::<pty_window_size_t>())
    };

    if success < 0 {
        Err(format_err!("get_window_size Ioctl failure"))
    } else {
        Ok(window)
    }
}

const IOCTL_PTY_GET_WINDOW_SIZE: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_PTY,
    0x01
);

const IOCTL_PTY_SET_WINDOW_SIZE: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_PTY,
    0x20
);
