// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{ensure, Error},
    std::{ffi::c_void, ops::Deref, ptr, slice},
};

#[repr(C)]
pub struct BufferProvider {
    // Acquire a buffer with a given minimum length from the provider.
    // The provider must release the underlying buffer's ownership and transfer it to this crate.
    // The buffer will be returned via the `return_buffer` callback when it's no longer used.
    take_buffer: unsafe extern "C" fn(min_len: usize) -> Buffer,
}

impl BufferProvider {
    pub fn take_buffer(&self, min_len: usize) -> Result<Buffer, Error> {
        // Resulting buffer is checked for null pointers.
        // The buffer is guaranteed to be released whenever it goes out of scope.
        let buf = unsafe { (self.take_buffer)(min_len) };
        ensure!(!buf.raw.is_null(), "buffer's raw ptr must not be null");
        ensure!(!buf.data.is_null(), "buffer's data ptr must not be null");
        Ok(buf)
    }
}

#[derive(Debug)]
#[repr(C)]
pub struct Buffer {
    // Returns the buffer's ownership. The buffer will no longer be used and can be freed safely.
    return_buffer: unsafe extern "C" fn(raw: *mut c_void),
    // Pointer to the buffer's underlying data structure.
    raw: *mut c_void,
    // Pointer to the start of the buffer's data portion and its length.
    data: *mut u8,
    len: usize,
}

impl Drop for Buffer {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            // A buffer can never be dropped twice as it'll be reset once dropped.
            unsafe { (self.return_buffer)(self.raw) };
            self.reset();
        }
    }
}

impl Buffer {
    pub fn as_slice(&self) -> &[u8] {
        if self.data.is_null() {
            &[]
        } else {
            // `data` pointer is never null. This could still be problematic if the buffer was
            // already destroyed, however, that's out of this code's control.
            unsafe { slice::from_raw_parts(self.data, self.len) }
        }
    }

    fn reset(&mut self) {
        self.raw = ptr::null_mut();
        self.data = ptr::null_mut();
        self.len = 0;
    }
}

impl Deref for Buffer {
    type Target = [u8];

    fn deref(&self) -> &[u8] {
        self.as_slice()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::ptr};

    extern "C" fn default_return_buffer(_raw: *mut c_void) {}

    #[test]
    fn return_out_of_scope_buffer() {
        static mut RETURNED_RAW_BUFFER: *mut c_void = ptr::null_mut();

        unsafe extern "C" fn assert_return_buffer(raw: *mut c_void) {
            // Safe, this is a function specific static field.
            unsafe {
                RETURNED_RAW_BUFFER = raw;
            }
        }

        // Once buffer goes out of scope the raw pointer should be returned to the provider.
        {
            Buffer {
                return_buffer: assert_return_buffer,
                raw: 42 as *mut c_void,
                data: ptr::null_mut(),
                len: 0,
            };
        }

        // Safe, this is a function specific static field.
        unsafe {
            assert_eq!(42 as *mut c_void, RETURNED_RAW_BUFFER);
        }
    }

    #[test]
    fn as_slice_null_data() {
        let buf = Buffer {
            return_buffer: default_return_buffer,
            raw: 42 as *mut c_void,
            data: ptr::null_mut(),
            len: 10,
        };
        assert_eq!(buf.as_slice(), []);
    }

    #[test]
    fn as_slice() {
        let mut data = [0u8, 1, 2, 3];
        let buf = Buffer {
            return_buffer: default_return_buffer,
            raw: 42 as *mut c_void,
            data: data.as_mut_ptr(),
            len: data.len(),
        };
        assert_eq!(buf.as_slice(), &data[..]);
    }

}
