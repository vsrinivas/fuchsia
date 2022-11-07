use std::{
    mem,
    os::unix::io::RawFd
};

use inotify_sys as ffi;
use libc::{
    c_void,
    size_t,
};


pub fn read_into_buffer(fd: RawFd, buffer: &mut [u8]) -> isize {
    unsafe {
        // Discard the unaligned portion, if any, of the supplied buffer
        let buffer = align_buffer_mut(buffer);

        ffi::read(
            fd,
            buffer.as_mut_ptr() as *mut c_void,
            buffer.len() as size_t
        )
    }
}

pub fn align_buffer(buffer: &[u8]) -> &[u8] {
    if buffer.len() >= mem::align_of::<ffi::inotify_event>() {
        let ptr = buffer.as_ptr();
        let offset = ptr.align_offset(mem::align_of::<ffi::inotify_event>());
        &buffer[offset..]
    } else {
        &buffer[0..0]
    }
}

pub fn align_buffer_mut(buffer: &mut [u8]) -> &mut [u8] {
   if buffer.len() >= mem::align_of::<ffi::inotify_event>() {
        let ptr = buffer.as_mut_ptr();
        let offset = ptr.align_offset(mem::align_of::<ffi::inotify_event>());
        &mut buffer[offset..]
   } else {
       &mut buffer[0..0]
   } 
}
