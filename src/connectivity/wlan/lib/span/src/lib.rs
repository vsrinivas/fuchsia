// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;

/// A convenient C-wrapper for read-only memory that is neither owned or managed by Rust
#[repr(C)]
pub struct CSpan<'a> {
    data: *const u8,
    size: usize,
    _p: PhantomData<&'a u8>,
}

impl<'a> From<CSpan<'a>> for &'a [u8] {
    fn from(span: CSpan<'a>) -> &'a [u8] {
        if span.data.is_null() {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(span.data, span.size) }
        }
    }
}

impl From<CSpan<'_>> for Vec<u8> {
    fn from(span: CSpan<'_>) -> Vec<u8> {
        if span.data.is_null() {
            vec![]
        } else {
            <&[u8]>::from(span).to_vec()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn null_ptr_to_empty_slice() {
        let span = CSpan { data: std::ptr::null(), size: 42, _p: PhantomData };
        assert_eq!(<&[u8]>::from(span), &[]);
    }
    #[test]
    fn zero_size_to_empty_slice() {
        let arr = [];
        assert_eq!((arr.as_ptr() as *const u8).is_null(), false);
        let span = CSpan { data: arr.as_ptr(), size: arr.len(), _p: PhantomData };
        assert_eq!(<&[u8]>::from(span), &[]);
    }

    #[test]
    fn normal_slice() {
        let arr = [3u8; 6];
        let span = CSpan { data: arr.as_ptr(), size: arr.len(), _p: PhantomData };
        assert_eq!(<&[u8]>::from(span), &arr[..]);
    }
}
