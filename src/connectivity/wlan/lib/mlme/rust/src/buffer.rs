// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    std::{
        ffi::c_void,
        ops::{Deref, DerefMut},
        ptr, slice,
    },
};

#[repr(C)]
pub struct BufferProvider {
    /// Acquire a `InBuf` with a given minimum length from the provider.
    /// The provider must release the underlying buffer's ownership and transfer it to this crate.
    /// The buffer will be returned via the `free_buffer` callback when it's no longer used.
    get_buffer: unsafe extern "C" fn(min_len: usize) -> InBuf,
}

impl BufferProvider {
    pub fn get_buffer(&self, min_len: usize) -> Result<InBuf, Error> {
        // Resulting buffer is checked for null pointers.
        let buf = unsafe { (self.get_buffer)(min_len) };
        if buf.raw.is_null() || buf.data.is_null() {
            Err(Error::NoResources(min_len))
        } else {
            Ok(buf)
        }
    }
}

/// An input buffer will always be returned to its original owner when no longer being used.
/// An input buffer is used for every buffer handed from C++ to Rust.
#[derive(Debug)]
#[repr(C)]
pub struct InBuf {
    /// Returns the buffer's ownership and free it.
    free_buffer: unsafe extern "C" fn(raw: *mut c_void),
    /// Pointer to the buffer's underlying data structure.
    raw: *mut c_void,
    /// Pointer to the start of the buffer's data portion and its length.
    data: *mut u8,
    len: usize,
}

impl Drop for InBuf {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            // A buffer can never be dropped twice as it'll be reset once dropped.
            unsafe { (self.free_buffer)(self.raw) };
            self.reset();
        }
    }
}

impl InBuf {
    pub fn as_slice(&self) -> &[u8] {
        if self.data.is_null() {
            &[]
        } else {
            // `data` pointer is never null. This could still be problematic if the buffer was
            // already destroyed, however, that's out of this code's control.
            unsafe { slice::from_raw_parts(self.data, self.len) }
        }
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        if self.data.is_null() {
            &mut []
        } else {
            // `data` pointer is never null. This could still be problematic if the buffer was
            // already destroyed, however, that's out of this code's control.
            unsafe { slice::from_raw_parts_mut(self.data, self.len) }
        }
    }

    fn reset(&mut self) {
        self.raw = ptr::null_mut();
        self.data = ptr::null_mut();
        self.len = 0;
    }
}

impl Deref for InBuf {
    type Target = [u8];

    fn deref(&self) -> &[u8] {
        self.as_slice()
    }
}

impl DerefMut for InBuf {
    fn deref_mut(&mut self) -> &mut [u8] {
        self.as_mut_slice()
    }
}

/// An output buffer requires its owner to manage the underlying buffer's memory themselves.
/// An output buffer is used for every buffer handed from Rust to C++.
#[derive(Debug)]
#[repr(C)]
pub struct OutBuf {
    /// Pointer to the buffer's underlying data structure.
    raw: *mut c_void,
    /// Pointer to the start of the buffer's data portion and the amount of bytes written.
    data: *mut u8,
    written_bytes: usize,
}

impl OutBuf {
    /// Converts a given `InBuf` into an `OutBuf`.
    /// The resulting `OutBuf` must be used and returned to `InBuf`'s original owner.
    /// If the buffer is not returned, memory is leaked.
    #[must_use]
    pub fn from(buf: InBuf, written_bytes: usize) -> Self {
        let outbuf = OutBuf { raw: buf.raw, data: buf.data, written_bytes };
        std::mem::forget(buf);
        outbuf
    }

    pub fn as_slice(&self) -> &[u8] {
        if self.data.is_null() {
            &[]
        } else {
            // `data` pointer is never null.
            unsafe { slice::from_raw_parts(self.data, self.written_bytes) }
        }
    }

    #[cfg(test)]
    pub fn free(self) {
        let data = self.raw as *mut Vec<u8>;
        unsafe {
            drop(Box::from_raw(data));
        }
    }
}

#[cfg(test)]
pub struct FakeBufferProvider;

#[cfg(test)]
impl FakeBufferProvider {
    pub fn new() -> BufferProvider {
        BufferProvider { get_buffer: Self::get_buffer }
    }

    pub extern "C" fn free_buffer(raw: *mut c_void) {
        let data = raw as *mut Vec<u8>;
        unsafe {
            drop(Box::from_raw(data));
        }
    }

    pub extern "C" fn get_buffer(min_len: usize) -> InBuf {
        let mut data = Box::new(vec![0u8; min_len]);
        let data_ptr = data.as_mut_ptr();
        let ptr = Box::into_raw(data);
        InBuf {
            free_buffer: Self::free_buffer,
            raw: ptr as *mut c_void,
            data: data_ptr,
            len: min_len,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::ptr};

    extern "C" fn default_free_buffer(_raw: *mut c_void) {}

    #[test]
    fn return_out_of_scope_buffer() {
        static mut RETURNED_RAW_BUFFER: *mut c_void = ptr::null_mut();

        unsafe extern "C" fn assert_free_buffer(raw: *mut c_void) {
            RETURNED_RAW_BUFFER = raw;
        }

        // Once buffer goes out of scope the raw pointer should be returned to the provider.
        {
            InBuf {
                free_buffer: assert_free_buffer,
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
        let buf = InBuf {
            free_buffer: default_free_buffer,
            raw: 42 as *mut c_void,
            data: ptr::null_mut(),
            len: 10,
        };
        assert_eq!(buf.as_slice(), []);
    }

    #[test]
    fn as_slice() {
        let mut data = [0u8, 1, 2, 3];
        let buf = InBuf {
            free_buffer: default_free_buffer,
            raw: 42 as *mut c_void,
            data: data.as_mut_ptr(),
            len: data.len(),
        };
        assert_eq!(buf.as_slice(), &data[..]);
        assert_eq!(&buf[..], &data[..]);
    }

    #[test]
    fn as_mut_slice_null_data() {
        let mut buf = InBuf {
            free_buffer: default_free_buffer,
            raw: 42 as *mut c_void,
            data: ptr::null_mut(),
            len: 10,
        };
        assert_eq!(buf.as_mut_slice(), []);
    }

    #[test]
    fn as_mut_slice() {
        let mut data = [0u8, 1, 2, 3];
        let mut buf = InBuf {
            free_buffer: default_free_buffer,
            raw: 42 as *mut c_void,
            data: data.as_mut_ptr(),
            len: data.len(),
        };
        assert_eq!(buf.as_mut_slice(), &data[..]);
        assert_eq!(&buf[..], &data[..]);
    }

    #[test]
    fn from_in_buf() {
        static mut RETURNED_RAW_BUFFER: *mut c_void = ptr::null_mut();

        unsafe extern "C" fn assert_free_buffer(raw: *mut c_void) {
            RETURNED_RAW_BUFFER = raw;
        }

        let inbuf = InBuf {
            free_buffer: assert_free_buffer,
            raw: 42 as *mut c_void,
            data: 43 as *mut u8,
            len: 20,
        };
        let outbuf = OutBuf::from(inbuf, 10);
        assert_eq!(42 as *mut c_void, outbuf.raw);
        assert_eq!(43 as *mut u8, outbuf.data);
        assert_eq!(10, outbuf.written_bytes);

        // Safe, this is a function specific static field.
        unsafe {
            assert!(RETURNED_RAW_BUFFER.is_null());
        }
    }
}
